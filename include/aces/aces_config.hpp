#ifndef ACES_CONFIG_HPP
#define ACES_CONFIG_HPP

/**
 * @file aces_config.hpp
 * @brief Configuration structures and parser for ACES.
 */

#include <yaml-cpp/yaml.h>

#include <string>
#include <unordered_map>
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
    std::vector<std::string> masks;         ///< List of geographical mask fields.
    double scale = 1.0;                     ///< Scaling factor for this layer.
    int hierarchy = 0;                      ///< Hierarchy level (higher overwrites lower).
    std::string category = "1";             ///< Emission category.
    std::vector<std::string> scale_fields;  ///< List of additional scale fields to apply.
    std::string diurnal_cycle;              ///< Name of the diurnal cycle to apply.
    std::string weekly_cycle;               ///< Name of the weekly cycle to apply.
};

/**
 * @struct TemporalCycle
 * @brief Represents a periodic scaling cycle.
 */
struct TemporalCycle {
    std::vector<double> factors;  ///< Scaling factors (e.g., 24 for diurnal, 7 for weekly).
};

/**
 * @struct DemsStreamConfig
 * @brief Configuration for a single DEMS/DEMIS input stream.
 * Provides full control over CDEPS/DEMS stream attributes.
 */
struct DemsStreamConfig {
    std::string name;        ///< Name of the stream.
    std::string file_path;   ///< Path to the NetCDF file(s) (space separated).
    std::string taxmode = "cycle";      ///< "cycle", "extend", or "limit".
    std::string tintalgo = "linear";    ///< "linear", "nearest", "lower", "upper", "coszen".
    std::string readmode = "single";    ///< "single" or "full_file".
    std::string mapalgo = "bilinear";   ///< "bilinear", "nn", "consd", "consf", "redist", "map_point".
    double dtlimit = 1.5;               ///< Delta time ratio limit.
    int yearFirst = 0;                  ///< First year of stream data to use.
    int yearLast = 0;                   ///< Last year of stream data to use.
    int yearAlign = 0;                  ///< Simulation year to align with yearFirst.
    std::string vectors = "";           ///< Paired vector field names (e.g., "u:v").
    std::string meshfile = "";          ///< Path to stream mesh file.
    std::string lev_dimname = "";       ///< Name of vertical dimension.
    int offset = 0;                     ///< Time offset in seconds.
};

/**
 * @struct AcesDemsConfig
 * @brief Configuration for DEMS/DEMIS data ingestion.
 */
struct AcesDemsConfig {
    std::vector<DemsStreamConfig> streams;  ///< List of input streams.
};

/**
 * @struct DiagnosticConfig
 * @brief Configuration for diagnostic output.
 */
struct DiagnosticConfig {
    int output_interval_seconds = 0;     ///< Output frequency in seconds.
    std::string grid_type = "native";    ///< "native", "gaussian", or "mesh".
    std::string grid_file = "";          ///< Path to ESMF mesh file if grid_type is "mesh".
    int nx = 0;                          ///< Grid X for Gaussian or native.
    int ny = 0;                          ///< Grid Y for Gaussian or native.
    std::vector<std::string> variables;  ///< Variables to output.
};

/**
 * @struct AcesConfig
 * @brief Top-level configuration for ACES.
 */
struct AcesConfig {
    /// Map of species names to their ordered list of emission layers.
    std::unordered_map<std::string, std::vector<EmissionLayer>> species_layers;
    /// Map of internal meteorology names to external names (e.g., CF standard
    /// names).
    std::unordered_map<std::string, std::string> met_mapping;
    /// Map of internal scale factor names to external names.
    std::unordered_map<std::string, std::string> scale_factor_mapping;
    /// Map of internal mask names to external names.
    std::unordered_map<std::string, std::string> mask_mapping;
    /// Map of cycle names to their temporal scaling factors.
    std::unordered_map<std::string, TemporalCycle> temporal_cycles;
    /// Map of profile names to their temporal scaling factors.
    std::unordered_map<std::string, TemporalCycle> temporal_profiles;
    /// List of active physics schemes to be executed.
    std::vector<PhysicsSchemeConfig> physics_schemes;
    /// Configuration for diagnostic output.
    DiagnosticConfig diagnostics;
    /// Configuration for DEMS/DEMIS data ingestion.
    AcesDemsConfig dems_config;
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
