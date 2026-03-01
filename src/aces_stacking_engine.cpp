#include "aces/aces_stacking_engine.hpp"

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <iostream>

namespace aces {

/**
 * @brief Constructs the StackingEngine and performs initial configuration compilation.
 * @param config The ACES configuration.
 */
StackingEngine::StackingEngine(const AcesConfig& config) : m_config(config) {
    PreCompile();
}

/**
 * @brief Pre-compiles the emission layers, sorting them by hierarchy.
 * @details This method also pre-allocates the necessary device-side Views for
 * layer handles to minimize allocations during the simulation run.
 */
void StackingEngine::PreCompile() {
    m_compiled.clear();
    for (auto const& [species, layers] : m_config.species_layers) {
        CompiledSpecies spec;
        spec.name = species;
        for (auto const& layer : layers) {
            spec.layers.push_back({layer.field_name, layer.operation, layer.scale, layer.hierarchy,
                                   layer.masks, layer.scale_fields, layer.diurnal_cycle,
                                   layer.weekly_cycle});
        }

        std::sort(spec.layers.begin(), spec.layers.end(),
                  [](const CompiledLayer& a, const CompiledLayer& b) {
                      return a.hierarchy < b.hierarchy;
                  });

        spec.device_layers = Kokkos::View<DeviceLayer*, Kokkos::DefaultExecutionSpace>(
            "device_layers_" + species, spec.layers.size());
        spec.host_layers = Kokkos::create_mirror_view(spec.device_layers);

        m_compiled.push_back(std::move(spec));
    }
}

/**
 * @brief Binds external fields to device-side Views and prepares layer metadata.
 * @param spec The species to bind.
 * @param resolver The field resolver.
 * @param nx X dimension.
 * @param ny Y dimension.
 * @param nz Z dimension.
 * @param hour Current hour.
 * @param day_of_week Current day of week.
 */
void StackingEngine::BindSpecies(CompiledSpecies& spec, FieldResolver& resolver, int nx, int ny,
                                 int nz, int hour, int day_of_week) {
    for (size_t i = 0; i < spec.layers.size(); ++i) {
        const auto& layer = spec.layers[i];
        DeviceLayer dev;

        dev.field = resolver.ResolveImportDevice(layer.field_name, nx, ny, nz);
        dev.replace_flag = (layer.operation == "replace") ? 1.0 : 0.0;

        double scale = layer.base_scale;
        if (!layer.diurnal_cycle.empty()) {
            auto it = m_config.temporal_cycles.find(layer.diurnal_cycle);
            if (it != m_config.temporal_cycles.end() && it->second.factors.size() == 24) {
                scale *= it->second.factors[hour % 24];
            }
        }
        if (!layer.weekly_cycle.empty()) {
            auto it = m_config.temporal_cycles.find(layer.weekly_cycle);
            if (it != m_config.temporal_cycles.end() && it->second.factors.size() == 7) {
                scale *= it->second.factors[day_of_week % 7];
            }
        }
        dev.scale = scale;

        dev.num_scales = 0;
        for (const auto& sf_name : layer.scale_fields) {
            if (dev.num_scales >= DeviceLayer::MAX_SCALES) break;
            auto sf_view = resolver.ResolveImportDevice(sf_name, nx, ny, nz);
            if (sf_view.data() != nullptr) {
                dev.scales[dev.num_scales++] = sf_view;
            }
        }

        dev.num_masks = 0;
        for (const auto& m_name : layer.masks) {
            if (dev.num_masks >= DeviceLayer::MAX_MASKS) break;
            auto m_view = resolver.ResolveImportDevice(m_name, nx, ny, nz);
            if (m_view.data() != nullptr) {
                dev.masks[dev.num_masks++] = m_view;
            }
        }

        spec.host_layers(i) = dev;
    }

    Kokkos::deep_copy(spec.device_layers, spec.host_layers);
}

/**
 * @brief Executes the emission stacking using fused Kokkos kernels.
 * @details Iterates through all species, binds their fields, and dispatches
 * a single fused kernel per species to accumulate or replace emissions based
 * on the pre-compiled hierarchy.
 *
 * @param resolver Field resolver for import/export views.
 * @param nx X dimension.
 * @param ny Y dimension.
 * @param nz Z dimension.
 * @param default_mask Fallback 1.0 mask.
 * @param hour Current hour.
 * @param day_of_week Current day of week.
 */
void StackingEngine::Execute(
    FieldResolver& resolver, int nx, int ny, int nz,
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> default_mask,
    int hour, int day_of_week) {
    if (!default_mask.data()) {
        default_mask = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>(
            "default_mask_internal", nx, ny, nz);
        Kokkos::deep_copy(default_mask, 1.0);
    }

    for (auto& spec : m_compiled) {
        std::string export_name = "total_" + spec.name + "_emissions";
        auto total_view = resolver.ResolveExportDevice(export_name, nx, ny, nz);

        if (total_view.data() == nullptr || spec.layers.empty()) {
            continue;
        }

        BindSpecies(spec, resolver, nx, ny, nz, hour, day_of_week);

        Kokkos::deep_copy(total_view, 0.0);

        auto layers = spec.device_layers;
        int num_layers = layers.extent(0);

        Kokkos::parallel_for(
            "StackingEngine_FusedSpeciesKernel",
            Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {nx, ny, nz}),
            KOKKOS_LAMBDA(int i, int j, int k) {
                for (int l = 0; l < num_layers; ++l) {
                    const auto& layer = layers(l);
                    if (layer.field.data() == nullptr) continue;

                    double combined_scale = layer.scale;
                    for (int s = 0; s < layer.num_scales; ++s) {
                        combined_scale *= layer.scales[s](i, j, k);
                    }

                    double combined_mask = 0.0;
                    if (layer.num_masks > 0) {
                        combined_mask = 1.0;
                        for (int m = 0; m < layer.num_masks; ++m) {
                            combined_mask *= layer.masks[m](i, j, k);
                        }
                    } else {
                        combined_mask = default_mask(i, j, k);
                    }

                    total_view(i, j, k) =
                        total_view(i, j, k) * (1.0 - layer.replace_flag * combined_mask) +
                        layer.field(i, j, k) * combined_scale * combined_mask;
                }
            });
    }
    Kokkos::fence();
}

}  // namespace aces
