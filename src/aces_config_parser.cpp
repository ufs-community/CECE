#include <yaml-cpp/yaml.h>

#include <iostream>

#include "aces/aces_config.hpp"

/**
 * @file aces_config_parser.cpp
 * @brief Implementation of the YAML configuration parser for ACES.
 */

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

    // Parse CDEPS configuration
    if (root["cdeps_inline_config"]) {
        auto cdeps_node = root["cdeps_inline_config"];
        if (cdeps_node["streams"]) {
            for (auto const& stream_node : cdeps_node["streams"]) {
                CdepsStreamConfig stream;
                stream.name = stream_node["name"].as<std::string>();
                stream.file_path = stream_node["file"].as<std::string>();
                if (stream_node["interpolation"]) {
                    stream.interpolation_method = stream_node["interpolation"].as<std::string>();
                }
                config.cdeps_config.streams.push_back(stream);
            }
        }
    }

    return config;
}

}  // namespace aces
