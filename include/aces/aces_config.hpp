#ifndef ACES_CONFIG_HPP
#define ACES_CONFIG_HPP

#include <string>
#include <vector>
#include <map>
#include <yaml-cpp/yaml.h>

namespace aces {

/**
 * @brief Represents configuration for a physics scheme.
 */
struct PhysicsSchemeConfig {
    std::string name;
    std::string language;
    YAML::Node options;
};

/**
 * @brief Represents a single layer of emissions to be applied.
 */
struct EmissionLayer {
    std::string operation; ///< "add" or "replace"
    std::string field_name; ///< Name of the base field in ESMF State
    std::string mask_name;  ///< Name of the mask field in ESMF State (empty if no mask)
    double scale = 1.0;     ///< Scaling factor for this layer
};

/**
 * @brief Configuration for ACES, containing layers for each species.
 */
struct AcesConfig {
    std::map<std::string, std::vector<EmissionLayer>> species_layers;
    std::vector<PhysicsSchemeConfig> physics_schemes;
};

/**
 * @brief Parses the ACES configuration from a YAML file.
 * @param filename Path to the YAML configuration file.
 * @return AcesConfig object containing the parsed layers.
 */
AcesConfig ParseConfig(const std::string& filename);

} // namespace aces

#endif // ACES_CONFIG_HPP
