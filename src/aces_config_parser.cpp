/**
 * @file aces_config_parser.cpp
 * @brief Implementation of the YAML configuration parser for ACES.
 *
 * This module handles parsing of YAML configuration files containing:
 * - Species definitions and emission layer configurations
 * - Physics scheme parameters and settings
 * - Grid and timing configuration options
 * - Data stream specifications for TIDE integration
 *
 * The parser provides robust error handling, validation, and default value
 * management to ensure configuration consistency across ACES components.
 *
 * @author Barry Baker
 * @date 2024
 * @version 1.0
 */

#include <sys/stat.h>
#include <yaml-cpp/yaml.h>

#include <iostream>
#include <string>
#include <vector>

#include "aces/aces_config.hpp"

namespace aces {

/**
 * @brief Parses the ACES configuration from a YAML file.
 *
 * This function reads species definitions, emission layers, and physics scheme
 * configurations from the specified file.
 *
 * @param filename Path to the YAML configuration file.
 * @return AcesConfig object containing all parsed information.
 */
AcesConfig ParseConfig(const std::string& filename) {
    std::cout << "DEBUG: ParseConfig called with filename: '" << filename << "'" << std::endl;

    // Check if file exists
    struct stat buffer;
    if (stat(filename.c_str(), &buffer) != 0) {
        std::cout << "ERROR: File does not exist: " << filename << std::endl;
        throw std::runtime_error("File not found: " + filename);
    }
    std::cout << "DEBUG: File exists, proceeding to load" << std::endl;

    AcesConfig config;
    YAML::Node root = YAML::LoadFile(filename);

    // Parse species and their associated emission layers
    if (root["species"]) {
        for (auto const& species_node : root["species"]) {
            std::string species_name = species_node.first.as<std::string>();
            std::vector<EmissionLayer> layers;

            for (auto const& layer_node : species_node.second) {
                EmissionLayer layer;
                layer.operation = layer_node["operation"].as<std::string>();
                layer.field_name = layer_node["field"].as<std::string>();
                if (layer_node["mask"]) {
                    if (layer_node["mask"].IsSequence()) {
                        for (auto const& m : layer_node["mask"]) {
                            layer.masks.push_back(m.as<std::string>());
                        }
                    } else {
                        layer.masks.push_back(layer_node["mask"].as<std::string>());
                    }
                }
                if (layer_node["scale"]) {
                    layer.scale = layer_node["scale"].as<double>();
                }
                if (layer_node["hierarchy"]) {
                    layer.hierarchy = layer_node["hierarchy"].as<int>();
                }
                if (layer_node["category"]) {
                    layer.category = layer_node["category"].as<std::string>();
                }
                if (layer_node["scale_fields"]) {
                    for (auto const& sf_node : layer_node["scale_fields"]) {
                        layer.scale_fields.push_back(sf_node.as<std::string>());
                    }
                }
                if (layer_node["diurnal_cycle"]) {
                    layer.diurnal_cycle = layer_node["diurnal_cycle"].as<std::string>();
                }
                if (layer_node["weekly_cycle"]) {
                    layer.weekly_cycle = layer_node["weekly_cycle"].as<std::string>();
                }
                if (layer_node["seasonal_cycle"]) {
                    layer.seasonal_cycle = layer_node["seasonal_cycle"].as<std::string>();
                }
                if (layer_node["vdist"]) {
                    auto vdist = layer_node["vdist"];
                    if (vdist["method"]) {
                        std::string method_str = vdist["method"].as<std::string>();
                        if (method_str == "range") {
                            layer.vdist_method = VerticalDistributionMethod::RANGE;
                        } else if (method_str == "pressure") {
                            layer.vdist_method = VerticalDistributionMethod::PRESSURE;
                        } else if (method_str == "height") {
                            layer.vdist_method = VerticalDistributionMethod::HEIGHT;
                        } else if (method_str == "pbl") {
                            layer.vdist_method = VerticalDistributionMethod::PBL;
                        } else {
                            layer.vdist_method = VerticalDistributionMethod::SINGLE;
                        }
                    }
                    if (vdist["layer_start"]) {
                        layer.vdist_layer_start = vdist["layer_start"].as<int>();
                    }
                    if (vdist["layer_end"]) {
                        layer.vdist_layer_end = vdist["layer_end"].as<int>();
                    }
                    if (vdist["p_start"]) {
                        layer.vdist_p_start = vdist["p_start"].as<double>();
                    }
                    if (vdist["p_end"]) {
                        layer.vdist_p_end = vdist["p_end"].as<double>();
                    }
                    if (vdist["h_start"]) {
                        layer.vdist_h_start = vdist["h_start"].as<double>();
                    }
                    if (vdist["h_end"]) {
                        layer.vdist_h_end = vdist["h_end"].as<double>();
                    }
                }
                layers.push_back(layer);
            }
            config.species_layers[species_name] = layers;
        }
    }

    // Parse meteorology mapping
    if (root["meteorology"]) {
        for (auto const& met_node : root["meteorology"]) {
            config.met_mapping[met_node.first.as<std::string>()] =
                met_node.second.as<std::string>();
        }
    }

    // Parse meteorology registry (internal name -> list of external aliases)
    if (root["met_registry"]) {
        for (auto const& reg_node : root["met_registry"]) {
            std::string internal_name = reg_node.first.as<std::string>();
            std::vector<std::string> aliases;
            if (reg_node.second.IsSequence()) {
                for (auto const& alias : reg_node.second) {
                    aliases.push_back(alias.as<std::string>());
                }
            } else if (reg_node.second.IsScalar()) {
                aliases.push_back(reg_node.second.as<std::string>());
            }
            config.met_registry[internal_name] = std::move(aliases);
        }
    }

    // Parse scale factor mapping
    if (root["scale_factors"]) {
        for (auto const& sf_node : root["scale_factors"]) {
            config.scale_factor_mapping[sf_node.first.as<std::string>()] =
                sf_node.second.as<std::string>();
        }
    }

    // Parse mask mapping
    if (root["masks"]) {
        for (auto const& mask_node : root["masks"]) {
            config.mask_mapping[mask_node.first.as<std::string>()] =
                mask_node.second.as<std::string>();
        }
    }

    // Parse temporal cycles (backward compatibility)
    if (root["temporal_cycles"]) {
        for (auto const& cycle_node : root["temporal_cycles"]) {
            std::string cycle_name = cycle_node.first.as<std::string>();
            TemporalCycle cycle;
            if (cycle_node.second.IsSequence()) {
                for (auto const& factor : cycle_node.second) {
                    cycle.factors.push_back(factor.as<double>());
                }
            }
            config.temporal_cycles[cycle_name] = cycle;
        }
    }

    // Parse temporal profiles
    if (root["temporal_profiles"]) {
        for (auto const& profile_node : root["temporal_profiles"]) {
            std::string profile_name = profile_node.first.as<std::string>();
            TemporalCycle profile;
            if (profile_node.second.IsSequence()) {
                for (auto const& factor : profile_node.second) {
                    profile.factors.push_back(factor.as<double>());
                }
            }
            config.temporal_profiles[profile_name] = profile;
        }
    }

    // Parse physics schemes and their options
    if (root["physics_schemes"]) {
        for (auto const& scheme_node : root["physics_schemes"]) {
            PhysicsSchemeConfig scheme;
            scheme.name = scheme_node["name"].as<std::string>();
            if (scheme_node["language"]) {
                scheme.language = scheme_node["language"].as<std::string>();
            }
            if (scheme_node["options"]) {
                scheme.options = scheme_node["options"];
            }
            config.physics_schemes.push_back(scheme);
        }
    }

    // Parse diagnostics
    if (root["diagnostics"]) {
        auto diag_node = root["diagnostics"];
        if (diag_node.IsSequence()) {
            // Backward compatibility for simple list
            for (auto const& item : diag_node) {
                config.diagnostics.variables.push_back(item.as<std::string>());
            }
        } else if (diag_node.IsMap()) {
            if (diag_node["output_interval"]) {
                config.diagnostics.output_interval_seconds = diag_node["output_interval"].as<int>();
            }
            if (diag_node["grid_type"]) {
                config.diagnostics.grid_type = diag_node["grid_type"].as<std::string>();
            }
            if (diag_node["grid_file"]) {
                config.diagnostics.grid_file = diag_node["grid_file"].as<std::string>();
            }
            if (diag_node["nx"]) {
                config.diagnostics.nx = diag_node["nx"].as<int>();
            }
            if (diag_node["ny"]) {
                config.diagnostics.ny = diag_node["ny"].as<int>();
            }
            if (diag_node["variables"]) {
                for (auto const& var_node : diag_node["variables"]) {
                    config.diagnostics.variables.push_back(var_node.as<std::string>());
                }
            }
        }
    }

    // Parse vertical grid configuration
    if (root["vertical_grid"]) {
        auto v_node = root["vertical_grid"];
        if (v_node["type"]) {
            std::string type_str = v_node["type"].as<std::string>();
            if (type_str == "fv3") {
                config.vertical_config.type = VerticalCoordType::FV3;
            } else if (type_str == "mpas") {
                config.vertical_config.type = VerticalCoordType::MPAS;
            } else if (type_str == "wrf") {
                config.vertical_config.type = VerticalCoordType::WRF;
            }
        }
        if (v_node["ak_field"]) {
            config.vertical_config.ak_field = v_node["ak_field"].as<std::string>();
        }
        if (v_node["bk_field"]) {
            config.vertical_config.bk_field = v_node["bk_field"].as<std::string>();
        }
        if (v_node["p_surf_field"]) {
            config.vertical_config.p_surf_field = v_node["p_surf_field"].as<std::string>();
        }
        if (v_node["z_field"]) {
            config.vertical_config.z_field = v_node["z_field"].as<std::string>();
        }
        if (v_node["pbl_field"]) {
            config.vertical_config.pbl_field = v_node["pbl_field"].as<std::string>();
        }
    }

    // Parse aces_data configuration
    YAML::Node data_node;
    if (root["aces_data"]) {
        data_node = root["aces_data"];
    }
    if (data_node && data_node["streams"]) {
        for (auto const& stream_node : data_node["streams"]) {
            AcesDataStreamConfig stream;
            if (stream_node["name"]) {
                stream.name = stream_node["name"].as<std::string>();
            }

            if (stream_node["file"]) {
                if (stream_node["file"].IsSequence()) {
                    for (auto const& f : stream_node["file"]) {
                        stream.file_paths.push_back(f.as<std::string>());
                    }
                } else {
                    stream.file_paths.push_back(stream_node["file"].as<std::string>());
                }
            }

            if (stream_node["variables"]) {
                for (auto const& var_node : stream_node["variables"]) {
                    AcesDataVariableConfig var;
                    if (var_node.IsScalar()) {
                        var.name_in_file = var_node.as<std::string>();
                        var.name_in_model = var.name_in_file;
                    } else if (var_node.IsMap()) {
                        if (var_node["file"]) {
                            var.name_in_file = var_node["file"].as<std::string>();
                        }
                        if (var_node["model"]) {
                            var.name_in_model = var_node["model"].as<std::string>();
                        }
                    }
                    stream.variables.push_back(var);
                }
            } else if (!stream.name.empty()) {
                AcesDataVariableConfig var;
                var.name_in_file = stream.name;
                var.name_in_model = stream.name;
                stream.variables.push_back(var);
            }

            if (stream_node["taxmode"]) {
                stream.taxmode = stream_node["taxmode"].as<std::string>();
            }
            if (stream_node["tintalgo"]) {
                stream.tintalgo = stream_node["tintalgo"].as<std::string>();
            } else if (stream_node["interpolation"]) {
                stream.tintalgo = stream_node["interpolation"].as<std::string>();
            }
            if (stream_node["mapalgo"]) {
                stream.mapalgo = stream_node["mapalgo"].as<std::string>();
            }
            if (stream_node["dtlimit"]) {
                stream.dtlimit = stream_node["dtlimit"].as<int>();
            }
            if (stream_node["yearFirst"]) {
                stream.yearFirst = stream_node["yearFirst"].as<int>();
            }
            if (stream_node["yearLast"]) {
                stream.yearLast = stream_node["yearLast"].as<int>();
            }
            if (stream_node["yearAlign"]) {
                stream.yearAlign = stream_node["yearAlign"].as<int>();
            }
            if (stream_node["offset"]) {
                stream.offset = stream_node["offset"].as<int>();
            }
            if (stream_node["meshfile"]) {
                stream.meshfile = stream_node["meshfile"].as<std::string>();
            }
            if (stream_node["lev_dimname"]) {
                stream.lev_dimname = stream_node["lev_dimname"].as<std::string>();
            }
            if (stream_node["time_var"]) {
                stream.time_var = stream_node["time_var"].as<std::string>();
            }
            if (stream_node["lon_var"]) {
                stream.lon_var = stream_node["lon_var"].as<std::string>();
            }
            if (stream_node["lat_var"]) {
                stream.lat_var = stream_node["lat_var"].as<std::string>();
            }

            config.aces_data.streams.push_back(stream);
        }
    }

    // Parse standalone output configuration (Requirement 11.12)
    if (root["output"]) {
        auto out_node = root["output"];
        config.output_config.enabled = true;

        if (out_node["directory"]) {
            config.output_config.directory = out_node["directory"].as<std::string>();
        }
        if (out_node["filename_pattern"]) {
            config.output_config.filename_pattern = out_node["filename_pattern"].as<std::string>();
        }
        if (out_node["frequency_steps"]) {
            config.output_config.frequency_steps = out_node["frequency_steps"].as<int>();
        }
        if (out_node["fields"]) {
            for (auto const& f : out_node["fields"]) {
                config.output_config.fields.push_back(f.as<std::string>());
            }
        }
        if (out_node["diagnostics"]) {
            config.output_config.include_diagnostics = out_node["diagnostics"].as<bool>();
        }

        // Validate output directory writability; log INFO if it needs to be created.
        // Actual directory creation is deferred to AcesStandaloneWriter::Initialize.
        const std::string& dir = config.output_config.directory;
        struct stat st {};
        if (stat(dir.c_str(), &st) != 0) {
            std::cout << "[ACES INFO] Output directory '" << dir
                      << "' does not exist and will be created at runtime.\n";
        } else if (!(st.st_mode & S_IWUSR)) {
            std::cerr << "[ACES ERROR] Output directory '" << dir << "' is not writable.\n";
        }
    }

    // Parse driver configuration (optional, for standalone execution)
    // Requirements: 1.1, 2.1, 3.1, 14.1, 15.1
    if (root["driver"]) {
        auto driver_node = root["driver"];

        if (driver_node["start_time"]) {
            config.driver_config.start_time = driver_node["start_time"].as<std::string>();
        }
        if (driver_node["end_time"]) {
            config.driver_config.end_time = driver_node["end_time"].as<std::string>();
        }
        if (driver_node["timestep_seconds"]) {
            config.driver_config.timestep_seconds = driver_node["timestep_seconds"].as<int>();
        }
        if (driver_node["mesh_file"]) {
            config.driver_config.mesh_file = driver_node["mesh_file"].as<std::string>();
        }
        if (driver_node["grid"]) {
            auto grid_node = driver_node["grid"];
            if (grid_node["nx"]) {
                config.driver_config.grid.nx = grid_node["nx"].as<int>();
            }
            if (grid_node["ny"]) {
                config.driver_config.grid.ny = grid_node["ny"].as<int>();
            }
            if (grid_node["lon_min"]) {
                config.driver_config.grid.lon_min = grid_node["lon_min"].as<double>();
            }
            if (grid_node["lon_max"]) {
                config.driver_config.grid.lon_max = grid_node["lon_max"].as<double>();
            }
            if (grid_node["lat_min"]) {
                config.driver_config.grid.lat_min = grid_node["lat_min"].as<double>();
            }
            if (grid_node["lat_max"]) {
                config.driver_config.grid.lat_max = grid_node["lat_max"].as<double>();
            }
        }
    }

    return config;
}

// ---------------------------------------------------------------------------
// Runtime dynamic registration helpers
// ---------------------------------------------------------------------------

/**
 * @brief Adds a new emission species with its layers to an existing config at runtime.
 */
void AddSpecies(AcesConfig& config, const std::string& species_name,
                std::vector<EmissionLayer> layers) {
    config.species_layers[species_name] = std::move(layers);
}

/**
 * @brief Adds a new scale factor mapping to an existing config at runtime.
 */
void AddScaleFactor(AcesConfig& config, const std::string& internal_name,
                    const std::string& external_name) {
    config.scale_factor_mapping[internal_name] = external_name;
}

/**
 * @brief Adds a new mask mapping to an existing config at runtime.
 */
void AddMask(AcesConfig& config, const std::string& internal_name,
             const std::string& external_name) {
    config.mask_mapping[internal_name] = external_name;
}

}  // namespace aces
