#include "aces/physics/aces_soil_nox.hpp"

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

#include "aces/aces_physics_factory.hpp"

namespace aces {

/// Self-registration for the SoilNoxScheme scheme.
static PhysicsRegistration<SoilNoxScheme> register_scheme("soil_nox");

/**
 * @brief Soil NOx Emissions (Ported from hcox_soilnox_mod.F90)
 */

KOKKOS_INLINE_FUNCTION
double soil_temp_term(double tc, double tc_max, double exp_coeff) {
    if (tc <= 0.0) {
        return 0.0;
    }
    return std::exp(exp_coeff * std::min(tc_max, tc));
}

KOKKOS_INLINE_FUNCTION
double soil_wet_term(double gw, double wet_c1, double wet_c2) {
    // Non-arid default Poisson response (max at WFPS=0.3)
    return wet_c1 * gw * std::exp(wet_c2 * gw * gw);
}

void SoilNoxScheme::Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);
    a_biome_wet_ = 0.5;
    if (config["biome_coefficient_wet"]) {
        a_biome_wet_ = config["biome_coefficient_wet"].as<double>();
    }

    tc_max_ = 30.0;
    exp_coeff_ = 0.103;
    wet_c1_ = 5.5;
    wet_c2_ = -5.55;

    if (config["temp_limit"]) tc_max_ = config["temp_limit"].as<double>();
    if (config["temp_exp_coeff"]) exp_coeff_ = config["temp_exp_coeff"].as<double>();
    if (config["wet_coeff_1"]) wet_c1_ = config["wet_coeff_1"].as<double>();
    if (config["wet_coeff_2"]) wet_c2_ = config["wet_coeff_2"].as<double>();

    std::cout << "SoilNoxScheme: Initialized with Hudman et al. (2012) logic." << "\n";
}

void SoilNoxScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto temp = ResolveImport("temperature", import_state);
    auto gwet = ResolveImport("soil_moisture", import_state);
    auto soil_nox = ResolveExport("soil_nox_emissions", export_state);

    if (temp.data() == nullptr || gwet.data() == nullptr || soil_nox.data() == nullptr) {
        return;
    }

    int nx = static_cast<int>(soil_nox.extent(0));
    int ny = static_cast<int>(soil_nox.extent(1));

    const double MW_NO = 30.0;
    const double UNITCONV = 1.0e-12 / 14.0 * MW_NO;  // ng N -> kg NO
    double a_biome_wet = a_biome_wet_;
    double tc_max = tc_max_;
    double exp_coeff = exp_coeff_;
    double wet_c1 = wet_c1_;
    double wet_c2 = wet_c2_;

    Kokkos::parallel_for(
        "SoilNoxKernel_Optimized",
        Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<2>>({0, 0}, {nx, ny}),
        KOKKOS_LAMBDA(int i, int j) {
            double tc = temp(i, j, 0) - 273.15;
            double gw = gwet(i, j, 0);

            double t_term = soil_temp_term(tc, tc_max, exp_coeff);
            double w_term = soil_wet_term(gw, wet_c1, wet_c2);

            // Pulse factor placeholder (HEMCO uses complex stateful pulsing logic)
            double pulse = 1.0;

            // Total emission [kg NO/m2/s]
            double emiss = a_biome_wet * UNITCONV * t_term * w_term * pulse;
            soil_nox(i, j, 0) += emiss;
        });

    Kokkos::fence();
    MarkModified("soil_nox_emissions", export_state);
}

}  // namespace aces
