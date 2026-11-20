// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

#include "cece/physics/cece_fire.hpp"

TEST(test_fire_speciation, LoadsTypedSyntheticProfileAndVerifiesEveryReferencedAsset) {
    const std::string profile = std::string(CECE_SOURCE_DIR) + "/tests/data/fire/synthetic_target_profile.yaml";
    const auto loaded = cece::LoadFireTargetProfile(profile, "SYNTHETIC", "test-v1");
    EXPECT_EQ(loaded.identity.id, "synthetic-fire-profile");
    EXPECT_EQ(loaded.identity.version, "test-v1");
    EXPECT_EQ(loaded.target_id, "SYNTHETIC");
    EXPECT_EQ(loaded.mechanism_species.size(), 4);
    EXPECT_EQ(loaded.asset_hashes.size(), 1);
    EXPECT_EQ(loaded.mappings.size(), 4);
    EXPECT_EQ(loaded.dispositions.size(), 11);
    EXPECT_FALSE(loaded.production_eligible);
    ASSERT_TRUE(loaded.nox_partition);
    EXPECT_DOUBLE_EQ(loaded.nox_partition->no_mass_fraction, 0.25);
    EXPECT_DOUBLE_EQ(loaded.nox_partition->no2_mass_fraction, 0.75);
    EXPECT_EQ(loaded.oc_om_policy.kind, cece::FireOcOmPolicyKind::ConvertWithFactor);
    EXPECT_DOUBLE_EQ(loaded.oc_om_policy.om_per_oc, 1.4);
    EXPECT_DOUBLE_EQ(loaded.budget_tolerances.relative, 1e-6);
    EXPECT_EQ(loaded.asset_hashes.at("synthetic-factor-note"),
              cece::FireFileSha256(std::string(CECE_SOURCE_DIR) + "/tests/data/fire/synthetic_target_asset.txt"));
    EXPECT_EQ(loaded.identity.content_sha256, cece::FireFileSha256(profile));
}

TEST(test_fire_speciation, ProfileLoaderRejectsUnverifiedAssetsAndTargetRevisionMismatch) {
    const std::string source = std::string(CECE_SOURCE_DIR) + "/tests/data/fire/synthetic_target_profile.yaml";
    EXPECT_THROW(cece::LoadFireTargetProfile(source, "CB6", "test-v1"), std::invalid_argument);
    EXPECT_THROW(cece::LoadFireTargetProfile(source, "SYNTHETIC", "wrong-version"), std::invalid_argument);
    char name[] = "/tmp/cece-fire-profile-XXXXXX";
    const int descriptor = mkstemp(name);
    ASSERT_GE(descriptor, 0);
    close(descriptor);
    struct Cleanup {
        const char* path;
        ~Cleanup() {
            std::remove(path);
        }
    } cleanup{name};
    std::ifstream input(source);
    auto tampered = YAML::Load(input);
    tampered["assets"][0]["path"] = (std::filesystem::path(source).parent_path() / "synthetic_target_asset.txt").string();
    tampered["assets"][0]["sha256"] = std::string(64, '0');
    std::ofstream(name) << YAML::Dump(tampered);
    EXPECT_THROW(cece::LoadFireTargetProfile(name, "SYNTHETIC", "test-v1"), std::invalid_argument);
}

TEST(test_fire_speciation, ProfileLoaderRejectsIncompleteOrDimensionallyInvalidDeclarations) {
    const std::string source = std::string(CECE_SOURCE_DIR) + "/tests/data/fire/synthetic_target_profile.yaml";
    const auto asset = (std::filesystem::path(source).parent_path() / "synthetic_target_asset.txt").string();
    const auto rejects = [&](const std::function<void(YAML::Node&)>& mutate) {
        char name[] = "/tmp/cece-fire-profile-invalid-XXXXXX";
        const int descriptor = mkstemp(name);
        EXPECT_GE(descriptor, 0);
        if (descriptor < 0) return;
        close(descriptor);
        struct Cleanup {
            const char* path;
            ~Cleanup() { std::remove(path); }
        } cleanup{name};
        auto profile = YAML::LoadFile(source);
        profile["assets"][0]["path"] = asset;
        mutate(profile);
        std::ofstream(name) << YAML::Dump(profile);
        EXPECT_THROW(cece::LoadFireTargetProfile(name, "SYNTHETIC", "test-v1"), std::invalid_argument);
    };
    rejects([](YAML::Node& profile) { profile["production_eligible"] = true; });
    rejects([](YAML::Node& profile) { profile["source_disposition"].remove("BC"); });
    rejects([](YAML::Node& profile) { profile["typed_mappings"][0]["target_species"] = "UNKNOWN"; });
    rejects([](YAML::Node& profile) { profile["typed_mappings"][0]["output_unit"] = "kmol"; });
    rejects([](YAML::Node& profile) { profile["nox_partition"]["no2_mass_fraction"] = 0.5; });
    rejects([](YAML::Node& profile) { profile["oc_om_policy"].remove("om_per_oc"); });
    rejects([](YAML::Node& profile) { profile["budgets_tolerances"]["relative"] = 2.0; });
    rejects([](YAML::Node& profile) { profile["source_disposition"]["CO"]["kind"] = "excluded"; });
}

TEST(test_fire_speciation, LoadsVersionedUtcAndLocalSolarTemporalProfiles) {
    const auto root = std::string(CECE_SOURCE_DIR) + "/tests/data/fire/";
    const auto utc = cece::LoadFireTemporalAllocationProfile(root + "temporal_profile_utc.yaml");
    EXPECT_EQ(utc.identity.id, "synthetic-utc-diurnal");
    EXPECT_EQ(utc.time_basis, cece::FireTemporalProfileTimeBasis::Utc);
    EXPECT_EQ(utc.source_cadence_seconds, 7200);
    ASSERT_EQ(utc.segments.size(), 2);
    EXPECT_EQ(utc.identity.content_sha256, cece::FireFileSha256(root + "temporal_profile_utc.yaml"));

    const auto solar = cece::LoadFireTemporalAllocationProfile(root + "temporal_profile_local_solar.yaml");
    EXPECT_EQ(solar.time_basis, cece::FireTemporalProfileTimeBasis::LocalSolar);
    EXPECT_EQ(solar.source_cadence_seconds, 86400);
    ASSERT_EQ(solar.segments.size(), 2);

    char name[] = "/tmp/cece-fire-time-profile-XXXXXX";
    const int descriptor = mkstemp(name);
    ASSERT_GE(descriptor, 0);
    close(descriptor);
    struct Cleanup {
        const char* path;
        ~Cleanup() { std::remove(path); }
    } cleanup{name};
    auto tampered = YAML::LoadFile(root + "temporal_profile_utc.yaml");
    tampered["weights"][1]["offset_seconds"] = 3500;
    std::ofstream(name) << YAML::Dump(tampered);
    EXPECT_THROW(cece::LoadFireTemporalAllocationProfile(name), std::invalid_argument);
}

TEST(test_fire_speciation, SyntheticMapperPreservesComponentsAndTypedMassBudgets) {
    auto grid = std::make_shared<const cece::FireGrid>(cece::FireGridDefinition{
        "mapper-grid", std::string(64, 'b'), 2, 1, {0, 1}, {0, 0}, {{{-1, 0, 0, -1}, {0, 1, 1, 0}}},
        {{{-1, -1, 1, 1}, {-1, -1, 1, 1}}}, {25, 75}, "EPSG:4326", cece::FireLongitudeConvention::Minus180To180});
    const cece::FireProfileIdentity biome{"synthetic-biome", "1", std::string(64, 'c')};
    const cece::FireProfileIdentity comp_a{"composition-a", "1", std::string(64, 'd')};
    const cece::FireProfileIdentity comp_b{"composition-b", "1", std::string(64, 'e')};
    cece::FireBulkInventory inventory({
        cece::FireInputIdentity({1, "synthetic", "product", "algorithm", "delivery", std::string(64, 'a'),
                                cece::FireUtcTime("2022-07-01T00:00:00Z"), cece::FireUtcTime("2022-07-01T01:00:00Z"),
                                cece::FireUtcTime("2022-07-01T00:00:00Z"), cece::FireUtcTime("2022-07-01T00:00:00Z"),
                                cece::FireUtcTime("2022-07-01T02:00:00Z"), cece::FireCutoffMode::Fixed, grid}),
        {cece::FireBulkSpecies::CO, cece::FireBulkSpecies::NOX, cece::FireBulkSpecies::VOC}, {"a", "b"},
        {{"a", {biome, comp_a}}, {"b", {biome, comp_b}}},
        {10, 20, 2, 4, 8, 12, 30, 40, 6, 8, 16, 20}, {cece::FireQualityState::Observed, cece::FireQualityState::Observed},
        {1, 0.5}, {std::string(64, 'a')}, cece::FireNoxReferenceBasis::NO2, cece::FireProfileIdentity{"voc", "1", std::string(64, 'f')},
        std::nullopt, std::nullopt, std::nullopt});
    const auto profile = cece::LoadFireTargetProfile(std::string(CECE_SOURCE_DIR) + "/tests/data/fire/synthetic_target_profile.yaml",
                                                     "SYNTHETIC", "test-v1");
    const auto mapped = cece::MapFireBulkInventory(inventory, profile,
                                                   {cece::FireBulkSpecies::CO, cece::FireBulkSpecies::NOX, cece::FireBulkSpecies::VOC});
    EXPECT_EQ(mapped.component_ids, (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(mapped.component_profiles.at("b").composition.id, "composition-b");
    ASSERT_EQ(mapped.fields.size(), 4);
    EXPECT_EQ(mapped.fields[0].target_species, "FIRE_CO");
    EXPECT_EQ(mapped.fields[0].unit, "kg");
    EXPECT_EQ(mapped.fields[1].target_species, "FIRE_NO");
    EXPECT_EQ(mapped.fields[2].target_species, "FIRE_NO2");
    EXPECT_EQ(mapped.fields[3].target_species, "FIRE_VOC");
    ASSERT_EQ(mapped.values.size(), 16);
    const auto field_index = [&](std::string_view name) {
        for (std::size_t i = 0; i < mapped.fields.size(); ++i)
            if (mapped.fields[i].target_species == name) return i;
        throw std::runtime_error("missing mapped field");
    };
    EXPECT_DOUBLE_EQ(mapped.values[(field_index("FIRE_CO")) * 2], 10);
    EXPECT_DOUBLE_EQ(mapped.values[((1 * 4 + field_index("FIRE_CO")) * 2)], 30);
    EXPECT_DOUBLE_EQ(mapped.values[(field_index("FIRE_NO")) * 2], 0.5);
    EXPECT_DOUBLE_EQ(mapped.values[(field_index("FIRE_NO2")) * 2], 1.5);
    EXPECT_DOUBLE_EQ(mapped.values[((1 * 4 + field_index("FIRE_VOC")) * 2)], 16);
    EXPECT_EQ(mapped.budgets.size(), mapped.fields.size());
    EXPECT_DOUBLE_EQ(mapped.budgets[field_index("FIRE_NO")].output_quantity, 5);
    EXPECT_DOUBLE_EQ(mapped.budgets[field_index("FIRE_VOC")].output_quantity, 56);
}

TEST(test_fire_speciation, CompositionSpecificMappingsSelectExactComponentProfiles) {
    const std::string source = std::string(CECE_SOURCE_DIR) + "/tests/data/fire/synthetic_target_profile.yaml";
    const std::string asset = (std::filesystem::path(source).parent_path() / "synthetic_target_asset.txt").string();
    auto profile_yaml = YAML::LoadFile(source);
    profile_yaml["assets"][0]["path"] = asset;
    profile_yaml["mechanism_species"].push_back(YAML::Load("{name: FIRE_CO_A, molecular_weight_g_mol: 28.0}"));
    profile_yaml["mechanism_species"].push_back(YAML::Load("{name: FIRE_CO_B, molecular_weight_g_mol: 28.0}"));
    YAML::Node mappings(YAML::NodeType::Sequence);
    for (const auto& mapping : profile_yaml["typed_mappings"]) {
        if (mapping["source_species"].as<std::string>() != "CO") mappings.push_back(mapping);
    }
    mappings.push_back(YAML::Load(
        "{source_species: CO, target_species: FIRE_CO_A, basis: mass, factor: 1.0, output_unit: kg, "
        "composition_profile: {id: composition-a, version: '1', "
        "content_sha256: dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd}}"));
    mappings.push_back(YAML::Load(
        "{source_species: CO, target_species: FIRE_CO_B, basis: mass, factor: 1.0, output_unit: kg, "
        "composition_profile: {id: composition-b, version: '1', "
        "content_sha256: eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee}}"));
    profile_yaml["typed_mappings"] = mappings;

    char name[] = "/tmp/cece-fire-composition-profile-XXXXXX";
    const int descriptor = mkstemp(name);
    ASSERT_GE(descriptor, 0);
    close(descriptor);
    struct Cleanup {
        const char* path;
        ~Cleanup() { std::remove(path); }
    } cleanup{name};
    std::ofstream(name) << YAML::Dump(profile_yaml);
    const auto profile = cece::LoadFireTargetProfile(name, "SYNTHETIC", "test-v1");

    const auto grid = std::make_shared<const cece::FireGrid>(cece::FireGridDefinition{
        "composition-grid", std::string(64, 'b'), 2, 1, {0, 1}, {0, 0}, {{{-1, 0, 0, -1}, {0, 1, 1, 0}}},
        {{{-1, -1, 1, 1}, {-1, -1, 1, 1}}}, {25, 75}, "EPSG:4326", cece::FireLongitudeConvention::Minus180To180});
    const cece::FireProfileIdentity biome{"synthetic-biome", "1", std::string(64, 'c')};
    const cece::FireProfileIdentity comp_a{"composition-a", "1", std::string(64, 'd')};
    const cece::FireProfileIdentity comp_b{"composition-b", "1", std::string(64, 'e')};
    cece::FireBulkInventory inventory({
        cece::FireInputIdentity({1, "synthetic", "product", "algorithm", "delivery", std::string(64, 'a'),
                                cece::FireUtcTime("2022-07-01T00:00:00Z"), cece::FireUtcTime("2022-07-01T01:00:00Z"),
                                cece::FireUtcTime("2022-07-01T00:00:00Z"), cece::FireUtcTime("2022-07-01T00:00:00Z"),
                                cece::FireUtcTime("2022-07-01T02:00:00Z"), cece::FireCutoffMode::Fixed, grid}),
        {cece::FireBulkSpecies::CO}, {"a", "b"}, {{"a", {biome, comp_a}}, {"b", {biome, comp_b}}},
        {10, 20, 30, 40}, {cece::FireQualityState::Observed, cece::FireQualityState::Observed}, {1, 1},
        {std::string(64, 'a')}, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt});
    const auto mapped = cece::MapFireBulkInventory(inventory, profile, {cece::FireBulkSpecies::CO});
    ASSERT_EQ(mapped.fields.size(), 2);
    EXPECT_EQ(mapped.fields[0].target_species, "FIRE_CO_A");
    EXPECT_EQ(mapped.fields[1].target_species, "FIRE_CO_B");
    EXPECT_DOUBLE_EQ(mapped.values[0], 10);
    EXPECT_DOUBLE_EQ(mapped.values[1], 20);
    EXPECT_DOUBLE_EQ(mapped.values[2], 0);
    EXPECT_DOUBLE_EQ(mapped.values[3], 0);
    EXPECT_DOUBLE_EQ(mapped.values[4], 0);
    EXPECT_DOUBLE_EQ(mapped.values[5], 0);
    EXPECT_DOUBLE_EQ(mapped.values[6], 30);
    EXPECT_DOUBLE_EQ(mapped.values[7], 40);

    auto unresolved_data = inventory.Data();
    unresolved_data.component_profiles.at("b").composition.id = "unlisted-composition";
    const cece::FireBulkInventory unresolved(std::move(unresolved_data));
    EXPECT_THROW(cece::MapFireBulkInventory(unresolved, profile, {cece::FireBulkSpecies::CO}), std::invalid_argument);
}

TEST(test_fire_speciation, MapperRejectsMissingSpeciesBasisMismatchAndUnmappedRequests) {
    auto grid = std::make_shared<const cece::FireGrid>(cece::FireGridDefinition{
        "mapper-grid", std::string(64, 'b'), 1, 1, {0}, {0}, {{{-1, 1, 1, -1}}}, {{{-1, -1, 1, 1}}}, {25}, "EPSG:4326",
        cece::FireLongitudeConvention::Minus180To180});
    const cece::FireProfileIdentity profile_identity{"synthetic", "1", std::string(64, 'c')};
    cece::FireBulkInventory inventory({
        cece::FireInputIdentity({1, "synthetic", "product", "algorithm", "delivery", std::string(64, 'a'),
                                cece::FireUtcTime("2022-07-01T00:00:00Z"), cece::FireUtcTime("2022-07-01T01:00:00Z"),
                                cece::FireUtcTime("2022-07-01T00:00:00Z"), cece::FireUtcTime("2022-07-01T00:00:00Z"),
                                cece::FireUtcTime("2022-07-01T02:00:00Z"), cece::FireCutoffMode::Fixed, grid}),
        {cece::FireBulkSpecies::CO}, {"component"}, {{"component", {profile_identity, profile_identity}}}, {1},
        {cece::FireQualityState::Observed}, {1}, {std::string(64, 'a')}, std::nullopt, std::nullopt, std::nullopt,
        std::nullopt, std::nullopt});
    const auto target = cece::LoadFireTargetProfile(std::string(CECE_SOURCE_DIR) + "/tests/data/fire/synthetic_target_profile.yaml",
                                                    "SYNTHETIC", "test-v1");
    EXPECT_THROW(cece::MapFireBulkInventory(inventory, target, {cece::FireBulkSpecies::NH3}), std::invalid_argument);
    EXPECT_THROW(cece::MapFireBulkInventory(inventory, target, {cece::FireBulkSpecies::CO2}), std::invalid_argument);
    auto wrong_basis = inventory.Data();
    wrong_basis.species.push_back(cece::FireBulkSpecies::NOX);
    wrong_basis.mass_kg.push_back(1);
    wrong_basis.nox_reference_basis = cece::FireNoxReferenceBasis::NO;
    cece::FireBulkInventory nox(std::move(wrong_basis));
    EXPECT_THROW(cece::MapFireBulkInventory(nox, target, {cece::FireBulkSpecies::CO, cece::FireBulkSpecies::NOX}),
                 std::invalid_argument);
    EXPECT_THROW(cece::MapFireBulkInventory(inventory, target, {}), std::invalid_argument);
}

TEST(test_fire_speciation, ArtificialReferenceSplitPreservesItsDeclaredMassPool) {
    const auto fixture = YAML::LoadFile(std::string(CECE_SOURCE_DIR) + "/tests/data/fire/synthetic_interval.yaml");
    ASSERT_FALSE(fixture["production_use"].as<bool>());
    const double voc_kg = fixture["expected"][2]["values"][10].as<double>();
    const auto factors = fixture["inputs"]["artificial_mass_mapping"];
    const auto expected = fixture["expected"][4]["values"];
    ASSERT_EQ(factors.size(), 2);
    ASSERT_EQ(expected.size(), 2);
    EXPECT_DOUBLE_EQ(factors[0].as<double>() + factors[1].as<double>(), 1.0);
    EXPECT_DOUBLE_EQ(voc_kg * factors[0].as<double>(), expected[0].as<double>());
    EXPECT_DOUBLE_EQ(voc_kg * factors[1].as<double>(), expected[1].as<double>());
    EXPECT_DOUBLE_EQ(expected[0].as<double>() + expected[1].as<double>(), voc_kg);
}
