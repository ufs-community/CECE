#include "aces/aces_stacking_engine.hpp"

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
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
        spec.export_name = species;
        for (auto const& layer : layers) {
            spec.layers.push_back({layer.field_name, layer.operation, layer.scale, layer.hierarchy,
                                   layer.masks, layer.scale_fields, layer.diurnal_cycle,
                                   layer.weekly_cycle, layer.vdist_method, layer.vdist_layer_start,
                                   layer.vdist_layer_end, layer.vdist_p_start, layer.vdist_p_end,
                                   layer.vdist_h_start, layer.vdist_h_end});
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
 * @details This is only performed when fields_bound is false, minimizing
 * expensive string-based field resolution.
 * @param spec The species to bind.
 * @param resolver The field resolver.
 * @param nx X dimension.
 * @param ny Y dimension.
 * @param nz Z dimension.
 */
void StackingEngine::BindFields(CompiledSpecies& spec, FieldResolver& resolver, int nx, int ny,
                                int nz) {
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
void StackingEngine::UpdateTemporalScales(CompiledSpecies& spec, int hour, int day_of_week) {
    for (size_t i = 0; i < spec.layers.size(); ++i) {
        const auto& layer = spec.layers[i];
        DeviceLayer& dev = spec.host_layers(i);

        double scale = layer.base_scale;
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
        dev.scale = scale;
    }
    Kokkos::deep_copy(spec.device_layers, spec.host_layers);
}

void StackingEngine::ResetBindings() {
    for (auto& spec : m_compiled) {
        spec.fields_bound = false;
    }
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
        if (spec.layers.empty()) {
            continue;
        }

        BindFields(spec, resolver, nx, ny, nz);

        if (spec.export_field.data() == nullptr) {
            continue;
        }

        UpdateTemporalScales(spec, hour, day_of_week);

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
                for (int l = 0; l < num_layers; ++l) {
                    const auto& layer = layers(l);
                    if (layer.field.data() == nullptr) {
                        continue;
                    }

                    // Determine if this (i,j,k) point should receive emissions from this layer
                    // and calculate the fractional weight for distribution if it's a 2D field.
                    double weight = 0.0;
                    bool in_vertical_range = false;

                    if (layer.vdist_method == 0) {  // SINGLE
                        if (k == layer.vdist_layer_start) {
                            in_vertical_range = true;
                            weight = 1.0;
                        }
                    } else if (layer.vdist_method == 1) {  // RANGE
                        if (k >= layer.vdist_layer_start && k <= layer.vdist_layer_end) {
                            in_vertical_range = true;
                            weight = 1.0 / (layer.vdist_layer_end - layer.vdist_layer_start + 1);
                        }
                    } else if (layer.vdist_method == 2) {  // PRESSURE
                        double p_top = 0.0, p_bot = 0.0;
                        if (vtype == VerticalCoordType::FV3 && ak.data() && bk.data() &&
                            ps.data()) {
                            // Correct indexing for 1D/3D coefficients
                            p_top = ak(0, 0, k) + bk(0, 0, k) * ps(i, j, 0);
                            p_bot = ak(0, 0, k + 1) + bk(0, 0, k + 1) * ps(i, j, 0);
                        } else if ((vtype == VerticalCoordType::MPAS ||
                                    vtype == VerticalCoordType::WRF) &&
                                   z_coord.data()) {
                            constexpr double P0 = 101325.0;
                            constexpr double H = 8000.0;
                            p_top = P0 * Kokkos::exp(-z_coord(i, j, k) / H);
                            if (k + 1 < nz) {
                                p_bot = P0 * Kokkos::exp(-z_coord(i, j, k + 1) / H);
                            } else {
                                p_bot = ps.data() ? ps(i, j, 0) : P0;
                            }
                        }
                        double overlap_top =
                            (p_top > layer.vdist_p_start) ? p_top : layer.vdist_p_start;
                        double overlap_bot =
                            (p_bot < layer.vdist_p_end) ? p_bot : layer.vdist_p_end;

                        if (overlap_bot > overlap_top) {
                            in_vertical_range = true;
                            weight = (overlap_bot - overlap_top) /
                                     (layer.vdist_p_end - layer.vdist_p_start);
                        }
                    } else if (layer.vdist_method == 3) {  // HEIGHT
                        if (z_coord.data()) {
                            double z = z_coord(i, j, k);
                            if (z >= layer.vdist_h_start && z <= layer.vdist_h_end) {
                                in_vertical_range = true;
                                // Per-column normalization
                                int count = 0;
                                for (int kk = 0; kk < nz; ++kk) {
                                    if (z_coord(i, j, kk) >= layer.vdist_h_start &&
                                        z_coord(i, j, kk) <= layer.vdist_h_end) {
                                        count++;
                                    }
                                }
                                if (count > 0) {
                                    weight = 1.0 / count;
                                }
                            }
                        }
                    } else if (layer.vdist_method == 4) {  // PBL
                        if (pbl.data() && z_coord.data()) {
                            double z = z_coord(i, j, k);
                            double h_pbl = pbl(i, j, 0);
                            if (z <= h_pbl) {
                                in_vertical_range = true;
                                // Per-column normalization
                                int count = 0;
                                for (int kk = 0; kk < nz; ++kk) {
                                    if (z_coord(i, j, kk) <= h_pbl) {
                                        count++;
                                    }
                                }
                                if (count > 0) {
                                    weight = 1.0 / count;
                                }
                            }
                        }
                    }

                    if (!in_vertical_range) {
                        continue;
                    }

                    double combined_scale = layer.scale;
                    for (int s = 0; s < layer.num_scales; ++s) {
                        if (layer.scales[s].extent(2) == 1) {
                            combined_scale *= layer.scales[s](i, j, 0);
                        } else {
                            combined_scale *= layer.scales[s](i, j, k);
                        }
                    }

                    double combined_mask = 0.0;
                    if (layer.num_masks > 0) {
                        combined_mask = 1.0;
                        for (int m = 0; m < layer.num_masks; ++m) {
                            if (layer.masks[m].extent(2) == 1) {
                                combined_mask *= layer.masks[m](i, j, 0);
                            } else {
                                combined_mask *= layer.masks[m](i, j, k);
                            }
                        }
                    } else {
                        combined_mask = default_mask(i, j, k);
                    }

                    double val = 0.0;
                    if (layer.field.extent(2) == 1) {
                        val = layer.field(i, j, 0) * weight;
                    } else {
                        val = layer.field(i, j, k);
                    }

                    total_view(i, j, k) =
                        total_view(i, j, k) * (1.0 - layer.replace_flag * combined_mask) +
                        val * combined_scale * combined_mask;
                }
            });
    }
    Kokkos::fence();
}

}  // namespace aces
