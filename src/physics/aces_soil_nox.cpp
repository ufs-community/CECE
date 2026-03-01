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

void SoilNoxScheme::Initialize(const YAML::Node& /*config*/,
                               AcesDiagnosticManager* /*diag_manager*/) {
    std::cout << "SoilNoxScheme: Initialized with Hudman et al. (2012) logic." << std::endl;
}

void SoilNoxScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto temp = ResolveImport("temperature", import_state);
    auto gwet = ResolveImport("gwettop", import_state);
    auto soil_nox = ResolveExport("total_soil_nox_emissions", export_state);

    if (!temp.data() || !gwet.data() || !soil_nox.data()) return;

    int nx = soil_nox.extent(0);
    int ny = soil_nox.extent(1);
    int nz = soil_nox.extent(2);

    const double MW_NO = 30.0;
    const double UNITCONV = 1.0e-12 / 14.0 * MW_NO;  // ng N -> kg NO
    const double A_BIOME_WET = 0.5;                  // Example wet biome coefficient

    Kokkos::parallel_for(
        "SoilNoxKernel_Full",
        Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<3>>({0, 0, 0},
                                                                              {nx, ny, nz}),
        KOKKOS_LAMBDA(int i, int j, int k) {
            double tc = temp(i, j, k) - 273.15;
            double gw = gwet(i, j, k);

            double t_term = soil_temp_term(tc);
            double w_term = soil_wet_term(gw);

            // Pulse factor placeholder (HEMCO uses complex stateful pulsing logic)
            double pulse = 1.0;

            // Surface source
            if (k == 0) {
                // Total emission [kg NO/m2/s]
                double emiss = A_BIOME_WET * UNITCONV * t_term * w_term * pulse;
                soil_nox(i, j, k) += emiss;
            }
        });

    Kokkos::fence();
    MarkModified("total_soil_nox_emissions", export_state);
}

}  // namespace aces
