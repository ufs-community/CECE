#include "aces/physics/aces_dust.hpp"

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

#include "aces/aces_physics_factory.hpp"

namespace aces {

/// Self-registration for the DustScheme scheme.
static PhysicsRegistration<DustScheme> register_scheme("dust");

/**
 * @brief Threshold velocity logic (Ported from hcox_dustginoux_mod.F90)
 */
KOKKOS_INLINE_FUNCTION
double get_u_ts0(double den, double diam, double g, double rhoa) {
    double reynol = 1331.0 * std::pow(diam, 1.56) + 0.38;
    double alpha = den * g * diam / rhoa;
    double beta = 1.0 + (6.0e-3 / (den * g * std::pow(diam, 2.5)));
    double gamma = (1.928 * std::pow(reynol, 0.092)) - 1.0;
    return 129.0e-5 * std::sqrt(alpha) * std::sqrt(beta) / std::sqrt(gamma);
}

void DustScheme::Initialize(const YAML::Node& /*config*/, AcesDiagnosticManager* /*diag_manager*/) {
    std::cout << "DustScheme: Initialized." << std::endl;
}

void DustScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto u10m = ResolveImport("wind_speed_10m", import_state);
    auto gwettop = ResolveImport("gwettop", import_state);
    auto srce_sand = ResolveImport("GINOUX_SAND", import_state);
    auto dust_emis = ResolveExport("total_dust_emissions", export_state);

    if (!u10m.data() || !gwettop.data() || !srce_sand.data() || !dust_emis.data()) return;

    int nx = dust_emis.extent(0);
    int ny = dust_emis.extent(1);
    int nz = dust_emis.extent(2);

    const double G = 980.665;          // cm/s^2
    const double RHOA = 1.25e-3;       // g/cm3
    const double CH_DUST = 9.375e-10;  // Default tuning factor

    // Example for one bin (Sand-based)
    const double den = 2500.0 * 1.0e-3;         // g/cm3
    const double diam = 2.0 * 0.73e-6 * 1.0e2;  // cm

    Kokkos::parallel_for(
        "DustKernel_Faithful",
        Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<3>>({0, 0, 0},
                                                                              {nx, ny, nz}),
        KOKKOS_LAMBDA(int i, int j, int k) {
            if (k != 0) return;

            double gw = gwettop(i, j, k);
            double u10 = u10m(i, j, k);
            double w2 = u10 * u10;
            double u_ts0 = get_u_ts0(den, diam, G, RHOA);

            double u_ts =
                (gw < 0.2) ? u_ts0 * (1.2 + 0.2 * std::log10(std::max(1.0e-3, gw))) : 100.0;

            if (std::sqrt(w2) > u_ts) {
                double srce = srce_sand(i, j, k);
                double flux = CH_DUST * srce * w2 * (std::sqrt(w2) - u_ts);
                dust_emis(i, j, k) += std::max(0.0, flux);
            }
        });

    Kokkos::fence();
    MarkModified("total_dust_emissions", export_state);
}

}  // namespace aces
