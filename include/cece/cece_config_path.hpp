/**
 * @file cece_config_path.hpp
 * @brief Configuration file path management for CECE.
 */

#ifndef CECE_CONFIG_PATH_HPP
#define CECE_CONFIG_PATH_HPP

#include <string>

namespace cece {

/**
 * @brief Set the configuration file path for CECE initialization.
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

}  // namespace cece

#endif  // CECE_CONFIG_PATH_HPP
