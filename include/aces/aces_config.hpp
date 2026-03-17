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
 * @enum VerticalCoordType
 * @brief Supported vertical coordinate systems.
 */
enum class VerticalCoordType : std::uint8_t {
    NONE,  ///< 2D only
    FV3,   ///< FV3-style hybrid sigma-pressure
    MPAS,  ///< MPAS-style height-based
    WRF    ///< WRF-style mass-based
};

/**
 * @enum VerticalDistributionMethod
 * @brief Methods for distributing emissions vertically.
 */
enum class VerticalDistributionMethod : std::uint8_t {
    SINGLE = 0,    ///< Put all emissions in a single specific layer.
    RANGE = 1,     ///< Uniformly distribute emissions between two layer indices.
    PRESSURE = 2,  ///< Distribute based on a pressure range (Pa).
    HEIGHT = 3,    ///< Distribute based on a height range (m).
    PBL = 4        ///< Distribute uniformly within the PBL.
};

/**
 * @struct VerticalConfig
 * @brief Configuration for the model's vertical grid.
 */
struct VerticalConfig {
    VerticalCoordType type = VerticalCoordType::NONE;
    std::string ak_field = "hyam";    ///< Name of 'ak' coefficients field (FV3).
    std::string bk_field = "hybm";    ///< Name of 'bk' coefficients field (FV3).
    std::string p_surf_field = "ps";  ///< Name of surface pressure field.
    std::string z_field = "height";   ///< Name of height/altitude field (MPAS/WRF).
    std::string pbl_field = "hpbl";   ///< Name of PBL height field.
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
    std::vector<std::string> scale_fields;  ///< List of additional scale fields to
                                            ///< apply.
    std::string diurnal_cycle;              ///< Name of the diurnal cycle to apply (24 factors).
    std::string weekly_cycle;               ///< Name of the weekly cycle to apply (7 factors).
    std::string seasonal_cycle;             ///< Name of the seasonal cycle to apply (12 factors).

    // Vertical distribution
    VerticalDistributionMethod vdist_method =
        VerticalDistributionMethod::SINGLE;  ///< Method for vertical distribution.
    int vdist_layer_start = 0;               ///< Start layer (0-indexed).
    int vdist_layer_end = 0;                 ///< End layer (0-indexed).
    double vdist_p_start = 0.0;              ///< Start pressure (Pa).
    double vdist_p_end = 0.0;                ///< End pressure (Pa).
    double vdist_h_start = 0.0;              ///< Start height (m).
    double vdist_h_end = 0.0;                ///< End height (m).
};

/**
 * @struct TemporalCycle
 * @brief Represents a periodic scaling cycle.
 */
struct TemporalCycle {
    std::vector<double> factors;  ///< Scaling factors (e.g., 24 for diurnal, 7 for weekly).
};

/**
 * @struct CdepsVariableConfig
 * @brief Configuration for a single variable within a CDEPS stream.
 */
struct CdepsVariableConfig {
    std::string name_in_file;   ///< Variable name in the NetCDF file.
    std::string name_in_model;  ///< Internal name used by ACES.
};

/**
 * @struct CdepsStreamConfig
 * @brief Configuration for a single CDEPS input stream.
 */
struct CdepsStreamConfig {
    std::string name;                            ///< Name of the stream.
    std::vector<std::string> file_paths;         ///< Paths to the NetCDF files.
    std::vector<CdepsVariableConfig> variables;  ///< Variables to read from this stream.
    std::string taxmode = "cycle";               ///< Time axis mode (cycle, extend, etc.).
    std::string tintalgo = "linear";             ///< Time interpolation algorithm.
    std::string mapalgo = "bilinear";            ///< Spatial mapping algorithm.
    int dtlimit = 1500000000;                    ///< Delta time limit in seconds.
    int yearFirst = 1;                           ///< First year in data.
    int yearLast = 1;                            ///< Last year in data.
    int yearAlign = 1;                           ///< Year to align with model time.
    int offset = 0;                              ///< Time offset in seconds.
    std::string meshfile;                        ///< Path to source mesh file.
    std::string lev_dimname = "lev";             ///< Name of vertical dimension.
};

/**
 * @struct AcesCdepsConfig
 * @brief Configuration for CDEPS-inline data ingestion.
 */
struct AcesCdepsConfig {
    std::vector<CdepsStreamConfig> streams;  ///< List of input streams.
};

/**
 * @struct AcesOutputConfig
 * @brief Configuration for standalone NetCDF output (Requirement 11.12).
 */
struct AcesOutputConfig {
    std::string directory = ".";  ///< Output directory (created if absent).
    std::string filename_pattern =
        "aces_output_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc";  ///< Filename pattern with time tokens.
    int frequency_steps = 1;                           ///< Write every N time steps.
    std::vector<std::string> fields;   ///< Fields to write; empty means all export fields.
    bool include_diagnostics = false;  ///< Also write diagnostic fields when true.
    bool enabled = false;              ///< True when an output block is present in the YAML.
};

/**
 * @struct DiagnosticConfig
 * @brief Configuration for diagnostic output.
 */
struct DiagnosticConfig {
    int output_interval_seconds = 0;     ///< Output frequency in seconds.
    std::string grid_type = "native";    ///< "native", "gaussian", or "mesh".
    std::string grid_file;               ///< Path to ESMF mesh file if grid_type is
                                         ///< "mesh".
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
    /// Configuration for CDEPS-inline data ingestion.
    AcesCdepsConfig cdeps_config;
    /// Configuration for vertical grid.
    VerticalConfig vertical_config;
    /// Configuration for standalone NetCDF output.
    AcesOutputConfig output_config;
};

/**
 * @brief Parses the ACES configuration from a YAML file.
 * @param filename Path to the YAML configuration file.
 * @return AcesConfig object containing the parsed species and schemes.
 * @throws YAML::Exception if the file is invalid or missing.
 */
AcesConfig ParseConfig(const std::string& filename);

/**
 * @brief Adds a new emission species with its layers to an existing config at runtime.
 * @details Allows dynamic addition of species without recompilation. The
 *          StackingEngine must call ResetBindings() after this to pick up the change.
 * @param config The config to mutate.
 * @param species_name Internal species name.
 * @param layers Ordered list of emission layers for this species.
 */
void AddSpecies(AcesConfig& config, const std::string& species_name,
                std::vector<EmissionLayer> layers);

/**
 * @brief Adds a new scale factor mapping to an existing config at runtime.
 * @param config The config to mutate.
 * @param internal_name Internal scale factor name used in layer definitions.
 * @param external_name External field name in the ESMF state.
 */
void AddScaleFactor(AcesConfig& config, const std::string& internal_name,
                    const std::string& external_name);

/**
 * @brief Adds a new mask mapping to an existing config at runtime.
 * @param config The config to mutate.
 * @param internal_name Internal mask name used in layer definitions.
 * @param external_name External field name in the ESMF state.
 */
void AddMask(AcesConfig& config, const std::string& internal_name,
             const std::string& external_name);

}  // namespace aces

#endif  // ACES_CONFIG_HPP
