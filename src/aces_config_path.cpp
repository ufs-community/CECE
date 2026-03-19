/**
 * @file aces_config_path.cpp
 * @brief Global configuration file path management for ACES.
 *
 * Provides a thread-safe mechanism to set and retrieve the configuration file path
 * used by ACES initialization routines. This allows the Fortran NUOPC cap to specify
 * a custom config file path instead of hardcoding "aces_config.yaml".
 */

#include <string>
#include <mutex>

namespace aces {

// Global config file path (default: aces_config.yaml)
static std::string g_config_file_path = "aces_config.yaml";
static std::mutex g_config_path_mutex;

/**
 * @brief Set the configuration file path for ACES initialization.
 *
 * This function allows the Fortran NUOPC cap to specify a custom configuration
 * file path. Thread-safe via mutex.
 *
 * @param config_path C-string path to the configuration file
 */
void SetConfigFilePath(const char* config_path) {
    if (config_path == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_config_path_mutex);
    g_config_file_path = config_path;
}

/**
 * @brief Get the current configuration file path.
 *
 * Returns the configuration file path that will be used by initialization routines.
 * Defaults to "aces_config.yaml" if not explicitly set.
 *
 * @return std::string reference to the current config file path
 */
const std::string& GetConfigFilePath() {
    std::lock_guard<std::mutex> lock(g_config_path_mutex);
    return g_config_file_path;
}

}  // namespace aces

// C interface for Fortran
extern "C" {

/**
 * @brief C interface to set the configuration file path.
 *
 * @param config_path C-string path to the configuration file (null-terminated)
 * @param path_len Length of the config_path string (for Fortran compatibility)
 */
void aces_set_config_file_path(const char* config_path, int path_len) {
    if (config_path == nullptr || path_len <= 0) {
        return;
    }
    // Create a null-terminated string from the Fortran string
    std::string path(config_path, path_len);
    // Trim trailing spaces (Fortran strings are often padded)
    path.erase(path.find_last_not_of(" \t\n\r\f\v") + 1);
    aces::SetConfigFilePath(path.c_str());
}

}  // extern "C"
