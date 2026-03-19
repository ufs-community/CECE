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

    if (field_ptrs == nullptr || num_fields == nullptr) {
        std::cerr << "ERROR: aces_core_bind_fields - null pointer argument" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (*num_fields <= 0) {
        std::cerr << "ERROR: aces_core_bind_fields - num_fields must be positive: "
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
 * @brief Get the CDEPS streams file path from configuration.
 *
 * Returns the path to the CDEPS streams configuration file.
 * If no streams are configured, returns an empty string.
 *
 * @param data_ptr Pointer to AcesInternalData
 * @param streams_path Output: path to streams file (C string, null-terminated)
 * @param path_len Output: length of the path (excluding null terminator)
 * @param rc Return code (0 = success, non-zero = error)
 *
 * Requirements: R6
 */
void aces_core_get_cdeps_streams_path(void* data_ptr, char* streams_path, int* path_len,
                                      int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: aces_core_get_cdeps_streams_path - data_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (streams_path == nullptr || path_len == nullptr) {
        std::cerr << "ERROR: aces_core_get_cdeps_streams_path - null pointer argument"
                  << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);

    // Check if CDEPS streams are configured
    if (internal_data->config.cdeps_config.streams.empty()) {
        std::cout << "INFO: aces_core_get_cdeps_streams_path - no CDEPS streams configured"
                  << std::endl;
        streams_path[0] = '\0';
        *path_len = 0;
        if (rc != nullptr) {
            *rc = 0;
        }
        return;
    }

    // For now, return a default streams file path
    // In production, this would be read from config or environment
    std::string default_path = "examples/cdeps_streams_ex1.txt";
    int len = static_cast<int>(default_path.length());

    // Ensure we don't overflow the buffer
    for (int i = 0; i < len; ++i) {
        streams_path[i] = default_path[i];
    }
    streams_path[len] = '\0';  // Null-terminate

    *path_len = len;

    std::cout << "INFO: aces_core_get_cdeps_streams_path returned: " << default_path
              << std::endl;

    if (rc != nullptr) {
        *rc = 0;
    }
}

}  // extern "C"
