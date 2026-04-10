/**
 * @file cece_sea_salt.cpp
 * @brief Sea salt aerosol emission scheme based on Gong et al. (2003).
 *
 * Implements sea salt aerosol emission calculations using the Gong et al. (2003)
 * source function for oceanic regions. The scheme computes size-resolved emissions
 * for accumulaton mode (SALA) and coarse mode (SALC) sea salt particles based on
 * surface wind speed and sea surface conditions.
 *
 * The implementation includes:
 * - Gong (2003) source function with size-dependent parameterization
 * - Integration over specified size ranges for different modes
 * - Wind speed dependency with power-law scaling
 * - Temperature and humidity corrections for hygroscopic growth
 *
 * References:
 * - Gong, S.L., et al. (2003), A parameterization of sea-salt aerosol source
 *   function for sub- and super-micron particles, Global Biogeochem. Cycles, 17(4).
 *
 * @author Barry Baker
 * @date 2024
 * @version 1.0
 */

#include "cece/physics/cece_sea_salt.hpp"

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>

#include "cece/cece_physics_factory.hpp"

namespace cece {

/// @brief Self-registration for the Gong sea salt emission scheme.
static PhysicsRegistration<SeaSaltScheme> register_scheme("sea_salt");

/**
 * @brief Normalized Gong (2003) sea salt source function.
 *
 * Computes the size-resolved sea salt particle source function dF/dr_80
 * normalized to unit wind speed (u10 = 1.0 m/s). The actual emission rate
 * is obtained by scaling with wind speed cubed.
 *
 * The function represents the number of particles produced per unit area,
 * time, and size interval at radius r80 (radius at 80% relative humidity).
 *
 * @param r80 Particle radius at 80% relative humidity [μm]
 * @return Normalized source function [particles/m²/s/μm] at u10=1.0 m/s
 */
KOKKOS_INLINE_FUNCTION
double gong_source_normalized(double r80) {
    if (r80 <= 0.0) {
        return 0.0;
    }

    // Calculate size-dependent exponent coefficient
    double a = 4.7 * std::pow(1.0 + 30.0 * r80, -0.017 * std::pow(r80, -1.44));

    // Calculate logarithmic term for size distribution shape
    double b = (0.433 - std::log10(r80)) / 0.433;

    // Gong (2003) source function with size-dependent terms:
    // - Power law component: r80^(-a) with variable exponent
    // - Large particle enhancement: (1 + 0.057 * r80^3.45)
    // - Log-normal contribution: 10^(1.607 * exp(-b²))
    return 1.373 * std::pow(r80, -a) * (1.0 + 0.057 * std::pow(r80, 3.45)) *
           std::pow(10.0, 1.607 * std::exp(-b * b));
}

void SeaSaltScheme::Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);
    // Pre-calculate the integral of the Gong source function (normalized to u10=1.0)
    double dr = 0.05;         // Integration step in um (dry)
    double betha = 2.0;       // r80 / r_dry
    double ss_dens = 2200.0;  // kg/m3
    const double pi = std::numbers::pi;

    if (config["integration_step"]) dr = config["integration_step"].as<double>();
    if (config["r80_dry_ratio"]) betha = config["r80_dry_ratio"].as<double>();
    if (config["sea_salt_density"]) ss_dens = config["sea_salt_density"].as<double>();

    double r_sala_min = 0.01, r_sala_max = 0.5;
    double r_salc_min = 0.5, r_salc_max = 8.0;

    if (config["r_sala_min"]) {
        r_sala_min = config["r_sala_min"].as<double>();
    }
    if (config["r_sala_max"]) {
        r_sala_max = config["r_sala_max"].as<double>();
    }
    if (config["r_salc_min"]) {
        r_salc_min = config["r_salc_min"].as<double>();
    }
    if (config["r_salc_max"]) {
        r_salc_max = config["r_salc_max"].as<double>();
    }

    // Default SST scaling coefficients
    sst_c0_ = 0.329;
    sst_c1_ = 0.0904;
    sst_c2_ = -0.00717;
    sst_c3_ = 0.000207;

    if (config["sst_coeff"]) {
        auto sc = config["sst_coeff"];
        if (sc.IsSequence() && sc.size() == 4) {
            sst_c0_ = sc[0].as<double>();
            sst_c1_ = sc[1].as<double>();
            sst_c2_ = sc[2].as<double>();
            sst_c3_ = sc[3].as<double>();
        }
    }

    u_pow_ = 3.41;
    if (config["u_power"]) {
        u_pow_ = config["u_power"].as<double>();
    }

    srrc_SALA_ = 0.0;
    for (double r = r_sala_min; r < r_sala_max; r += dr) {
        double r_mid = r + 0.5 * dr;
        double r80_mid = r_mid * betha;
        double df_dr80 = gong_source_normalized(r80_mid);
        double n_particles = df_dr80 * betha * dr;
        srrc_SALA_ += n_particles * (4.0 / 3.0 * pi * std::pow(r_mid * 1.0e-6, 3) * ss_dens);
    }

    srrc_SALC_ = 0.0;
    for (double r = r_salc_min; r < r_salc_max; r += dr) {
        double r_mid = r + 0.5 * dr;
        double r80_mid = r_mid * betha;
        double df_dr80 = gong_source_normalized(r80_mid);
        double n_particles = df_dr80 * betha * dr;
        srrc_SALC_ += n_particles * (4.0 / 3.0 * pi * std::pow(r_mid * 1.0e-6, 3) * ss_dens);
    }

    std::cout << "SeaSaltScheme: Initialized. SALA_REF=" << srrc_SALA_ << " SALC_REF=" << srrc_SALC_
              << "\n";
}

void SeaSaltScheme::Run(CeceImportState& import_state, CeceExportState& export_state) {
    auto u10m = ResolveImport("wind_speed", import_state);
    auto tskin = ResolveImport("tskin", import_state);
    auto sala = ResolveExport("secondary_input", export_state);
    auto salc = ResolveExport("coarse_input", export_state);

    if (u10m.data() == nullptr || tskin.data() == nullptr) {
        return;
    }

    int nx = static_cast<int>(u10m.extent(0));
    int ny = static_cast<int>(u10m.extent(1));

    double ref_sala = srrc_SALA_;
    double ref_salc = srrc_SALC_;
    double c0 = sst_c0_, c1 = sst_c1_, c2 = sst_c2_, c3 = sst_c3_;
    double u_pow = u_pow_;

    if (sala.data() != nullptr) {
        Kokkos::parallel_for(
            "SeaSalt_SALA_Gong_Optimized",
            Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<2>>({0, 0}, {nx, ny}),
            KOKKOS_LAMBDA(int i, int j) {
                double u = u10m(i, j, 0);
                double sst = tskin(i, j, 0) - 273.15;
                sst = std::max(0.0, std::min(30.0, sst));

                // Horner's Method for SST scaling polynomial
                double scale = c0 + sst * (c1 + sst * (c2 + sst * c3));

                // Gong (2003) normalized: df/dr80 proportional to u^u_pow
                double u_factor = std::pow(u, u_pow);
                sala(i, j, 0) += scale * u_factor * ref_sala;
            });
        MarkModified("secondary_input", export_state);
    }

    if (salc.data() != nullptr) {
        Kokkos::parallel_for(
            "SeaSalt_SALC_Gong_Optimized",
            Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<2>>({0, 0}, {nx, ny}),
            KOKKOS_LAMBDA(int i, int j) {
                double u = u10m(i, j, 0);
                double sst = tskin(i, j, 0) - 273.15;
                sst = std::max(0.0, std::min(30.0, sst));

                double scale = c0 + sst * (c1 + sst * (c2 + sst * c3));
                double u_factor = std::pow(u, u_pow);
                salc(i, j, 0) += scale * u_factor * ref_salc;
            });
        MarkModified("coarse_input", export_state);
    }
    Kokkos::fence();
}

}  // namespace cece
