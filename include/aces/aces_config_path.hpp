/**
 * @file aces_config_path.hpp
 * @brief Configuration file path management for ACES.
 */

#ifndef ACES_CONFIG_PATH_HPP
#define ACES_CONFIG_PATH_HPP

#include <string>

namespace aces {

/**
 * @brief Set the configuration file path for ACES initialization.
 *
 * @param config_path Path to the configuration file
 */
void SetConfigFilePath(const char* config_path);

/**
 * @brief Get the current configuration file path.
 *
 * @return Copy of the current config file path (thread-safe)
 */
std::string GetConfigFilePath();

}  // namespace aces

#endif  // ACES_CONFIG_PATH_HPP
