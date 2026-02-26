#include "aces/aces_config.hpp"
#include <yaml-cpp/yaml.h>
#include <iostream>

namespace aces {

AcesConfig ParseConfig(const std::string& filename) {
    AcesConfig config;
    YAML::Node root = YAML::LoadFile(filename);

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
                layers.push_back(layer);
            }
            config.species_layers[species_name] = layers;
        }
    }

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

    return config;
}

} // namespace aces
