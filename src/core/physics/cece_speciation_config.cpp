#include "cece/physics/cece_speciation_config.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include "conf/config.hpp"

namespace cece {

SpeciationConfig SpeciationConfigLoader::Load(const std::string& mechanism_path, const std::string& mapping_path, const std::string& dataset) const {
    if (!std::filesystem::exists(mechanism_path)) throw std::runtime_error("Mechanism file not found: " + mechanism_path);
    if (!std::filesystem::exists(mapping_path)) throw std::runtime_error("Mapping file not found: " + mapping_path);

    conf::Config mechanism = conf::Config::from_file(mechanism_path);
    conf::Config mapping = conf::Config::from_file(mapping_path);
    SpeciationConfig config = ParseMechanism(mechanism.root());
    ParseMapping(mapping.root(), config, dataset);
    Validate(config);
    return config;
}

SpeciationConfig SpeciationConfigLoader::ParseMechanism(const conf::Value& node) const {
    SpeciationConfig config;
    if (!node["name"]) throw std::invalid_argument("Mechanism file missing required 'name' key");
    config.mechanism_name = node["name"].as_string();

    conf::Value species = node["species"];
    if (!species || species.kind() != conf::Node_Kind::Sequence) throw std::invalid_argument("Mechanism file missing required 'species' list");
    for (std::size_t i = 0; i < species.size(); ++i) {
        conf::Value entry = species[i];
        if (!entry["name"]) throw std::invalid_argument("Mechanism species entry " + std::to_string(i) + " missing required 'name' field");
        if (!entry["molecular weight [kg mol-1]"]) {
            throw std::invalid_argument("Mechanism species entry " + std::to_string(i) + " missing required 'molecular weight [kg mol-1]' field");
        }
        MechanismSpecies sp{entry["name"].as_string(), entry["molecular weight [kg mol-1]"].as_double() * 1000.0};
        if (sp.molecular_weight <= 0.0) throw std::invalid_argument("Mechanism species '" + sp.name + "' has non-positive molecular weight");
        config.species.push_back(std::move(sp));
    }
    return config;
}

void SpeciationConfigLoader::ParseMapping(const conf::Value& node, SpeciationConfig& config, const std::string& dataset) const {
    if (!node["mechanism"]) throw std::invalid_argument("Mapping file missing required 'mechanism' key");
    conf::Value datasets = node["datasets"];
    if (!datasets || datasets.kind() != conf::Node_Kind::Map) throw std::invalid_argument("Mapping file missing required 'datasets' section");
    conf::Value selected = datasets[dataset];
    if (!selected) throw std::invalid_argument("Requested dataset '" + dataset + "' not found in mapping file");
    if (selected.kind() != conf::Node_Kind::Map) throw std::invalid_argument("Dataset '" + dataset + "' is not a map");
    config.dataset_name = dataset;

    for (const auto& mechanism_species : selected.keys()) {
        conf::Value class_map = selected[mechanism_species];
        if (class_map.kind() != conf::Node_Kind::Map) {
            throw std::invalid_argument("Mechanism species '" + mechanism_species + "' in dataset '" + dataset +
                                        "' is not a map of emission classes");
        }
        for (const auto& class_name : class_map.keys()) {
            EmissionClass emission_class;
            std::string normalized = class_name;
            if (normalized == "false" || normalized == "no") normalized = "NO";
            if (!StringToEmissionClass(normalized, emission_class)) {
                std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                if (!StringToEmissionClass(normalized, emission_class)) {
                    throw std::invalid_argument("Invalid emission class '" + class_name + "' for mechanism species '" + mechanism_species + "'");
                }
            }
            double scale_factor = class_map[class_name].as_double();
            if (scale_factor <= 0.0) throw std::invalid_argument("Non-positive scale factor for emission class '" + class_name + "'");
            config.mappings.push_back({mechanism_species, emission_class, scale_factor});
        }
    }
}

void SpeciationConfigLoader::Validate(const SpeciationConfig& config) const {
    std::unordered_set<std::string> species_names;
    for (const auto& species : config.species) species_names.insert(species.name);
    std::vector<std::string> unknown;
    for (const auto& mapping : config.mappings)
        if (!species_names.contains(mapping.mechanism_species)) unknown.push_back(mapping.mechanism_species);
    if (!unknown.empty()) {
        std::sort(unknown.begin(), unknown.end());
        unknown.erase(std::unique(unknown.begin(), unknown.end()), unknown.end());
        std::ostringstream message;
        message << "Mapping references unknown mechanism species not in mechanism file: ";
        for (std::size_t i = 0; i < unknown.size(); ++i) message << (i ? ", " : "") << "'" << unknown[i] << "'";
        throw std::invalid_argument(message.str());
    }
    for (const auto& mapping : config.mappings) {
        int index = static_cast<int>(mapping.emission_class);
        if (index < 0 || index >= static_cast<int>(EmissionClass::COUNT)) throw std::invalid_argument("Invalid emission class index");
    }
}

std::string SpeciationConfigLoader::ToYaml(const SpeciationConfig& config) {
    // Direct stream formatting avoids reintroducing yaml-cpp emitter dependency
    std::ostringstream out;
    out << "name: \"" << config.mechanism_name << "\"\nspecies:\n";
    for (const auto& species : config.species) {
        out << "  - name: \"" << species.name << "\"\n";
        out << "    molecular weight [kg mol-1]: " << species.molecular_weight / 1000.0 << "\n";
    }
    out << "mechanism: \"" << config.mechanism_name << "\"\ndatasets:\n  \"" << config.dataset_name << "\":\n";
    std::unordered_map<std::string, std::vector<const SpeciationMapping*>> grouped;
    for (const auto& mapping : config.mappings) grouped[mapping.mechanism_species].push_back(&mapping);
    for (const auto& [species, mappings] : grouped) {
        out << "    \"" << species << "\":\n";
        for (const auto* mapping : mappings)
            out << "      \"" << EmissionClassToString(mapping->emission_class) << "\": " << mapping->scale_factor << "\n";
    }
    return out.str();
}

}  // namespace cece
