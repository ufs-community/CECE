#include <conf/conf.hpp>
#include <cstring>
#include <fstream>
/**
 * @file cece_core_field_helpers.cpp
 * @brief Helper functions for field management in CECE.
 *
 * Provides C interface functions for the Fortran cap to:
 * - Query the number of species
 * - Get species names
 * - Bind field data pointers to internal data
 *
 * Requirements: R4, R5
 */

#include <iostream>
#include <string>

#include "cece/cece_internal.hpp"

extern "C" {

/**
 * @brief Get the number of species from the configuration.
 *
 * @param data_ptr Pointer to CeceInternalData
 * @param count Output: number of species
 * @param rc Return code (0 = success, non-zero = error)
 *
 * Requirements: R4
 */
void cece_core_get_species_count(void* data_ptr, int* count, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: cece_core_get_species_count - data_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (count == nullptr) {
        std::cerr << "ERROR: cece_core_get_species_count - count pointer is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);
    *count = static_cast<int>(internal_data->config.species_layers.size());

    std::cout << "INFO: cece_core_get_species_count returned " << *count << " species" << std::endl;

    if (rc != nullptr) {
        *rc = 0;
    }
}

/**
 * @brief Get the name of a species by index.
 *
 * @param data_ptr Pointer to CeceInternalData
 * @param index Zero-based index of the species
 * @param name Output: species name (C string, null-terminated)
 * @param name_len Output: length of the species name (excluding null terminator)
 * @param rc Return code (0 = success, non-zero = error)
 *
 * Requirements: R4
 */
void cece_core_get_species_name(void* data_ptr, int* index, char* name, int* name_len, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: cece_core_get_species_name - data_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (index == nullptr || name == nullptr || name_len == nullptr) {
        std::cerr << "ERROR: cece_core_get_species_name - null pointer argument" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);

    // Get the species name at the given index
    int idx = 0;
    for (const auto& [species_name, layers] : internal_data->config.species_layers) {
        if (idx == *index) {
            // Copy the species name to the output buffer
            std::string name_str = species_name;
            int len = static_cast<int>(name_str.length());

            // Ensure we don't overflow the buffer (Fortran will allocate enough space)
            for (int i = 0; i < len; ++i) {
                name[i] = name_str[i];
            }
            name[len] = '\0';  // Null-terminate

            *name_len = len;

            std::cout << "INFO: cece_core_get_species_name[" << *index << "] = " << name_str << std::endl;

            if (rc != nullptr) {
                *rc = 0;
            }
            return;
        }
        ++idx;
    }

    // Index out of range
    std::cerr << "ERROR: cece_core_get_species_name - index " << *index << " out of range" << std::endl;
    if (rc != nullptr) {
        *rc = -1;
    }
}

/**
 * @brief Get grid configuration parameters from CECE configuration.
 *
 * @param data_ptr Pointer to CeceInternalData
 * @param nx Output: number of grid points in X direction
 * @param ny Output: number of grid points in Y direction
 * @param lon_min Output: minimum longitude
 * @param lon_max Output: maximum longitude
 * @param lat_min Output: minimum latitude
 * @param lat_max Output: maximum latitude
 * @param rc Return code (0 = success, non-zero = error)
 */
void cece_core_get_grid_config(void* data_ptr, int* nx, int* ny, int* nz, double* lon_min, double* lon_max, double* lat_min, double* lat_max,
                               int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: cece_core_get_grid_config - data_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);

    // Check output pointers
    if (nx == nullptr || ny == nullptr || nz == nullptr || lon_min == nullptr || lon_max == nullptr || lat_min == nullptr || lat_max == nullptr) {
        std::cerr << "ERROR: cece_core_get_grid_config - null output pointer" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    // Get grid configuration from parsed YAML config
    *nx = internal_data->config.driver_config.grid.nx;
    *ny = internal_data->config.driver_config.grid.ny;
    *nz = internal_data->config.driver_config.grid.nz;
    *lon_min = internal_data->config.driver_config.grid.lon_min;
    *lon_max = internal_data->config.driver_config.grid.lon_max;
    *lat_min = internal_data->config.driver_config.grid.lat_min;
    *lat_max = internal_data->config.driver_config.grid.lat_max;

    std::cout << "INFO: Grid config retrieved: nx=" << *nx << " ny=" << *ny << " nz=" << *nz << " lon_min=" << *lon_min << " lon_max=" << *lon_max
              << " lat_min=" << *lat_min << " lat_max=" << *lat_max << std::endl;
}

/**
 * @brief Get the data stream debug level from the cece_data section of the config.
 * @param data_ptr Pointer to CeceInternalData
 * @param level Output: debug level (0=off, 1=time-matching info, ...)
 * @param rc Return code (0 = success, non-zero = error)
 */
void cece_core_get_stream_debug_level(void* data_ptr, int* level, int* rc) {
    if (rc != nullptr) *rc = 0;
    if (data_ptr == nullptr || level == nullptr) {
        if (rc != nullptr) *rc = -1;
        return;
    }
    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);
    *level = internal_data->config.cece_data.debug_level;
}

/**
 * @brief Get timing configuration from YAML config.
 * @param data_ptr Pointer to CeceInternalData
 * @param start_time Output: start time as string (ISO8601 format)
 * @param end_time Output: end time as string (ISO8601 format)
 * @param timestep_seconds Output: timestep in seconds
 * @param max_len Maximum length for time strings (including null terminator)
 * @param rc Return code (0 = success, non-zero = error)
 */
void cece_core_get_timing_config(void* data_ptr, char* start_time, char* end_time, int* timestep_seconds, int max_len, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: cece_core_get_timing_config - data_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);

    // Check output pointers
    if (start_time == nullptr || end_time == nullptr || timestep_seconds == nullptr) {
        std::cerr << "ERROR: cece_core_get_timing_config - null output pointer" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    // Get timing configuration from parsed YAML config
    const auto& driver_config = internal_data->config.driver_config;

    // Copy strings safely
    strncpy(start_time, driver_config.start_time.c_str(), max_len - 1);
    start_time[max_len - 1] = '\0';

    strncpy(end_time, driver_config.end_time.c_str(), max_len - 1);
    end_time[max_len - 1] = '\0';

    *timestep_seconds = driver_config.timestep_seconds;

    std::cout << "INFO: Timing config retrieved: start=" << start_time << " end=" << end_time << " timestep=" << *timestep_seconds << " seconds"
              << std::endl;
}

/**
 * @brief Get the gridspec_file path from the parsed YAML config.
 * @param data_ptr Pointer to CeceInternalData
 * @param path Output buffer for the file path
 * @param path_len Output: number of characters written (0 if not set)
 * @param rc Return code (0 = success, non-zero = error)
 */
void cece_core_get_gridspec_file_path(void* data_ptr, char* path, int* path_len, int* rc) {
    if (rc != nullptr) *rc = 0;
    if (path_len != nullptr) *path_len = 0;

    if (data_ptr == nullptr || path == nullptr || path_len == nullptr) {
        std::cerr << "ERROR: cece_core_get_gridspec_file_path - null pointer argument" << std::endl;
        if (rc != nullptr) *rc = -1;
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);
    const std::string& gf = internal_data->config.driver_config.gridspec_file;

    if (gf.empty()) {
        *path_len = 0;
        return;
    }

    const std::size_t path_capacity = 512;
    const std::size_t max_copy_len = path_capacity - 1;
    const std::size_t copy_len = (gf.size() < max_copy_len) ? gf.size() : max_copy_len;
    std::memcpy(path, gf.c_str(), copy_len);
    path[copy_len] = '\0';
    *path_len = static_cast<int>(copy_len);
    std::cout << "INFO: gridspec_file path: " << gf << std::endl;
}

/**
 * @brief Read timing configuration from YAML file (for driver use).
 * @param config_path YAML config file path
 * @param path_len Length of config_path string
 * @param start_time Output: start time as string (ISO8601 format)
 * @param end_time Output: end time as string (ISO8601 format)
 * @param timestep_seconds Output: timestep in seconds
 * @param max_len Maximum length for time strings (including null terminator)
 * @param rc Return code (0 = success, non-zero = error)
 */
void cece_read_timing_config(const char* config_path, int path_len, char* start_time, char* end_time, int* timestep_seconds, int max_len, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    try {
        // Convert C string to std::string
        std::string yaml_path(config_path, path_len);

        // Parse config file
        conf::Config cfg = conf::Config::from_file(yaml_path);

        // Read timing configuration (start_time and end_time are required)
        auto start_opt = cfg.try_string("driver.start_time");
        auto end_opt = cfg.try_string("driver.end_time");
        if (!start_opt.has_value() || !end_opt.has_value()) {
            throw std::invalid_argument("Configuration missing required 'driver.start_time' and/or 'driver.end_time'");
        }
        std::string start_default = *start_opt;
        std::string end_default = *end_opt;
        // 'driver.timestep_seconds' is required so both launch paths treat the
        // timing keys the same way: the standalone driver reads it with a
        // throwing accessor and aborts on a missing key, so defaulting here
        // would let one config produce different clocks on the two drivers.
        auto step_opt = cfg.try_int("driver.timestep_seconds");
        if (!step_opt.has_value()) {
            throw std::invalid_argument("Configuration missing required 'driver.timestep_seconds'");
        }
        int timestep_default = *step_opt;

        // Copy strings safely
        strncpy(start_time, start_default.c_str(), max_len - 1);
        start_time[max_len - 1] = '\0';

        strncpy(end_time, end_default.c_str(), max_len - 1);
        end_time[max_len - 1] = '\0';

        *timestep_seconds = timestep_default;

        std::cout << "INFO: Driver timing config loaded: start=" << start_time << " end=" << end_time << " timestep=" << *timestep_seconds
                  << " seconds" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "ERROR: Failed to read timing config from " << config_path << ": " << e.what() << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
    }
}

/**
 * @brief Bind field data pointers to internal data.
 *
 * Stores the field data pointers passed from Fortran in the CeceInternalData
 * structure for later access during the Run phase.
 *
 * @param data_ptr Pointer to CeceInternalData
 * @param field_ptrs Array of field data pointers (one per species)
 * @param num_fields Number of fields in the array
 * @param rc Return code (0 = success, non-zero = error)
 *
 * Requirements: R5
 */
void cece_core_bind_fields(void* data_ptr, void** field_ptrs, int* num_fields, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: cece_core_bind_fields - data_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (num_fields == nullptr) {
        std::cerr << "ERROR: cece_core_bind_fields - num_fields pointer is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    // Special case: zero fields is a valid no-op
    if (*num_fields == 0) {
        std::cout << "INFO: cece_core_bind_fields - zero fields requested (no-op)" << std::endl;
        if (rc != nullptr) {
            *rc = 0;
        }
        return;
    }

    if (field_ptrs == nullptr) {
        std::cerr << "ERROR: cece_core_bind_fields - null pointer argument" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (*num_fields < 0) {
        std::cerr << "ERROR: cece_core_bind_fields - num_fields must be non-negative: " << *num_fields << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);

    // Clear any existing field pointers
    internal_data->field_pointers.clear();
    internal_data->field_names.clear();

    // Store the field pointers and names
    int idx = 0;
    for (const auto& [species_name, layers] : internal_data->config.species_layers) {
        if (idx >= *num_fields) {
            std::cerr << "ERROR: cece_core_bind_fields - num_fields mismatch: " << "expected " << idx << " got " << *num_fields << std::endl;
            if (rc != nullptr) {
                *rc = -1;
            }
            return;
        }

        if (field_ptrs[idx] == nullptr) {
            std::cerr << "ERROR: cece_core_bind_fields - field_ptrs[" << idx << "] is null for species " << species_name << std::endl;
            if (rc != nullptr) {
                *rc = -1;
            }
            return;
        }

        internal_data->field_pointers.push_back(field_ptrs[idx]);
        internal_data->field_names.push_back(species_name);

        std::cout << "INFO: cece_core_bind_fields - bound field " << idx << " for species " << species_name << " at address " << field_ptrs[idx]
                  << std::endl;

        ++idx;
    }

    if (idx != *num_fields) {
        std::cerr << "ERROR: cece_core_bind_fields - num_fields mismatch: expected " << idx << " got " << *num_fields << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    std::cout << "INFO: cece_core_bind_fields - bound " << *num_fields << " field pointers" << std::endl;

    if (rc != nullptr) {
        *rc = 0;
    }
}

/**
 * @brief Get the data streams file path from configuration.
 *
 * Returns the path to the data streams configuration file.
 * If no streams are configured, returns an empty string.
 *
 * @param data_ptr Pointer to CeceInternalData
 * @param streams_path Output: path to streams file (C string, null-terminated)
 * @param path_len Output: length of the path (excluding null terminator)
 * @param rc Return code (0 = success, non-zero = error)
 *
 * Requirements: R6
 */
void cece_core_get_ingestor_streams_path(void* data_ptr, char* streams_path, int* path_len, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: cece_core_get_ingestor_streams_path - data_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (streams_path == nullptr || path_len == nullptr) {
        std::cerr << "ERROR: cece_core_get_ingestor_streams_path - null pointer argument" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);

    // Check if ingestor streams are configured
    if (internal_data->config.cece_data.streams.empty()) {
        streams_path[0] = '\0';
        *path_len = 0;
        if (rc != nullptr) {
            *rc = 0;
        }
        return;
    }

    // Generate data stream YAML configuration
    std::string config_content = internal_data->ingestor.SerializeStreamYaml(internal_data->config.cece_data);

    // Write to file (using .yaml extension for data stream configuration)
    std::string filename = "cece_data_streams.yaml";
    std::ofstream outfile(filename);
    if (!outfile.is_open()) {
        std::cerr << "ERROR: Failed to open output file for data streams: " << filename << std::endl;
        if (rc != nullptr) *rc = -1;
        return;
    }
    outfile << config_content;
    outfile.close();

    // Return filename
    if (filename.length() >= 512) {  // Assuming 512 is buffer size from Fortran
        std::cerr << "ERROR: streams path too long" << std::endl;
        if (rc != nullptr) *rc = -1;
        return;
    }

    std::strncpy(streams_path, filename.c_str(), 512);
    *path_len = filename.length();

    if (rc != nullptr) {
        *rc = 0;
    }
}

/**
 * @brief Initialize the CF data ingestor from the Fortran cap.
 *
 * Called by the NUOPC cap (cece_cap.F90) after mesh creation when ingestor
 * streams are configured.  Delegates to CeceDataIngestor::IngestEmissionsInline
 *
 * @param data_ptr  Pointer to CeceInternalData.
 * @param c_clock   Opaque pointer to clock handle.
 * @param c_mesh    Opaque pointer to mesh (target mesh).
 * @param rc        Return code (0 = success, non-zero = error).
 */
void cece_ingestor_init(void* data_ptr, void* c_clock, void* c_mesh, int* rc) {
    if (rc != nullptr) *rc = 0;

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: cece_ingestor_init - data_ptr is null" << std::endl;
        if (rc != nullptr) *rc = -1;
        return;
    }

    // In the new architecture, we do not initialize a C++ ingestor.
    // Data stream ingestion operates in the driver facade.
    if (rc != nullptr) *rc = 0;
}

/**
 * @brief Register an export field data pointer in the internal field map.
 *
 * Called from the driver cap during realize after obtaining the field array
 * pointer for each species.
 *
 * @param data_ptr   Pointer to CeceInternalData.
 * @param name       Species name (not null-terminated; use name_len).
 * @param name_len   Length of the name string.
 * @param field_data Raw pointer to the field data (from c_loc(fptr(1,1,1))).
 * @param nx, ny, nz Grid dimensions.
 * @param rc         Return code (0 = success, -1 = error).
 */
void cece_core_set_export_field(void* data_ptr, const char* name, int name_len, double* field_data, int nx, int ny, int nz, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: cece_core_set_export_field - data_ptr is null" << std::endl;
        if (rc != nullptr) *rc = -1;
        return;
    }

    if (name == nullptr || name_len <= 0) {
        std::cerr << "ERROR: cece_core_set_export_field - invalid name argument" << std::endl;
        if (rc != nullptr) *rc = -1;
        return;
    }

    if (field_data == nullptr) {
        std::cerr << "ERROR: cece_core_set_export_field - field_data is null" << std::endl;
        if (rc != nullptr) *rc = -1;
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);
    std::string name_str(name, static_cast<size_t>(name_len));

    // Wrap the externally-owned memory as an unmanaged host view (zero-copy)
    using UnmanagedHost = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace, Kokkos::MemoryTraits<Kokkos::Unmanaged>>;
    UnmanagedHost h_view(field_data, nx, ny, nz);

    // Allocate a managed DualView3D and deep-copy the host data into it.
    // This ensures the DualView owns its memory and can be safely used
    // throughout the simulation lifetime.
    cece::DualView3D dv(name_str, nx, ny, nz);
    Kokkos::deep_copy(dv.view_host(), h_view);
    dv.modify_host();
    dv.sync_device();

    internal_data->export_state.fields[name_str] = dv;
    internal_data->persistent_export_ptrs[name_str] = field_data;

    std::cout << "INFO: cece_core_set_export_field - registered field '" << name_str << "' (" << nx << "x" << ny << "x" << nz << ")" << std::endl;

    if (rc != nullptr) {
        *rc = 0;
    }
}

/**
 * @brief Get the number of unique input fields required by the configuration.
 *
 * @param data_ptr Pointer to CeceInternalData
 * @param count Output: number of unique input fields
 * @param rc Return code (0 = success, non-zero = error)
 */
void cece_core_get_input_field_count(void* data_ptr, int* count, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: cece_core_get_input_field_count - data_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (count == nullptr) {
        std::cerr << "ERROR: cece_core_get_input_field_count - count pointer is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);
    *count = static_cast<int>(internal_data->unique_input_fields.size());

    if (rc != nullptr) {
        *rc = 0;
    }
}

/**
 * @brief Get the name of a required input field by index.
 *
 * @param data_ptr Pointer to CeceInternalData
 * @param index Zero-based index of the input field
 * @param name Output: input field name (C string, null-terminated)
 * @param name_len Output: length of the name (excluding null terminator)
 * @param rc Return code (0 = success, non-zero = error)
 */
void cece_core_get_input_field_name(void* data_ptr, int* index, char* name, int* name_len, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: cece_core_get_input_field_name - data_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (index == nullptr || name == nullptr || name_len == nullptr) {
        std::cerr << "ERROR: cece_core_get_input_field_name - null pointer argument" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);
    int idx = *index;

    if (idx < 0 || idx >= static_cast<int>(internal_data->unique_input_fields.size())) {
        std::cerr << "ERROR: cece_core_get_input_field_name - index out of bounds: " << idx << " (size: " << internal_data->unique_input_fields.size()
                  << ")" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    const std::string& field_name = internal_data->unique_input_fields[idx];

    // Check buffer size (assuming caller provides enough space, typically 256)
    // We safeguard against overflow purely by length check if we knew buffer size,
    // but here we assume standard Fortran string passing.
    std::strncpy(name, field_name.c_str(), 256);
    *name_len = field_name.length();

    if (rc != nullptr) {
        *rc = 0;
    }
}

/**
 * @brief Get the number of stream fields configured for data ingestion.
 *
 * @param data_ptr Pointer to CeceInternalData
 * @param count Output: number of stream fields
 * @param rc Return code (0 = success, non-zero = error)
 */
void cece_core_get_stream_field_count(void* data_ptr, int* count, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: cece_core_get_stream_field_count - data_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (count == nullptr) {
        std::cerr << "ERROR: cece_core_get_stream_field_count - count pointer is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);

    // Count all fields from all streams
    int total_fields = 0;
    for (const auto& stream : internal_data->config.cece_data.streams) {
        total_fields += stream.variables.size();
    }

    *count = total_fields;

    if (rc != nullptr) {
        *rc = 0;
    }
}

/**
 * @brief Get the name of a stream field by index.
 *
 * @param data_ptr Pointer to CeceInternalData
 * @param index Zero-based index of the field
 * @param name Output: field name (C string)
 * @param name_len Output: length of the field name
 * @param rc Return code (0 = success, non-zero = error)
 */
void cece_core_get_stream_field_name(void* data_ptr, int* index, char* name, int* name_len, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: cece_core_get_stream_field_name - data_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (index == nullptr || name == nullptr || name_len == nullptr) {
        std::cerr << "ERROR: cece_core_get_stream_field_name - null pointer argument" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);
    int idx = *index;

    // Flatten the stream variables into a single index
    int current_idx = 0;
    for (const auto& stream : internal_data->config.cece_data.streams) {
        for (const auto& var : stream.variables) {
            if (current_idx == idx) {
                // Found the field at this index
                const std::string& field_name = var.name_in_model;

                // Safe copy with bounds check
                std::strncpy(name, field_name.c_str(), 256);
                name[255] = '\0';  // Ensure null termination
                *name_len = std::min(static_cast<int>(field_name.length()), 255);

                if (rc != nullptr) {
                    *rc = 0;
                }
                return;
            }
            current_idx++;
        }
    }

    // Index out of bounds
    std::cerr << "ERROR: cece_core_get_stream_field_name - index out of bounds: " << idx << std::endl;
    if (rc != nullptr) {
        *rc = -1;
    }
}

/**
 * @brief Get the number of external fields required by CECE.
 *
 * @param data_ptr Pointer to CeceInternalData
 * @param count Output: number of external fields
 * @param rc Return code (0 = success, non-zero = error)
 */
void cece_core_get_external_field_count(void* data_ptr, int* count, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: cece_core_get_external_field_count - data_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (count == nullptr) {
        std::cerr << "ERROR: cece_core_get_external_field_count - count pointer is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);
    *count = static_cast<int>(internal_data->external_esmf_fields.size());

    if (rc != nullptr) {
        *rc = 0;
    }
}

/**
 * @brief Get the name of an external field by index.
 *
 * @param data_ptr Pointer to CeceInternalData
 * @param index Zero-based index of the field
 * @param name Output: field name (C string)
 * @param name_len Output: length of the field name
 * @param rc Return code (0 = success, non-zero = error)
 */
void cece_core_get_external_field_name(void* data_ptr, int* index, char* name, int* name_len, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: cece_core_get_external_field_name - data_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (index == nullptr || name == nullptr || name_len == nullptr) {
        std::cerr << "ERROR: cece_core_get_external_field_name - null pointer argument" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);
    int idx = *index;

    if (idx < 0 || idx >= static_cast<int>(internal_data->external_esmf_fields.size())) {
        std::cerr << "ERROR: cece_core_get_external_field_name - index out of bounds: " << idx
                  << " (size: " << internal_data->external_esmf_fields.size() << ")" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    const std::string& field_name = internal_data->external_esmf_fields[idx];

    // Safe copy with bounds check
    std::strncpy(name, field_name.c_str(), 256);
    name[255] = '\0';  // Ensure null terminator
    *name_len = std::min(static_cast<int>(field_name.length()), 255);

    if (rc != nullptr) {
        *rc = 0;
    }
}

}  // extern "C"

// -----------------------------------------------------------------------------
// NUOPC field-coupling config queries (path-based, callable before the
// simulation exists). These read the optional `nuopc:` section directly from
// the config file, mirroring cece_read_timing_config above. The Fortran cap
// calls them during Advertise (to declare fields) and again during Realize/Run
// (to map species/input keys to state item names). The parsed lists are already
// sorted alphabetically by key, so the advertisement order is identical on every
// rank regardless of YAML document order.
// -----------------------------------------------------------------------------

namespace {

// Copy a string into a caller-owned fixed buffer. Returns false (without
// writing) when the buffer cannot hold the string plus its null terminator, so
// the caller surfaces an error instead of silently truncating. On success the
// null-terminated value is written and *out_len receives the string length
// (zero for an unset optional attribute, which the cap reads as "omit").
bool nuopc_copy_str(const std::string& value, char* buf, int cap, int* out_len) {
    if (buf == nullptr || out_len == nullptr || cap <= 0) return false;
    if (static_cast<int>(value.size()) >= cap) return false;
    std::memcpy(buf, value.data(), value.size());
    buf[value.size()] = '\0';
    *out_len = static_cast<int>(value.size());
    return true;
}

// Populate one field spec from a parsed config's (already-sorted) list. Shared
// by the export and import spec entry points; only the error label differs.
void nuopc_fill_spec(const std::vector<std::pair<std::string, cece::NuopcFieldSpec>>& fields, int index, const std::string& label, char* key,
                     int key_cap, int* key_len, char* std_name, int std_cap, int* std_len, char* units, int units_cap, int* units_len, char* name,
                     int name_cap, int* name_len, int* rc) {
    if (rc != nullptr) *rc = 0;
    if (index < 0 || static_cast<size_t>(index) >= fields.size()) {
        std::cerr << "ERROR: cece_nuopc_" << label << "_spec - index out of range: " << index << " (size: " << fields.size() << ")" << std::endl;
        if (rc != nullptr) *rc = -1;
        return;
    }
    const std::string& cfg_key = fields[index].first;
    const cece::NuopcFieldSpec& spec = fields[index].second;
    // Validate every buffer fits before writing any, so a failure leaves all
    // buffers untouched.
    if (static_cast<int>(cfg_key.size()) >= key_cap || static_cast<int>(spec.standard_name.size()) >= std_cap ||
        static_cast<int>(spec.units.size()) >= units_cap || static_cast<int>(spec.name.size()) >= name_cap) {
        std::cerr << "ERROR: cece_nuopc_" << label << "_spec - output buffer too small for field '" << cfg_key << "'" << std::endl;
        if (rc != nullptr) *rc = -1;
        return;
    }
    bool ok = nuopc_copy_str(cfg_key, key, key_cap, key_len) && nuopc_copy_str(spec.standard_name, std_name, std_cap, std_len) &&
              nuopc_copy_str(spec.units, units, units_cap, units_len) && nuopc_copy_str(spec.name, name, name_cap, name_len);
    if (!ok) {
        if (rc != nullptr) *rc = -1;
        return;
    }
    if (rc != nullptr) *rc = 0;
}

// Count the entries of one nuopc list parsed from a config path. A missing or
// empty section yields zero (success), so the cap advertises nothing. `label`
// names the direction only for error messages.
void nuopc_count(const char* config_path, int path_len, const char* label, bool is_export, int* count, int* rc) {
    if (rc != nullptr) *rc = 0;
    if (count == nullptr) {
        if (rc != nullptr) *rc = -1;
        return;
    }
    *count = 0;
    try {
        std::string yaml_path(config_path, path_len);
        cece::CeceConfig config = cece::ParseConfig(yaml_path);
        *count = is_export ? static_cast<int>(config.nuopc.export_fields.size()) : static_cast<int>(config.nuopc.import_fields.size());
    } catch (const std::exception& e) {
        std::cerr << "ERROR: cece_nuopc_" << label << "_count - failed to parse config: " << e.what() << std::endl;
        if (rc != nullptr) *rc = -1;
    }
}

}  // namespace

extern "C" {

/**
 * @brief Count configured NUOPC export fields (0 when the section is absent).
 */
void cece_nuopc_export_count(const char* config_path, int path_len, int* count, int* rc) {
    nuopc_count(config_path, path_len, "export", true, count, rc);
}

/**
 * @brief Copy the index-th NUOPC export field spec (sorted by species key).
 */
void cece_nuopc_export_spec(const char* config_path, int path_len, int index, char* species, int species_cap, int* species_len, char* std_name,
                            int std_cap, int* std_len, char* units, int units_cap, int* units_len, char* name, int name_cap, int* name_len, int* rc) {
    try {
        std::string yaml_path(config_path, path_len);
        cece::CeceConfig config = cece::ParseConfig(yaml_path);
        nuopc_fill_spec(config.nuopc.export_fields, index, "export", species, species_cap, species_len, std_name, std_cap, std_len, units, units_cap,
                        units_len, name, name_cap, name_len, rc);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: cece_nuopc_export_spec - failed to parse config: " << e.what() << std::endl;
        if (rc != nullptr) *rc = -1;
    }
}

/**
 * @brief Count configured NUOPC import fields (0 when the section is absent).
 */
void cece_nuopc_import_count(const char* config_path, int path_len, int* count, int* rc) {
    nuopc_count(config_path, path_len, "import", false, count, rc);
}

/**
 * @brief Copy the index-th NUOPC import field spec (sorted by input key).
 */
void cece_nuopc_import_spec(const char* config_path, int path_len, int index, char* field, int field_cap, int* field_len, char* std_name, int std_cap,
                            int* std_len, char* units, int units_cap, int* units_len, char* name, int name_cap, int* name_len, int* rc) {
    try {
        std::string yaml_path(config_path, path_len);
        cece::CeceConfig config = cece::ParseConfig(yaml_path);
        nuopc_fill_spec(config.nuopc.import_fields, index, "import", field, field_cap, field_len, std_name, std_cap, std_len, units, units_cap,
                        units_len, name, name_cap, name_len, rc);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: cece_nuopc_import_spec - failed to parse config: " << e.what() << std::endl;
        if (rc != nullptr) *rc = -1;
    }
}

}  // extern "C"
