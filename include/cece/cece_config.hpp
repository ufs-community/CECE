#ifndef CECE_CONFIG_HPP
#define CECE_CONFIG_HPP

/**
 * @file cece_config.hpp
 * @brief Configuration structures and parser for CECE.
 */

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cece {

/**
 * @struct PhysicsSchemeConfig
 * @brief Configuration for a physics scheme.
 */
struct PhysicsSchemeConfig {
    std::string name;                  ///< Name of the physics scheme.
    std::string language;              ///< Implementation language (e.g., "cpp", "fortran").
    YAML::Node options;                ///< Scheme-specific options.
    int refresh_interval_seconds = 0;  ///< Refresh interval in seconds (0 means use base timestep).
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
    VerticalDistributionMethod vdist_method = VerticalDistributionMethod::SINGLE;  ///< Method for vertical distribution.
    int vdist_layer_start = 0;                                                     ///< Start layer (0-indexed).
    int vdist_layer_end = 0;                                                       ///< End layer (0-indexed).
    double vdist_p_start = 0.0;                                                    ///< Start pressure (Pa).
    double vdist_p_end = 0.0;                                                      ///< End pressure (Pa).
    double vdist_h_start = 0.0;                                                    ///< Start height (m).
    double vdist_h_end = 0.0;                                                      ///< End height (m).
};

/**
 * @struct TemporalCycle
 * @brief Represents a periodic scaling cycle.
 */
struct TemporalCycle {
    std::vector<double> factors;  ///< Scaling factors (e.g., 24 for diurnal, 7 for weekly).
};

/**
 * @struct CeceDataVariableConfig
 * @brief Configuration for a single variable within an cece_data stream.
 */
struct CeceDataVariableConfig {
    std::string name_in_file;   ///< Variable name in the NetCDF file.
    std::string name_in_model;  ///< Internal name used by CECE.
};

/**
 * @struct CeceDataStreamConfig
 * @brief Configuration for a single cece_data input stream.
 */
struct CeceDataStreamConfig {
    std::string name;                               ///< Name of the stream.
    std::vector<std::string> file_paths;            ///< Paths to the NetCDF files.
    std::vector<CeceDataVariableConfig> variables;  ///< Variables to read from this stream.
    std::string taxmode = "cycle";                  ///< Time axis mode (cycle, extend, etc.).
    std::string tintalgo = "linear";                ///< Time interpolation algorithm.
    std::string mapalgo =
        "bilinear";            ///< Spatial mapping algorithm: bilinear, consd, consf, nn, redist, passthrough (skip regridding, same-grid data).
    int dtlimit = 1500000000;  ///< Delta time limit in seconds.
    int yearFirst = 1;         ///< First year in data.
    int yearLast = 1;          ///< Last year in data.
    int yearAlign = 1;         ///< Year to align with model time.
    int offset = 0;            ///< Time offset in seconds.
    std::string meshfile;      ///< Path to source mesh file.
    std::string lev_dimname = "lev";   ///< Name of vertical dimension.
    std::string time_var = "time";     ///< Name of time coordinate variable.
    std::string lon_var = "lon";       ///< Name of longitude coordinate variable.
    std::string lat_var = "lat";       ///< Name of latitude coordinate variable.
    int refresh_interval_seconds = 0;  ///< Refresh interval in seconds (0 means use base timestep).
};

/**
 * @struct CeceDataConfig
 * @brief Configuration for CF-compliant data ingestion (cece_data section).
 */
struct CeceDataConfig {
    std::vector<CeceDataStreamConfig> streams;  ///< List of input streams.
    int debug_level = 0;                        ///< Data stream debug verbosity level (0=off, 1=time-matching info).
};

/// The coordinate variables the standalone writer manages itself: they
/// carry fixed built-in attributes, are never written as data fields, and
/// never receive a coordinates attribute of their own. Listed in the
/// written field shape order [time, lev, lat, lon] — the order of the
/// default coordinates attribute derived below.
inline constexpr std::array<std::string_view, 4> kCoordinateNames{"time", "lev", "lat", "lon"};

inline bool IsCoordinateName(std::string_view name) {
    return std::ranges::find(kCoordinateNames, name) != kCoordinateNames.end();
}

/// Default CF coordinates attribute for data fields: the coordinate names
/// joined in written-shape order. Structural, so every data field gets it
/// unless the entry configures its own coordinates value.
inline const std::string kDefaultCoordinates = [] {
    std::string joined;
    for (const std::string_view name : kCoordinateNames) {
        if (!joined.empty()) {
            joined += ' ';
        }
        joined += name;
    }
    return joined;
}();

/**
 * @struct CeceOutputField
 * @brief One output.fields entry: a field to write and its NetCDF attributes.
 */
struct CeceOutputField {
    std::string name;  ///< Export field name.
    /// NetCDF attributes (attribute -> value) from the entry's optional
    /// "attributes" map. Fields without configured attributes get none —
    /// the writer never fabricates units/long_name.
    std::map<std::string, std::string> attributes;

    /// The effective coordinates attribute: the entry's configured value
    /// when present, kDefaultCoordinates otherwise. The returned view is
    /// valid as long as this field's attributes map is unmodified.
    std::string_view GetCoordinates() const {
        auto it = attributes.find("coordinates");
        return it != attributes.end() ? std::string_view(it->second) : kDefaultCoordinates;
    }

    /// This field's AMIO manifest variable block. Only configured
    /// attributes are emitted; a field without configuration gets none —
    /// never fabricated units/long_name. The structural coordinates
    /// attribute is always present on data fields, resolved by
    /// GetCoordinates; coordinate variables never receive one.
    std::string CreateIOManifest() const {
        std::string block = "  " + name + ":\n    attributes:\n";
        for (const auto& [attr_name, attr_value] : attributes) {
            if (attr_name == "coordinates") {
                continue;  // emitted last via GetCoordinates
            }
            block += "      " + attr_name + ": \"" + attr_value + "\"\n";
        }
        if (!IsCoordinateName(name)) {
            block += "      coordinates: \"" + std::string(GetCoordinates()) + "\"\n";
        }
        return block;
    }
};

/// The writer-managed coordinate variables seeded into every
/// CeceOutputFieldCollection. time's units attribute is runtime-derived
/// ("seconds since <start>") and patched by the writer at manifest time.
inline const std::vector<CeceOutputField> kCoordinateFields{
    {"lon", {{"units", "degrees_east"}, {"long_name", "longitude"}}},
    {"lat", {{"units", "degrees_north"}, {"long_name", "latitude"}}},
    {"lev", {{"units", "level"}, {"long_name", "vertical level"}}},
    {"time", {{"long_name", "time"}}},
};

/**
 * @class CeceOutputFieldCollection
 * @brief Owns the output fields and consolidates operations over them:
 *        coordinate/data partition, name lookup, and manifest rendering
 *        (composed from each field's CreateIOManifest). Every collection
 *        is seeded with the coordinate variables (kCoordinateFields);
 *        configured data fields join them via push_back.
 */
class CeceOutputFieldCollection {
   public:
    CeceOutputFieldCollection() : fields_(kCoordinateFields) {}
    CeceOutputFieldCollection(std::initializer_list<CeceOutputField> fields) : CeceOutputFieldCollection() {
        for (const auto& field : fields) {
            push_back(field);
        }
    }

    /// References to the entries naming coordinate variables
    /// (kCoordinateNames), in declaration order. The references are valid
    /// until the collection is mutated.
    std::vector<std::reference_wrapper<const CeceOutputField>> GetCoordinateFields() const {
        return Filter(true);
    }

    /// References to the entries naming data fields (everything that is not
    /// a coordinate variable), in declaration order. The references are
    /// valid until the collection is mutated.
    std::vector<std::reference_wrapper<const CeceOutputField>> GetDataFields() const {
        return Filter(false);
    }

    /// The entry with this field name, or nullptr. The pointer is valid
    /// until the collection is mutated.
    const CeceOutputField* Find(std::string_view field_name) const {
        auto it = std::ranges::find_if(fields_, [&](const CeceOutputField& f) { return f.name == field_name; });
        return it != fields_.end() ? &*it : nullptr;
    }

    /// True when any entry carries this field name.
    bool Contains(std::string_view field_name) const {
        return Find(field_name) != nullptr;
    }

    /// Sets the seeded time field's units from the run start time
    /// ("seconds since YYYY-MM-DD hh:mm:ss"). Called where the collection
    /// is initialized (ParseConfig uses driver.start_time); rendering
    /// never mutates the collection.
    void SetTimeUnits(std::string_view start_time_iso8601) {
        std::string units = "seconds since " + std::string(start_time_iso8601);
        if (const auto t_pos = units.find('T'); t_pos != std::string::npos) {
            units[t_pos] = ' ';
        }
        auto it = std::ranges::find_if(fields_, [](const CeceOutputField& f) { return f.name == "time"; });
        it->attributes["units"] = std::move(units);
    }

    /// The whole variable side of an AMIO manifest: the variable_names
    /// list followed by every field's variable block, in declaration order.
    /// Throws when time's units were never set (SetTimeUnits) — a time
    /// block without units is structural breakage, not a configuration
    /// choice.
    std::string CreateIOManifest() const {
        if (Find("time")->attributes.count("units") == 0) {
            throw std::runtime_error("time units are not set — call SetTimeUnits() before CreateIOManifest()");
        }
        std::string manifest = "variable_names: [";
        bool first_name = true;
        for (const auto& field : fields_) {
            manifest += (first_name ? "\"" : ", \"") + field.name + "\"";
            first_name = false;
        }
        manifest += "]\nvariables:\n";
        for (const auto& field : fields_) {
            manifest += field.CreateIOManifest();
        }
        return manifest;
    }

    // std-style surface so the collection drops in where the storage vector
    // was used directly (range-for, parser push_back, test indexing).
    auto begin() const {
        return fields_.begin();
    }
    auto end() const {
        return fields_.end();
    }
    bool empty() const {
        return fields_.empty();
    }
    std::size_t size() const {
        return fields_.size();
    }
    /// Appends a data field. Field names must be unique across the whole
    /// collection — a duplicate data name, or any coordinate name (the
    /// seeded coordinate variables are always present and writer-managed),
    /// is rejected.
    void push_back(CeceOutputField field) {
        if (Contains(field.name)) {
            throw std::runtime_error("duplicate output field name '" + field.name +
                                     "' — field names must be unique, and the coordinate variables (lon, lat, lev, time) are always present");
        }
        fields_.push_back(std::move(field));
    }
    const CeceOutputField& operator[](std::size_t index) const {
        return fields_[index];
    }

   private:
    std::vector<std::reference_wrapper<const CeceOutputField>> Filter(bool coordinates) const {
        std::vector<std::reference_wrapper<const CeceOutputField>> matches;
        for (const auto& field : fields_) {
            if (IsCoordinateName(field.name) == coordinates) {
                matches.emplace_back(field);
            }
        }
        return matches;
    }

    std::vector<CeceOutputField> fields_;
};

/**
 * @struct CeceOutputConfig
 * @brief Configuration for standalone NetCDF output (Requirement 11.12).
 */
struct CeceOutputConfig {
    std::string directory = ".";                                                  ///< Output directory (created if absent).
    std::string filename_pattern = "cece_output_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc";  ///< Filename pattern with time tokens.
    int frequency_steps = 1;                                                      ///< Write every N time steps.
    CeceOutputFieldCollection fields;  ///< Coordinate variables + configured data fields; no data fields means write all export fields.
    bool include_diagnostics = false;  ///< Also write diagnostic fields when true.
    bool enabled = false;              ///< True when an output block is present in the YAML.
    int amio_worker_threads = -1;      ///< Number of AMIO background I/O worker threads (default: -1, meaning use fallback).
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
 * @struct DriverGridConfig
 * @brief Grid configuration for the driver (Requirement 14).
 */
struct DriverGridConfig {
    int nx = 4;               ///< Grid points in X direction (default: 4).
    int ny = 4;               ///< Grid points in Y direction (default: 4).
    int nz = 1;               ///< Grid points in Z (vertical) direction (default: 1).
    double lon_min = -135.0;  ///< Minimum longitude (default: -135.0).
    double lon_max = 135.0;   ///< Maximum longitude (default: 135.0).
    double lat_min = -67.5;   ///< Minimum latitude (default: -67.5).
    double lat_max = 67.5;    ///< Maximum latitude (default: 67.5).
};

/**
 * @struct DriverConfig
 * @brief Configuration for the standalone NUOPC driver (Requirements 1-3, 14-15).
 */
struct DriverConfig {
    std::string start_time = "2020-01-01T00:00:00";  ///< ISO8601 start time (default: 2020-01-01T00:00:00).
    std::string end_time = "2020-01-02T00:00:00";    ///< ISO8601 end time (default: 2020-01-02T00:00:00).
    int timestep_seconds = 3600;                     ///< Timestep in seconds (default: 3600).
    std::string
        gridspec_file;      ///< Path to ESMF GRIDSPEC NetCDF file (optional). If set, loaded instead of generating a grid from driver.grid params.
    DriverGridConfig grid;  ///< Grid configuration for generated Gaussian grid.
    int stacking_refresh_interval_seconds = 0;  ///< Stacking engine refresh interval in seconds (0 means use base timestep).
    int amio_worker_threads = 1;                ///< Number of AMIO background I/O worker threads (default: 1).
};

/**
 * @struct CeceConfig
 * @brief Top-level configuration for CECE.
 */
struct CeceConfig {
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
    /// Configuration for CF-compliant data ingestion (cece_data section).
    CeceDataConfig cece_data;
    /// Configuration for vertical grid.
    VerticalConfig vertical_config;
    /// Configuration for standalone NetCDF output.
    CeceOutputConfig output_config;
    /// Configuration for the standalone NUOPC driver (optional).
    DriverConfig driver_config;
    /// Registry of meteorology variable internal names to their external aliases.
    std::unordered_map<std::string, std::vector<std::string>> met_registry;
};

/**
 * @brief Parses the CECE configuration from a YAML file.
 * @param filename Path to the YAML configuration file.
 * @return CeceConfig object containing the parsed species and schemes.
 * @throws YAML::Exception if the file is invalid or missing.
 */
CeceConfig ParseConfig(const std::string& filename);

/**
 * @brief Adds a new emission species with its layers to an existing config at runtime.
 * @details Allows dynamic addition of species without recompilation. The
 *          StackingEngine must call ResetBindings() after this to pick up the change.
 * @param config The config to mutate.
 * @param species_name Internal species name.
 * @param layers Ordered list of emission layers for this species.
 */
void AddSpecies(CeceConfig& config, const std::string& species_name, std::vector<EmissionLayer> layers);

/**
 * @brief Adds a new scale factor mapping to an existing config at runtime.
 * @param config The config to mutate.
 * @param internal_name Internal scale factor name used in layer definitions.
 * @param external_name External field name in the ESMF state.
 */
void AddScaleFactor(CeceConfig& config, const std::string& internal_name, const std::string& external_name);

/**
 * @brief Adds a new mask mapping to an existing config at runtime.
 * @param config The config to mutate.
 * @param internal_name Internal mask name used in layer definitions.
 * @param external_name External field name in the ESMF state.
 */
void AddMask(CeceConfig& config, const std::string& internal_name, const std::string& external_name);

}  // namespace cece

#endif  // CECE_CONFIG_HPP
