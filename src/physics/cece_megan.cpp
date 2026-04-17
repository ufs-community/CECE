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
 * Gamma functions (get_gamma_lai, get_gamma_age, get_gamma_sm, get_gamma_t_li,
 * get_gamma_t_ld, get_gamma_par_pceea, get_gamma_co2) are defined in
 * cece_megan.hpp as KOKKOS_INLINE_FUNCTION so they can be shared with
 * cece_megan3.cpp and cece_emission_activity.cpp.
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

void MeganScheme::Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);

    gamma_co2_coeff_1_ = 8.9406;
    gamma_co2_coeff_2_ = 0.0024;
    if (config["gamma_co2_coeff_1"]) gamma_co2_coeff_1_ = config["gamma_co2_coeff_1"].as<double>();
    if (config["gamma_co2_coeff_2"]) gamma_co2_coeff_2_ = config["gamma_co2_coeff_2"].as<double>();

    if (config["anew"]) anew_ = config["anew"].as<double>();
    if (config["agro"]) agro_ = config["agro"].as<double>();
    if (config["amat"]) amat_ = config["amat"].as<double>();
    if (config["aold"]) aold_ = config["aold"].as<double>();
    if (config["is_bidirectional"]) is_bidirectional_ = config["is_bidirectional"].as<bool>();
    if (config["use_wilkinson"]) use_wilkinson_ = config["use_wilkinson"].as<bool>();
    if (config["is_ald2_or_eoh"]) is_ald2_or_eoh_ = config["is_ald2_or_eoh"].as<bool>();

    double co2a = 400.0;
    if (config["co2_concentration"]) {
        co2a = config["co2_concentration"].as<double>();
    }
    gamma_co2_ = get_gamma_co2(co2a, gamma_co2_coeff_1_, gamma_co2_coeff_2_, use_wilkinson_);

    beta_ = 0.13;
    ct1_ = 95.0;
    ceo_ = 2.0;
    ldf_ = 1.0;

    aef_ = 1.0e-9;
    if (config["aef"]) {
        aef_ = config["aef"].as<double>();
    } else if (config["aef_isop"]) {
        aef_ = config["aef_isop"].as<double>();
    }

    species_name_ = "isoprene";
    if (config["species_name"]) {
        species_name_ = config["species_name"].as<std::string>();
    }

    export_field_name_ = species_name_ + "_emissions";
    if (config["export_field_name"]) {
        export_field_name_ = config["export_field_name"].as<std::string>();
    }

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
    auto emissions_out = ResolveExport(export_field_name_, export_state);
    auto lai = ResolveImport("leaf_area_index", import_state);
    auto pardr = ResolveImport("par_direct", import_state);
    auto pardf = ResolveImport("par_diffuse", import_state);
    auto suncos = ResolveImport("solar_cosine", import_state);
    auto pmlai = ResolveImport("leaf_area_index_prev", import_state);
    auto gwetroot = ResolveImport("soil_moisture_root", import_state);

    if (temp.data() == nullptr || emissions_out.data() == nullptr || lai.data() == nullptr || pardr.data() == nullptr || pardf.data() == nullptr ||
        suncos.data() == nullptr) {
        return;
    }

    int nx = static_cast<int>(emissions_out.extent(0));
    int ny = static_cast<int>(emissions_out.extent(1));

    double beta = beta_, ct1 = ct1_, ceo = ceo_, ldf = ldf_, aef = aef_;
    double lai_c1 = lai_coeff_1_, lai_c2 = lai_coeff_2_, std_t = standard_temp_;
    double R = gas_constant_, ct2 = ct2_const_;
    double t_opt_c1 = t_opt_coeff_1_, t_opt_c2 = t_opt_coeff_2_, e_opt_c = e_opt_coeff_;
    double wm2_umol = wm2_to_umolm2s_, ptoa_c1 = ptoa_coeff_1_, ptoa_c2 = ptoa_coeff_2_;
    double gp_c1 = gamma_p_coeff_1_, gp_c2 = gamma_p_coeff_2_;
    double gp_c3 = gamma_p_coeff_3_, gp_c4 = gamma_p_coeff_4_;
    double anew = anew_, agro = agro_, amat = amat_, aold = aold_;
    bool is_bidirectional = is_bidirectional_, is_ald2_or_eoh = is_ald2_or_eoh_;
    const double NORM_FAC = 1.0 / 1.0101081;
    double gamma_co2_const = gamma_co2_;
    bool has_pmlai = (pmlai.data() != nullptr);
    bool has_gwetroot = (gwetroot.data() != nullptr);

    Kokkos::parallel_for(
        "MeganKernel_Optimized", Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<2>>({0, 0}, {nx, ny}),
        KOKKOS_LAMBDA(int i, int j) {
            double T = temp(i, j, 0);
            double L = lai(i, j, 0);
            double sc = suncos(i, j, 0);
            if (L <= 0.0) return;

            double T_AVG_15 = 297.0, PAR_AVG = 400.0, dbtwn = 30.0;
            int doy = 180;
            double L_prev = has_pmlai ? pmlai(i, j, 0) : L;
            double gwet = has_gwetroot ? gwetroot(i, j, 0) : 1.0;

            double g_lai = get_gamma_lai(L, lai_c1, lai_c2, is_bidirectional);
            double g_t_li = get_gamma_t_li(T, beta, std_t);
            double g_t_ld = get_gamma_t_ld(T, T_AVG_15, ct1, ceo, R, ct2, t_opt_c1, t_opt_c2, e_opt_c);
            double g_par = get_gamma_par_pceea(pardr(i, j, 0), pardf(i, j, 0), PAR_AVG, sc, doy, wm2_umol, ptoa_c1, ptoa_c2, gp_c1, gp_c2, gp_c3, gp_c4);
            double g_age = get_gamma_age(L, L_prev, dbtwn, T, anew, agro, amat, aold);
            double g_sm = get_gamma_sm(gwet, is_ald2_or_eoh);

            double megan_emis = NORM_FAC * aef * g_age * g_sm * g_lai * gamma_co2_const * ((1.0 - ldf) * g_t_li + (ldf * g_par * g_t_ld));
            emissions_out(i, j, 0) += megan_emis;
        });

    Kokkos::fence();
    MarkModified(export_field_name_, export_state);
}

}  // namespace cece
