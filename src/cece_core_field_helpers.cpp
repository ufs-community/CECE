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

            std::cout << "INFO: cece_core_get_species_name[" << *index << "] = " << name_str
                      << std::endl;

            if (rc != nullptr) {
                *rc = 0;
            }
            return;
        }
        ++idx;
    }

    // Index out of range
    std::cerr << "ERROR: cece_core_get_species_name - index " << *index << " out of range"
              << std::endl;
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
void cece_core_get_grid_config(void* data_ptr, int* nx, int* ny, double* lon_min, double* lon_max,
                               double* lat_min, double* lat_max, int* rc) {
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
    if (nx == nullptr || ny == nullptr || lon_min == nullptr || lon_max == nullptr ||
        lat_min == nullptr || lat_max == nullptr) {
        std::cerr << "ERROR: cece_core_get_grid_config - null output pointer" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    // Get grid configuration from parsed YAML config
    *nx = internal_data->config.driver_config.grid.nx;
    *ny = internal_data->config.driver_config.grid.ny;
    *lon_min = internal_data->config.driver_config.grid.lon_min;
    *lon_max = internal_data->config.driver_config.grid.lon_max;
    *lat_min = internal_data->config.driver_config.grid.lat_min;
    *lat_max = internal_data->config.driver_config.grid.lat_max;

    std::cout << "INFO: Grid config retrieved: nx=" << *nx << " ny=" << *ny
              << " lon_min=" << *lon_min << " lon_max=" << *lon_max << " lat_min=" << *lat_min
              << " lat_max=" << *lat_max << std::endl;
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
void cece_core_get_timing_config(void* data_ptr, char* start_time, char* end_time,
                                 int* timestep_seconds, int max_len, int* rc) {
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

    std::cout << "INFO: Timing config retrieved: start=" << start_time << " end=" << end_time
              << " timestep=" << *timestep_seconds << " seconds" << std::endl;
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
void cece_read_timing_config(const char* config_path, int path_len, char* start_time,
                             char* end_time, int* timestep_seconds, int max_len, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    try {
        // Convert C string to std::string
        std::string yaml_path(config_path, path_len);

        // Parse YAML file
        YAML::Node root = YAML::LoadFile(yaml_path);

        // Default values
        std::string start_default = "2020-01-01T00:00:00";
        std::string end_default = "2020-01-01T06:00:00";
        int timestep_default = 3600;

        // Read timing configuration
        if (root["driver"]) {
            const auto& driver_node = root["driver"];

            if (driver_node["start_time"]) {
                start_default = driver_node["start_time"].as<std::string>();
            }
            if (driver_node["end_time"]) {
                end_default = driver_node["end_time"].as<std::string>();
            }
            if (driver_node["timestep_seconds"]) {
                timestep_default = driver_node["timestep_seconds"].as<int>();
            }
        }

        // Copy strings safely
        strncpy(start_time, start_default.c_str(), max_len - 1);
        start_time[max_len - 1] = '\0';

        strncpy(end_time, end_default.c_str(), max_len - 1);
        end_time[max_len - 1] = '\0';

        *timestep_seconds = timestep_default;

        std::cout << "INFO: Driver timing config loaded: start=" << start_time
                  << " end=" << end_time << " timestep=" << *timestep_seconds << " seconds"
                  << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "ERROR: Failed to read timing config from " << config_path << ": " << e.what()
                  << std::endl;
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
        std::cerr << "ERROR: cece_core_bind_fields - num_fields must be non-negative: "
                  << *num_fields << std::endl;
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
            std::cerr << "ERROR: cece_core_bind_fields - num_fields mismatch: " << "expected "
                      << idx << " got " << *num_fields << std::endl;
            if (rc != nullptr) {
                *rc = -1;
            }
            return;
        }

        if (field_ptrs[idx] == nullptr) {
            std::cerr << "ERROR: cece_core_bind_fields - field_ptrs[" << idx
                      << "] is null for species " << species_name << std::endl;
            if (rc != nullptr) {
                *rc = -1;
            }
            return;
        }

        internal_data->field_pointers.push_back(field_ptrs[idx]);
        internal_data->field_names.push_back(species_name);

        std::cout << "INFO: cece_core_bind_fields - bound field " << idx << " for species "
                  << species_name << " at address " << field_ptrs[idx] << std::endl;

        ++idx;
    }

    if (idx != *num_fields) {
        std::cerr << "ERROR: cece_core_bind_fields - num_fields mismatch: expected " << idx
                  << " got " << *num_fields << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    std::cout << "INFO: cece_core_bind_fields - bound " << *num_fields << " field pointers"
              << std::endl;

    if (rc != nullptr) {
        *rc = 0;
    }
}

/**
 * @brief Get the TIDE streams file path from configuration.
 *
 * Returns the path to the TIDE streams configuration file.
 * If no streams are configured, returns an empty string.
 *
 * @param data_ptr Pointer to CeceInternalData
 * @param streams_path Output: path to streams file (C string, null-terminated)
 * @param path_len Output: length of the path (excluding null terminator)
 * @param rc Return code (0 = success, non-zero = error)
 *
 * Requirements: R6
 */
void cece_core_get_ingestor_streams_path(void* data_ptr, char* streams_path, int* path_len,
                                         int* rc) {
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
        std::cerr << "ERROR: cece_core_get_ingestor_streams_path - null pointer argument"
                  << std::endl;
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

    // Generate TIDE YAML configuration
    std::string config_content =
        internal_data->ingestor.SerializeTideYaml(internal_data->config.cece_data);

    // Write to file (using .yaml extension for modern TIDE interface)
    std::string filename = "cece_data_streams.yaml";
    std::ofstream outfile(filename);
    if (!outfile.is_open()) {
        std::cerr << "ERROR: Failed to open output file for TIDE streams: " << filename
                  << std::endl;
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
 * @param c_clock   Opaque pointer to ESMF Clock handle.
 * @param c_mesh    Opaque pointer to ESMF Mesh (target mesh).
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
    // TIDE operates in Fortran.
    if (rc != nullptr) *rc = 0;
}

/**
 * @brief Register an ESMF export field data pointer in the internal field map.
 *
 * Called from the Fortran cap during CECE_InitializeRealize after
 * ESMF_FieldGet(farrayPtr=fptr) for each species.
 *
 * @param data_ptr   Pointer to CeceInternalData.
 * @param name       Species name (not null-terminated; use name_len).
 * @param name_len   Length of the name string.
 * @param field_data Raw pointer to the field data (from c_loc(fptr(1,1,1))).
 * @param nx, ny, nz Grid dimensions.
 * @param rc         Return code (0 = success, -1 = error).
 */
void cece_core_set_export_field(void* data_ptr, const char* name, int name_len, double* field_data,
                                int nx, int ny, int nz, int* rc) {
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

    // Wrap the ESMF-owned memory as an unmanaged host view (zero-copy)
    using UnmanagedHost = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace,
                                       Kokkos::MemoryTraits<Kokkos::Unmanaged>>;
    UnmanagedHost h_view(field_data, nx, ny, nz);

    // Allocate a managed DualView3D and deep-copy the host data into it.
    // This ensures the DualView owns its memory and can be safely used
    // throughout the simulation lifetime.
    cece::DualView3D dv(name_str, nx, ny, nz);
    Kokkos::deep_copy(dv.view_host(), h_view);
    dv.modify_host();
    dv.sync_device();

    internal_data->export_state.fields[name_str] = dv;

    std::cout << "INFO: cece_core_set_export_field - registered field '" << name_str << "' (" << nx
              << "x" << ny << "x" << nz << ")" << std::endl;

    if (rc != nullptr) {
        *rc = 0;
    }
}

/**
 * @brief Write one output timestep via the standalone writer.
 *
 * No-op if not in standalone mode or writer is null.
 * Respects the output frequency configured in output_config.frequency_steps.
 *
 * @param data_ptr     Pointer to CeceInternalData.
 * @param time_seconds Elapsed time in seconds since simulation start.
 * @param step_index   Current step index (0-based).
 * @param rc           Return code (0 = success, -1 = error).
 */
void cece_core_write_step(void* data_ptr, double time_seconds, int step_index, int* rc) {
    if (rc != nullptr) *rc = 0;

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: cece_core_write_step - data_ptr is null" << std::endl;
        if (rc != nullptr) *rc = -1;
        return;
    }

    auto* d = static_cast<cece::CeceInternalData*>(data_ptr);

    if (!d->standalone_mode || !d->standalone_writer) return;

    const int freq = d->config.output_config.frequency_steps;
    if (step_index % freq != 0) return;

    // Critical: ensure all computations complete before writing
    Kokkos::fence();

    int w = d->standalone_writer->WriteTimeStep(d->export_state.fields, time_seconds, step_index);

    // Critical: sync to ensure all I/O completes before returning
    Kokkos::fence();

    if (w != 0) {
        std::cerr << "WARNING: cece_core_write_step - WriteTimeStep returned " << w << std::endl;
        if (rc != nullptr) *rc = w;
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
void cece_core_get_input_field_name(void* data_ptr, int* index, char* name, int* name_len,
                                    int* rc) {
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
        std::cerr << "ERROR: cece_core_get_input_field_name - index out of bounds: " << idx
                  << " (size: " << internal_data->unique_input_fields.size() << ")" << std::endl;
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
void cece_core_get_stream_field_name(void* data_ptr, int* index, char* name, int* name_len,
                                     int* rc) {
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
    std::cerr << "ERROR: cece_core_get_stream_field_name - index out of bounds: " << idx
              << std::endl;
    if (rc != nullptr) {
        *rc = -1;
    }
}

/**
 * @brief Get the number of external ESMF fields required by CECE.
 *
 * @param data_ptr Pointer to CeceInternalData
 * @param count Output: number of external ESMF fields
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
        std::cerr << "ERROR: cece_core_get_external_field_count - count pointer is null"
                  << std::endl;
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
 * @brief Get the name of an external ESMF field by index.
 *
 * @param data_ptr Pointer to CeceInternalData
 * @param index Zero-based index of the field
 * @param name Output: field name (C string)
 * @param name_len Output: length of the field name
 * @param rc Return code (0 = success, non-zero = error)
 */
void cece_core_get_external_field_name(void* data_ptr, int* index, char* name, int* name_len,
                                       int* rc) {
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
        std::cerr << "ERROR: cece_core_get_external_field_name - null pointer argument"
                  << std::endl;
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
    name[255] = '\0';  // Ensure null termination
    *name_len = std::min(static_cast<int>(field_name.length()), 255);

    if (rc != nullptr) {
        *rc = 0;
    }
}

}  // extern "C"
