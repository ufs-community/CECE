/**
 * @file test_speciation.cpp
 * @brief Property-based tests for the speciation configuration loader and engine.
 *
 * Properties tested:
 * 1. Speciation Configuration Round-Trip (Requirements 5.1, 5.2, 5.7, 7.6)
 * 2. Speciation Config Cross-Reference Validation (Requirements 5.5, 5.6)
 * 3. Speciation YAML Schema Enforcement (Requirements 7.2, 7.4)
 * 10. Speciation Pipeline Correctness (Requirements 6.1, 6.2, 6.3)
 * 17. Speciation Accumulation Determinism (Requirements 10.2)
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "cece/cece_state.hpp"
#include "cece/physics/cece_speciation_config.hpp"
#include "cece/physics/cece_speciation_engine.hpp"

namespace cece {

// ============================================================================
// Helpers
// ============================================================================

/// Valid emission class names for generating valid configs.
/// Note: "NO" is excluded because yaml-cpp YAML 1.1 interprets unquoted "NO"
/// as boolean false, causing parsing issues even when quoted in some versions.
/// The "NO" class is tested separately in the real data file tests.
static const std::vector<std::string> kValidEmissionClasses = {"ISOP",    "MBO",    "MT_PINE", "MT_ACYC", "MT_CAMP", "MT_SABI",
                                                               "MT_AROM", "SQT_HR", "SQT_LR",  "MEOH",    "ACTO",    "ETOH",
                                                               "ACID",    "LVOC",   "OXPROD",  "STRESS",  "OTHER",   "CO"};

/// Generate a random alphanumeric string of given length.
static std::string GenAlphaString(std::size_t len) {
    static const std::vector<char> kAlphaNum = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                                                'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                                                'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                                                'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    auto chars = *rc::gen::container<std::string>(len, rc::gen::elementOf(kAlphaNum));
    return chars;
}

/// RAII helper for temporary files.
struct TempFile {
    std::string path;
    TempFile(const std::string& content, const std::string& suffix) {
        // Use a unique path based on address to avoid conflicts
        path = "/tmp/cece_spec_test_" + std::to_string(reinterpret_cast<uintptr_t>(this)) + suffix;
        std::ofstream ofs(path);
        RC_ASSERT(ofs.is_open());
        ofs << content;
        ofs.close();
    }
    ~TempFile() {
        std::remove(path.c_str());
    }
};

/// Write a SpeciationConfig to mechanism + mapping YAML temp files (new dataset-oriented format).
/// Returns pair of (mechanism_yaml, mapping_yaml) strings.
static std::pair<std::string, std::string> ConfigToYamlPair(const SpeciationConfig& config) {
    // Mechanism file (MICM format) — use YAML emitter
    YAML::Emitter mech_out;
    mech_out << YAML::BeginMap;
    mech_out << YAML::Key << "name" << YAML::Value << config.mechanism_name;
    mech_out << YAML::Key << "species" << YAML::Value << YAML::BeginSeq;
    for (const auto& sp : config.species) {
        mech_out << YAML::BeginMap;
        mech_out << YAML::Key << "name" << YAML::Value << sp.name;
        mech_out << YAML::Key << "molecular weight [kg mol-1]" << YAML::Value << (sp.molecular_weight / 1000.0);
        mech_out << YAML::EndMap;
    }
    mech_out << YAML::EndSeq;
    mech_out << YAML::EndMap;

    // Mapping file (dataset-oriented format) — write as raw string to ensure
    // correct YAML output without yaml-cpp emitter quirks
    std::ostringstream map_ss;
    map_ss << "mechanism: " << config.mechanism_name << "\n";
    map_ss << "datasets:\n";
    map_ss << "  " << config.dataset_name << ":\n";

    // Group mappings by mechanism species
    std::unordered_map<std::string, std::vector<const SpeciationMapping*>> grouped;
    for (const auto& m : config.mappings) {
        grouped[m.mechanism_species].push_back(&m);
    }

    for (const auto& [mech_sp, mapping_ptrs] : grouped) {
        // Quote all keys to avoid YAML 1.1 boolean/null interpretation
        map_ss << "    \"" << mech_sp << "\":\n";
        for (const auto* mp : mapping_ptrs) {
            map_ss << "      \"" << EmissionClassToString(mp->emission_class) << "\": " << std::fixed << std::setprecision(6) << mp->scale_factor
                   << "\n";
        }
    }

    return {mech_out.c_str(), map_ss.str()};
}

/// Generate a valid SpeciationConfig using RapidCheck generators.
static rc::Gen<SpeciationConfig> genValidConfig() {
    return rc::gen::exec([]() {
        SpeciationConfig config;

        // Random mechanism name (3-10 chars, prefixed to avoid YAML keywords)
        auto name_len = *rc::gen::inRange(3, 11);
        config.mechanism_name = "MECH_" + GenAlphaString(static_cast<std::size_t>(name_len));
        config.dataset_name = "MEGAN";

        // Random species list (1-50 species)
        auto num_species = *rc::gen::inRange(1, 51);
        std::set<std::string> species_names_set;
        for (int i = 0; i < num_species; ++i) {
            MechanismSpecies sp;
            do {
                auto sp_len = *rc::gen::inRange(2, 8);
                sp.name = "SP_" + GenAlphaString(static_cast<std::size_t>(sp_len));
            } while (species_names_set.count(sp.name) > 0);
            species_names_set.insert(sp.name);
            sp.molecular_weight = *rc::gen::cast<double>(rc::gen::inRange(1, 501));
            config.species.push_back(sp);
        }

        // Build species name vector for referencing in mappings
        std::vector<std::string> species_names(species_names_set.begin(), species_names_set.end());

        // Random mappings — avoid duplicate (mechanism_species, emission_class) pairs
        // since YAML maps can't have duplicate keys
        auto num_mappings = *rc::gen::inRange(1, std::min(201, num_species * 10 + 1));
        std::set<std::pair<std::string, int>> used_pairs;
        for (int i = 0; i < num_mappings; ++i) {
            SpeciationMapping mapping;
            auto sp_idx = *rc::gen::inRange(0, static_cast<int>(species_names.size()));
            mapping.mechanism_species = species_names[static_cast<std::size_t>(sp_idx)];
            auto ec_idx = *rc::gen::inRange(0, static_cast<int>(kValidEmissionClasses.size()));
            EmissionClass ec;
            StringToEmissionClass(kValidEmissionClasses[static_cast<std::size_t>(ec_idx)], ec);
            mapping.emission_class = ec;

            // Skip if this (mechanism_species, emission_class) pair already exists
            auto key = std::make_pair(mapping.mechanism_species, static_cast<int>(ec));
            if (used_pairs.count(key) > 0) {
                continue;
            }
            used_pairs.insert(key);

            mapping.scale_factor = (*rc::gen::inRange(1, 100001)) / 1000.0;
            config.mappings.push_back(mapping);
        }

        // Ensure at least one mapping
        if (config.mappings.empty()) {
            SpeciationMapping mapping;
            mapping.mechanism_species = species_names[0];
            EmissionClass ec;
            StringToEmissionClass(kValidEmissionClasses[0], ec);
            mapping.emission_class = ec;
            mapping.scale_factor = 1.0;
            config.mappings.push_back(mapping);
        }

        return config;
    });
}

// ============================================================================
// Property 1: Speciation Configuration Round-Trip
// Feature: megan3-integration, Property 1: Speciation Configuration Round-Trip
// **Validates: Requirements 5.1, 5.2, 5.7, 7.6**
// ============================================================================

RC_GTEST_PROP(SpeciationProperty, Property1_RoundTrip, ()) {
    auto config = *genValidConfig();

    // Serialize to separate mechanism and mapping YAML strings
    auto [mech_yaml, map_yaml] = ConfigToYamlPair(config);

    // Write to temp files
    TempFile mech_file(mech_yaml, "_mech.yaml");
    TempFile map_file(map_yaml, "_map.yaml");

    // Parse back via Load
    SpeciationConfigLoader loader;
    SpeciationConfig parsed = loader.Load(mech_file.path, map_file.path, config.dataset_name);

    // Assert equivalence: mechanism name
    RC_ASSERT(parsed.mechanism_name == config.mechanism_name);

    // Assert equivalence: dataset name
    RC_ASSERT(parsed.dataset_name == config.dataset_name);

    // Assert equivalence: species list (same size, same names and MWs)
    RC_ASSERT(parsed.species.size() == config.species.size());
    for (std::size_t i = 0; i < config.species.size(); ++i) {
        RC_ASSERT(parsed.species[i].name == config.species[i].name);
        double diff = std::abs(parsed.species[i].molecular_weight - config.species[i].molecular_weight);
        RC_ASSERT(diff < 1e-6);
    }

    // Assert equivalence: mappings (same size)
    RC_ASSERT(parsed.mappings.size() == config.mappings.size());

    // Build lookup maps for comparison (order may differ due to YAML map iteration)
    // Key: (mechanism_species, emission_class) -> scale_factor
    std::map<std::pair<std::string, int>, double> orig_map, parsed_map;
    for (const auto& m : config.mappings) {
        auto key = std::make_pair(m.mechanism_species, static_cast<int>(m.emission_class));
        orig_map[key] += m.scale_factor;
    }
    for (const auto& m : parsed.mappings) {
        auto key = std::make_pair(m.mechanism_species, static_cast<int>(m.emission_class));
        parsed_map[key] += m.scale_factor;
    }

    RC_ASSERT(orig_map.size() == parsed_map.size());
    for (const auto& [key, val] : orig_map) {
        auto it = parsed_map.find(key);
        RC_ASSERT(it != parsed_map.end());
        RC_ASSERT(std::abs(it->second - val) < 1e-10);
    }
}

// ============================================================================
// Property 2: Speciation Config Cross-Reference Validation
// Feature: megan3-integration, Property 2: Speciation Config Cross-Reference Validation
// **Validates: Requirements 5.5, 5.6**
// ============================================================================

RC_GTEST_PROP(SpeciationProperty, Property2_ValidConfigPassesValidation, ()) {
    auto config = *genValidConfig();
    auto [mech_yaml, map_yaml] = ConfigToYamlPair(config);
    TempFile mech_file(mech_yaml, "_mech.yaml");
    TempFile map_file(map_yaml, "_map.yaml");

    SpeciationConfigLoader loader;
    SpeciationConfig parsed = loader.Load(mech_file.path, map_file.path, config.dataset_name);
    RC_ASSERT(parsed.mechanism_name == config.mechanism_name);
}

RC_GTEST_PROP(SpeciationProperty, Property2_InvalidMechanismSpeciesReference, ()) {
    auto config = *genValidConfig();
    RC_PRE(!config.mappings.empty());

    // Mutate: pick a random mapping and change mechanism_species to a name not in species list
    auto idx = *rc::gen::inRange(0, static_cast<int>(config.mappings.size()));
    std::string invalid_name = "NONEXISTENT_SPECIES_" + GenAlphaString(5);
    config.mappings[static_cast<std::size_t>(idx)].mechanism_species = invalid_name;

    auto [mech_yaml, map_yaml] = ConfigToYamlPair(config);
    TempFile mech_file(mech_yaml, "_mech.yaml");
    TempFile map_file(map_yaml, "_map.yaml");

    SpeciationConfigLoader loader;
    try {
        loader.Load(mech_file.path, map_file.path, config.dataset_name);
        RC_FAIL("Expected std::invalid_argument for invalid mechanism species reference");
    } catch (const std::invalid_argument&) {
        // Expected — validation caught the invalid reference
    }
}

RC_GTEST_PROP(SpeciationProperty, Property2_InvalidEmissionClassReference, ()) {
    auto config = *genValidConfig();
    RC_PRE(!config.mappings.empty());

    // We can't directly set an invalid emission class in the struct (it's an enum),
    // so we test by writing a MAP file with an invalid class name string.
    // Generate a valid config, serialize it, then manually inject an invalid class name.
    auto [mech_yaml, _] = ConfigToYamlPair(config);

    // Build a MAP YAML with one invalid emission class name
    YAML::Emitter map_out;
    map_out << YAML::BeginMap;
    map_out << YAML::Key << "mechanism" << YAML::Value << config.mechanism_name;
    map_out << YAML::Key << "datasets" << YAML::Value << YAML::BeginMap;
    map_out << YAML::Key << config.dataset_name << YAML::Value << YAML::BeginMap;

    // Write first mapping with an invalid class name
    if (!config.mappings.empty()) {
        const auto& m = config.mappings[0];
        map_out << YAML::Key << m.mechanism_species << YAML::Value << YAML::BeginMap;
        map_out << YAML::Key << "INVALID_CLASS_XYZ" << YAML::Value << m.scale_factor;
        map_out << YAML::EndMap;
    }

    map_out << YAML::EndMap;  // end dataset
    map_out << YAML::EndMap;  // end datasets
    map_out << YAML::EndMap;

    TempFile mech_file(mech_yaml, "_mech.yaml");
    TempFile map_file(map_out.c_str(), "_map.yaml");

    SpeciationConfigLoader loader;
    try {
        loader.Load(mech_file.path, map_file.path, config.dataset_name);
        RC_FAIL("Expected std::invalid_argument for invalid emission class reference");
    } catch (const std::invalid_argument&) {
        // Expected — validation caught the invalid emission class
    }
}

// ============================================================================
// Property 3: Speciation YAML Schema Enforcement
// Feature: megan3-integration, Property 3: Speciation YAML Schema Enforcement
// **Validates: Requirements 7.1, 7.2, 7.3, 7.4, 7.5**
// ============================================================================

RC_GTEST_PROP(SpeciationProperty, Property3_MechanismFileMissingName, ()) {
    auto config = *genValidConfig();

    // Mechanism YAML missing 'name'
    YAML::Emitter mech_out;
    mech_out << YAML::BeginMap;
    mech_out << YAML::Key << "species" << YAML::Value << YAML::BeginSeq;
    for (const auto& sp : config.species) {
        mech_out << YAML::BeginMap;
        mech_out << YAML::Key << "name" << YAML::Value << sp.name;
        mech_out << YAML::Key << "molecular weight [kg mol-1]" << YAML::Value << (sp.molecular_weight / 1000.0);
        mech_out << YAML::EndMap;
    }
    mech_out << YAML::EndSeq;
    mech_out << YAML::EndMap;

    auto [_, map_yaml] = ConfigToYamlPair(config);
    TempFile mech_file(mech_out.c_str(), "_mech.yaml");
    TempFile map_file(map_yaml, "_map.yaml");

    SpeciationConfigLoader loader;
    try {
        loader.Load(mech_file.path, map_file.path, config.dataset_name);
        RC_FAIL("Expected exception for mechanism file missing 'name' key");
    } catch (const std::invalid_argument&) {
        // Expected
    }
}

RC_GTEST_PROP(SpeciationProperty, Property3_MechanismSpeciesMissingFields, ()) {
    auto config = *genValidConfig();
    RC_PRE(!config.species.empty());

    auto omit_field = *rc::gen::inRange(0, 2);
    auto omit_idx = *rc::gen::inRange(0, static_cast<int>(config.species.size()));

    YAML::Emitter mech_out;
    mech_out << YAML::BeginMap;
    mech_out << YAML::Key << "name" << YAML::Value << config.mechanism_name;
    mech_out << YAML::Key << "species" << YAML::Value << YAML::BeginSeq;
    for (int i = 0; i < static_cast<int>(config.species.size()); ++i) {
        mech_out << YAML::BeginMap;
        if (i != omit_idx || omit_field != 0) {
            mech_out << YAML::Key << "name" << YAML::Value << config.species[static_cast<std::size_t>(i)].name;
        }
        if (i != omit_idx || omit_field != 1) {
            mech_out << YAML::Key << "molecular weight [kg mol-1]" << YAML::Value
                     << (config.species[static_cast<std::size_t>(i)].molecular_weight / 1000.0);
        }
        mech_out << YAML::EndMap;
    }
    mech_out << YAML::EndSeq;
    mech_out << YAML::EndMap;

    auto [_, map_yaml] = ConfigToYamlPair(config);
    TempFile mech_file(mech_out.c_str(), "_mech.yaml");
    TempFile map_file(map_yaml, "_map.yaml");

    SpeciationConfigLoader loader;
    try {
        loader.Load(mech_file.path, map_file.path, config.dataset_name);
        RC_FAIL("Expected exception for mechanism species missing required field");
    } catch (const std::invalid_argument&) {
        // Expected
    }
}

RC_GTEST_PROP(SpeciationProperty, Property3_NonPositiveMolecularWeight, ()) {
    auto config = *genValidConfig();
    RC_PRE(!config.species.empty());

    auto bad_idx = *rc::gen::inRange(0, static_cast<int>(config.species.size()));
    auto bad_mw = *rc::gen::element(0.0, -1.0, -100.0, -0.001);

    YAML::Emitter mech_out;
    mech_out << YAML::BeginMap;
    mech_out << YAML::Key << "name" << YAML::Value << config.mechanism_name;
    mech_out << YAML::Key << "species" << YAML::Value << YAML::BeginSeq;
    for (int i = 0; i < static_cast<int>(config.species.size()); ++i) {
        mech_out << YAML::BeginMap;
        mech_out << YAML::Key << "name" << YAML::Value << config.species[static_cast<std::size_t>(i)].name;
        double mw_kg = (i == bad_idx) ? bad_mw : (config.species[static_cast<std::size_t>(i)].molecular_weight / 1000.0);
        mech_out << YAML::Key << "molecular weight [kg mol-1]" << YAML::Value << mw_kg;
        mech_out << YAML::EndMap;
    }
    mech_out << YAML::EndSeq;
    mech_out << YAML::EndMap;

    auto [_, map_yaml] = ConfigToYamlPair(config);
    TempFile mech_file(mech_out.c_str(), "_mech.yaml");
    TempFile map_file(map_yaml, "_map.yaml");

    SpeciationConfigLoader loader;
    try {
        loader.Load(mech_file.path, map_file.path, config.dataset_name);
        RC_FAIL("Expected exception for non-positive molecular weight");
    } catch (const std::invalid_argument&) {
        // Expected
    }
}

RC_GTEST_PROP(SpeciationProperty, Property3_MappingFileMissingMechanism, ()) {
    auto config = *genValidConfig();

    auto [mech_yaml, _] = ConfigToYamlPair(config);

    // Mapping YAML missing 'mechanism' key
    YAML::Emitter map_out;
    map_out << YAML::BeginMap;
    map_out << YAML::Key << "datasets" << YAML::Value << YAML::BeginMap;
    map_out << YAML::Key << config.dataset_name << YAML::Value << YAML::BeginMap;
    if (!config.mappings.empty()) {
        const auto& m = config.mappings[0];
        map_out << YAML::Key << m.mechanism_species << YAML::Value << YAML::BeginMap;
        map_out << YAML::Key << EmissionClassToString(m.emission_class) << YAML::Value << m.scale_factor;
        map_out << YAML::EndMap;
    }
    map_out << YAML::EndMap;
    map_out << YAML::EndMap;
    map_out << YAML::EndMap;

    TempFile mech_file(mech_yaml, "_mech.yaml");
    TempFile map_file(map_out.c_str(), "_map.yaml");

    SpeciationConfigLoader loader;
    try {
        loader.Load(mech_file.path, map_file.path, config.dataset_name);
        RC_FAIL("Expected exception for mapping file missing 'mechanism' key");
    } catch (const std::invalid_argument&) {
        // Expected
    }
}

RC_GTEST_PROP(SpeciationProperty, Property3_MappingFileMissingDatasets, ()) {
    auto config = *genValidConfig();

    auto [mech_yaml, _] = ConfigToYamlPair(config);

    // Mapping YAML missing 'datasets' section
    YAML::Emitter map_out;
    map_out << YAML::BeginMap;
    map_out << YAML::Key << "mechanism" << YAML::Value << config.mechanism_name;
    map_out << YAML::EndMap;

    TempFile mech_file(mech_yaml, "_mech.yaml");
    TempFile map_file(map_out.c_str(), "_map.yaml");

    SpeciationConfigLoader loader;
    try {
        loader.Load(mech_file.path, map_file.path, config.dataset_name);
        RC_FAIL("Expected exception for mapping file missing 'datasets' section");
    } catch (const std::invalid_argument&) {
        // Expected
    }
}

RC_GTEST_PROP(SpeciationProperty, Property3_NonPositiveScaleFactor, ()) {
    auto config = *genValidConfig();
    RC_PRE(!config.mappings.empty());

    auto bad_sf = *rc::gen::element(0.0, -1.0, -0.001, -100.0);

    auto [mech_yaml, _] = ConfigToYamlPair(config);

    // Build MAP YAML with one non-positive scale factor
    YAML::Emitter map_out;
    map_out << YAML::BeginMap;
    map_out << YAML::Key << "mechanism" << YAML::Value << config.mechanism_name;
    map_out << YAML::Key << "datasets" << YAML::Value << YAML::BeginMap;
    map_out << YAML::Key << config.dataset_name << YAML::Value << YAML::BeginMap;

    // Write first mapping with bad scale factor
    const auto& m = config.mappings[0];
    map_out << YAML::Key << m.mechanism_species << YAML::Value << YAML::BeginMap;
    map_out << YAML::Key << EmissionClassToString(m.emission_class) << YAML::Value << bad_sf;
    map_out << YAML::EndMap;

    map_out << YAML::EndMap;
    map_out << YAML::EndMap;
    map_out << YAML::EndMap;

    TempFile mech_file(mech_yaml, "_mech.yaml");
    TempFile map_file(map_out.c_str(), "_map.yaml");

    SpeciationConfigLoader loader;
    try {
        loader.Load(mech_file.path, map_file.path, config.dataset_name);
        RC_FAIL("Expected exception for non-positive scale factor");
    } catch (const std::invalid_argument&) {
        // Expected
    }
}

// ============================================================================
// Helper: Generate configs for engine tests
// ============================================================================

/// Generate a simple speciation config where each of the 19 emission classes
/// has exactly one mapping to a unique mechanism species.
static rc::Gen<SpeciationConfig> genSimpleEngineConfig() {
    return rc::gen::exec([]() {
        SpeciationConfig config;
        config.mechanism_name = "TEST_MECH";
        config.dataset_name = "MEGAN";

        // Create 19 mechanism species, one per emission class
        for (int c = 0; c < 19; ++c) {
            MechanismSpecies sp;
            sp.name = "MECH_" + std::to_string(c);
            sp.molecular_weight = 10.0 + (*rc::gen::inRange(0, 1901)) / 10.0;
            config.species.push_back(sp);
        }

        // Create one mapping per class
        for (int c = 0; c < 19; ++c) {
            SpeciationMapping mapping;
            mapping.mechanism_species = "MECH_" + std::to_string(c);
            mapping.emission_class = static_cast<EmissionClass>(c);
            mapping.scale_factor = 0.1 + (*rc::gen::inRange(0, 9901)) / 1000.0;
            config.mappings.push_back(mapping);
        }

        return config;
    });
}

/// Generate a config with multiple mappings per class to test accumulation.
static rc::Gen<SpeciationConfig> genMultiMappingConfig() {
    return rc::gen::exec([]() {
        SpeciationConfig config;
        config.mechanism_name = "TEST_MULTI";
        config.dataset_name = "MEGAN";

        // Create a small set of mechanism species (3-5)
        auto num_mech = *rc::gen::inRange(3, 6);
        for (int s = 0; s < num_mech; ++s) {
            MechanismSpecies sp;
            sp.name = "M_" + std::to_string(s);
            sp.molecular_weight = 10.0 + (*rc::gen::inRange(0, 4901)) / 10.0;
            config.species.push_back(sp);
        }

        // Create 1-3 mappings per emission class, targeting random mechanism species
        auto num_classes_used = *rc::gen::inRange(1, 20);
        for (int c = 0; c < num_classes_used; ++c) {
            int class_idx = c % 19;
            auto num_maps = *rc::gen::inRange(1, 4);
            for (int m = 0; m < num_maps; ++m) {
                SpeciationMapping mapping;
                auto mech_idx = *rc::gen::inRange(0, num_mech);
                mapping.mechanism_species = "M_" + std::to_string(mech_idx);
                mapping.emission_class = static_cast<EmissionClass>(class_idx);
                mapping.scale_factor = 0.1 + (*rc::gen::inRange(0, 9901)) / 1000.0;
                config.mappings.push_back(mapping);
            }
        }

        return config;
    });
}

/// Helper to run the speciation engine and collect output into a map of
/// mechanism species name -> accumulated value across all grid cells.
static std::unordered_map<std::string, double> RunEngineAndCollect(
    SpeciationEngine& engine, const Kokkos::View<const double**, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>& class_totals,
    const std::vector<std::string>& mech_names, int nx, int ny) {
    CeceExportState export_state;
    for (const auto& name : mech_names) {
        std::string field_name = "MEGAN_" + name;
        export_state.fields[field_name] = DualView3D("export_" + field_name, nx, ny, 1);
        Kokkos::deep_copy(export_state.fields[field_name].view_device(), 0.0);
        export_state.fields[field_name].modify_device();
    }

    engine.Run(class_totals, export_state, nx, ny);

    std::unordered_map<std::string, double> results;
    for (const auto& name : mech_names) {
        std::string field_name = "MEGAN_" + name;
        auto& dv = export_state.fields[field_name];
        dv.sync_host();
        auto h_view = dv.view_host();
        double total = 0.0;
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                total += h_view(i, j, 0);
            }
        }
        results[name] = total;
    }
    return results;
}

// ============================================================================
// Property 10: Speciation Pipeline Correctness
// Feature: megan3-integration, Property 10: Speciation Pipeline Correctness
// **Validates: Requirements 6.1, 6.2, 6.3**
//
// For each mechanism species s:
//   output[s] = (Σ class_total[c] × scale_factor[c→s]) × MW[s]
// ============================================================================

RC_GTEST_PROP(SpeciationProperty, Property10_PipelineCorrectness, ()) {
    auto config = *genMultiMappingConfig();

    SpeciationEngine engine;
    engine.Initialize(config);
    const auto& mech_names = engine.GetMechanismSpeciesNames();

    // Use a 1x1 grid for simplicity
    int nx = 1, ny = 1;
    int num_cells = 1;

    // Generate 19 non-negative class totals
    Kokkos::View<double**, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> class_totals_rw("class_totals", 19, num_cells);
    auto h_ct = Kokkos::create_mirror_view(class_totals_rw);

    for (int c = 0; c < 19; ++c) {
        h_ct(c, 0) = (*rc::gen::inRange(0, 10001)) * 1e-10;
    }
    Kokkos::deep_copy(class_totals_rw, h_ct);

    Kokkos::View<const double**, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> class_totals = class_totals_rw;

    auto results = RunEngineAndCollect(engine, class_totals, mech_names, nx, ny);

    // Build MW lookup
    std::unordered_map<std::string, double> mw_lookup;
    for (const auto& sp : config.species) {
        mw_lookup[sp.name] = sp.molecular_weight;
    }

    // Compute expected per-mechanism-species totals on host
    // output[s] = (Σ class_total[c] × scale_factor[c→s]) × MW[s]
    std::unordered_map<std::string, double> expected;
    for (const auto& name : mech_names) {
        expected[name] = 0.0;
    }

    for (const auto& mapping : config.mappings) {
        int class_idx = static_cast<int>(mapping.emission_class);
        double contribution = h_ct(class_idx, 0) * mapping.scale_factor;
        expected[mapping.mechanism_species] += contribution;
    }

    // Apply MW
    for (auto& [name, val] : expected) {
        val *= mw_lookup[name];
    }

    // Assert each mechanism species matches expected
    for (const auto& name : mech_names) {
        double exp_val = expected[name];
        double act_val = results[name];
        double tol = std::max(1e-10, std::abs(exp_val) * 1e-6);
        RC_ASSERT(std::abs(act_val - exp_val) < tol);
    }
}

// Also test with the simple 1:1 config for mass conservation
RC_GTEST_PROP(SpeciationProperty, Property10_SimpleConfigCorrectness, ()) {
    auto config = *genSimpleEngineConfig();

    SpeciationEngine engine;
    engine.Initialize(config);
    const auto& mech_names = engine.GetMechanismSpeciesNames();

    int nx = 2, ny = 2;
    int num_cells = nx * ny;

    Kokkos::View<double**, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> class_totals_rw("class_totals", 19, num_cells);
    auto h_ct = Kokkos::create_mirror_view(class_totals_rw);

    for (int c = 0; c < 19; ++c) {
        for (int cell = 0; cell < num_cells; ++cell) {
            h_ct(c, cell) = (*rc::gen::inRange(0, 10001)) * 1e-10;
        }
    }
    Kokkos::deep_copy(class_totals_rw, h_ct);

    Kokkos::View<const double**, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> class_totals = class_totals_rw;

    auto results = RunEngineAndCollect(engine, class_totals, mech_names, nx, ny);

    // Compute expected total output
    double expected_total = 0.0;
    for (const auto& mapping : config.mappings) {
        int class_idx = static_cast<int>(mapping.emission_class);
        double mw = 0.0;
        for (const auto& sp : config.species) {
            if (sp.name == mapping.mechanism_species) {
                mw = sp.molecular_weight;
                break;
            }
        }
        for (int cell = 0; cell < num_cells; ++cell) {
            expected_total += h_ct(class_idx, cell) * mapping.scale_factor * mw;
        }
    }

    double actual_total = 0.0;
    for (const auto& [name, val] : results) {
        actual_total += val;
    }

    double tol = std::max(1e-10, std::abs(expected_total) * 1e-6);
    RC_ASSERT(std::abs(actual_total - expected_total) < tol);
}

// ============================================================================
// Property 17: Speciation Accumulation Determinism
// Feature: megan3-integration, Property 17: Speciation Accumulation Determinism
// **Validates: Requirements 10.2**
//
// Run the speciation kernel twice with identical inputs and assert
// bit-identical outputs.
// ============================================================================

RC_GTEST_PROP(SpeciationProperty, Property17_AccumulationDeterminism, ()) {
    auto config = *genMultiMappingConfig();

    SpeciationEngine engine;
    engine.Initialize(config);
    const auto& mech_names = engine.GetMechanismSpeciesNames();

    int nx = 3, ny = 3;
    int num_cells = nx * ny;

    Kokkos::View<double**, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> class_totals_rw("class_totals", 19, num_cells);
    auto h_ct = Kokkos::create_mirror_view(class_totals_rw);

    for (int c = 0; c < 19; ++c) {
        for (int cell = 0; cell < num_cells; ++cell) {
            h_ct(c, cell) = (*rc::gen::inRange(0, 10001)) * 1e-10;
        }
    }
    Kokkos::deep_copy(class_totals_rw, h_ct);

    Kokkos::View<const double**, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> class_totals = class_totals_rw;

    // Run 1
    auto results1 = RunEngineAndCollect(engine, class_totals, mech_names, nx, ny);

    // Run 2 (identical inputs)
    auto results2 = RunEngineAndCollect(engine, class_totals, mech_names, nx, ny);

    // Assert bit-identical outputs
    for (const auto& name : mech_names) {
        double v1 = results1[name];
        double v2 = results2[name];
        RC_ASSERT(std::memcmp(&v1, &v2, sizeof(double)) == 0);
    }
}

// ============================================================================
// Unit Test: CB6 Config Loading (Task 12.4)
// Validates: Requirements 6.5
// ============================================================================

class CB6ConfigLoadingTest : public ::testing::Test {
   public:
    static void SetUpTestSuite() {
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }
    }
};

TEST_F(CB6ConfigLoadingTest, LoadCB6SpeciationConfig) {
    std::string src_dir = CECE_SOURCE_DIR;
    std::string spc_path = src_dir + "/data/speciation/spc_cb6.yaml";
    std::string map_path = src_dir + "/data/speciation/map_cb6.yaml";

    SpeciationConfigLoader loader;
    SpeciationConfig config = loader.Load(spc_path, map_path, "MEGAN");

    // CB6_AE7 mechanism should have 36 species
    EXPECT_EQ(config.species.size(), 36u) << "CB6_AE7 mechanism should define 36 species";

    // Mechanism name should match
    EXPECT_EQ(config.mechanism_name, "CB6_AE7");

    // Dataset name should be MEGAN
    EXPECT_EQ(config.dataset_name, "MEGAN");

    // Should have 53 class-to-mechanism mappings
    EXPECT_EQ(config.mappings.size(), 53u) << "CB6 MAP file should define 53 emission class mappings";

    // Verify a few known species exist with correct molecular weights
    bool found_isop = false;
    bool found_no = false;
    bool found_co = false;
    for (const auto& sp : config.species) {
        if (sp.name == "ISOP") {
            found_isop = true;
            EXPECT_NEAR(sp.molecular_weight, 68.12, 0.1);
        }
        if (sp.name == "NO") {
            found_no = true;
            EXPECT_NEAR(sp.molecular_weight, 30.01, 0.1);
        }
        if (sp.name == "CO") {
            found_co = true;
            EXPECT_NEAR(sp.molecular_weight, 28.01, 0.1);
        }
    }
    EXPECT_TRUE(found_isop) << "ISOP species should be present";
    EXPECT_TRUE(found_no) << "NO species should be present";
    EXPECT_TRUE(found_co) << "CO species should be present";

    // Verify ISOP mapping exists with scale factor 1.0
    bool found_isop_mapping = false;
    for (const auto& m : config.mappings) {
        if (m.mechanism_species == "ISOP" && m.emission_class == EmissionClass::ISOP) {
            found_isop_mapping = true;
            EXPECT_NEAR(m.scale_factor, 1.0, 1e-10);
        }
    }
    EXPECT_TRUE(found_isop_mapping) << "ISOP->ISOP mapping should exist";
}

}  // namespace cece

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (!Kokkos::is_initialized()) {
        Kokkos::initialize(argc, argv);
    }
    int result = RUN_ALL_TESTS();
    if (Kokkos::is_initialized()) {
        Kokkos::finalize();
    }
    return result;
}
