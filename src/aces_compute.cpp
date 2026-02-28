#include "aces/aces_compute.hpp"

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <iostream>
#include <map>

/**
 * @file aces_compute.cpp
 * @brief Implementation of the core emissions compute engine.
 */

namespace aces {

/**
 * @brief Performs the emission computation for all species defined in the configuration.
 *
 * This function iterates over all species and their respective emission layers.
 * It applies a branchless formula to combine layers based on whether they should
 * 'add' to or 'replace' the current total for each grid cell.
 *
 * Formula: Total = Total * (1.0 - replace_flag * Mask) + (Field * Scale * Mask)
 *
 * @param config The ACES configuration containing species and layer definitions.
 * @param resolver A FieldResolver to bridge between the compute engine and data sources (like
 * ESMF).
 * @param nx Size of the first grid dimension.
 * @param ny Size of the second grid dimension.
 * @param nz Size of the third grid dimension.
 */
void ComputeEmissions(
    const AcesConfig& config, FieldResolver& resolver, int nx, int ny, int nz,
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> default_mask,
    int hour, int day_of_week) {
    // Create local views if persistent ones are not provided.
    if (!default_mask.data()) {
        default_mask = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>(
            "default_mask_local", nx, ny, nz);
        Kokkos::deep_copy(default_mask, 1.0);
    }

    for (auto const& [species, layers] : config.species_layers) {
        std::string export_name = "total_" + species + "_emissions";
        auto total_view = resolver.ResolveExportDevice(export_name, nx, ny, nz);

        if (total_view.data() == nullptr) {
            std::cerr << "ACES_Compute: Warning - Could not resolve export field " << export_name
                      << std::endl;
            continue;
        }

        // Initialize export field to 0 before accumulating layers.
        Kokkos::deep_copy(total_view, 0.0);

        // Sort ALL layers for this species by hierarchy (ascending) globally across categories.
        // This ensures that higher-hierarchy layers can correctly 'replace' lower-hierarchy
        // layers regardless of which category they belong to, enabling true HEMCO-style overrides.
        auto sorted_layers = layers;
        std::sort(sorted_layers.begin(), sorted_layers.end(),
                  [](const EmissionLayer& a, const EmissionLayer& b) {
                      return a.hierarchy < b.hierarchy;
                  });

        for (auto const& layer : sorted_layers) {
            auto field_view = resolver.ResolveImportDevice(layer.field_name, nx, ny, nz);
            if (field_view.data() == nullptr) {
                std::cerr << "ACES_Compute: Warning - Could not resolve input field "
                          << layer.field_name << std::endl;
                continue;
            }

            // Resolve additional scale fields
            std::vector<
                Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>>
                resolved_scale_fields;
            for (const auto& sf_name : layer.scale_fields) {
                auto sf_view = resolver.ResolveImportDevice(sf_name, nx, ny, nz);
                if (sf_view.data() != nullptr) {
                    resolved_scale_fields.push_back(sf_view);
                } else {
                    std::cerr << "ACES_Compute: Warning - Could not resolve scale field " << sf_name
                              << std::endl;
                }
            }

            // Resolve and combine multiple geographical masks
            std::vector<
                Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>>
                resolved_masks;
            for (const auto& m_name : layer.masks) {
                auto m_view = resolver.ResolveImportDevice(m_name, nx, ny, nz);
                if (m_view.data() != nullptr) {
                    resolved_masks.push_back(m_view);
                } else {
                    std::cerr << "ACES_Compute: Warning - Could not resolve mask " << m_name
                              << std::endl;
                }
            }

            constexpr int MAX_MASKS = 8;
            Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>
                masks_arr[MAX_MASKS];
            int num_masks = std::min((int)resolved_masks.size(), MAX_MASKS);
            for (int m = 0; m < num_masks; ++m) {
                masks_arr[m] = resolved_masks[m];
            }

            // 0.0 for 'add', 1.0 for 'replace'
            double replace_flag = (layer.operation == "replace") ? 1.0 : 0.0;
            double scale = layer.scale;

            // Apply temporal cycles
            if (!layer.diurnal_cycle.empty()) {
                auto it = config.temporal_cycles.find(layer.diurnal_cycle);
                if (it != config.temporal_cycles.end() && it->second.factors.size() == 24) {
                    scale *= it->second.factors[hour % 24];
                }
            }
            if (!layer.weekly_cycle.empty()) {
                auto it = config.temporal_cycles.find(layer.weekly_cycle);
                if (it != config.temporal_cycles.end() && it->second.factors.size() == 7) {
                    scale *= it->second.factors[day_of_week % 7];
                }
            }

            // Use a fixed-size array for scales to avoid std::vector capture in lambda.
            constexpr int MAX_SCALES = 16;
            Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>
                scales_arr[MAX_SCALES];
            int num_scales = std::min((int)resolved_scale_fields.size(), MAX_SCALES);
            for (int s = 0; s < num_scales; ++s) {
                scales_arr[s] = resolved_scale_fields[s];
            }

            // Compute the layer application in parallel using Kokkos.
            // We apply directly to total_view to support global hierarchy-based overrides.
            Kokkos::parallel_for(
                "EmissionLayerKernel",
                Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {nx, ny, nz}),
                KOKKOS_LAMBDA(int i, int j, int k) {
                    double combined_scale = scale;
                    for (int s = 0; s < num_scales; ++s) {
                        combined_scale *= scales_arr[s](i, j, k);
                    }

                    double combined_mask = 1.0;
                    for (int m = 0; m < num_masks; ++m) {
                        combined_mask *= masks_arr[m](i, j, k);
                    }

                    total_view(i, j, k) =
                        total_view(i, j, k) * (1.0 - replace_flag * combined_mask) +
                        field_view(i, j, k) * combined_scale * combined_mask;
                });
        }
    }
    // Ensure all kernels are finished before returning.
    Kokkos::fence();
}

}  // namespace aces
