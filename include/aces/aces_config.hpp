#ifndef ACES_CONFIG_HPP
#define ACES_CONFIG_HPP

/**
 * @file aces_config.hpp
 * @brief Configuration structures and parser for ACES.
 */

#include <yaml-cpp/yaml.h>

#include <map>
#include <string>
#include <vector>

namespace aces {

/**
 * @struct PhysicsSchemeConfig
 * @brief Configuration for a physics scheme.
 */
struct PhysicsSchemeConfig {
    std::string name;      ///< Name of the physics scheme.
    std::string language;  ///< Implementation language (e.g., "cpp", "fortran").
    YAML::Node options;    ///< Scheme-specific options.
};

/**
 * @struct EmissionLayer
 * @brief Represents a single layer of emissions to be applied.
 */
struct EmissionLayer {
    std::string operation;                  ///< Layer operation: "add" or "replace".
    std::string field_name;                 ///< Name of the base field in the ESMF State.
    std::string mask_name;                  ///< Name of the geographical mask field (optional).
    double scale = 1.0;                     ///< Scaling factor for this layer.
    int hierarchy = 0;                      ///< Hierarchy level (higher overwrites lower).
    std::string category = "1";             ///< Emission category.
    std::vector<std::string> scale_fields;  ///< List of additional scale fields to apply.
};

/**
 * @struct CdepsStreamConfig
 * @brief Configuration for a single CDEPS input stream.
 */
struct CdepsStreamConfig {
    std::string name;                  ///< Name of the stream.
    std::string file_path;             ///< Path to the NetCDF file.
    std::string interpolation_method;  ///< Interpolation method (e.g., "linear").
};

/**
 * @struct AcesCdepsConfig
 * @brief Configuration for CDEPS-inline data ingestion.
 */
struct AcesCdepsConfig {
    std::vector<CdepsStreamConfig> streams;  ///< List of input streams.
};

/**
 * @struct AcesConfig
 * @brief Top-level configuration for ACES.
 */
struct AcesConfig {
    /// Map of species names to their ordered list of emission layers.
    std::map<std::string, std::vector<EmissionLayer>> species_layers;
    /// List of active physics schemes to be executed.
    std::vector<PhysicsSchemeConfig> physics_schemes;
    /// Configuration for CDEPS-inline data ingestion.
    AcesCdepsConfig cdeps_config;
};

/**
 * @brief Parses the ACES configuration from a YAML file.
 * @param filename Path to the YAML configuration file.
 * @return AcesConfig object containing the parsed species and schemes.
 * @throws YAML::Exception if the file is invalid or missing.
 */
AcesConfig ParseConfig(const std::string& filename);

}  // namespace aces

#endif  // ACES_CONFIG_HPP
