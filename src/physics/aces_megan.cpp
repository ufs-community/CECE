#include "aces/physics/aces_megan.hpp"

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>

#include "aces/aces_physics_factory.hpp"

namespace aces {

/// Self-registration for the MeganScheme scheme.
static PhysicsRegistration<MeganScheme> register_scheme("megan");

/**
 * @brief MEGAN Gamma Factors (Ported from hcox_megan_mod.F90)
 */

KOKKOS_INLINE_FUNCTION
double get_gamma_lai(double lai) {
    return 0.49 * lai / std::sqrt(1.0 + 0.2 * lai * lai);
}

KOKKOS_INLINE_FUNCTION
double get_gamma_t_li(double temp, double beta) {
    const double T_STANDARD = 303.0;
    return std::exp(beta * (temp - T_STANDARD));
}

KOKKOS_INLINE_FUNCTION
double get_gamma_t_ld(double T, double PT_15, double CT1, double CEO) {
    const double R = 8.3144598e-3;  // kJ/mol/K
    const double CT2 = 200.0;

    double e_opt = CEO * std::exp(0.08 * (PT_15 - 297.0));
    double t_opt = 313.0 + 0.6 * (PT_15 - 297.0);
    double x = (1.0 / t_opt - 1.0 / T) / R;

    double c_t = e_opt * CT2 * std::exp(CT1 * x) / (CT2 - CT1 * (1.0 - std::exp(CT2 * x)));
    return std::max(c_t, 0.0);
}

KOKKOS_INLINE_FUNCTION
double get_gamma_par_pceea(double q_dir, double q_diff, double par_avg, double suncos, int doy) {
    const double WM2_TO_UMOLM2S = 4.766;
    const double PI = std::numbers::pi;

    if (suncos <= 0.0) {
        return 0.0;
    }

    double pac_instant = (q_dir + q_diff) * WM2_TO_UMOLM2S;
    double pac_daily = par_avg * WM2_TO_UMOLM2S;

    double ptoa = 3000.0 + 99.0 * std::cos(2.0 * PI * (doy - 10.0) / 365.0);
    double phi = pac_instant / (suncos * ptoa);

    double bbb = 1.0 + 0.0005 * (pac_daily - 400.0);
    double aaa = (2.46 * bbb * phi) - (0.9 * phi * phi);

    double gamma_p = suncos * aaa;
    return std::max(gamma_p, 0.0);
}

KOKKOS_INLINE_FUNCTION
double get_gamma_co2(double co2a) {
    // Ported from GET_GAMMA_CO2 (LPOSSELL relationship)
    return 8.9406 / (1.0 + 8.9406 * 0.0024 * co2a);
}

void MeganScheme::Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);
    double co2a = 400.0;
    if (config["co2_concentration"]) {
        co2a = config["co2_concentration"].as<double>();
    }
    gamma_co2_ = get_gamma_co2(co2a);

    beta_ = 0.13;
    ct1_ = 95.0;
    ceo_ = 2.0;
    ldf_ = 1.0;
    aef_isop_ = 1.0e-9;

    if (config["beta"]) beta_ = config["beta"].as<double>();
    if (config["ct1"]) ct1_ = config["ct1"].as<double>();
    if (config["ceo"]) ceo_ = config["ceo"].as<double>();
    if (config["ldf"]) ldf_ = config["ldf"].as<double>();
    if (config["aef_isop"]) aef_isop_ = config["aef_isop"].as<double>();

    std::cout << "MeganScheme: Initialized. GAMMA_CO2=" << gamma_co2_ << "\n";
}

void MeganScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto temp = ResolveImport("temperature", import_state);
    auto isoprene = ResolveExport("isoprene", export_state);
    auto lai = ResolveImport("lai", import_state);
    auto pardr = ResolveImport("pardr", import_state);
    auto pardf = ResolveImport("pardf", import_state);
    auto suncos = ResolveImport("suncos", import_state);

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

            double gamma_lai = get_gamma_lai(L);
            double gamma_t_li = get_gamma_t_li(T, beta);
            double gamma_t_ld = get_gamma_t_ld(T, T_AVG_15, ct1, ceo);
            double gamma_par =
                get_gamma_par_pceea(pardr(i, j, 0), pardf(i, j, 0), PAR_AVG, sc, doy);

            double megan_emis = NORM_FAC * aef_isop * gamma_lai * gamma_co2_const *
                                ((1.0 - ldf) * gamma_t_li + (ldf * gamma_par * gamma_t_ld));

            isoprene(i, j, 0) += megan_emis;
        });

    Kokkos::fence();
    MarkModified("isoprene", export_state);
}

}  // namespace aces
