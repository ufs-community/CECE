/**
 * @file aces_volcano.cpp
 * @brief Volcanic sulfur dioxide (SO₂) emission scheme implementation.
 *
 * Implements volcanic SO₂ emission calculations for explosive and effusive
 * volcanic eruptions. The scheme handles both point source emissions from
 * specific volcanic locations and distributed emissions from volcanic regions.
 *
 * Key features:
 * - Configurable volcanic source locations (grid indices)
 * - Elevation-dependent emission injection heights
 * - Cloud penetration and plume rise calculations
 * - Temporal emission profiles for eruptive events
 *
 * The volcanic emissions are critical for atmospheric sulfur budgets and
 * can have significant impacts on regional and global air quality and
 * climate through aerosol formation processes.
 *
 * @author Barry Baker
 * @date 2024
 * @version 1.0
 */

#include "aces/physics/aces_volcano.hpp"

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

#include "aces/aces_physics_factory.hpp"

namespace aces {

/// @brief Self-registration for the volcanic SO₂ emission scheme.
static PhysicsRegistration<VolcanoScheme> register_scheme("volcano");

/**
 * @brief Initialize the volcanic emission scheme.
 *
 * Sets up volcanic emission parameters including source location,
 * emission strength, and vertical distribution characteristics.
 *
 * Configuration parameters:
 * - target_i, target_j: Grid indices for volcanic source location
 * - sulfur_emission: SO₂ emission rate [kg/s]
 * - elevation: Volcanic vent elevation [m]
 * - cloud_top: Typical cloud top height for plume calculations [m]
 *
 * @param config YAML configuration node with volcanic parameters
 * @param diag_manager Diagnostic manager for tracking outputs
 */
void VolcanoScheme::Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);

    target_i_ = 1;
    target_j_ = 1;
    volcano_sulf_ = 1.0;
    volcano_elv_ = 600.0;
    volcano_cld_ = 2000.0;

    if (config["target_i"]) target_i_ = config["target_i"].as<int>();
    if (config["target_j"]) target_j_ = config["target_j"].as<int>();
    if (config["sulfur_emission"]) volcano_sulf_ = config["sulfur_emission"].as<double>();
    if (config["elevation"]) volcano_elv_ = config["elevation"].as<double>();
    if (config["cloud_top"]) volcano_cld_ = config["cloud_top"].as<double>();

    std::cout << "VolcanoScheme: Initialized." << "\n";
}

void VolcanoScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto so2 = ResolveExport("volcanic_so2", export_state);
    auto zsfc = ResolveImport("surface_altitude", import_state);
    auto bxheight = ResolveImport("layer_thickness", import_state);

    if (so2.data() == nullptr || zsfc.data() == nullptr || bxheight.data() == nullptr) {
        return;
    }

    int nz = static_cast<int>(so2.extent(2));

    int target_i = target_i_;
    int target_j = target_j_;
    double volcano_sulf = volcano_sulf_;
    double volcano_elv = volcano_elv_;
    double volcano_cld = volcano_cld_;

    Kokkos::parallel_for(
        "VolcanoKernel_Optimized", Kokkos::RangePolicy<Kokkos::DefaultExecutionSpace>(0, nz),
        KOKKOS_LAMBDA(int k) {
            // Refactored to only iterate over the vertical column for the point source.
            // In a real implementation, target_i/target_j would be mapped from local process
            // bounds.
            int i = target_i;
            int j = target_j;

            double z_bot_box = zsfc(i, j, 0);
            for (int l = 0; l < k; ++l) {
                z_bot_box += bxheight(i, j, l);
            }
            double z_top_box = z_bot_box + bxheight(i, j, k);

            double z_bot_volc = std::max(volcano_elv, zsfc(i, j, 0));
            double z_top_volc = std::max(volcano_cld, zsfc(i, j, 0));

            // Eruptive: top 1/3
            if (z_bot_volc != z_top_volc) {
                z_bot_volc = z_top_volc - (z_top_volc - z_bot_volc) / 3.0;
            }

            double plume_hgt = z_top_volc - z_bot_volc;
            if (plume_hgt <= 0.0) {
                if (k == 0) {
                    so2(i, j, k) += volcano_sulf;
                }
                return;
            }

            if (z_bot_volc >= z_top_box || z_top_volc <= z_bot_box) {
                return;
            }

            double overlap = std::min(z_top_volc, z_top_box) - std::max(z_bot_volc, z_bot_box);
            double frac = overlap / plume_hgt;
            so2(i, j, k) += frac * volcano_sulf;
        });

    Kokkos::fence();
    MarkModified("volcanic_so2", export_state);
}

}  // namespace aces
