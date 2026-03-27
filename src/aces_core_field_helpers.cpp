#include <fstream>
#include <cstring>
/**
 * @file aces_core_field_helpers.cpp
 * @brief Helper functions for field management in ACES.
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

#include "aces/aces_internal.hpp"

extern "C" {

/**
 * @brief Get the number of species from the configuration.
 *
 * @param data_ptr Pointer to AcesInternalData
 * @param count Output: number of species
 * @param rc Return code (0 = success, non-zero = error)
 *
 * Requirements: R4
 */
void aces_core_get_species_count(void* data_ptr, int* count, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: aces_core_get_species_count - data_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (count == nullptr) {
        std::cerr << "ERROR: aces_core_get_species_count - count pointer is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);
    *count = static_cast<int>(internal_data->config.species_layers.size());

    std::cout << "INFO: aces_core_get_species_count returned " << *count << " species"
              << std::endl;

    if (rc != nullptr) {
        *rc = 0;
    }
}

/**
 * @brief Get the name of a species by index.
 *
 * @param data_ptr Pointer to AcesInternalData
 * @param index Zero-based index of the species
 * @param name Output: species name (C string, null-terminated)
 * @param name_len Output: length of the species name (excluding null terminator)
 * @param rc Return code (0 = success, non-zero = error)
 *
 * Requirements: R4
 */
void aces_core_get_species_name(void* data_ptr, int* index, char* name, int* name_len,
                                int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: aces_core_get_species_name - data_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (index == nullptr || name == nullptr || name_len == nullptr) {
        std::cerr << "ERROR: aces_core_get_species_name - null pointer argument" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);

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

            std::cout << "INFO: aces_core_get_species_name[" << *index << "] = " << name_str
                      << std::endl;

            if (rc != nullptr) {
                *rc = 0;
            }
            return;
        }
        ++idx;
    }

    // Index out of range
    std::cerr << "ERROR: aces_core_get_species_name - index " << *index << " out of range"
              << std::endl;
    if (rc != nullptr) {
        *rc = -1;
    }
}

/**
 * @brief Bind field data pointers to internal data.
 *
 * Stores the field data pointers passed from Fortran in the AcesInternalData
 * structure for later access during the Run phase.
 *
 * @param data_ptr Pointer to AcesInternalData
 * @param field_ptrs Array of field data pointers (one per species)
 * @param num_fields Number of fields in the array
 * @param rc Return code (0 = success, non-zero = error)
 *
 * Requirements: R5
 */
void aces_core_bind_fields(void* data_ptr, void** field_ptrs, int* num_fields, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: aces_core_bind_fields - data_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (num_fields == nullptr) {
        std::cerr << "ERROR: aces_core_bind_fields - num_fields pointer is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    // Special case: zero fields is a valid no-op
    if (*num_fields == 0) {
        std::cout << "INFO: aces_core_bind_fields - zero fields requested (no-op)" << std::endl;
        if (rc != nullptr) {
            *rc = 0;
        }
        return;
    }

    if (field_ptrs == nullptr) {
        std::cerr << "ERROR: aces_core_bind_fields - null pointer argument" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (*num_fields < 0) {
        std::cerr << "ERROR: aces_core_bind_fields - num_fields must be non-negative: "
                  << *num_fields << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);

    // Clear any existing field pointers
    internal_data->field_pointers.clear();
    internal_data->field_names.clear();

    // Store the field pointers and names
    int idx = 0;
    for (const auto& [species_name, layers] : internal_data->config.species_layers) {
        if (idx >= *num_fields) {
            std::cerr << "ERROR: aces_core_bind_fields - num_fields mismatch: "
                      << "expected " << idx << " got " << *num_fields << std::endl;
            if (rc != nullptr) {
                *rc = -1;
            }
            return;
        }

        if (field_ptrs[idx] == nullptr) {
            std::cerr << "ERROR: aces_core_bind_fields - field_ptrs[" << idx
                      << "] is null for species " << species_name << std::endl;
            if (rc != nullptr) {
                *rc = -1;
            }
            return;
        }

        internal_data->field_pointers.push_back(field_ptrs[idx]);
        internal_data->field_names.push_back(species_name);

        std::cout << "INFO: aces_core_bind_fields - bound field " << idx << " for species "
                  << species_name << " at address " << field_ptrs[idx] << std::endl;

        ++idx;
    }

    if (idx != *num_fields) {
        std::cerr << "ERROR: aces_core_bind_fields - num_fields mismatch: expected " << idx
                  << " got " << *num_fields << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    std::cout << "INFO: aces_core_bind_fields - bound " << *num_fields << " field pointers"
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
 * @param data_ptr Pointer to AcesInternalData
 * @param streams_path Output: path to streams file (C string, null-terminated)
 * @param path_len Output: length of the path (excluding null terminator)
 * @param rc Return code (0 = success, non-zero = error)
 *
 * Requirements: R6
 */
void aces_core_get_ingestor_streams_path(void* data_ptr, char* streams_path, int* path_len,
                                         int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: aces_core_get_ingestor_streams_path - data_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (streams_path == nullptr || path_len == nullptr) {
        std::cerr << "ERROR: aces_core_get_ingestor_streams_path - null pointer argument"
                  << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);

    // Check if ingestor streams are configured
    if (internal_data->config.aces_data.streams.empty()) {
        streams_path[0] = '\0';
        *path_len = 0;
        if (rc != nullptr) {
            *rc = 0;
        }
        return;
    }

    // Generate TIDE ESMF configuration
    std::string config_content = internal_data->ingestor.SerializeTideESMFConfig(internal_data->config.aces_data);

    // Write to file (using .rc extension common for ESMF resources)
    std::string filename = "aces_data_streams.rc";
    std::ofstream outfile(filename);
    if (!outfile.is_open()) {
        std::cerr << "ERROR: Failed to open output file for TIDE streams: " << filename << std::endl;
        if (rc != nullptr) *rc = -1;
        return;
    }
    outfile << config_content;
    outfile.close();

    // Return filename
    if (filename.length() >= 512) { // Assuming 512 is buffer size from Fortran
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
 * Called by the NUOPC cap (aces_cap.F90) after mesh creation when ingestor
 * streams are configured.  Delegates to AcesDataIngestor::IngestEmissionsInline
 *
 * @param data_ptr  Pointer to AcesInternalData.
 * @param c_clock   Opaque pointer to ESMF Clock handle.
 * @param c_mesh    Opaque pointer to ESMF Mesh (target mesh).
 * @param rc        Return code (0 = success, non-zero = error).
 */
void aces_ingestor_init(void* data_ptr, void* c_clock, void* c_mesh, int* rc) {
    if (rc != nullptr) *rc = 0;

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: aces_ingestor_init - data_ptr is null" << std::endl;
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
 * Called from the Fortran cap during ACES_InitializeRealize after
 * ESMF_FieldGet(farrayPtr=fptr) for each species.
 *
 * @param data_ptr   Pointer to AcesInternalData.
 * @param name       Species name (not null-terminated; use name_len).
 * @param name_len   Length of the name string.
 * @param field_data Raw pointer to the field data (from c_loc(fptr(1,1,1))).
 * @param nx, ny, nz Grid dimensions.
 * @param rc         Return code (0 = success, -1 = error).
 */
void aces_core_set_export_field(void* data_ptr, const char* name, int name_len,
                                double* field_data, int nx, int ny, int nz, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: aces_core_set_export_field - data_ptr is null" << std::endl;
        if (rc != nullptr) *rc = -1;
        return;
    }

    if (name == nullptr || name_len <= 0) {
        std::cerr << "ERROR: aces_core_set_export_field - invalid name argument" << std::endl;
        if (rc != nullptr) *rc = -1;
        return;
    }

    if (field_data == nullptr) {
        std::cerr << "ERROR: aces_core_set_export_field - field_data is null" << std::endl;
        if (rc != nullptr) *rc = -1;
        return;
    }

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);
    std::string name_str(name, static_cast<size_t>(name_len));

    // Wrap the ESMF-owned memory as an unmanaged host view (zero-copy)
    using UnmanagedHost = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace,
                                       Kokkos::MemoryTraits<Kokkos::Unmanaged>>;
    UnmanagedHost h_view(field_data, nx, ny, nz);

    // Allocate a managed DualView3D and deep-copy the host data into it.
    // This ensures the DualView owns its memory and can be safely used
    // throughout the simulation lifetime.
    aces::DualView3D dv(name_str, nx, ny, nz);
    Kokkos::deep_copy(dv.view_host(), h_view);
    dv.modify_host();
    dv.sync_device();

    internal_data->export_state.fields[name_str] = dv;

    std::cout << "INFO: aces_core_set_export_field - registered field '" << name_str
              << "' (" << nx << "x" << ny << "x" << nz << ")" << std::endl;

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
 * @param data_ptr     Pointer to AcesInternalData.
 * @param time_seconds Elapsed time in seconds since simulation start.
 * @param step_index   Current step index (0-based).
 * @param rc           Return code (0 = success, -1 = error).
 */
void aces_core_write_step(void* data_ptr, double time_seconds, int step_index, int* rc) {
    if (rc != nullptr) *rc = 0;

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: aces_core_write_step - data_ptr is null" << std::endl;
        if (rc != nullptr) *rc = -1;
        return;
    }

    auto* d = static_cast<aces::AcesInternalData*>(data_ptr);

    if (!d->standalone_mode || !d->standalone_writer) return;

    const int freq = d->config.output_config.frequency_steps;
    if (step_index % freq != 0) return;

    int w = d->standalone_writer->WriteTimeStep(d->export_state.fields, time_seconds, step_index);
    if (w != 0) {
        std::cerr << "WARNING: aces_core_write_step - WriteTimeStep returned " << w << std::endl;
        if (rc != nullptr) *rc = w;
    }
}



/**
 * @brief Get the number of unique input fields required by the configuration.
 *
 * @param data_ptr Pointer to AcesInternalData
 * @param count Output: number of unique input fields
 * @param rc Return code (0 = success, non-zero = error)
 */
void aces_core_get_input_field_count(void* data_ptr, int* count, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: aces_core_get_input_field_count - data_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (count == nullptr) {
        std::cerr << "ERROR: aces_core_get_input_field_count - count pointer is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);
    *count = static_cast<int>(internal_data->unique_input_fields.size());

    if (rc != nullptr) {
        *rc = 0;
    }
}

/**
 * @brief Get the name of a required input field by index.
 *
 * @param data_ptr Pointer to AcesInternalData
 * @param index Zero-based index of the input field
 * @param name Output: input field name (C string, null-terminated)
 * @param name_len Output: length of the name (excluding null terminator)
 * @param rc Return code (0 = success, non-zero = error)
 */
void aces_core_get_input_field_name(void* data_ptr, int* index, char* name, int* name_len,
                                    int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: aces_core_get_input_field_name - data_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (index == nullptr || name == nullptr || name_len == nullptr) {
        std::cerr << "ERROR: aces_core_get_input_field_name - null pointer argument" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);
    int idx = *index;

    if (idx < 0 || idx >= static_cast<int>(internal_data->unique_input_fields.size())) {
        std::cerr << "ERROR: aces_core_get_input_field_name - index out of bounds: " << idx
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
 * @param data_ptr Pointer to AcesInternalData
 * @param count Output: number of stream fields
 * @param rc Return code (0 = success, non-zero = error)
 */
void aces_core_get_stream_field_count(void* data_ptr, int* count, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: aces_core_get_stream_field_count - data_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (count == nullptr) {
        std::cerr << "ERROR: aces_core_get_stream_field_count - count pointer is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);

    // Count all fields from all streams
    int total_fields = 0;
    for (const auto& stream : internal_data->config.aces_data.streams) {
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
 * @param data_ptr Pointer to AcesInternalData
 * @param index Zero-based index of the field
 * @param name Output: field name (C string)
 * @param name_len Output: length of the field name
 * @param rc Return code (0 = success, non-zero = error)
 */
void aces_core_get_stream_field_name(void* data_ptr, int* index, char* name, int* name_len, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: aces_core_get_stream_field_name - data_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (index == nullptr || name == nullptr || name_len == nullptr) {
        std::cerr << "ERROR: aces_core_get_stream_field_name - null pointer argument" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);
    int idx = *index;

    // Flatten the stream variables into a single index
    int current_idx = 0;
    for (const auto& stream : internal_data->config.aces_data.streams) {
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
    std::cerr << "ERROR: aces_core_get_stream_field_name - index out of bounds: " << idx << std::endl;
    if (rc != nullptr) {
        *rc = -1;
    }
}

/**
 * @brief Get the number of external ESMF fields required by ACES.
 *
 * @param data_ptr Pointer to AcesInternalData
 * @param count Output: number of external ESMF fields
 * @param rc Return code (0 = success, non-zero = error)
 */
void aces_core_get_external_field_count(void* data_ptr, int* count, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: aces_core_get_external_field_count - data_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (count == nullptr) {
        std::cerr << "ERROR: aces_core_get_external_field_count - count pointer is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);
    *count = static_cast<int>(internal_data->external_esmf_fields.size());

    if (rc != nullptr) {
        *rc = 0;
    }
}

/**
 * @brief Get the name of an external ESMF field by index.
 *
 * @param data_ptr Pointer to AcesInternalData
 * @param index Zero-based index of the field
 * @param name Output: field name (C string)
 * @param name_len Output: length of the field name
 * @param rc Return code (0 = success, non-zero = error)
 */
void aces_core_get_external_field_name(void* data_ptr, int* index, char* name, int* name_len, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: aces_core_get_external_field_name - data_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (index == nullptr || name == nullptr || name_len == nullptr) {
        std::cerr << "ERROR: aces_core_get_external_field_name - null pointer argument" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);
    int idx = *index;

    if (idx < 0 || idx >= static_cast<int>(internal_data->external_esmf_fields.size())) {
        std::cerr << "ERROR: aces_core_get_external_field_name - index out of bounds: " << idx
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
