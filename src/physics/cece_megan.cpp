/**
 * @file cece_megan.cpp
 * @brief MEGAN (Model of Emissions of Gases and Aerosols from Nature) biogenic emission scheme.
 *
 * Implements the MEGAN biogenic emission model for calculating natural emissions
 * from vegetation. This module handles temperature-dependent emission factors,
 * leaf area index corrections, and photosynthetically active radiation effects.
 *
 * The implementation is ported from HEMCO's hcox_megan_mod.F90 with optimizations
 * for Kokkos parallel execution.
 *
 * @author Barry Baker
 * @date 2024
 * @version 1.0
 */

#include "cece/physics/cece_megan.hpp"

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>

#include "cece/cece_physics_factory.hpp"

namespace cece {

/// @brief Self-registration for the MEGAN biogenic emission scheme.
static PhysicsRegistration<MeganScheme> register_scheme("megan");

/**
 * @brief Calculate MEGAN Gamma LAI factor.
 *
 * Computes the Leaf Area Index (LAI) correction factor for biogenic emissions.
 * This factor accounts for the vegetation density effect on emission rates.
 *
 * @param lai Current leaf area index [m²/m²]
 * @param c1 MEGAN coefficient 1 (typically species-specific)
 * @param c2 MEGAN coefficient 2 (typically species-specific)
 * @return LAI correction factor (dimensionless)
 */

KOKKOS_INLINE_FUNCTION
double get_gamma_lai(double lai, double c1, double c2) {
    return c1 * lai / std::sqrt(1.0 + c2 * lai * lai);
}

/**
 * @brief Calculate temperature-dependent gamma factor (light-independent).
 *
 * Computes the emission factor correction for temperature effects on
 * light-independent biogenic emissions.
 *
 * @param temp Current temperature [K]
 * @param beta Temperature response coefficient
 * @param t_standard Standard reference temperature [K]
 * @return Temperature correction factor (dimensionless)
 */
KOKKOS_INLINE_FUNCTION
double get_gamma_t_li(double temp, double beta, double t_standard) {
    return std::exp(beta * (temp - t_standard));
}

/**
 * @brief Calculate temperature-dependent gamma factor (light-dependent).
 *
 * Computes the emission factor correction for temperature effects on
 * light-dependent biogenic emissions using the MEGAN algorithm.
 *
 * @param T Current temperature [K]
 * @param PT_15 15-day averaged temperature [K]
 * @param CT1 MEGAN temperature coefficient 1
 * @param CEO MEGAN emission coefficient
 * @param R Gas constant [J mol⁻¹ K⁻¹]
 * @param CT2 MEGAN temperature coefficient 2
 * @param t_opt_c1 Optimal temperature coefficient 1
 * @param t_opt_c2 Optimal temperature coefficient 2
 * @param e_opt_coeff Optimal emission coefficient
 * @return Temperature correction factor (dimensionless)
 */
KOKKOS_INLINE_FUNCTION
double get_gamma_t_ld(double T, double PT_15, double CT1, double CEO, double R, double CT2,
                      double t_opt_c1, double t_opt_c2, double e_opt_coeff) {
    // Calculate optimal emission potential
    double e_opt = CEO * std::exp(e_opt_coeff * (PT_15 - 297.0));

    // Calculate optimal temperature
    double t_opt = t_opt_c1 + t_opt_c2 * (PT_15 - 297.0);

    // Calculate temperature-dependent term
    double x = (1.0 / t_opt - 1.0 / T) / R;

    // Calculate emission correction factor
    double c_t = e_opt * CT2 * std::exp(CT1 * x) / (CT2 - CT1 * (1.0 - std::exp(CT2 * x)));
    return std::max(c_t, 0.0);
}

/**
 * @brief Calculate photosynthetically active radiation (PAR) gamma factor.
 *
 * Computes the PAR correction factor for light-dependent biogenic emissions
 * using the PCEEA (Parameterized Canopy Environment Emission Activity) algorithm.
 *
 * @param q_dir Direct PAR flux [W/m²]
 * @param q_diff Diffuse PAR flux [W/m²]
 * @param par_avg Daily averaged PAR [W/m²]
 * @param suncos Cosine of solar zenith angle (dimensionless)
 * @param doy Day of year [1-365]
 * @param wm2_to_umol Conversion factor from W/m² to μmol/m²/s
 * @param ptoa_c1 Top-of-atmosphere PAR coefficient 1
 * @param ptoa_c2 Top-of-atmosphere PAR coefficient 2
 * @param gamma_p_c1 PAR gamma coefficient 1
 * @param gamma_p_c2 PAR gamma coefficient 2
 * @param gamma_p_c3 PAR gamma coefficient 3
 * @param gamma_p_c4 PAR gamma coefficient 4
 * @return PAR correction factor (dimensionless)
 */
KOKKOS_INLINE_FUNCTION
double get_gamma_par_pceea(double q_dir, double q_diff, double par_avg, double suncos, int doy,
                           double wm2_to_umol, double ptoa_c1, double ptoa_c2, double gamma_p_c1,
                           double gamma_p_c2, double gamma_p_c3, double gamma_p_c4) {
    const double PI = std::numbers::pi;

    // Early exit for nighttime conditions
    if (suncos <= 0.0) {
        return 0.0;
    }

    // Calculate instantaneous and daily PAR in photon units
    double pac_instant = (q_dir + q_diff) * wm2_to_umol;
    double pac_daily = par_avg * wm2_to_umol;

    // Calculate top-of-atmosphere PAR with seasonal variation
    double ptoa = ptoa_c1 + ptoa_c2 * std::cos(2.0 * PI * (doy - 10.0) / 365.0);

    // Calculate PAR penetration fraction
    double phi = pac_instant / (suncos * ptoa);

    // Calculate PAR-dependent coefficients
    double bbb = gamma_p_c1 + gamma_p_c2 * (pac_daily - 400.0);
    double aaa = (gamma_p_c3 * bbb * phi) - (gamma_p_c4 * phi * phi);

    // Calculate final PAR gamma factor
    double gamma_p = suncos * aaa;
    return std::max(gamma_p, 0.0);
}

KOKKOS_INLINE_FUNCTION
double get_gamma_co2(double co2a, double c1, double c2) {
    // Ported from GET_GAMMA_CO2 (LPOSSELL relationship)
    return c1 / (1.0 + c1 * c2 * co2a);
}

void MeganScheme::Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);

    gamma_co2_coeff_1_ = 8.9406;
    gamma_co2_coeff_2_ = 0.0024;
    if (config["gamma_co2_coeff_1"]) gamma_co2_coeff_1_ = config["gamma_co2_coeff_1"].as<double>();
    if (config["gamma_co2_coeff_2"]) gamma_co2_coeff_2_ = config["gamma_co2_coeff_2"].as<double>();

    double co2a = 400.0;
    if (config["co2_concentration"]) {
        co2a = config["co2_concentration"].as<double>();
    }
    gamma_co2_ = get_gamma_co2(co2a, gamma_co2_coeff_1_, gamma_co2_coeff_2_);

    beta_ = 0.13;
    ct1_ = 95.0;
    ceo_ = 2.0;
    ldf_ = 1.0;
    aef_isop_ = 1.0e-9;

    lai_coeff_1_ = 0.49;
    lai_coeff_2_ = 0.2;
    standard_temp_ = 303.0;
    gas_constant_ = 8.3144598e-3;
    ct2_const_ = 200.0;
    t_opt_coeff_1_ = 313.0;
    t_opt_coeff_2_ = 0.6;
    e_opt_coeff_ = 0.08;
    wm2_to_umolm2s_ = 4.766;
    ptoa_coeff_1_ = 3000.0;
    ptoa_coeff_2_ = 99.0;
    gamma_p_coeff_1_ = 1.0;
    gamma_p_coeff_2_ = 0.0005;
    gamma_p_coeff_3_ = 2.46;
    gamma_p_coeff_4_ = 0.9;

    if (config["beta"]) beta_ = config["beta"].as<double>();
    if (config["ct1"]) ct1_ = config["ct1"].as<double>();
    if (config["ceo"]) ceo_ = config["ceo"].as<double>();
    if (config["ldf"]) ldf_ = config["ldf"].as<double>();
    if (config["aef_isop"]) aef_isop_ = config["aef_isop"].as<double>();
    if (config["lai_coeff_1"]) lai_coeff_1_ = config["lai_coeff_1"].as<double>();
    if (config["lai_coeff_2"]) lai_coeff_2_ = config["lai_coeff_2"].as<double>();
    if (config["standard_temp"]) standard_temp_ = config["standard_temp"].as<double>();
    if (config["gas_constant"]) gas_constant_ = config["gas_constant"].as<double>();
    if (config["ct2_const"]) ct2_const_ = config["ct2_const"].as<double>();
    if (config["t_opt_coeff_1"]) t_opt_coeff_1_ = config["t_opt_coeff_1"].as<double>();
    if (config["t_opt_coeff_2"]) t_opt_coeff_2_ = config["t_opt_coeff_2"].as<double>();
    if (config["e_opt_coeff"]) e_opt_coeff_ = config["e_opt_coeff"].as<double>();
    if (config["wm2_to_umolm2s"]) wm2_to_umolm2s_ = config["wm2_to_umolm2s"].as<double>();
    if (config["ptoa_coeff_1"]) ptoa_coeff_1_ = config["ptoa_coeff_1"].as<double>();
    if (config["ptoa_coeff_2"]) ptoa_coeff_2_ = config["ptoa_coeff_2"].as<double>();
    if (config["gamma_p_coeff_1"]) gamma_p_coeff_1_ = config["gamma_p_coeff_1"].as<double>();
    if (config["gamma_p_coeff_2"]) gamma_p_coeff_2_ = config["gamma_p_coeff_2"].as<double>();
    if (config["gamma_p_coeff_3"]) gamma_p_coeff_3_ = config["gamma_p_coeff_3"].as<double>();
    if (config["gamma_p_coeff_4"]) gamma_p_coeff_4_ = config["gamma_p_coeff_4"].as<double>();

    std::cout << "MeganScheme: Initialized. GAMMA_CO2=" << gamma_co2_ << "\n";
}

void MeganScheme::Run(CeceImportState& import_state, CeceExportState& export_state) {
    auto temp = ResolveImport("temperature", import_state);
    auto isoprene = ResolveExport("isoprene_emissions", export_state);
    auto lai = ResolveImport("leaf_area_index", import_state);
    auto pardr = ResolveImport("par_direct", import_state);
    auto pardf = ResolveImport("par_diffuse", import_state);
    auto suncos = ResolveImport("solar_cosine", import_state);

    if (temp.data() == nullptr || isoprene.data() == nullptr || lai.data() == nullptr ||
        pardr.data() == nullptr || pardf.data() == nullptr || suncos.data() == nullptr) {
        return;
    }

    int nx = static_cast<int>(isoprene.extent(0));
    int ny = static_cast<int>(isoprene.extent(1));

    double beta = beta_;
    double ct1 = ct1_;
    double ceo = ceo_;
    double ldf = ldf_;
    double aef_isop = aef_isop_;
    double lai_c1 = lai_coeff_1_;
    double lai_c2 = lai_coeff_2_;
    double std_t = standard_temp_;
    double R = gas_constant_;
    double ct2 = ct2_const_;
    double t_opt_c1 = t_opt_coeff_1_;
    double t_opt_c2 = t_opt_coeff_2_;
    double e_opt_c = e_opt_coeff_;
    double wm2_umol = wm2_to_umolm2s_;
    double ptoa_c1 = ptoa_coeff_1_;
    double ptoa_c2 = ptoa_coeff_2_;
    double gp_c1 = gamma_p_coeff_1_;
    double gp_c2 = gamma_p_coeff_2_;
    double gp_c3 = gamma_p_coeff_3_;
    double gp_c4 = gamma_p_coeff_4_;

    const double NORM_FAC = 1.0 / 1.0101081;
    double gamma_co2_const = gamma_co2_;

    Kokkos::parallel_for(
        "MeganKernel_Optimized",
        Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<2>>({0, 0}, {nx, ny}),
        KOKKOS_LAMBDA(int i, int j) {
            double T = temp(i, j, 0);
            double L = lai(i, j, 0);
            double sc = suncos(i, j, 0);

            if (L <= 0.0) {
                return;
            }

            double T_AVG_15 = 297.0;
            double PAR_AVG = 400.0;
            int doy = 180;

            double gamma_lai = get_gamma_lai(L, lai_c1, lai_c2);
            double gamma_t_li = get_gamma_t_li(T, beta, std_t);
            double gamma_t_ld =
                get_gamma_t_ld(T, T_AVG_15, ct1, ceo, R, ct2, t_opt_c1, t_opt_c2, e_opt_c);
            double gamma_par =
                get_gamma_par_pceea(pardr(i, j, 0), pardf(i, j, 0), PAR_AVG, sc, doy, wm2_umol,
                                    ptoa_c1, ptoa_c2, gp_c1, gp_c2, gp_c3, gp_c4);

            double megan_emis = NORM_FAC * aef_isop * gamma_lai * gamma_co2_const *
                                ((1.0 - ldf) * gamma_t_li + (ldf * gamma_par * gamma_t_ld));

            isoprene(i, j, 0) += megan_emis;
        });

    Kokkos::fence();
    MarkModified("isoprene_emissions", export_state);
}

}  // namespace cece
