/**
 * @file aces_config_validator.cpp
 * @brief Implementation of configuration validation for ACES
 */

#include "aces/aces_config_validator.hpp"

#include <filesystem>
#include <sstream>

#include "aces/aces_logger.hpp"

namespace aces {

ValidationResult ConfigValidator::ValidateConfig(const YAML::Node& config) {
    ValidationResult result;

    // Validate species definitions
    ValidateSpecies(config, result);

    // Validate layer configurations
    ValidateLayers(config, result);

    // Validate vertical distribution settings
    ValidateVerticalDistribution(config, result);

    // Validate TIDE configuration
    ValidateTIDE(config, result);

    // Validate physics scheme configurations
    ValidatePhysicsSchemes(config, result);

    // Validate output configuration
    ValidateOutput(config, result);

    result.is_valid = result.errors.empty();
    return result;
}

void ConfigValidator::ValidateSpecies(const YAML::Node& config, ValidationResult& result) {
    if (!config["species"]) {
        result.errors.push_back({"species", "No species defined in configuration",
                                 "Add a 'species' section with at least one emission species"});
        return;
    }

    const auto& species_node = config["species"];
    if (!species_node.IsSequence()) {
        result.errors.push_back({"species", "Species must be a list",
                                 "Change 'species' to a YAML list (use '-' for each item)"});
        return;
    }

    if (species_node.size() == 0) {
        result.errors.push_back(
            {"species", "Species list is empty", "Add at least one species to the 'species' list"});
        return;
    }

    for (size_t i = 0; i < species_node.size(); ++i) {
        const auto& species = species_node[i];

        if (!species["name"]) {
            result.errors.push_back({"species[" + std::to_string(i) + "].name",
                                     "Species name is missing",
                                     "Add a 'name' field to each species"});
        }

        if (!species["units"]) {
            result.warnings.push_back("species[" + std::to_string(i) +
                                      "].units: Units not specified");
        }
    }
}

void ConfigValidator::ValidateLayers(const YAML::Node& config, ValidationResult& result) {
    if (!config["layers"]) {
        result.warnings.push_back("No layers defined in configuration");
        return;
    }

    const auto& layers_node = config["layers"];
    if (!layers_node.IsSequence()) {
        result.errors.push_back({"layers", "Layers must be a list",
                                 "Change 'layers' to a YAML list (use '-' for each item)"});
        return;
    }

    for (size_t i = 0; i < layers_node.size(); ++i) {
        const auto& layer = layers_node[i];

        if (!layer["name"]) {
            result.errors.push_back({"layers[" + std::to_string(i) + "].name",
                                     "Layer name is missing", "Add a 'name' field to each layer"});
        }

        if (!layer["species"]) {
            result.errors.push_back(
                {"layers[" + std::to_string(i) + "].species", "Layer species is missing",
                 "Add a 'species' field specifying which species this layer contributes to"});
        }

        if (!layer["file"]) {
            result.errors.push_back({"layers[" + std::to_string(i) + "].file",
                                     "Layer file path is missing",
                                     "Add a 'file' field with the path to the emission data file"});
        } else {
            std::string file_path = layer["file"].as<std::string>();
            if (!FileExists(file_path)) {
                result.errors.push_back({"layers[" + std::to_string(i) + "].file",
                                         "File not found: " + file_path,
                                         "Verify the file path is correct and the file exists"});
            }
        }

        if (!layer["variable"]) {
            result.errors.push_back({"layers[" + std::to_string(i) + "].variable",
                                     "Variable name is missing",
                                     "Add a 'variable' field specifying the NetCDF variable name"});
        }

        if (!layer["operation"]) {
            result.warnings.push_back("layers[" + std::to_string(i) +
                                      "].operation: Operation not specified, defaulting to 'add'");
        } else {
            std::string op = layer["operation"].as<std::string>();
            if (op != "add" && op != "replace") {
                result.errors.push_back({"layers[" + std::to_string(i) + "].operation",
                                         "Invalid operation: " + op,
                                         "Operation must be either 'add' or 'replace'"});
            }
        }
    }
}

void ConfigValidator::ValidateVerticalDistribution(const YAML::Node& config,
                                                   ValidationResult& result) {
    if (!config["layers"]) {
        return;
    }

    const auto& layers_node = config["layers"];
    for (size_t i = 0; i < layers_node.size(); ++i) {
        const auto& layer = layers_node[i];

        if (!layer["vertical_distribution"]) {
            result.warnings.push_back(
                "layers[" + std::to_string(i) +
                "].vertical_distribution: Not specified, defaulting to SINGLE");
            continue;
        }

        const auto& vdist = layer["vertical_distribution"];

        if (!vdist["method"]) {
            result.errors.push_back(
                {"layers[" + std::to_string(i) + "].vertical_distribution.method",
                 "Vertical distribution method is missing",
                 "Add a 'method' field: SINGLE, RANGE, PRESSURE, HEIGHT, or PBL"});
        } else {
            std::string method = vdist["method"].as<std::string>();
            if (method != "SINGLE" && method != "RANGE" && method != "PRESSURE" &&
                method != "HEIGHT" && method != "PBL") {
                result.errors.push_back(
                    {"layers[" + std::to_string(i) + "].vertical_distribution.method",
                     "Invalid vertical distribution method: " + method,
                     "Method must be one of: SINGLE, RANGE, PRESSURE, HEIGHT, PBL"});
            }
        }
    }
}

void ConfigValidator::ValidateTIDE(const YAML::Node& config, ValidationResult& result) {
    if (!config["aces_data"]) {
        return;
    }

    const auto& aces_data = config["aces_data"];

    if (aces_data["streams_yaml"]) {
        std::string streams_file = aces_data["streams_yaml"].as<std::string>();
        if (!FileExists(streams_file)) {
            result.errors.push_back(
                {"aces_data.streams_yaml", "Streams file not found: " + streams_file,
                 "Verify the streams file path is correct and the file exists"});
        }
    }

    if (aces_data["data_root"]) {
        std::string data_root = aces_data["data_root"].as<std::string>();
        if (!std::filesystem::exists(data_root)) {
            result.errors.push_back(
                {"aces_data.data_root", "Data root directory not found: " + data_root,
                 "Verify the data root path is correct and the directory exists"});
        }
    }
}

void ConfigValidator::ValidatePhysicsSchemes(const YAML::Node& config, ValidationResult& result) {
    if (!config["physics_schemes"]) {
        result.warnings.push_back("No physics schemes configured");
        return;
    }

    const auto& schemes_node = config["physics_schemes"];
    if (!schemes_node.IsSequence()) {
        result.errors.push_back(
            {"physics_schemes", "Physics schemes must be a list",
             "Change 'physics_schemes' to a YAML list (use '-' for each item)"});
        return;
    }

    for (size_t i = 0; i < schemes_node.size(); ++i) {
        const auto& scheme = schemes_node[i];

        if (!scheme["name"]) {
            result.errors.push_back({"physics_schemes[" + std::to_string(i) + "].name",
                                     "Scheme name is missing",
                                     "Add a 'name' field to each physics scheme"});
        }

        if (scheme["enabled"] && !scheme["enabled"].as<bool>()) {
            result.warnings.push_back("physics_schemes[" + std::to_string(i) +
                                      "]: Scheme is disabled");
        }
    }
}

void ConfigValidator::ValidateOutput(const YAML::Node& config, ValidationResult& result) {
    if (!config["output"]) {
        return;
    }

    const auto& output = config["output"];

    if (output["directory"]) {
        std::string dir = output["directory"].as<std::string>();
        if (!DirectoryIsWritable(dir)) {
            result.warnings.push_back(
                "output.directory: Directory not writable or does not exist: " + dir);
        }
    }

    if (output["frequency_steps"]) {
        int freq = output["frequency_steps"].as<int>();
        if (freq <= 0) {
            result.errors.push_back({"output.frequency_steps", "Output frequency must be positive",
                                     "Set frequency_steps to a positive integer"});
        }
    }
}

bool ConfigValidator::FileExists(const std::string& path) {
    return std::filesystem::exists(path) && std::filesystem::is_regular_file(path);
}

bool ConfigValidator::DirectoryIsWritable(const std::string& path) {
    if (!std::filesystem::exists(path)) {
        return true;  // Directory doesn't exist yet, can be created
    }
    return std::filesystem::is_directory(path);
}

}  // namespace aces
