/**
 * @file aces_config_validator.hpp
 * @brief Configuration validation for ACES
 */

#ifndef ACES_CONFIG_VALIDATOR_HPP
#define ACES_CONFIG_VALIDATOR_HPP

#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace aces {

/**
 * @struct ValidationError
 * @brief Represents a configuration validation error
 */
struct ValidationError {
    std::string field;        ///< Field that failed validation
    std::string message;      ///< Error message
    std::string suggestion;   ///< Suggested corrective action
};

/**
 * @struct ValidationResult
 * @brief Result of configuration validation
 */
struct ValidationResult {
    bool is_valid = true;                           ///< Whether configuration is valid
    std::vector<ValidationError> errors;            ///< List of validation errors
    std::vector<std::string> warnings;              ///< List of warnings

    /**
     * @brief Check if validation passed
     * @return True if no errors
     */
    bool IsValid() const {
        return is_valid && errors.empty();
    }

    /**
     * @brief Get error count
     * @return Number of errors
     */
    size_t GetErrorCount() const {
        return errors.size();
    }

    /**
     * @brief Get warning count
     * @return Number of warnings
     */
    size_t GetWarningCount() const {
        return warnings.size();
    }
};

/**
 * @class ConfigValidator
 * @brief Comprehensive configuration validation for ACES
 */
class ConfigValidator {
 public:
    /**
     * @brief Validate ACES configuration
     * @param config YAML configuration node
     * @return Validation result with errors and warnings
     */
    static ValidationResult ValidateConfig(const YAML::Node& config);

 private:
    /**
     * @brief Validate species definitions
     */
    static void ValidateSpecies(const YAML::Node& config, ValidationResult& result);

    /**
     * @brief Validate layer configurations
     */
    static void ValidateLayers(const YAML::Node& config, ValidationResult& result);

    /**
     * @brief Validate vertical distribution settings
     */
    static void ValidateVerticalDistribution(const YAML::Node& config, ValidationResult& result);

    /**
     * @brief Validate CDEPS configuration
     */
    static void ValidateCDEPS(const YAML::Node& config, ValidationResult& result);

    /**
     * @brief Validate physics scheme configurations
     */
    static void ValidatePhysicsSchemes(const YAML::Node& config, ValidationResult& result);

    /**
     * @brief Validate output configuration
     */
    static void ValidateOutput(const YAML::Node& config, ValidationResult& result);

    /**
     * @brief Check if file exists
     */
    static bool FileExists(const std::string& path);

    /**
     * @brief Check if directory is writable
     */
    static bool DirectoryIsWritable(const std::string& path);
};

}  // namespace aces

#endif  // ACES_CONFIG_VALIDATOR_HPP
