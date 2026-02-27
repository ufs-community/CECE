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
                    layer.mask_name = layer_node["mask"].as<std::string>();
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
                layers.push_back(layer);
            }
            config.species_layers[species_name] = layers;
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
