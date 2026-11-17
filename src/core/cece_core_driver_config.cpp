/**
 * @file cece_core_driver_config.cpp
 * @brief C interface to access driver configuration from config file.
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
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

#include "cece/cece_config.hpp"
#include "cece/cece_config_path.hpp"
#include "cece/cece_logger.hpp"

namespace fs = std::filesystem;
using namespace std::literals::string_literals;

extern "C" {

/**
 * @brief Parse config file format (simple key: value pairs)
 *
 * Reads a simple config file with format:
 *   key: value
 *
 * @param config_file Path to the config file
 * @param config Map to store parsed key-value pairs
 * @return 0 on success, -1 on error
 */
static int parse_esmf_config_file(const std::string& config_file, std::map<std::string, std::string>& config) {
    std::ifstream file(config_file);
    if (!file.is_open()) {
        CECE_LOG_ERROR("ERROR: Failed to open config file: "s + config_file + "\n"s);
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
            CECE_LOG_WARNING("WARNING: Line "s + std::to_string(line_num) + " has no colon separator: "s + line + "\n");
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
 * @brief Get driver configuration values from config file.
 *
 * @param config_file Path to the config file.
 * @param config_file_len Length of the config_file string.
 * @param start_time Output buffer for start time (ISO 8601 format).
 * @param start_time_len Length of the start_time buffer.
 * @param end_time Output buffer for end time (ISO 8601 format).
 * @param end_time_len Length of the end_time buffer.
 * @param timestep_seconds Output for timestep in seconds.
 * @param gridspec_file Output buffer for GRIDSPEC file path (empty if not specified).
 * @param gridspec_file_len Length of the gridspec_file buffer.
 * @param nx Output for grid nx.
 * @param ny Output for grid ny.
 * @param rc Return code (0 on success, -1 on error).
 */
// TODO: Check for callers and remove
void cece_core_get_driver_config(const char* config_file, int config_file_len, char* start_time, int start_time_len, char* end_time, int end_time_len,
                                 int* timestep_seconds, char* gridspec_file, int gridspec_file_len, int* nx, int* ny, int* rc) {
    *rc = 0;

    if (config_file == nullptr) {
        CECE_LOG_ERROR("ERROR: cece_core_get_driver_config - null config_file pointer\n");
        *rc = -1;
        return;
    }

    if (start_time == nullptr || end_time == nullptr || gridspec_file == nullptr) {
        CECE_LOG_ERROR("ERROR: cece_core_get_driver_config - null output buffers\n");
        *rc = -1;
        return;
    }

    try {
        // Convert C string to std::string
        std::string config_path(config_file, config_file_len);

        // Parse the config file
        std::map<std::string, std::string> config;
        int parse_rc = parse_esmf_config_file(config_path, config);
        if (parse_rc != 0) {
            CECE_LOG_ERROR("ERROR: Failed to parse config file: "s + config_path + "\n"s);
            *rc = -1;
            return;
        }

        // Extract driver config values with defaults
        std::string start_str = "2020-01-01T00:00:00";
        std::string end_str = "2020-01-02T00:00:00";
        int timestep_secs = 3600;
        std::string gridspec_str = "";
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
                CECE_LOG_WARNING("WARNING: Failed to parse timestep_seconds: "s + e.what() + "\n"s);
                timestep_secs = 3600;
            }
        }

        // Read grid_nx
        if (config.find("grid_nx") != config.end()) {
            try {
                grid_nx = std::stoi(config["grid_nx"]);
            } catch (const std::exception& e) {
                CECE_LOG_WARNING("WARNING: Failed to parse grid_nx: "s + e.what() + "\n"s);
                grid_nx = 4;
            }
        }

        // Read grid_ny
        if (config.find("grid_ny") != config.end()) {
            try {
                grid_ny = std::stoi(config["grid_ny"]);
            } catch (const std::exception& e) {
                CECE_LOG_WARNING("WARNING: Failed to parse grid_ny: "s + e.what() + "\n"s);
                grid_ny = 4;
            }
        }

        // Read gridspec_file (optional)
        if (config.find("gridspec_file") != config.end()) {
            gridspec_str = config["gridspec_file"];
        }

        // Copy start_time
        if (start_str.length() >= static_cast<size_t>(start_time_len)) {
            CECE_LOG_WARNING("WARNING: start_time buffer too small, truncating\n");
            std::strncpy(start_time, start_str.c_str(), start_time_len - 1);
            start_time[start_time_len - 1] = '\0';
        } else {
            std::strcpy(start_time, start_str.c_str());
        }

        // Copy end_time
        if (end_str.length() >= static_cast<size_t>(end_time_len)) {
            CECE_LOG_WARNING("WARNING: end_time buffer too small, truncating\n");
            std::strncpy(end_time, end_str.c_str(), end_time_len - 1);
            end_time[end_time_len - 1] = '\0';
        } else {
            std::strcpy(end_time, end_str.c_str());
        }

        // Copy timestep_seconds
        *timestep_seconds = timestep_secs;

        // Copy gridspec_file - use strncpy with proper null termination
        if (gridspec_str.empty()) {
            // Null-terminate so C/C++ consumers see an empty string; fill the
            // rest with spaces for Fortran callers that rely on blank-padding.
            gridspec_file[0] = '\0';
            for (int i = 1; i < gridspec_file_len; ++i) {
                gridspec_file[i] = ' ';
            }
        } else if (gridspec_str.length() >= static_cast<size_t>(gridspec_file_len)) {
            CECE_LOG_WARNING("WARNING: gridspec_file buffer too small, truncating\n");
            std::strncpy(gridspec_file, gridspec_str.c_str(), gridspec_file_len - 1);
            gridspec_file[gridspec_file_len - 1] = '\0';
        } else {
            // Use strncpy instead of strcpy for safety
            std::strncpy(gridspec_file, gridspec_str.c_str(), gridspec_file_len - 1);
            gridspec_file[gridspec_str.length()] = '\0';
            // Fill the rest with spaces (Fortran convention)
            for (size_t i = gridspec_str.length() + 1; i < static_cast<size_t>(gridspec_file_len); ++i) {
                gridspec_file[i] = ' ';
            }
        }

        // Copy grid dimensions
        *nx = grid_nx;
        *ny = grid_ny;

        // Validate configuration values (Requirements 1.4, 2.4, 3.3, 14.7)

        // Validate timestep_seconds > 0
        if (*timestep_seconds <= 0) {
            CECE_LOG_ERROR("ERROR: [cece_core_get_driver_config] timestep_seconds must be positive, got: "s + std::to_string(*timestep_seconds) +
                           "\n"s);
            *rc = -1;
            return;
        }

        // Validate grid dimensions nx, ny > 0
        if (*nx <= 0) {
            CECE_LOG_ERROR("ERROR: [cece_core_get_driver_config] grid_nx must be positive, got: "s + std::to_string(*nx) + "\n"s);
            *rc = -1;
            return;
        }

        if (*ny <= 0) {
            CECE_LOG_ERROR("ERROR: [cece_core_get_driver_config] grid_ny must be positive, got: "s + std::to_string(*ny) + "\n"s);
            *rc = -1;
            return;
        }

        // Note: start_time < end_time validation is performed in the driver
        // after parsing ISO8601 strings to time objects, as string comparison
        // is not reliable for datetime validation.

        std::string logmsg = "INFO: [cece_core_get_driver_config] Successfully read driver config:\n"s;
        logmsg += "  start_time: "s + start_str + "\n"s;
        logmsg += "  end_time: "s + end_str + "\n"s;
        logmsg += "  timestep_seconds: "s + std::to_string(*timestep_seconds) + "\n"s;
        logmsg += "  gridspec_file: "s + (gridspec_str.empty() ? "(none - will generate grid)"s : gridspec_str) + "\n"s;
        logmsg += "  grid: "s + std::to_string(*nx) + " x "s + std::to_string(*ny) + "\n"s;
        CECE_LOG_INFO(logmsg);

    } catch (const std::exception& e) {
        CECE_LOG_ERROR("ERROR: cece_core_get_driver_config - Exception: "s + e.what() + "\n"s);
        *rc = -1;
        return;
    }
}

}  // extern "C"
