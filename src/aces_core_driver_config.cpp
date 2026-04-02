/**
 * @file aces_core_driver_config.cpp
 * @brief C interface to access driver configuration from ESMF config file.
 *
 * This module provides validation for driver configuration parameters:
 * - Validates timestep_seconds > 0 (Requirement 3.3)
 * - Validates grid dimensions nx, ny > 0 (Requirement 14.7)
 * - Returns error code -1 if validation fails
 *
 * Additional validation performed in Fortran driver:
 * - start_time < end_time validation (Requirement 1.4, 2.4)
 * - Timestep divisibility warning (Requirement 3.4, 14.7)
 */

#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

#include "aces/aces_config.hpp"
#include "aces/aces_config_path.hpp"

extern "C" {

/**
 * @brief Parse ESMF config file format (simple key: value pairs)
 *
 * Reads a simple config file with format:
 *   key: value
 *
 * @param config_file Path to the config file
 * @param config Map to store parsed key-value pairs
 * @return 0 on success, -1 on error
 */
static int parse_esmf_config_file(const std::string& config_file,
                                  std::map<std::string, std::string>& config) {
    std::ifstream file(config_file);
    if (!file.is_open()) {
        std::cerr << "ERROR: Failed to open config file: " << config_file << "\n";
        return -1;
    }

    std::string line;
    int line_num = 0;
    while (std::getline(file, line)) {
        line_num++;

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Find the colon separator
        size_t colon_pos = line.find(':');
        if (colon_pos == std::string::npos) {
            std::cerr << "WARNING: Line " << line_num << " has no colon separator: " << line
                      << "\n";
            continue;
        }

        // Extract key and value
        std::string key = line.substr(0, colon_pos);
        std::string value = line.substr(colon_pos + 1);

        // Trim whitespace from key
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);

        // Trim whitespace from value
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);

        if (!key.empty() && !value.empty()) {
            config[key] = value;
        }
    }

    file.close();
    return 0;
}

/**
 * @brief Get driver configuration values from ESMF config file.
 *
 * @param config_file Path to the ESMF config file.
 * @param config_file_len Length of the config_file string.
 * @param start_time Output buffer for start time (ISO 8601 format).
 * @param start_time_len Length of the start_time buffer.
 * @param end_time Output buffer for end time (ISO 8601 format).
 * @param end_time_len Length of the end_time buffer.
 * @param timestep_seconds Output for timestep in seconds.
 * @param mesh_file Output buffer for mesh file path (empty if not specified).
 * @param mesh_file_len Length of the mesh_file buffer.
 * @param nx Output for grid nx.
 * @param ny Output for grid ny.
 * @param rc Return code (0 on success, -1 on error).
 */
void aces_core_get_driver_config(const char* config_file, int config_file_len, char* start_time,
                                 int start_time_len, char* end_time, int end_time_len,
                                 int* timestep_seconds, char* mesh_file, int mesh_file_len, int* nx,
                                 int* ny, int* rc) {
    *rc = 0;

    if (config_file == nullptr) {
        std::cerr << "ERROR: aces_core_get_driver_config - null config_file pointer\n";
        *rc = -1;
        return;
    }

    if (start_time == nullptr || end_time == nullptr || mesh_file == nullptr) {
        std::cerr << "ERROR: aces_core_get_driver_config - null output buffers\n";
        *rc = -1;
        return;
    }

    try {
        // Convert C string to std::string
        std::string config_path(config_file, config_file_len);

        // Parse the ESMF config file
        std::map<std::string, std::string> config;
        int parse_rc = parse_esmf_config_file(config_path, config);
        if (parse_rc != 0) {
            std::cerr << "ERROR: Failed to parse config file: " << config_path << "\n";
            *rc = -1;
            return;
        }

        // Extract driver config values with defaults
        std::string start_str = "2020-01-01T00:00:00";
        std::string end_str = "2020-01-02T00:00:00";
        int timestep_secs = 3600;
        std::string mesh_str = "";
        int grid_nx = 4;
        int grid_ny = 4;

        // Read start_time
        if (config.find("start_time") != config.end()) {
            start_str = config["start_time"];
        }

        // Read end_time
        if (config.find("end_time") != config.end()) {
            end_str = config["end_time"];
        }

        // Read timestep_seconds
        if (config.find("timestep_seconds") != config.end()) {
            try {
                timestep_secs = std::stoi(config["timestep_seconds"]);
            } catch (const std::exception& e) {
                std::cerr << "WARNING: Failed to parse timestep_seconds: " << e.what() << "\n";
                timestep_secs = 3600;
            }
        }

        // Read grid_nx
        if (config.find("grid_nx") != config.end()) {
            try {
                grid_nx = std::stoi(config["grid_nx"]);
            } catch (const std::exception& e) {
                std::cerr << "WARNING: Failed to parse grid_nx: " << e.what() << "\n";
                grid_nx = 4;
            }
        }

        // Read grid_ny
        if (config.find("grid_ny") != config.end()) {
            try {
                grid_ny = std::stoi(config["grid_ny"]);
            } catch (const std::exception& e) {
                std::cerr << "WARNING: Failed to parse grid_ny: " << e.what() << "\n";
                grid_ny = 4;
            }
        }

        // Read mesh_file (optional)
        if (config.find("mesh_file") != config.end()) {
            mesh_str = config["mesh_file"];
        }

        // Copy start_time
        if (start_str.length() >= static_cast<size_t>(start_time_len)) {
            std::cerr << "WARNING: start_time buffer too small, truncating\n";
            std::strncpy(start_time, start_str.c_str(), start_time_len - 1);
            start_time[start_time_len - 1] = '\0';
        } else {
            std::strcpy(start_time, start_str.c_str());
        }

        // Copy end_time
        if (end_str.length() >= static_cast<size_t>(end_time_len)) {
            std::cerr << "WARNING: end_time buffer too small, truncating\n";
            std::strncpy(end_time, end_str.c_str(), end_time_len - 1);
            end_time[end_time_len - 1] = '\0';
        } else {
            std::strcpy(end_time, end_str.c_str());
        }

        // Copy timestep_seconds
        *timestep_seconds = timestep_secs;

        // Copy mesh_file - use strncpy with proper null termination
        if (mesh_str.empty()) {
            // If mesh_file is empty, fill entire buffer with spaces (Fortran convention)
            for (int i = 0; i < mesh_file_len; ++i) {
                mesh_file[i] = ' ';
            }
        } else if (mesh_str.length() >= static_cast<size_t>(mesh_file_len)) {
            std::cerr << "WARNING: mesh_file buffer too small, truncating\n";
            std::strncpy(mesh_file, mesh_str.c_str(), mesh_file_len - 1);
            mesh_file[mesh_file_len - 1] = '\0';
        } else {
            // Use strncpy instead of strcpy for safety
            std::strncpy(mesh_file, mesh_str.c_str(), mesh_file_len - 1);
            mesh_file[mesh_str.length()] = '\0';
            // Fill the rest with spaces (Fortran convention)
            for (size_t i = mesh_str.length() + 1; i < static_cast<size_t>(mesh_file_len); ++i) {
                mesh_file[i] = ' ';
            }
        }

        // Copy grid dimensions
        *nx = grid_nx;
        *ny = grid_ny;

        // Validate configuration values (Requirements 1.4, 2.4, 3.3, 14.7)

        // Validate timestep_seconds > 0
        if (*timestep_seconds <= 0) {
            std::cerr
                << "ERROR: [aces_core_get_driver_config] timestep_seconds must be positive, got: "
                << *timestep_seconds << "\n";
            *rc = -1;
            return;
        }

        // Validate grid dimensions nx, ny > 0
        if (*nx <= 0) {
            std::cerr << "ERROR: [aces_core_get_driver_config] grid_nx must be positive, got: "
                      << *nx << "\n";
            *rc = -1;
            return;
        }

        if (*ny <= 0) {
            std::cerr << "ERROR: [aces_core_get_driver_config] grid_ny must be positive, got: "
                      << *ny << "\n";
            *rc = -1;
            return;
        }

        // Note: start_time < end_time validation is performed in the Fortran driver
        // after parsing ISO8601 strings to ESMF_Time objects, as string comparison
        // is not reliable for datetime validation.

        std::cout << "INFO: [aces_core_get_driver_config] Successfully read driver config:\n";
        std::cout << "  start_time: " << start_str << "\n";
        std::cout << "  end_time: " << end_str << "\n";
        std::cout << "  timestep_seconds: " << *timestep_seconds << "\n";
        std::cout << "  mesh_file: "
                  << (mesh_str.empty() ? "(none - will generate grid)" : mesh_str) << "\n";
        std::cout << "  grid: " << *nx << " x " << *ny << "\n";

    } catch (const std::exception& e) {
        std::cerr << "ERROR: aces_core_get_driver_config - Exception: " << e.what() << "\n";
        *rc = -1;
        return;
    }
}

}  // extern "C"
