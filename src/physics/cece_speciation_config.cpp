/**
 * @file cece_speciation_config.cpp
 * @brief Implementation of the two-tier YAML-based speciation configuration loader.
 *
 * Two-tier design:
 *   1. Mechanism species (names + molecular weights) from MICM/OpenAtmos YAML files
 *   2. Emission speciation mappings from CECE dataset-oriented YAML mapping files
 *
 * The MAP file uses a nested dataset-oriented format where each dataset maps
 * mechanism species to their contributing emission classes with per-class scale factors.
 * This replaces the previous 201-speciated-species intermediate step.
 *
 * Error handling:
 * - std::runtime_error for file-not-found
 * - YAML::ParserException for invalid YAML syntax (propagated from yaml-cpp)
 * - std::invalid_argument for schema and cross-reference validation failures
 *
 * @author CECE Team
 * @date 2024
 */

#include "cece/physics/cece_speciation_config.hpp"

#include <sys/stat.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace cece {

SpeciationConfig SpeciationConfigLoader::Load(const std::string& mechanism_path,
                                              const std::string& mapping_path,
                                              const std::string& dataset) {
    // Check mechanism file existence
    struct stat buffer;
    if (stat(mechanism_path.c_str(), &buffer) != 0) {
        throw std::runtime_error("Mechanism file not found: " + mechanism_path);
    }

    // Check mapping file existence
    if (stat(mapping_path.c_str(), &buffer) != 0) {
        throw std::runtime_error("Mapping file not found: " + mapping_path);
    }

    // Parse YAML files (YAML::ParserException propagates on bad syntax)
    YAML::Node mechanism_node = YAML::LoadFile(mechanism_path);
    YAML::Node mapping_node = YAML::LoadFile(mapping_path);

    // Parse mechanism, then mapping, then validate cross-references
    SpeciationConfig config = ParseMechanism(mechanism_node);
    ParseMapping(mapping_node, config, dataset);
    Validate(config);

    return config;
}

SpeciationConfig SpeciationConfigLoader::ParseMechanism(const YAML::Node& node) {
    SpeciationConfig config;

    // MICM format uses 'name' as the top-level mechanism identifier
    if (!node["name"]) {
        throw std::invalid_argument("Mechanism file missing required 'name' key");
    }
    config.mechanism_name = node["name"].as<std::string>();

    // Validate 'species' list
    if (!node["species"] || !node["species"].IsSequence()) {
        throw std::invalid_argument("Mechanism file missing required 'species' list");
    }

    for (std::size_t i = 0; i < node["species"].size(); ++i) {
        const auto& entry = node["species"][i];

        if (!entry["name"]) {
            throw std::invalid_argument(
                "Mechanism species entry " + std::to_string(i) + " missing required 'name' field");
        }

        // MICM format uses 'molecular weight [kg mol-1]'
        if (!entry["molecular weight [kg mol-1]"]) {
            throw std::invalid_argument(
                "Mechanism species entry " + std::to_string(i) +
                " missing required 'molecular weight [kg mol-1]' field");
        }

        MechanismSpecies sp;
        sp.name = entry["name"].as<std::string>();

        // Convert from kg/mol (MICM convention) to g/mol
        double mw_kg_per_mol = entry["molecular weight [kg mol-1]"].as<double>();
        if (mw_kg_per_mol <= 0.0) {
            throw std::invalid_argument(
                "Mechanism species '" + sp.name +
                "' has non-positive molecular weight: " + std::to_string(mw_kg_per_mol));
        }
        sp.molecular_weight = mw_kg_per_mol * 1000.0;  // kg/mol -> g/mol

        config.species.push_back(sp);
    }

    return config;
}

void SpeciationConfigLoader::ParseMapping(const YAML::Node& node, SpeciationConfig& config,
                                          const std::string& dataset) {
    // Validate 'mechanism' key — use iterator to avoid operator[] side effects
    bool has_mechanism = false;
    bool has_datasets = false;
    YAML::Node datasets_node;

    for (auto it = node.begin(); it != node.end(); ++it) {
        std::string key;
        try { key = it->first.as<std::string>(); } catch (...) { key = it->first.Scalar(); }

        if (key == "mechanism") {
            has_mechanism = true;
        } else if (key == "datasets") {
            has_datasets = true;
            datasets_node = YAML::Clone(it->second);
        }
    }

    if (!has_mechanism) {
        throw std::invalid_argument("Mapping file missing required 'mechanism' key");
    }
    if (!has_datasets || !datasets_node.IsMap()) {
        throw std::invalid_argument("Mapping file missing required 'datasets' section");
    }

    // Find the requested dataset
    YAML::Node dataset_node;
    bool found_dataset = false;
    for (auto ds_it = datasets_node.begin(); ds_it != datasets_node.end(); ++ds_it) {
        std::string ds_name;
        try { ds_name = ds_it->first.as<std::string>(); } catch (...) { ds_name = ds_it->first.Scalar(); }
        if (ds_name == dataset) {
            dataset_node = YAML::Clone(ds_it->second);
            found_dataset = true;
            break;
        }
    }

    if (!found_dataset) {
        throw std::invalid_argument("Requested dataset '" + dataset + "' not found in mapping file");
    }

    config.dataset_name = dataset;

    if (!dataset_node.IsMap()) {
        throw std::invalid_argument("Dataset '" + dataset + "' is not a map");
    }

    // Iterate mechanism species entries in the dataset
    for (auto mech_it = dataset_node.begin(); mech_it != dataset_node.end(); ++mech_it) {
        if (mech_it->first.Type() == YAML::NodeType::Null) continue;

        std::string mechanism_species;
        try { mechanism_species = mech_it->first.as<std::string>(); }
        catch (...) { mechanism_species = mech_it->first.Scalar(); }

        YAML::Node class_map = YAML::Clone(mech_it->second);

        if (!class_map.IsMap()) {
            throw std::invalid_argument(
                "Mechanism species '" + mechanism_species + "' in dataset '" + dataset +
                "' is not a map of emission classes");
        }

        // Iterate emission class → scale factor pairs
        for (auto class_it = class_map.begin(); class_it != class_map.end(); ++class_it) {
            const auto& key_node = class_it->first;

            // Skip null nodes
            if (key_node.Type() == YAML::NodeType::Null || !key_node.IsDefined()) {
                continue;
            }

            std::string class_name = key_node.Scalar();
            if (class_name.empty()) {
                try { class_name = key_node.as<std::string>(); } catch (...) {}
            }

            // Handle yaml-cpp YAML 1.1 boolean interpretation of "NO"
            if (class_name == "false" || class_name == "no") {
                class_name = "NO";
            }

            EmissionClass ec;
            if (!StringToEmissionClass(class_name, ec)) {
                std::string upper_name = class_name;
                std::transform(upper_name.begin(), upper_name.end(), upper_name.begin(), ::toupper);
                if (!StringToEmissionClass(upper_name, ec)) {
                    throw std::invalid_argument(
                        "Invalid emission class '" + class_name + "' for mechanism species '" +
                        mechanism_species + "' in dataset '" + dataset + "'");
                }
            }

            double scale_factor = class_it->second.as<double>();

            if (scale_factor <= 0.0) {
                throw std::invalid_argument(
                    "Non-positive scale factor " + std::to_string(scale_factor) +
                    " for emission class '" + class_name + "' → mechanism species '" +
                    mechanism_species + "' in dataset '" + dataset + "'");
            }

            SpeciationMapping mapping;
            mapping.mechanism_species = mechanism_species;
            mapping.emission_class = ec;
            mapping.scale_factor = scale_factor;
            config.mappings.push_back(mapping);
        }
    }
}

void SpeciationConfigLoader::Validate(const SpeciationConfig& config) {
    // Build set of mechanism species names for lookup
    std::unordered_set<std::string> mechanism_species_names;
    for (const auto& sp : config.species) {
        mechanism_species_names.insert(sp.name);
    }

    // Check all mapping mechanism species exist in mechanism species list
    std::vector<std::string> unknown_species;
    for (const auto& mapping : config.mappings) {
        if (mechanism_species_names.find(mapping.mechanism_species) == mechanism_species_names.end()) {
            unknown_species.push_back(mapping.mechanism_species);
        }
    }

    if (!unknown_species.empty()) {
        std::sort(unknown_species.begin(), unknown_species.end());
        unknown_species.erase(std::unique(unknown_species.begin(), unknown_species.end()),
                              unknown_species.end());
        std::ostringstream oss;
        oss << "Mapping references unknown mechanism species not in mechanism file: ";
        for (std::size_t i = 0; i < unknown_species.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << "'" << unknown_species[i] << "'";
        }
        throw std::invalid_argument(oss.str());
    }

    // Emission class validation is already done during ParseMapping (StringToEmissionClass),
    // but we double-check here for configs built programmatically
    for (const auto& mapping : config.mappings) {
        int ec_idx = static_cast<int>(mapping.emission_class);
        if (ec_idx < 0 || ec_idx >= static_cast<int>(EmissionClass::COUNT)) {
            throw std::invalid_argument(
                "Invalid emission class index " + std::to_string(ec_idx) +
                " in mapping for mechanism species '" + mapping.mechanism_species + "'");
        }
    }
}

std::string SpeciationConfigLoader::ToYaml(const SpeciationConfig& config) {
    YAML::Emitter out;

    // Emit mechanism section (MICM format)
    out << YAML::BeginMap;
    out << YAML::Key << "name" << YAML::Value << config.mechanism_name;
    out << YAML::Key << "species" << YAML::Value << YAML::BeginSeq;
    for (const auto& sp : config.species) {
        out << YAML::BeginMap;
        out << YAML::Key << "name" << YAML::Value << sp.name;
        // Convert g/mol back to kg/mol for MICM format
        out << YAML::Key << "molecular weight [kg mol-1]" << YAML::Value << (sp.molecular_weight / 1000.0);
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;

    // Emit mapping section (dataset-oriented format)
    out << YAML::Key << "mechanism" << YAML::Value << config.mechanism_name;
    out << YAML::Key << "datasets" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << config.dataset_name << YAML::Value << YAML::BeginMap;

    // Group mappings by mechanism species
    std::unordered_map<std::string, std::vector<const SpeciationMapping*>> grouped;
    for (const auto& m : config.mappings) {
        grouped[m.mechanism_species].push_back(&m);
    }

    for (const auto& [mech_sp, mapping_ptrs] : grouped) {
        // Quote mechanism species names to prevent yaml-cpp from interpreting
        // "NO" as boolean false (YAML 1.1 compatibility issue)
        out << YAML::Key << YAML::DoubleQuoted << mech_sp << YAML::Value << YAML::BeginMap;
        for (const auto* mp : mapping_ptrs) {
            // Quote emission class names too
            out << YAML::Key << YAML::DoubleQuoted
                << EmissionClassToString(mp->emission_class)
                << YAML::Value << mp->scale_factor;
        }
        out << YAML::EndMap;
    }

    out << YAML::EndMap;  // end dataset
    out << YAML::EndMap;  // end datasets
    out << YAML::EndMap;  // end root

    return out.c_str();
}

}  // namespace cece
