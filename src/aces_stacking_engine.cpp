/**
 * @file aces_stacking_engine.cpp
 * @brief Implementation of the ACES emission stacking and processing engine.
 *
 * The StackingEngine is the core computational component responsible for processing
 * and combining multiple emission layers according to configured operations and hierarchies.
 * It manages the execution order of physics schemes and handles field operations
 * like addition, multiplication, and masking.
 *
 * Key capabilities:
 * - Layer hierarchy management and execution ordering
 * - Multi-operation support (add, multiply, set, mask)
 * - Temporal scaling (diurnal, weekly, seasonal cycles)
 * - Vertical distribution algorithms
 * - Provenance tracking for reproducibility
 *
 * @author Barry Baker
 * @date 2024
 * @version 1.0
 */

#include "aces/aces_stacking_engine.hpp"

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

#include "aces/aces_provenance.hpp"

namespace aces {

/**
 * @brief Constructs the StackingEngine and performs initial configuration compilation.
 * @param config The ACES configuration.
 */
StackingEngine::StackingEngine(AcesConfig config) : m_config(std::move(config)) {
    PreCompile();
}

/**
 * @brief Pre-compiles the emission layers, sorting them by hierarchy.
 * @details This method also pre-allocates the necessary device-side Views for
 * layer handles to minimize allocations during the simulation run.
 */
void StackingEngine::PreCompile() {
    m_compiled.clear();
    m_provenance_tracker = ProvenanceTracker{};

    for (auto const& [species, layers] : m_config.species_layers) {
        CompiledSpecies spec;
        spec.name = species;
        spec.export_name = species;

        std::vector<LayerContribution> contributions;

        for (auto const& layer : layers) {
            spec.layers.push_back(
                {layer.field_name, layer.operation, layer.scale, layer.hierarchy, layer.masks,
                 layer.scale_fields, layer.diurnal_cycle, layer.weekly_cycle, layer.seasonal_cycle,
                 layer.vdist_method, layer.vdist_layer_start, layer.vdist_layer_end,
                 layer.vdist_p_start, layer.vdist_p_end, layer.vdist_h_start, layer.vdist_h_end});

            LayerContribution contrib;
            contrib.field_name = layer.field_name;
            contrib.operation = layer.operation;
            contrib.hierarchy = layer.hierarchy;
            contrib.category = layer.category;
            contrib.base_scale = layer.scale;
            contrib.masks = layer.masks;
            contrib.scale_fields = layer.scale_fields;
            contrib.diurnal_cycle = layer.diurnal_cycle;
            contrib.weekly_cycle = layer.weekly_cycle;
            contrib.seasonal_cycle = layer.seasonal_cycle;
            contributions.push_back(std::move(contrib));
        }

        std::sort(spec.layers.begin(), spec.layers.end(),
                  [](const CompiledLayer& a, const CompiledLayer& b) {
                      return a.hierarchy < b.hierarchy;
                  });
        // Keep contributions in the same sorted order
        std::sort(contributions.begin(), contributions.end(),
                  [](const LayerContribution& a, const LayerContribution& b) {
                      return a.hierarchy < b.hierarchy;
                  });

        spec.device_layers = Kokkos::View<DeviceLayer*, Kokkos::DefaultExecutionSpace>(
            "device_layers_" + species, spec.layers.size());
        spec.host_layers = Kokkos::create_mirror_view(spec.device_layers);

        m_compiled.push_back(std::move(spec));
        m_provenance_tracker.RegisterSpecies(species, std::move(contributions));
    }
}

/**
 * @brief Binds external fields to device-side Views and prepares layer metadata.
 * @details This is only performed when fields_bound is false, minimizing
 * expensive string-based field resolution.
 * @param spec The species to bind.
 * @param resolver The field resolver.
 * @param nx X dimension.
 * @param ny Y dimension.
 * @param nz Z dimension.
 */
void StackingEngine::BindFields(CompiledSpecies& spec, FieldResolver& resolver, int nx, int ny,
                                int nz) const {
    if (spec.fields_bound) {
        return;
    }

    spec.export_field = resolver.ResolveExportDevice(spec.export_name, nx, ny, nz);

    // Bind vertical coordinate fields if configured
    if (m_config.vertical_config.type != VerticalCoordType::NONE) {
        spec.p_surf =
            resolver.ResolveImportDevice(m_config.vertical_config.p_surf_field, nx, ny, 1);
        if (m_config.vertical_config.type == VerticalCoordType::FV3) {
            spec.ak = resolver.ResolveImportDevice(m_config.vertical_config.ak_field, 1, 1, nz + 1);
            spec.bk = resolver.ResolveImportDevice(m_config.vertical_config.bk_field, 1, 1, nz + 1);
        } else if (m_config.vertical_config.type == VerticalCoordType::MPAS ||
                   m_config.vertical_config.type == VerticalCoordType::WRF) {
            spec.z_coord =
                resolver.ResolveImportDevice(m_config.vertical_config.z_field, nx, ny, nz);
        }
        spec.pbl_height =
            resolver.ResolveImportDevice(m_config.vertical_config.pbl_field, nx, ny, 1);
    }

    for (size_t i = 0; i < spec.layers.size(); ++i) {
        const auto& layer = spec.layers[i];
        DeviceLayer& dev = spec.host_layers(i);

        dev.field = resolver.ResolveImportDevice(layer.field_name, nx, ny, nz);
        dev.replace_flag = (layer.operation == "replace") ? 1.0 : 0.0;

        dev.vdist_method = static_cast<int>(layer.vdist_method);
        dev.vdist_layer_start = layer.vdist_layer_start;
        dev.vdist_layer_end = layer.vdist_layer_end;
        dev.vdist_p_start = layer.vdist_p_start;
        dev.vdist_p_end = layer.vdist_p_end;
        dev.vdist_h_start = layer.vdist_h_start;
        dev.vdist_h_end = layer.vdist_h_end;

        dev.num_scales = 0;
        for (const auto& sf_name : layer.scale_fields) {
            if (dev.num_scales >= DeviceLayer::MAX_SCALES) {
                break;
            }
            auto sf_view = resolver.ResolveImportDevice(sf_name, nx, ny, nz);
            if (sf_view.data() != nullptr) {
                dev.scales[dev.num_scales++] = sf_view;
            }
        }

        dev.num_masks = 0;
        for (const auto& m_name : layer.masks) {
            if (dev.num_masks >= DeviceLayer::MAX_MASKS) {
                break;
            }
            auto m_view = resolver.ResolveImportDevice(m_name, nx, ny, nz);
            if (m_view.data() != nullptr) {
                dev.masks[dev.num_masks++] = m_view;
            }
        }
    }
    spec.fields_bound = true;
}

/**
 * @brief Updates the temporal scaling factors for the currently bound layers.
 * @details Performs a deep_copy of the metadata to the device after updating.
 * @param spec The species to update.
 * @param hour Current hour.
 * @param day_of_week Current day of week.
 */
void StackingEngine::UpdateTemporalScales(CompiledSpecies& spec, int hour, int day_of_week,
                                          int month) {
    for (size_t i = 0; i < spec.layers.size(); ++i) {
        const auto& layer = spec.layers[i];
        DeviceLayer& dev = spec.host_layers(i);

        double scale = layer.base_scale;

        // Apply diurnal cycle (24 hourly factors)
        if (!layer.diurnal_cycle.empty()) {
            auto it_p = m_config.temporal_profiles.find(layer.diurnal_cycle);
            if (it_p != m_config.temporal_profiles.end() && it_p->second.factors.size() == 24) {
                scale *= it_p->second.factors[hour % 24];
            } else {
                auto it_c = m_config.temporal_cycles.find(layer.diurnal_cycle);
                if (it_c != m_config.temporal_cycles.end() && it_c->second.factors.size() == 24) {
                    scale *= it_c->second.factors[hour % 24];
                }
            }
        }

        // Apply day-of-week cycle (7 daily factors)
        if (!layer.weekly_cycle.empty()) {
            auto it_p = m_config.temporal_profiles.find(layer.weekly_cycle);
            if (it_p != m_config.temporal_profiles.end() && it_p->second.factors.size() == 7) {
                scale *= it_p->second.factors[day_of_week % 7];
            } else {
                auto it_c = m_config.temporal_cycles.find(layer.weekly_cycle);
                if (it_c != m_config.temporal_cycles.end() && it_c->second.factors.size() == 7) {
                    scale *= it_c->second.factors[day_of_week % 7];
                }
            }
        }

        // Apply seasonal cycle (12 monthly factors, month is 0-indexed)
        if (!layer.seasonal_cycle.empty()) {
            auto it_p = m_config.temporal_profiles.find(layer.seasonal_cycle);
            if (it_p != m_config.temporal_profiles.end() && it_p->second.factors.size() == 12) {
                scale *= it_p->second.factors[month % 12];
            } else {
                auto it_c = m_config.temporal_cycles.find(layer.seasonal_cycle);
                if (it_c != m_config.temporal_cycles.end() && it_c->second.factors.size() == 12) {
                    scale *= it_c->second.factors[month % 12];
                }
            }
        }

        dev.scale = scale;
    }
    Kokkos::deep_copy(spec.device_layers, spec.host_layers);
}

void StackingEngine::ResetBindings() {
    for (auto& spec : m_compiled) {
        spec.fields_bound = false;
    }
    // Provenance layer structure is preserved; only temporal state is stale
}

/**
 * @brief Dynamically adds a new species to the engine without full recompilation.
 */
void StackingEngine::AddSpecies(const std::string& species_name) {
    // Skip if already compiled
    for (const auto& spec : m_compiled) {
        if (spec.name == species_name) {
            return;
        }
    }

    auto it = m_config.species_layers.find(species_name);
    if (it == m_config.species_layers.end()) {
        return;
    }

    CompiledSpecies spec;
    spec.name = species_name;
    spec.export_name = species_name;

    std::vector<LayerContribution> contributions;
    for (const auto& layer : it->second) {
        spec.layers.push_back({layer.field_name, layer.operation, layer.scale, layer.hierarchy,
                               layer.masks, layer.scale_fields, layer.diurnal_cycle,
                               layer.weekly_cycle, layer.seasonal_cycle, layer.vdist_method,
                               layer.vdist_layer_start, layer.vdist_layer_end, layer.vdist_p_start,
                               layer.vdist_p_end, layer.vdist_h_start, layer.vdist_h_end});

        LayerContribution contrib;
        contrib.field_name = layer.field_name;
        contrib.operation = layer.operation;
        contrib.hierarchy = layer.hierarchy;
        contrib.category = layer.category;
        contrib.base_scale = layer.scale;
        contrib.masks = layer.masks;
        contrib.scale_fields = layer.scale_fields;
        contrib.diurnal_cycle = layer.diurnal_cycle;
        contrib.weekly_cycle = layer.weekly_cycle;
        contrib.seasonal_cycle = layer.seasonal_cycle;
        contributions.push_back(std::move(contrib));
    }

    std::sort(
        spec.layers.begin(), spec.layers.end(),
        [](const CompiledLayer& a, const CompiledLayer& b) { return a.hierarchy < b.hierarchy; });
    std::sort(contributions.begin(), contributions.end(),
              [](const LayerContribution& a, const LayerContribution& b) {
                  return a.hierarchy < b.hierarchy;
              });

    spec.device_layers = Kokkos::View<DeviceLayer*, Kokkos::DefaultExecutionSpace>(
        "device_layers_" + species_name, spec.layers.size());
    spec.host_layers = Kokkos::create_mirror_view(spec.device_layers);

    m_compiled.push_back(std::move(spec));
    m_provenance_tracker.RegisterSpecies(species_name, std::move(contributions));
}

/**
 * @brief Executes the emission stacking using fused Kokkos kernels.
 * @details Iterates through all species, binds their fields, and dispatches
 * a single fused kernel per species to accumulate or replace emissions based
 * on the pre-compiled hierarchy.
 *
 * **Optimization Strategy:**
 * - **Pre-computed Temporal Scales**: All temporal scale factors (diurnal, weekly, seasonal)
 *   are computed on the host in UpdateTemporalScales() and transferred to device via deep_copy.
 *   This avoids redundant calculations inside the kernel.
 * - **POD DeviceLayer Structure**: Uses Plain Old Data (POD) structures with Unmanaged Kokkos::View
 *   handles for efficient device transfer. The entire layer metadata is deep_copied once per
 *   timestep, minimizing host-device communication.
 * - **Fused Kernel Design**: Single Kokkos::parallel_for kernel fuses:
 *   - Layer aggregation (loop over layers)
 *   - Vertical distribution weight calculation (switch statement for branch prediction)
 *   - Scale factor application (fused multiplication loop)
 *   - Mask application (fused multiplication loop)
 *   - Replace vs add operation handling
 *   This reduces memory bandwidth by eliminating intermediate passes through data.
 * - **Memory Access Patterns**: Uses LayoutLeft for coalesced memory access on GPUs.
 *   Vertical distribution calculations are optimized with switch statements for better
 *   branch prediction compared to if-else chains.
 * - **Single Output Write**: Each grid point (i,j,k) is written exactly once at the end
 *   of the kernel, minimizing write traffic.
 *
 * @param resolver Field resolver for import/export views.
 * @param nx X dimension.
 * @param ny Y dimension.
 * @param nz Z dimension.
 * @param default_mask Fallback 1.0 mask.
 * @param hour Current hour (0-23) for temporal cycles.
 * @param day_of_week Current day of week (0-6) for weekly cycles.
 * @param month Current month (0-11) for seasonal cycles.
 * @param provenance Optional provenance tracker for external logging.
 */
void StackingEngine::Execute(
    FieldResolver& resolver, int nx, int ny, int nz,
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> default_mask,
    int hour, int day_of_week, int month, ProvenanceTracker* provenance) {
    if (default_mask.data() == nullptr) {
        default_mask = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>(
            "default_mask_internal", nx, ny, nz);
        Kokkos::deep_copy(default_mask, 1.0);
    }

    for (auto& spec : m_compiled) {
        if (spec.layers.empty()) {
            continue;
        }
        BindFields(spec, resolver, nx, ny, nz);

        if (spec.export_field.data() == nullptr) {
            continue;
        }

        UpdateTemporalScales(spec, hour, day_of_week, month);

        // Update provenance with effective scales for this timestep
        {
            std::vector<double> eff_scales(spec.layers.size());
            for (size_t li = 0; li < spec.layers.size(); ++li) {
                eff_scales[li] = spec.host_layers(li).scale;
            }
            m_provenance_tracker.UpdateTemporalScales(spec.name, hour, day_of_week, month,
                                                      eff_scales);
            if (provenance != nullptr) {
                provenance->UpdateTemporalScales(spec.name, hour, day_of_week, month, eff_scales);
            }
        }

        auto total_view = spec.export_field;
        Kokkos::deep_copy(total_view, 0.0);

        auto layers = spec.device_layers;
        int num_layers = static_cast<int>(layers.extent(0));

        auto ak = spec.ak;
        auto bk = spec.bk;
        auto ps = spec.p_surf;
        auto z_coord = spec.z_coord;
        auto pbl = spec.pbl_height;
        auto vtype = m_config.vertical_config.type;

        Kokkos::parallel_for(
            "StackingEngine_FusedSpeciesKernel",
            Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {nx, ny, nz}),
            KOKKOS_LAMBDA(int i, int j, int k) {
                double accumulated = 0.0;

                // Iterate over all layers
                for (int l = 0; l < num_layers; ++l) {
                    const auto& layer = layers(l);
                    if (layer.field.data() == nullptr) {
                        continue;
                    }

                    // Determine distribution weight
                    double weight = 0.0;
                    bool in_vertical_range = false;

                    // Support 3D emission fields natively
                    bool is_3d_field = (layer.field.extent(2) == nz);

                    if (is_3d_field) {
                        in_vertical_range = true;
                        weight = 1.0;
                    } else {
                        // 2D field logic
                        if (layer.vdist_method == 0) {  // SINGLE
                            if (k == layer.vdist_layer_start) {
                                in_vertical_range = true;
                                weight = 1.0;
                            }
                        } else if (layer.vdist_method == 1) {  // RANGE
                            if (k >= layer.vdist_layer_start && k <= layer.vdist_layer_end) {
                                in_vertical_range = true;
                                weight =
                                    1.0 / (layer.vdist_layer_end - layer.vdist_layer_start + 1);
                            }
                        } else if (layer.vdist_method == 2) {  // PRESSURE
                            // Compute total overlap for this specific layer's pressure range
                            double layer_total_overlap = 0.0;
                            for (int l2 = 0; l2 < nz; ++l2) {
                                double p_top2 = 0.0, p_bot2 = 0.0;
                                if (vtype == VerticalCoordType::FV3 && ak.data() != nullptr &&
                                    bk.data() != nullptr && ps.data() != nullptr) {
                                    p_top2 = ak(0, 0, l2) + bk(0, 0, l2) * ps(i, j, 0);
                                    p_bot2 = ak(0, 0, l2 + 1) + bk(0, 0, l2 + 1) * ps(i, j, 0);
                                } else if ((vtype == VerticalCoordType::MPAS ||
                                            vtype == VerticalCoordType::WRF) &&
                                           z_coord.data() != nullptr) {
                                    constexpr double P0 = 101325.0;
                                    constexpr double H = 8000.0;
                                    p_top2 = P0 * Kokkos::exp(-z_coord(i, j, l2) / H);
                                    if (l2 + 1 < nz) {
                                        p_bot2 = P0 * Kokkos::exp(-z_coord(i, j, l2 + 1) / H);
                                    } else {
                                        p_bot2 = ps.data() != nullptr ? ps(i, j, 0) : P0;
                                    }
                                }
                                double overlap_top2 =
                                    (p_top2 > layer.vdist_p_start) ? p_top2 : layer.vdist_p_start;
                                double overlap_bot2 =
                                    (p_bot2 < layer.vdist_p_end) ? p_bot2 : layer.vdist_p_end;
                                if (overlap_bot2 > overlap_top2) {
                                    layer_total_overlap += (overlap_bot2 - overlap_top2);
                                }
                            }

                            double p_top = 0.0, p_bot = 0.0;
                            if (vtype == VerticalCoordType::FV3 && ak.data() != nullptr &&
                                bk.data() != nullptr && ps.data() != nullptr) {
                                p_top = ak(0, 0, k) + bk(0, 0, k) * ps(i, j, 0);
                                p_bot = ak(0, 0, k + 1) + bk(0, 0, k + 1) * ps(i, j, 0);
                            } else if ((vtype == VerticalCoordType::MPAS ||
                                        vtype == VerticalCoordType::WRF) &&
                                       z_coord.data() != nullptr) {
                                constexpr double P0 = 101325.0;
                                constexpr double H = 8000.0;
                                p_top = P0 * Kokkos::exp(-z_coord(i, j, k) / H);
                                if (k + 1 < nz) {
                                    p_bot = P0 * Kokkos::exp(-z_coord(i, j, k + 1) / H);
                                } else {
                                    p_bot = ps.data() != nullptr ? ps(i, j, 0) : P0;
                                }
                            }
                            double overlap_top =
                                (p_top > layer.vdist_p_start) ? p_top : layer.vdist_p_start;
                            double overlap_bot =
                                (p_bot < layer.vdist_p_end) ? p_bot : layer.vdist_p_end;

                            if (overlap_bot > overlap_top && layer_total_overlap > 0.0) {
                                in_vertical_range = true;
                                weight = (overlap_bot - overlap_top) / layer_total_overlap;
                            }
                        } else if (layer.vdist_method == 3) {  // HEIGHT
                            if (z_coord.data() != nullptr) {
                                double layer_total_overlap = 0.0;
                                for (int l2 = 0; l2 < nz; ++l2) {
                                    double z_t = z_coord(i, j, l2);
                                    double z_b = z_coord(i, j, l2 + 1);
                                    double z_top2 = (z_t > z_b) ? z_t : z_b;
                                    double z_bot2 = (z_t < z_b) ? z_t : z_b;
                                    double overlap_top2 =
                                        (z_top2 < layer.vdist_h_end) ? z_top2 : layer.vdist_h_end;
                                    double overlap_bot2 = (z_bot2 > layer.vdist_h_start)
                                                              ? z_bot2
                                                              : layer.vdist_h_start;
                                    if (overlap_top2 > overlap_bot2) {
                                        layer_total_overlap += (overlap_top2 - overlap_bot2);
                                    }
                                }

                                double z_t = z_coord(i, j, k);
                                double z_b = z_coord(i, j, k + 1);
                                double z_top = (z_t > z_b) ? z_t : z_b;
                                double z_bot = (z_t < z_b) ? z_t : z_b;
                                double overlap_top =
                                    (z_top < layer.vdist_h_end) ? z_top : layer.vdist_h_end;
                                double overlap_bot =
                                    (z_bot > layer.vdist_h_start) ? z_bot : layer.vdist_h_start;

                                if (overlap_top > overlap_bot && layer_total_overlap > 0.0) {
                                    in_vertical_range = true;
                                    weight = (overlap_top - overlap_bot) / layer_total_overlap;
                                }
                            }
                        } else if (layer.vdist_method == 4) {  // PBL
                            if (pbl.data() != nullptr && z_coord.data() != nullptr) {
                                double h_pbl = pbl(i, j, 0);
                                double layer_total_overlap = 0.0;
                                for (int l2 = 0; l2 < nz; ++l2) {
                                    double z_a = z_coord(i, j, l2);
                                    double z_b2 = z_coord(i, j, l2 + 1);
                                    double z_top2 = (z_a > z_b2) ? z_a : z_b2;
                                    double z_bot2 = (z_a < z_b2) ? z_a : z_b2;
                                    double ov_top = (z_top2 < h_pbl) ? z_top2 : h_pbl;
                                    double ov_bot = (z_bot2 > 0.0) ? z_bot2 : 0.0;
                                    if (ov_top > ov_bot) {
                                        layer_total_overlap += (ov_top - ov_bot);
                                    }
                                }
                                double z_a = z_coord(i, j, k);
                                double z_b2 = z_coord(i, j, k + 1);
                                double z_top = (z_a > z_b2) ? z_a : z_b2;
                                double z_bot = (z_a < z_b2) ? z_a : z_b2;
                                double ov_top = (z_top < h_pbl) ? z_top : h_pbl;
                                double ov_bot = (z_bot > 0.0) ? z_bot : 0.0;

                                if (ov_top > ov_bot && layer_total_overlap > 0.0) {
                                    in_vertical_range = true;
                                    weight = (ov_top - ov_bot) / layer_total_overlap;
                                }
                            }
                        }
                    }

                    if (!in_vertical_range) {
                        continue;
                    }

                    double val = 0.0;
                    if (is_3d_field) {
                        val = layer.field(i, j, k) * weight;
                    } else {
                        val = layer.field(i, j, 0) * weight;
                    }

                    // Fused scale/mask
                    double combined_scale = layer.scale;
                    for (int s = 0; s < layer.num_scales; ++s) {
                        if (layer.scales[s].data() != nullptr) {
                            double scale_val = (layer.scales[s].extent(2) == 1)
                                                   ? layer.scales[s](i, j, 0)
                                                   : layer.scales[s](i, j, k);
                            combined_scale *= scale_val;
                        }
                    }

                    double combined_mask = 1.0;
                    if (layer.num_masks > 0) {
                        for (int m = 0; m < layer.num_masks; ++m) {
                            if (layer.masks[m].data() != nullptr) {
                                double mask_val = (layer.masks[m].extent(2) == 1)
                                                      ? layer.masks[m](i, j, 0)
                                                      : layer.masks[m](i, j, k);
                                combined_mask *= mask_val;
                            }
                        }
                    } else {
                        if (default_mask.data() != nullptr) {
                            combined_mask = default_mask(i, j, k);
                        }
                    }

                    double contribution = val * combined_scale * combined_mask;

                    if (layer.replace_flag > 0.5) {
                        if (combined_mask > 0.0) accumulated = contribution;
                    } else {
                        accumulated += contribution;
                    }
                }

                total_view(i, j, k) = accumulated;
            });
    }
    Kokkos::fence();
}

}  // namespace aces
