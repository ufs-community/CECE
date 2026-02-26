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

    return config;
}

} // namespace aces
