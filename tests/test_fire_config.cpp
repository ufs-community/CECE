// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <string>

#include "cece/cece_config.hpp"
#include "cece/cece_config_validator.hpp"
#include "cece/cece_fire_input.hpp"
#include "conf/config.hpp"

namespace {
cece::FireGridDefinition MakeGridDefinition() {
    return {"synthetic-grid",
            std::string(64, 'b'),
            1,
            1,
            {0.0},
            {0.0},
            {{{-0.5, 0.5, 0.5, -0.5}}},
            {{{-0.5, -0.5, 0.5, 0.5}}},
            {25.0},
            "EPSG:4326",
            cece::FireLongitudeConvention::Minus180To180};
}

cece::FireIdentityMetadata MakeIdentityMetadata() {
    return {1,
            "synthetic-source",
            "synthetic-product-v1",
            "synthetic-algorithm-v1",
            "delivery-1",
            std::string(64, 'a'),
            cece::FireUtcTime("2022-07-01T00:00:00Z"),
            cece::FireUtcTime("2022-07-01T01:00:00Z"),
            cece::FireUtcTime("2022-07-01T00:15:00Z"),
            cece::FireUtcTime("2022-07-01T01:30:00Z"),
            cece::FireUtcTime("2022-07-01T02:00:00Z"),
            cece::FireCutoffMode::Fixed,
            std::make_shared<const cece::FireGrid>(MakeGridDefinition())};
}

cece::FireBulkInventoryData MakeInventoryData() {
    const cece::FireProfileIdentity biome{"synthetic-biome", "1", std::string(64, 'c')};
    const cece::FireProfileIdentity composition{"synthetic-composition", "1", std::string(64, 'd')};
    const cece::FireProfileIdentity other_biome{"synthetic-other-biome", "1", std::string(64, 'e')};
    const cece::FireProfileIdentity other_composition{"synthetic-other-composition", "2", std::string(64, 'f')};
    return {cece::FireInputIdentity(MakeIdentityMetadata()),
            {cece::FireBulkSpecies::CO2, cece::FireBulkSpecies::VOC},
            {"component-a", "component-b"},
            {{"component-a", {biome, composition}}, {"component-b", {other_biome, other_composition}}},
            {1.0, 3.0, 2.0, 4.0},
            {cece::FireQualityState::Observed},
            {1.0},
            {std::string(64, 'a')},
            std::nullopt,
            composition,
            std::nullopt,
            std::nullopt,
            std::nullopt};
}
}  // namespace

TEST(test_fire_config, SharedNativeConfigurationCases) {
    const auto fixtures = YAML::LoadFile(std::string(CECE_SOURCE_DIR) + "/tests/data/fire/config_cases.yaml");
    for (const auto& test : fixtures["cases"]) {
        SCOPED_TRACE(test["id"].as<std::string>());
        auto options = YAML::Clone(fixtures["base_options"]);
        if (test["set"]) {
            for (const auto& entry : test["set"]) options[entry.first.as<std::string>()] = entry.second;
        }
        if (test["unset"]) {
            for (const auto& key : test["unset"]) options.remove(key.as<std::string>());
        }
        YAML::Node root;
        root["physics_schemes"][0]["name"] = "fire";
        root["physics_schemes"][0]["options"] = options;
        if (test["duplicate_fire"]) root["physics_schemes"].push_back(YAML::Clone(root["physics_schemes"][0]));
        const auto document = conf::Config::from_string(YAML::Dump(root));
        const auto validation = cece::ConfigValidator::ValidateConfig(document.root());
        std::vector<std::string> fire_errors;
        for (const auto& error : validation.errors) {
            if (error.field.starts_with("physics_schemes[")) fire_errors.push_back(error.message);
        }
        EXPECT_EQ(fire_errors.empty(), test["valid"].as<bool>());
        if (!test["valid"].as<bool>()) {
            ASSERT_FALSE(fire_errors.empty());
            EXPECT_NE(fire_errors.front().find(test["error"].as<std::string>()), std::string::npos) << fire_errors.front();
        }
        char path[] = "/tmp/cece-fire-config-XXXXXX";
        const int descriptor = mkstemp(path);
        ASSERT_GE(descriptor, 0);
        close(descriptor);
        struct Cleanup {
            const char* path;
            ~Cleanup() {
                std::remove(path);
            }
        } cleanup{path};
        std::ofstream(path) << YAML::Dump(root);
        if (test["valid"].as<bool>()) {
            const auto parsed = cece::ParseConfig(path);
            ASSERT_EQ(parsed.physics_schemes.size(), 1);
            ASSERT_TRUE(parsed.physics_schemes[0].fire);
            EXPECT_EQ(parsed.physics_schemes[0].fire->target_id, options["target_id"].as<std::string>());
        } else {
            try {
                (void)cece::ParseConfig(path);
                ADD_FAILURE() << "Invalid fire configuration was accepted";
            } catch (const std::exception& error) {
                EXPECT_NE(std::string(error.what()).find(test["error"].as<std::string>()), std::string::npos) << error.what();
            }
        }
    }
}

TEST(test_fire_config, FireFreeSchemesDoNotGainFireValidationOrTypedOptions) {
    char path[] = "/tmp/cece-fire-disabled-XXXXXX";
    const int descriptor = mkstemp(path);
    ASSERT_GE(descriptor, 0);
    close(descriptor);
    struct Cleanup {
        const char* path;
        ~Cleanup() {
            std::remove(path);
        }
    } cleanup{path};
    std::ofstream(path) << "physics_schemes:\n  - name: NativeExample\n    options: {arbitrary_existing_option: value}\n";
    const auto parsed = cece::ParseConfig(path);
    ASSERT_EQ(parsed.physics_schemes.size(), 1);
    EXPECT_FALSE(parsed.physics_schemes.front().fire);
}

TEST(test_fire_config, TypedFireOptionsPreserveDefaultsAndExplicitInputPolicy) {
    const auto fixtures = YAML::LoadFile(std::string(CECE_SOURCE_DIR) + "/tests/data/fire/config_cases.yaml");
    const auto document = conf::Config::from_string(YAML::Dump(fixtures["base_options"]));
    const auto options = cece::ParseFireConfig(document.root());
    EXPECT_EQ(options.mode, cece::FireInputMode::Product);
    EXPECT_EQ(options.input_provider, cece::FireInputProvider::Prepared);
    EXPECT_EQ(options.product_time_allocation, cece::FireTimeAllocation::SourceResolved);
    EXPECT_EQ(options.requested_species.size(), 11);
    EXPECT_EQ(options.cutoff_mode, cece::FireCutoffMode::Fixed);
    ASSERT_TRUE(options.availability_cutoff);
    EXPECT_EQ(options.availability_cutoff->ToString(), "2022-07-02T00:00:00Z");
    EXPECT_EQ(options.scaling, "none");
    EXPECT_EQ(options.missing_policy, "error");
    EXPECT_FALSE(options.max_gap_seconds);
    EXPECT_FALSE(options.max_observation_age_seconds);
    EXPECT_TRUE(options.scientific_profile.empty());
}

TEST(test_fire_config, ManifestSymlinkAliasesAreRejected) {
    char source[] = "/tmp/cece-fire-source-XXXXXX";
    const int descriptor = mkstemp(source);
    ASSERT_GE(descriptor, 0);
    close(descriptor);
    const auto alias = std::string(source) + ".alias";
    struct Cleanup {
        const char* source;
        std::string alias;
        ~Cleanup() {
            std::remove(alias.c_str());
            std::remove(source);
        }
    } cleanup{source, alias};
    std::filesystem::create_symlink(source, alias);
    const auto fixtures = YAML::LoadFile(std::string(CECE_SOURCE_DIR) + "/tests/data/fire/config_cases.yaml");
    auto options = YAML::Clone(fixtures["base_options"]);
    options["source_manifest"] = source;
    options["input_manifest"] = alias;
    const auto document = conf::Config::from_string(YAML::Dump(options));
    EXPECT_THROW(cece::ParseFireConfig(document.root()), std::invalid_argument);
}

TEST(test_fire_config, GridRetainsDateLineAndPolarBoundsWithoutInventingAreas) {
    auto definition = MakeGridDefinition();
    definition.longitude_deg = {179.5};
    definition.latitude_deg = {89.5};
    definition.longitude_bounds_deg = {{{179.0, -180.0, -180.0, 179.0}}};
    definition.latitude_bounds_deg = {{{89.0, 89.0, 90.0, 90.0}}};
    const cece::FireGrid grid(definition);
    EXPECT_EQ(grid.CellCount(), 1);
    EXPECT_DOUBLE_EQ(grid.Definition().area_m2[0], 25.0);
    definition.longitude_convention = cece::FireLongitudeConvention::ZeroTo360;
    definition.longitude_deg = {359.5};
    definition.longitude_bounds_deg = {{{359.0, 360.0, 360.0, 359.0}}};
    EXPECT_NO_THROW(cece::FireGrid{definition});
}

TEST(test_fire_config, GridRejectsMissingIdentityShapeCrsAndInvalidGeometry) {
    auto invalid = MakeGridDefinition();
    invalid.grid_sha256.clear();
    EXPECT_THROW(cece::FireGrid{invalid}, std::invalid_argument);
    invalid = MakeGridDefinition();
    invalid.nx = 0;
    EXPECT_THROW(cece::FireGrid{invalid}, std::invalid_argument);
    invalid.nx = std::numeric_limits<std::size_t>::max();
    invalid.ny = 2;
    EXPECT_THROW(cece::FireGrid{invalid}, std::invalid_argument);
    invalid = MakeGridDefinition();
    invalid.longitude_bounds_deg.clear();
    EXPECT_THROW(cece::FireGrid{invalid}, std::invalid_argument);
    invalid = MakeGridDefinition();
    invalid.area_m2[0] = 0.0;
    EXPECT_THROW(cece::FireGrid{invalid}, std::invalid_argument);
    invalid.area_m2[0] = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(cece::FireGrid{invalid}, std::invalid_argument);
    invalid = MakeGridDefinition();
    invalid.latitude_deg[0] = 90.1;
    EXPECT_THROW(cece::FireGrid{invalid}, std::invalid_argument);
    invalid = MakeGridDefinition();
    invalid.longitude_bounds_deg[0][0] = 181.0;
    EXPECT_THROW(cece::FireGrid{invalid}, std::invalid_argument);
    invalid = MakeGridDefinition();
    invalid.crs = "unknown";
    EXPECT_THROW(cece::FireGrid{invalid}, std::invalid_argument);
    invalid = MakeGridDefinition();
    invalid.longitude_convention = cece::FireLongitudeConvention::Unspecified;
    EXPECT_THROW(cece::FireGrid{invalid}, std::invalid_argument);
    auto identity = MakeIdentityMetadata();
    identity.grid_definition.reset();
    EXPECT_THROW(cece::FireInputIdentity{identity}, std::invalid_argument);
}

TEST(test_fire_config, InventoryPreservesComponentsAndOnlyReportsIndividualSpeciesPools) {
    const cece::FireBulkInventory inventory(MakeInventoryData());
    EXPECT_DOUBLE_EQ(inventory.ComponentMassKg(0, cece::FireBulkSpecies::VOC, 0), 3.0);
    EXPECT_DOUBLE_EQ(inventory.ComponentMassKg(1, cece::FireBulkSpecies::VOC, 0), 4.0);
    EXPECT_DOUBLE_EQ(inventory.BulkMassKg(cece::FireBulkSpecies::VOC, 0), 7.0);
    EXPECT_DOUBLE_EQ(inventory.SpeciesBudgetKg(cece::FireBulkSpecies::CO2), 3.0);
    EXPECT_NE(inventory.Data().component_profiles.at("component-a").composition.content_sha256,
              inventory.Data().component_profiles.at("component-b").composition.content_sha256);
    EXPECT_FALSE(inventory.Data().fre_mj.has_value());
    EXPECT_FALSE(inventory.Data().dry_matter_kg.has_value());
    EXPECT_THROW(inventory.BulkMassKg(cece::FireBulkSpecies::NOX, 0), std::invalid_argument);
    EXPECT_THROW(inventory.ComponentMassKg(2, cece::FireBulkSpecies::VOC, 0), std::out_of_range);
    EXPECT_THROW(inventory.BulkMassKg(cece::FireBulkSpecies::VOC, 1), std::out_of_range);
}

TEST(test_fire_config, ElevenSpeciesRetainTheirOwnComponentAndBulkMasses) {
    auto data = MakeInventoryData();
    data.species.clear();
    for (const auto* name : {"CO2", "CO", "CH4", "PM25", "TPM", "SO2", "OC", "BC", "NOX", "NH3", "VOC"}) {
        data.species.push_back(cece::FireSpeciesFromName(name));
    }
    data.mass_kg.clear();
    for (double fraction : {0.25, 0.75}) {
        for (std::size_t s = 0; s < data.species.size(); ++s) {
            data.mass_kg.push_back(fraction * (s + 1));
        }
    }
    data.nox_reference_basis = cece::FireNoxReferenceBasis::NO2;
    data.particulate_overlap =
        cece::FireParticulateOverlap{{"synthetic-pools", "1", std::string(64, 'e')},
                                     {{cece::FireBulkSpecies::PM25, "Independent synthetic fine particulate quantity; overlaps other pools"},
                                      {cece::FireBulkSpecies::TPM, "Independent synthetic total particulate quantity; overlaps other pools"},
                                      {cece::FireBulkSpecies::OC, "Independent synthetic organic carbon quantity; overlaps other pools"},
                                      {cece::FireBulkSpecies::BC, "Independent synthetic black carbon quantity; overlaps other pools"}}};
    const cece::FireBulkInventory inventory(data);
    for (std::size_t s = 0; s < data.species.size(); ++s) {
        const double expected = s + 1;
        EXPECT_DOUBLE_EQ(inventory.ComponentMassKg(0, data.species[s], 0), 0.25 * expected);
        EXPECT_DOUBLE_EQ(inventory.ComponentMassKg(1, data.species[s], 0), 0.75 * expected);
        EXPECT_DOUBLE_EQ(inventory.BulkMassKg(data.species[s], 0), expected);
    }
}

TEST(test_fire_config, InventoryRejectsIncompleteCompositionSpeciesAndMass) {
    auto invalid = MakeInventoryData();
    invalid.component_ids[1] = invalid.component_ids[0];
    EXPECT_THROW(cece::FireBulkInventory{invalid}, std::invalid_argument);
    invalid = MakeInventoryData();
    invalid.component_profiles.erase("component-b");
    EXPECT_THROW(cece::FireBulkInventory{invalid}, std::invalid_argument);
    invalid = MakeInventoryData();
    invalid.component_profiles["component-b"].composition.content_sha256 = "wrong";
    EXPECT_THROW(cece::FireBulkInventory{invalid}, std::invalid_argument);
    invalid = MakeInventoryData();
    invalid.species[1] = invalid.species[0];
    EXPECT_THROW(cece::FireBulkInventory{invalid}, std::invalid_argument);
    invalid = MakeInventoryData();
    invalid.mass_kg.pop_back();
    EXPECT_THROW(cece::FireBulkInventory{invalid}, std::invalid_argument);
    invalid = MakeInventoryData();
    invalid.mass_kg[0] = -1;
    EXPECT_THROW(cece::FireBulkInventory{invalid}, std::invalid_argument);
    invalid.mass_kg[0] = std::numeric_limits<double>::infinity();
    EXPECT_THROW(cece::FireBulkInventory{invalid}, std::invalid_argument);
    invalid = MakeInventoryData();
    invalid.mass_kg[0] = std::numeric_limits<double>::max();
    invalid.mass_kg[2] = std::numeric_limits<double>::max();
    EXPECT_THROW(cece::FireBulkInventory{invalid}, std::invalid_argument);
    invalid = MakeInventoryData();
    invalid.voc_profile.reset();
    EXPECT_THROW(cece::FireBulkInventory{invalid}, std::invalid_argument);
    invalid = MakeInventoryData();
    invalid.species[0] = cece::FireBulkSpecies::NOX;
    EXPECT_THROW(cece::FireBulkInventory{invalid}, std::invalid_argument);
    invalid.nox_reference_basis = cece::FireNoxReferenceBasis::NO2;
    EXPECT_NO_THROW(cece::FireBulkInventory{invalid});
}

TEST(test_fire_config, InventoryDistinguishesUnavailableAndValidZeroWithCoverageAndProvenance) {
    auto data = MakeInventoryData();
    data.quality_state[0] = cece::FireQualityState{};
    EXPECT_THROW(cece::FireBulkInventory{data}, std::invalid_argument);
    data.quality_state[0] = cece::FireQualityState::Unavailable;
    EXPECT_THROW(cece::FireBulkInventory{data}, std::invalid_argument);
    data.quality_state[0] = cece::FireQualityState::ValidZero;
    EXPECT_THROW(cece::FireBulkInventory{data}, std::invalid_argument);
    data.mass_kg.assign(4, 0.0);
    EXPECT_NO_THROW(cece::FireBulkInventory{data});
    data.coverage_fraction[0] = 0.0;
    EXPECT_THROW(cece::FireBulkInventory{data}, std::invalid_argument);
    data.coverage_fraction[0] = 1.01;
    EXPECT_THROW(cece::FireBulkInventory{data}, std::invalid_argument);
    data = MakeInventoryData();
    data.accepted_source_sha256.clear();
    EXPECT_THROW(cece::FireBulkInventory{data}, std::invalid_argument);
    data = MakeInventoryData();
    data.dry_matter_kg = std::vector<double>{-1.0};
    EXPECT_THROW(cece::FireBulkInventory{data}, std::invalid_argument);
    data.dry_matter_kg = std::vector<double>{100.0};
    data.fre_mj = std::vector<double>{200.0};
    EXPECT_NO_THROW(cece::FireBulkInventory{data});
}

TEST(test_fire_config, InventoryRequiresExplicitDefinitionsForOverlappingParticulatePools) {
    auto data = MakeInventoryData();
    data.species = {cece::FireBulkSpecies::PM25, cece::FireBulkSpecies::OC};
    data.mass_kg = {3.0, 1.0, 5.0, 2.0};
    EXPECT_THROW(cece::FireBulkInventory{data}, std::invalid_argument);
    data.particulate_overlap = cece::FireParticulateOverlap{{"synthetic-pools", "1", std::string(64, 'e')},
                                                            {{cece::FireBulkSpecies::PM25, "Total fine particulate mass"},
                                                             {cece::FireBulkSpecies::OC, "Organic carbon included within fine particulate mass"}}};
    const cece::FireBulkInventory inventory(data);
    EXPECT_DOUBLE_EQ(inventory.SpeciesBudgetKg(cece::FireBulkSpecies::PM25), 8.0);
    EXPECT_DOUBLE_EQ(inventory.SpeciesBudgetKg(cece::FireBulkSpecies::OC), 3.0);
    data.particulate_overlap->pool_definitions.erase(cece::FireBulkSpecies::OC);
    EXPECT_THROW(cece::FireBulkInventory{data}, std::invalid_argument);
}

TEST(test_fire_config, SpeciesNamesAreTheExactElevenCanonicalIdentifiers) {
    for (const auto* name : {"CO2", "CO", "CH4", "PM25", "TPM", "SO2", "OC", "BC", "NOX", "NH3", "VOC"}) {
        EXPECT_EQ(cece::FireSpeciesName(cece::FireSpeciesFromName(name)), name);
    }
    for (const auto* invalid : {"PM2.5", "NOx", "co2", "ISOP", ""}) {
        EXPECT_THROW(cece::FireSpeciesFromName(invalid), std::invalid_argument);
    }
}

TEST(test_fire_config, UtcTimeRequiresExactValidGregorianUtc) {
    for (const auto* invalid :
         {"", "2022-07-01T00:00:00", "2022-07-01T00:00:00+00:00", "2022-07-01T00:00:00Zsuffix", "2022-02-29T00:00:00Z", "2024-02-30T00:00:00Z",
          "1900-02-29T00:00:00Z", "2022-13-01T00:00:00Z", "2022-00-01T00:00:00Z", "2022-07-00T00:00:00Z", "2022-07-01T24:00:00Z",
          "2022-07-01T00:60:00Z", "2022-07-01T00:00:60Z", "0000-01-01T00:00:00Z", "202x-07-01T00:00:00Z", "2022-07-01 00:00:00Z"}) {
        EXPECT_THROW(cece::FireUtcTime{invalid}, std::invalid_argument) << invalid;
    }
    for (const auto* valid :
         {"0001-01-01T00:00:00Z", "1969-12-31T23:59:59Z", "2000-02-29T00:00:00Z", "2024-02-29T23:59:59Z", "9999-12-31T23:59:59Z"}) {
        EXPECT_EQ(cece::FireUtcTime(valid).ToString(), valid);
    }
    EXPECT_EQ(cece::FireUtcTime("1970-01-01T00:00:00Z").EpochSeconds(), 0);
    EXPECT_EQ(cece::FireUtcTime("1969-12-31T23:59:59Z").EpochSeconds(), -1);
}

TEST(test_fire_config, IdentityKeepsVersionsSeparateAndUsesCutoffForAge) {
    const cece::FireInputIdentity identity(MakeIdentityMetadata());
    EXPECT_EQ(identity.DurationSeconds(), 3600);
    EXPECT_EQ(identity.ObservationAgeSeconds(), 6300);
    EXPECT_NE(identity.Metadata().product_version, identity.Metadata().algorithm_version);
    EXPECT_EQ(identity.Metadata().cutoff_mode, cece::FireCutoffMode::Fixed);
    auto metadata = MakeIdentityMetadata();
    metadata.cutoff_mode = cece::FireCutoffMode::HostSupplied;
    EXPECT_NO_THROW(cece::FireInputIdentity{metadata});
    metadata.acquired_at = cece::FireUtcTime("2022-07-01T01:15:00Z");
    // Retrospective acquisition may follow interval end, but never its cutoff.
    EXPECT_NO_THROW(cece::FireInputIdentity{metadata});
}

TEST(test_fire_config, IdentityRejectsMissingProvenanceInvalidIntervalsAndLateSources) {
    auto invalid = MakeIdentityMetadata();
    invalid.schema_version = 2;
    EXPECT_THROW(cece::FireInputIdentity{invalid}, std::invalid_argument);
    for (auto member : {&cece::FireIdentityMetadata::source_id, &cece::FireIdentityMetadata::product_version,
                        &cece::FireIdentityMetadata::algorithm_version, &cece::FireIdentityMetadata::delivery_id}) {
        invalid = MakeIdentityMetadata();
        invalid.*member = " ";
        EXPECT_THROW(cece::FireInputIdentity{invalid}, std::invalid_argument);
    }
    for (const auto* digest : {"", "abc", "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz"}) {
        invalid = MakeIdentityMetadata();
        invalid.content_sha256 = digest;
        EXPECT_THROW(cece::FireInputIdentity{invalid}, std::invalid_argument);
    }
    invalid = MakeIdentityMetadata();
    invalid.cutoff_mode = cece::FireCutoffMode::Unspecified;
    EXPECT_THROW(cece::FireInputIdentity{invalid}, std::invalid_argument);
    invalid = MakeIdentityMetadata();
    invalid.interval_end = invalid.interval_start;
    EXPECT_THROW(cece::FireInputIdentity{invalid}, std::invalid_argument);
    invalid.interval_end = cece::FireUtcTime("2022-06-30T23:59:59Z");
    EXPECT_THROW(cece::FireInputIdentity{invalid}, std::invalid_argument);
    invalid = MakeIdentityMetadata();
    invalid.available_at = cece::FireUtcTime("2022-07-01T02:00:01Z");
    EXPECT_THROW(cece::FireInputIdentity{invalid}, std::invalid_argument);
    invalid = MakeIdentityMetadata();
    invalid.acquired_at = cece::FireUtcTime("2022-07-01T02:00:01Z");
    EXPECT_THROW(cece::FireInputIdentity{invalid}, std::invalid_argument);
    invalid = MakeIdentityMetadata();
    invalid.available_at = cece::FireUtcTime("2022-07-01T00:00:00Z");
    EXPECT_THROW(cece::FireInputIdentity{invalid}, std::invalid_argument);
}

TEST(test_fire_config, IdentityReportsTheRejectedSourceAndField) {
    auto invalid = MakeIdentityMetadata();
    invalid.content_sha256 = "wrong";
    try {
        const cece::FireInputIdentity identity(invalid);
        FAIL() << "Invalid digest accepted";
    } catch (const std::invalid_argument& error) {
        EXPECT_NE(std::string(error.what()).find("synthetic-source"), std::string::npos);
        EXPECT_NE(std::string(error.what()).find("content_sha256"), std::string::npos);
    }
}

TEST(test_fire_config, SyntheticFixtureDeclaresIndependentExpectations) {
    const auto fixture = YAML::LoadFile(std::string(CECE_SOURCE_DIR) + "/tests/data/fire/synthetic_interval.yaml");
    EXPECT_EQ(fixture["schema_version"].as<int>(), 1);
    EXPECT_EQ(fixture["kind"].as<std::string>(), "synthetic");
    EXPECT_FALSE(fixture["production_use"].as<bool>());
    EXPECT_TRUE(fixture["provenance"]["redistribution"]["permitted"].as<bool>());
    EXPECT_FALSE(fixture["provenance"]["independent_reference"].as<std::string>().empty());
    ASSERT_TRUE(fixture["expected"].IsSequence());
    EXPECT_EQ(fixture["expected"].size(), 5);
    for (const auto& expected : fixture["expected"]) {
        EXPECT_FALSE(expected["method"].as<std::string>().empty());
        EXPECT_FALSE(expected["units"].as<std::string>().empty());
        EXPECT_LE(expected["relative_tolerance"].as<double>(), 1.0e-6);
        EXPECT_LE(expected["zero_absolute_tolerance"].as<double>(), 1.0e-12);
    }
}

TEST(test_fire_config, RegistryDoesNotClaimUnverifiedProductionTargets) {
    const auto registry = YAML::LoadFile(std::string(CECE_SOURCE_DIR) + "/data/fire/asset_registry.yaml");
    EXPECT_FALSE(registry["production_support"].as<bool>());
    std::set<std::string> ids;
    std::set<std::string> targets;
    std::set<std::string> categories;
    for (const auto& asset : registry["assets"]) {
        EXPECT_TRUE(ids.insert(asset["asset_id"].as<std::string>()).second);
        categories.insert(asset["category"].as<std::string>());
        if (asset["kind"].as<std::string>() == "synthetic") {
            EXPECT_EQ(asset["status"].as<std::string>(), "test_only");
            EXPECT_FALSE(asset["production_use"].as<bool>());
        } else {
            EXPECT_EQ(asset["status"].as<std::string>(), "blocked");
            EXPECT_FALSE(asset["blocker"].as<std::string>().empty());
        }
        if (asset["category"].as<std::string>() == "target") {
            targets.insert(asset["target_id"].as<std::string>());
        }
    }
    EXPECT_EQ(targets, (std::set<std::string>{"AM4", "CB6", "CRACMM2", "RACM2", "GOCART2G"}));
    for (const auto* category : {"product", "sensor", "ancillary", "algorithm", "target", "synthetic_fixture"}) {
        EXPECT_EQ(categories.count(category), 1);
    }
}
