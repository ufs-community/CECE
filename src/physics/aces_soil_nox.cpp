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
double soil_temp_term(double tc) {
    if (tc <= 0.0) return 0.0;
    return std::exp(0.103 * std::min(30.0, tc));
}

KOKKOS_INLINE_FUNCTION
double soil_wet_term(double gw) {
    // Non-arid default Poisson response (max at WFPS=0.3)
    return 5.5 * gw * std::exp(-5.55 * gw * gw);
}

void SoilNoxScheme::Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);
    std::cout << "SoilNoxScheme: Initialized with Hudman et al. (2012) logic." << "\n";
}

void SoilNoxScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto temp = ResolveImport("temperature", import_state);
    auto gwet = ResolveImport("gwettop", import_state);
    auto soil_nox = ResolveExport("soil_nox", export_state);

    if (temp.data() == nullptr || gwet.data() == nullptr || soil_nox.data() == nullptr) {
        return;
    }

    int nx = static_cast<int>(soil_nox.extent(0));
    int ny = static_cast<int>(soil_nox.extent(1));
    int nz = static_cast<int>(soil_nox.extent(2));

    const double MW_NO = 30.0;
    const double UNITCONV = 1.0e-12 / 14.0 * MW_NO;  // ng N -> kg NO
    const double A_BIOME_WET = 0.5;                  // Example wet biome coefficient

    Kokkos::parallel_for(
        "SoilNoxKernel_Optimized",
        Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<2>>({0, 0}, {nx, ny}),
        KOKKOS_LAMBDA(int i, int j) {
            double tc = temp(i, j, 0) - 273.15;
            double gw = gwet(i, j, 0);

            double t_term = soil_temp_term(tc);
            double w_term = soil_wet_term(gw);

            // Pulse factor placeholder (HEMCO uses complex stateful pulsing logic)
            double pulse = 1.0;

            // Total emission [kg NO/m2/s]
            double emiss = A_BIOME_WET * UNITCONV * t_term * w_term * pulse;
            soil_nox(i, j, 0) += emiss;
        });

    Kokkos::fence();
    MarkModified("soil_nox", export_state);
}

}  // namespace aces
