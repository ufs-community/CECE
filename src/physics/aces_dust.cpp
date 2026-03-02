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
double calculate_u_ts0(double den, double diam, double g, double rhoa) {
    double reynol = 1331.0 * std::pow(diam, 1.56) + 0.38;
    double alpha = den * g * diam / rhoa;
    double beta = 1.0 + (6.0e-3 / (den * g * std::pow(diam, 2.5)));
    double gamma = (1.928 * std::pow(reynol, 0.092)) - 1.0;
    return 129.0e-5 * std::sqrt(alpha) * std::sqrt(beta) / std::sqrt(gamma);
}

void DustScheme::Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);
    const double G = 980.665;     // cm/s^2
    const double RHOA = 1.25e-3;  // g/cm3

    double den = 2500.0 * 1.0e-3;         // g/cm3
    double diam = 2.0 * 0.73e-6 * 1.0e2;  // cm

    if (config["particle_density"]) den = config["particle_density"].as<double>() * 1.0e-3;
    if (config["particle_diameter"]) diam = config["particle_diameter"].as<double>() * 1.0e2;

    u_ts0_ = calculate_u_ts0(den, diam, G, RHOA);
    std::cout << "DustScheme: Initialized. U_TS0=" << u_ts0_ << "\n";
}

void DustScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto u10m = ResolveImport("wind_speed_10m", import_state);
    auto gwettop = ResolveImport("gwettop", import_state);
    auto srce_sand = ResolveImport("GINOUX_SAND", import_state);
    auto dust_emis = ResolveExport("total_dust_emissions", export_state);

    if (u10m.data() == nullptr || gwettop.data() == nullptr || srce_sand.data() == nullptr ||
        dust_emis.data() == nullptr)
        return;

    int nx = static_cast<int>(dust_emis.extent(0));
    int ny = static_cast<int>(dust_emis.extent(1));
    int nz = static_cast<int>(dust_emis.extent(2));

    const double CH_DUST = 9.375e-10;  // Default tuning factor
    double u_ts0_const = u_ts0_;

    Kokkos::parallel_for(
        "DustKernel_Optimized",
        Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<2>>({0, 0}, {nx, ny}),
        KOKKOS_LAMBDA(int i, int j) {
            double gw = gwettop(i, j, 0);
            double u10 = u10m(i, j, 0);
            double w2 = u10 * u10;

            double u_ts =
                (gw < 0.2) ? u_ts0_const * (1.2 + 0.2 * std::log10(std::max(1.0e-3, gw))) : 100.0;

            if (u10 > u_ts) {
                double srce = srce_sand(i, j, 0);
                double flux = CH_DUST * srce * w2 * (u10 - u_ts);
                dust_emis(i, j, 0) += std::max(0.0, flux);
            }
        });

    Kokkos::fence();
    MarkModified("total_dust_emissions", export_state);
}

}  // namespace aces
