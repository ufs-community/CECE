/**
 * @file cece_config_validator.cpp
 * @brief Implementation of configuration validation for CECE
 */

#include "cece/cece_config_validator.hpp"

#include <filesystem>
#include <set>
#include <sstream>

#include "cece/cece_config.hpp"
#include "cece/cece_logger.hpp"

namespace {

constexpr int kMinOutputStagingBufferCount = 1;
constexpr int kMaxOutputStagingBufferCount = 4096;
constexpr int kMinOutputStagingBufferCapacityBytes = 1;
constexpr int kMaxOutputStagingBufferCapacityBytes = 1073741824;
constexpr int kMinOutputStagingTimeoutMs = 1;
constexpr int kMaxOutputStagingTimeoutMs = 60000;

}  // namespace

namespace cece {

FireConfig ParseFireConfig(const conf::Value& options) {
    const auto invalid = [](const std::string& field, const std::string& message) {
        throw std::invalid_argument("fire.options." + field + ": " + message);
    };
    if (options.kind() != conf::Node_Kind::Map) {
        invalid("options", "must be a mapping");
    }
    const std::set<std::string> allowed = {"mode",
                                           "input_provider",
                                           "source_manifest",
                                           "input_manifest",
                                           "product_adapter",
                                           "product_time_allocation",
                                           "product_time_profile",
                                           "scientific_profile",
                                           "mechanism_file",
                                           "fire_mapping_file",
                                           "target_id",
                                           "target_version",
                                           "chemistry_state",
                                           "requested_species",
                                           "native_diagnostic_species",
                                           "missing_policy",
                                           "reconstruction_policy",
                                           "max_gap_seconds",
                                           "cutoff_mode",
                                           "availability_cutoff",
                                           "cutoff_schedule",
                                           "max_observation_age_seconds",
                                           "scaling",
                                           "input_mapping",
                                           "output_mapping"};
    for (const auto& key : options.keys()) {
        if (allowed.count(key) == 0) invalid(key, "unknown option");
    }
    const auto text = [&](const std::string& key, const std::string& fallback = "") {
        if (!options[key]) return fallback;
        const auto value = options[key].try_string();
        if (!value || value->find_first_not_of(" \t\r\n") == std::string::npos) invalid(key, "must be a nonempty string");
        return *value;
    };
    const auto required = [&](const std::string& key) {
        const auto value = text(key);
        if (value.empty()) invalid(key, "is required");
        return value;
    };
    const auto prohibit = [&](const std::string& key) {
        if (options[key]) invalid(key, "is prohibited for the selected mode/policy");
    };
    const auto positive = [&](const std::string& key) -> std::optional<int> {
        if (!options[key]) return std::nullopt;
        const auto value = options[key].try_int();
        if (!value || *value <= 0) invalid(key, "must be a positive integer no larger than 2147483647");
        return value;
    };
    const auto mode = required("mode");
    if (mode != "product" && mode != "derive_frp") invalid("mode", "must be product or derive_frp");
    const auto provider = required("input_provider");
    if (provider != "prepared" && provider != "coupled") invalid("input_provider", "must be prepared or coupled");
    const auto cutoff = required("cutoff_mode");
    if (cutoff != "fixed" && cutoff != "host_supplied") invalid("cutoff_mode", "must be fixed or host_supplied");
    FireConfig result(mode == "product" ? FireInputMode::Product : FireInputMode::DeriveFrp,
                      provider == "prepared" ? FireInputProvider::Prepared : FireInputProvider::Coupled,
                      cutoff == "fixed" ? FireCutoffMode::Fixed : FireCutoffMode::HostSupplied);
    result.source_manifest = text("source_manifest");
    if (result.input_provider == FireInputProvider::Prepared) {
        result.input_manifest = required("input_manifest");
    } else {
        prohibit("input_manifest");
    }
    if (!result.source_manifest.empty() && !result.input_manifest.empty() &&
        std::filesystem::weakly_canonical(result.source_manifest) == std::filesystem::weakly_canonical(result.input_manifest)) {
        invalid("source_manifest", "must be distinct from input_manifest");
    }
    const auto chemistry = text("chemistry_state", "bulk");
    if (chemistry != "bulk" && chemistry != "target_specific") invalid("chemistry_state", "must be bulk or target_specific");
    result.chemistry_state = chemistry == "bulk" ? FireChemistryState::Bulk : FireChemistryState::TargetSpecific;
    if (result.mode == FireInputMode::Product) {
        result.product_adapter = required("product_adapter");
        const auto allocation = required("product_time_allocation");
        if (allocation == "uniform")
            result.product_time_allocation = FireTimeAllocation::Uniform;
        else if (allocation == "source_resolved")
            result.product_time_allocation = FireTimeAllocation::SourceResolved;
        else if (allocation == "profile")
            result.product_time_allocation = FireTimeAllocation::Profile;
        else
            invalid("product_time_allocation", "must be uniform, source_resolved or profile");
        if (*result.product_time_allocation == FireTimeAllocation::Profile)
            result.product_time_profile = required("product_time_profile");
        else
            prohibit("product_time_profile");
        prohibit("scientific_profile");
    } else {
        for (const auto* key : {"product_adapter", "product_time_allocation", "product_time_profile"}) prohibit(key);
        if (result.chemistry_state != FireChemistryState::Bulk) invalid("chemistry_state", "derive_frp requires bulk chemistry");
        result.scientific_profile = required("scientific_profile");
    }
    result.mechanism_file = required("mechanism_file");
    if (result.chemistry_state == FireChemistryState::Bulk)
        result.fire_mapping_file = required("fire_mapping_file");
    else
        prohibit("fire_mapping_file");
    result.target_id = required("target_id");
    if (std::set<std::string>{"AM4", "CB6", "CRACMM2", "RACM2", "GOCART2G"}.count(result.target_id) == 0) {
        invalid("target_id", "must be AM4, CB6, CRACMM2, RACM2 or GOCART2G");
    }
    result.target_version = required("target_version");
    if (options["requested_species"]) {
        if (options["requested_species"].kind() != conf::Node_Kind::Sequence || options["requested_species"].size() == 0) {
            invalid("requested_species", "must be a nonempty list of canonical bulk species");
        }
        std::set<FireBulkSpecies> unique;
        const auto species = options["requested_species"];
        for (std::size_t i = 0; i < species.size(); ++i) {
            const auto name = species[i].try_string();
            if (!name) invalid("requested_species", "entries must be canonical bulk species strings");
            FireBulkSpecies id;
            try {
                id = FireSpeciesFromName(*name);
            } catch (const std::invalid_argument& error) {
                throw std::invalid_argument("fire.options.requested_species: " + std::string(error.what()));
            }
            if (!unique.insert(id).second) invalid("requested_species", "duplicate species '" + *name + "'");
            result.requested_species.push_back(id);
        }
    } else {
        for (const auto* name : {"CO2", "CO", "CH4", "PM25", "TPM", "SO2", "OC", "BC", "NOX", "NH3", "VOC"}) {
            result.requested_species.push_back(FireSpeciesFromName(name));
        }
    }
    if (options["native_diagnostic_species"]) {
        if (result.mode != FireInputMode::Product || result.input_provider != FireInputProvider::Prepared ||
            result.chemistry_state != FireChemistryState::Bulk) {
            invalid("native_diagnostic_species", "requires prepared bulk-product mode");
        }
        const auto diagnostics = options["native_diagnostic_species"];
        if (diagnostics.kind() != conf::Node_Kind::Sequence || diagnostics.size() == 0) {
            invalid("native_diagnostic_species", "must be a nonempty list");
        }
        std::set<FireBulkSpecies> unique(result.requested_species.begin(), result.requested_species.end());
        for (std::size_t i = 0; i < diagnostics.size(); ++i) {
            const auto name = diagnostics[i].try_string();
            if (!name || (*name != "PM25" && *name != "TPM" && *name != "NOX" && *name != "VOC")) {
                invalid("native_diagnostic_species", "entries must be PM25, TPM, NOX or VOC");
            }
            const auto species = FireSpeciesFromName(*name);
            if (!unique.insert(species).second) invalid("native_diagnostic_species", "must be unique and separate from requested_species");
            result.native_diagnostic_species.push_back(species);
        }
    }
    result.missing_policy = text("missing_policy", "error");
    if (result.missing_policy != "error" && result.missing_policy != "reconstruct") invalid("missing_policy", "must be error or reconstruct");
    if (result.missing_policy == "reconstruct") {
        result.reconstruction_policy = required("reconstruction_policy");
        result.max_gap_seconds = positive("max_gap_seconds");
        if (!result.max_gap_seconds) invalid("max_gap_seconds", "is required for reconstruction");
    } else {
        prohibit("reconstruction_policy");
        prohibit("max_gap_seconds");
    }
    result.max_observation_age_seconds = positive("max_observation_age_seconds");
    if ((result.mode == FireInputMode::DeriveFrp || result.missing_policy == "reconstruct") && !result.max_observation_age_seconds) {
        invalid("max_observation_age_seconds", "is required for derivation/reconstruction");
    }
    if (cutoff == "fixed") {
        const auto timestamp = required("availability_cutoff");
        try {
            result.availability_cutoff.emplace(timestamp);
        } catch (const std::invalid_argument& error) {
            invalid("availability_cutoff", error.what());
        }
        prohibit("cutoff_schedule");
    } else {
        prohibit("availability_cutoff");
        result.cutoff_schedule = result.input_provider == FireInputProvider::Prepared ? required("cutoff_schedule") : text("cutoff_schedule");
    }
    result.scaling = text("scaling", "none");
    for (const auto& [key, mapping] : {std::pair{"input_mapping", &result.input_mapping}, std::pair{"output_mapping", &result.output_mapping}}) {
        const auto node = options[key];
        if (!node) continue;
        if (node.kind() != conf::Node_Kind::Map) invalid(key, "must map nonempty strings to nonempty strings");
        std::set<std::string> outputs;
        for (const auto& name : node.keys()) {
            const auto value = node[name].try_string();
            if (name.find_first_not_of(" \t\r\n") == std::string::npos || !value || value->find_first_not_of(" \t\r\n") == std::string::npos) {
                invalid(key, "must map nonempty strings to nonempty strings");
            }
            if (std::string_view(key) == "output_mapping" && !outputs.insert(*value).second) invalid(key, "output names must be unique");
            mapping->emplace(name, *value);
        }
    }
    return result;
}

ValidationResult ConfigValidator::ValidateConfig(const conf::Value& config) {
    ValidationResult result;

    // Validate species definitions
    ValidateSpecies(config, result);

    // Validate layer configurations
    ValidateLayers(config, result);

    // Validate vertical distribution settings
    ValidateVerticalDistribution(config, result);

    // Validate data stream configuration
    ValidateDataStreams(config, result);

    // Validate physics scheme configurations
    ValidatePhysicsSchemes(config, result);

    // Validate output configuration
    ValidateOutput(config, result);

    result.is_valid = result.errors.empty();
    return result;
}

void ConfigValidator::ValidateSpecies(const conf::Value& config, ValidationResult& result) {
    if (!config["species"]) {
        result.errors.push_back({"species", "No species defined in configuration", "Add a 'species' section with at least one emission species"});
        return;
    }

    const auto& species_node = config["species"];
    if (species_node.kind() != conf::Node_Kind::Sequence) {
        result.errors.push_back({"species", "Species must be a list", "Change 'species' to a YAML list (use '-' for each item)"});
        return;
    }

    if (species_node.size() == 0) {
        result.errors.push_back({"species", "Species list is empty", "Add at least one species to the 'species' list"});
        return;
    }

    for (size_t i = 0; i < species_node.size(); ++i) {
        const auto& species = species_node[i];

        if (!species["name"]) {
            result.errors.push_back({"species[" + std::to_string(i) + "].name", "Species name is missing", "Add a 'name' field to each species"});
        }

        if (!species["units"]) {
            result.warnings.push_back("species[" + std::to_string(i) + "].units: Units not specified");
        }
    }
}

void ConfigValidator::ValidateLayers(const conf::Value& config, ValidationResult& result) {
    if (!config["layers"]) {
        result.warnings.push_back("No layers defined in configuration");
        return;
    }

    const auto& layers_node = config["layers"];
    if (layers_node.kind() != conf::Node_Kind::Sequence) {
        result.errors.push_back({"layers", "Layers must be a list", "Change 'layers' to a YAML list (use '-' for each item)"});
        return;
    }

    for (size_t i = 0; i < layers_node.size(); ++i) {
        const auto& layer = layers_node[i];

        if (!layer["name"]) {
            result.errors.push_back({"layers[" + std::to_string(i) + "].name", "Layer name is missing", "Add a 'name' field to each layer"});
        }

        if (!layer["species"]) {
            result.errors.push_back({"layers[" + std::to_string(i) + "].species", "Layer species is missing",
                                     "Add a 'species' field specifying which species this layer contributes to"});
        }

        if (!layer["file"]) {
            result.errors.push_back({"layers[" + std::to_string(i) + "].file", "Layer file path is missing",
                                     "Add a 'file' field with the path to the emission data file"});
        } else {
            std::string file_path = layer["file"].as_string();
            if (!FileExists(file_path)) {
                result.errors.push_back({"layers[" + std::to_string(i) + "].file", "File not found: " + file_path,
                                         "Verify the file path is correct and the file exists"});
            }
        }

        if (!layer["variable"]) {
            result.errors.push_back({"layers[" + std::to_string(i) + "].variable", "Variable name is missing",
                                     "Add a 'variable' field specifying the NetCDF variable name"});
        }

        if (!layer["operation"]) {
            result.warnings.push_back("layers[" + std::to_string(i) + "].operation: Operation not specified, defaulting to 'add'");
        } else {
            std::string op = layer["operation"].as_string();
            if (op != "add" && op != "replace") {
                result.errors.push_back(
                    {"layers[" + std::to_string(i) + "].operation", "Invalid operation: " + op, "Operation must be either 'add' or 'replace'"});
            }
        }
    }
}

void ConfigValidator::ValidateVerticalDistribution(const conf::Value& config, ValidationResult& result) {
    if (!config["layers"]) {
        return;
    }

    const auto& layers_node = config["layers"];
    for (size_t i = 0; i < layers_node.size(); ++i) {
        const auto& layer = layers_node[i];

        if (!layer["vertical_distribution"]) {
            result.warnings.push_back("layers[" + std::to_string(i) + "].vertical_distribution: Not specified, defaulting to SINGLE");
            continue;
        }

        const auto& vdist = layer["vertical_distribution"];

        if (!vdist["method"]) {
            result.errors.push_back({"layers[" + std::to_string(i) + "].vertical_distribution.method", "Vertical distribution method is missing",
                                     "Add a 'method' field: SINGLE, RANGE, PRESSURE, HEIGHT, or PBL"});
        } else {
            std::string method = vdist["method"].as_string();
            if (method != "SINGLE" && method != "RANGE" && method != "PRESSURE" && method != "HEIGHT" && method != "PBL") {
                result.errors.push_back({"layers[" + std::to_string(i) + "].vertical_distribution.method",
                                         "Invalid vertical distribution method: " + method,
                                         "Method must be one of: SINGLE, RANGE, PRESSURE, HEIGHT, PBL"});
            }
        }
    }
}

void ConfigValidator::ValidateDataStreams(const conf::Value& config, ValidationResult& result) {
    if (!config["cece_data"]) {
        return;
    }

    const auto& cece_data = config["cece_data"];

    if (cece_data["streams_yaml"]) {
        std::string streams_file = cece_data["streams_yaml"].as_string();
        if (!FileExists(streams_file)) {
            result.errors.push_back(
                {"cece_data.streams_yaml", "Streams file not found: " + streams_file, "Verify the streams file path is correct and the file exists"});
        }
    }

    if (cece_data["data_root"]) {
        std::string data_root = cece_data["data_root"].as_string();
        if (!std::filesystem::exists(data_root)) {
            result.errors.push_back({"cece_data.data_root", "Data root directory not found: " + data_root,
                                     "Verify the data root path is correct and the directory exists"});
        }
    }
}

void ConfigValidator::ValidatePhysicsSchemes(const conf::Value& config, ValidationResult& result) {
    if (!config["physics_schemes"]) {
        result.warnings.push_back("No physics schemes configured");
        return;
    }

    const auto& schemes_node = config["physics_schemes"];
    if (schemes_node.kind() != conf::Node_Kind::Sequence) {
        result.errors.push_back(
            {"physics_schemes", "Physics schemes must be a list", "Change 'physics_schemes' to a YAML list (use '-' for each item)"});
        return;
    }

    bool has_fire = false;
    for (size_t i = 0; i < schemes_node.size(); ++i) {
        const auto& scheme = schemes_node[i];

        if (!scheme["name"]) {
            result.errors.push_back(
                {"physics_schemes[" + std::to_string(i) + "].name", "Scheme name is missing", "Add a 'name' field to each physics scheme"});
        }

        if (scheme["enabled"] && !scheme["enabled"].as_bool()) {
            result.warnings.push_back("physics_schemes[" + std::to_string(i) + "]: Scheme is disabled");
        }
        if (scheme["name"].string_or("") == "fire") {
            const auto field = "physics_schemes[" + std::to_string(i) + "].options";
            try {
                if (has_fire) throw std::invalid_argument("duplicate fire contributions may overlap; configure one fire contribution");
                has_fire = true;
                (void)ParseFireConfig(scheme["options"]);
            } catch (const std::invalid_argument& error) {
                result.errors.push_back({field, error.what(), "Check fire mode, provider, assets and time/cutoff policy options"});
            }
        }
    }
}

void ConfigValidator::ValidateOutput(const conf::Value& config, ValidationResult& result) {
    if (!config["output"]) {
        return;
    }

    const auto& output = config["output"];

    if (output["directory"]) {
        std::string dir = output["directory"].as_string();
        if (!DirectoryIsWritable(dir)) {
            result.warnings.push_back("output.directory: Directory not writable or does not exist: " + dir);
        }
    }

    if (output["frequency_steps"]) {
        int freq = output["frequency_steps"].as_int();
        if (freq <= 0) {
            result.errors.push_back({"output.frequency_steps", "Output frequency must be positive", "Set frequency_steps to a positive integer"});
        }
    }

    if (output["amio_staging_buffer_count"]) {
        int count = output["amio_staging_buffer_count"].as_int();
        if (count < kMinOutputStagingBufferCount || count > kMaxOutputStagingBufferCount) {
            result.errors.push_back({"output.amio_staging_buffer_count", "Output staging buffer count must be in [1, 4096]",
                                     "Set amio_staging_buffer_count between 1 and 4096"});
        }
    }

    if (output["amio_staging_buffer_capacity_bytes"]) {
        int capacity = output["amio_staging_buffer_capacity_bytes"].as_int();
        if (capacity < kMinOutputStagingBufferCapacityBytes || capacity > kMaxOutputStagingBufferCapacityBytes) {
            result.errors.push_back({"output.amio_staging_buffer_capacity_bytes", "Output staging buffer capacity must be in [1, 1073741824]",
                                     "Set capacity to at least the largest output field and no more than 1 GiB"});
        }
    }

    if (output["amio_staging_timeout_ms"]) {
        int timeout = output["amio_staging_timeout_ms"].as_int();
        if (timeout < kMinOutputStagingTimeoutMs || timeout > kMaxOutputStagingTimeoutMs) {
            result.errors.push_back({"output.amio_staging_timeout_ms", "Output staging timeout must be in [1, 60000] milliseconds",
                                     "Set amio_staging_timeout_ms between 1 and 60000"});
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

}  // namespace cece
