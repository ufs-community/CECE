/**
 * @file cece_dust.cpp
 * @brief Dust emission scheme based on Ginoux parameterization.
 *
 * Implements dust emission calculations using the Ginoux et al. (2001) algorithm
 * for mineral dust emissions from arid and semi-arid regions. The scheme calculates
 * dust flux based on wind speed, surface roughness, and soil properties.
 *
 * This implementation is ported from HEMCO's hcox_dustginoux_mod.F90 with
 * optimizations for Kokkos parallel execution.
 *
 * References:
 * - Ginoux, P., et al. (2001), Sources and distributions of dust aerosols simulated
 *   with the GOCART model, JGR, 106, 20255-20273.
 *
 * @author Barry Baker
 * @date 2024
 * @version 1.0
 */

#include "cece/physics/cece_dust.hpp"

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

#include "cece/cece_physics_factory.hpp"

namespace cece {

/// @brief Self-registration for the Ginoux dust emission scheme.
static PhysicsRegistration<DustScheme> register_scheme("dust");

/**
 * @brief Calculate threshold wind velocity for dust mobilization.
 *
 * Computes the minimum wind velocity required to lift dust particles from the surface
 * using the Ginoux parameterization. This accounts for particle size, density, and
 * atmospheric conditions.
 *
 * Algorithm ported from hcox_dustginoux_mod.F90.
 *
 * @param den Particle density [g/cm³]
 * @param diam Particle diameter [cm]
 * @param g Gravitational acceleration [cm/s²]
 * @param rhoa Air density [g/cm³]
 * @return Threshold velocity [m/s]
 */
KOKKOS_INLINE_FUNCTION
double calculate_u_ts0(double den, double diam, double g, double rhoa) {
    // Calculate Reynolds number based on particle diameter
    double reynol = 1331.0 * std::pow(diam, 1.56) + 0.38;

    // Calculate gravitational parameter (particle weight factor)
    double alpha = den * g * diam / rhoa;

    // Calculate correction factor for particle-air interaction
    double beta = 1.0 + (6.0e-3 / (den * g * std::pow(diam, 2.5)));

    // Calculate Reynolds-based correction factor
    double gamma = (1.928 * std::pow(reynol, 0.092)) - 1.0;

    // Compute threshold velocity using Ginoux formulation
    return 129.0e-5 * std::sqrt(alpha) * std::sqrt(beta) / std::sqrt(gamma);
}

void DustScheme::Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);
    G_const_ = 980.665;     // cm/s^2
    RHOA_const_ = 1.25e-3;  // g/cm3

    if (config["g_constant"]) G_const_ = config["g_constant"].as<double>();
    if (config["air_density"]) RHOA_const_ = config["air_density"].as<double>();

    double den = 2500.0 * 1.0e-3;         // g/cm3
    double diam = 2.0 * 0.73e-6 * 1.0e2;  // cm

    if (config["particle_density"]) {
        den = config["particle_density"].as<double>() * 1.0e-3;
    }
    if (config["particle_diameter"]) {
        diam = config["particle_diameter"].as<double>() * 1.0e2;
    }

    u_ts0_ = calculate_u_ts0(den, diam, G_const_, RHOA_const_);

    ch_dust_ = 9.375e-10;
    if (config["tuning_factor"]) {
        ch_dust_ = config["tuning_factor"].as<double>();
    }

    std::cout << "DustScheme: Initialized. U_TS0=" << u_ts0_ << " CH_DUST=" << ch_dust_ << "\n";
}

void DustScheme::Run(CeceImportState& import_state, CeceExportState& export_state) {
    auto u10m = ResolveImport("wind_speed", import_state);
    auto gwettop = ResolveImport("soil_moisture", import_state);
    auto srce_sand = ResolveImport("erodibility", import_state);
    auto dust_emis = ResolveExport("dust_emissions", export_state);

    if (u10m.data() == nullptr || gwettop.data() == nullptr || srce_sand.data() == nullptr || dust_emis.data() == nullptr) {
        return;
    }

    int nx = static_cast<int>(dust_emis.extent(0));
    int ny = static_cast<int>(dust_emis.extent(1));

    double tuning = ch_dust_;
    double u_ts0_const = u_ts0_;

    Kokkos::parallel_for(
        "DustKernel_Optimized", Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<2>>({0, 0}, {nx, ny}), KOKKOS_LAMBDA(int i, int j) {
            double gw = gwettop(i, j, 0);
            double u10 = u10m(i, j, 0);
            double w2 = u10 * u10;

            double u_ts = (gw < 0.2) ? u_ts0_const * (1.2 + 0.2 * std::log10(std::max(1.0e-3, gw))) : 100.0;

            if (u10 > u_ts) {
                double srce = srce_sand(i, j, 0);
                double flux = tuning * srce * w2 * (u10 - u_ts);
                dust_emis(i, j, 0) += std::max(0.0, flux);
            }
        });

    Kokkos::fence();
    MarkModified("dust_emissions", export_state);
}

}  // namespace cece
