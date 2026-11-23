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

TEST(test_fire_speciation, GeneratedCb6ProfilesRetainIndependentManifestCoefficients) {
    const auto root = std::string(CECE_SOURCE_DIR) + "/data/fire/";
    const auto factor_profile = cece::LoadFireTargetProfile(root + "cb6_fire_speciation_profile.yaml", "CB6", "cb6r5_ae7_aq");
    EXPECT_EQ(factor_profile.mapping_factor_semantics, cece::FireMappingFactorSemantics::IndependentCoefficients);
    EXPECT_FALSE(factor_profile.production_eligible);
    ASSERT_EQ(factor_profile.mappings.size(), 43);
    ASSERT_EQ(factor_profile.mechanism_species.size(), 43);

    double co_total = 0;
    double pm25_total = 0;
    bool zero_coefficient_preserved = false;
    for (const auto& mapping : factor_profile.mappings) {
        EXPECT_EQ(mapping.basis, cece::FireMappingBasis::Mass);
        EXPECT_EQ(mapping.output_unit, "kg");
        if (mapping.source_species == cece::FireBulkSpecies::CO) co_total += mapping.factor;
        if (mapping.source_species == cece::FireBulkSpecies::PM25) {
            pm25_total += mapping.factor;
            zero_coefficient_preserved |= mapping.target_species == "PMN" && mapping.factor == 0;
        }
    }
    EXPECT_NEAR(co_total, 1.1247, 1e-12);
    EXPECT_NEAR(pm25_total, 1.0, 1e-12);
    EXPECT_TRUE(zero_coefficient_preserved);
    EXPECT_EQ(factor_profile.dispositions.at(cece::FireBulkSpecies::CO).kind, cece::FireSourceDispositionKind::Mapped);
    EXPECT_EQ(factor_profile.dispositions.at(cece::FireBulkSpecies::PM25).kind, cece::FireSourceDispositionKind::Mapped);
    EXPECT_EQ(factor_profile.dispositions.at(cece::FireBulkSpecies::SO2).kind, cece::FireSourceDispositionKind::Excluded);

    const auto native_profile = cece::LoadFireTargetProfile(root + "cb6_fire_native_profile.yaml", "CB6", "cb6r5_ae7_aq");
    EXPECT_EQ(native_profile.mappings.size(), 7);
    EXPECT_EQ(native_profile.dispositions.at(cece::FireBulkSpecies::PM25).kind, cece::FireSourceDispositionKind::Excluded);
}

TEST(test_fire_speciation, ProposedGocart2gStage1MapsBulkWithoutParticulateDoubleCounting) {
    const auto path = std::string(CECE_SOURCE_DIR) + "/data/fire/ufschem_gocart2g_stage1_profile.yaml";
    const auto profile = cece::LoadFireTargetProfile(path, "GOCART2G", "ufschem-proposed-v1");
    EXPECT_FALSE(profile.production_eligible);
    auto grid = std::make_shared<const cece::FireGrid>(cece::FireGridDefinition{
        "stage1-grid", std::string(64, 'b'), 1, 1, {0}, {0}, {{{-1, 1, 1, -1}}}, {{{-1, -1, 1, 1}}}, {25}, "EPSG:4326",
        cece::FireLongitudeConvention::Minus180To180});
    const std::vector<cece::FireBulkSpecies> species{
        cece::FireBulkSpecies::BC, cece::FireBulkSpecies::OC, cece::FireBulkSpecies::SO2, cece::FireBulkSpecies::NH3,
        cece::FireBulkSpecies::PM25};
    for (const std::string source : {"RAVE-HrlyEmiss-13km", "GBBEPx-all01GRID"}) {
        const cece::FireBulkInventory inventory({
            cece::FireInputIdentity({1, source, "test", "test", "test", std::string(64, 'a'),
                                    cece::FireUtcTime("2022-07-01T00:00:00Z"), cece::FireUtcTime("2022-07-01T01:00:00Z"),
                                    cece::FireUtcTime("2022-07-01T00:00:00Z"), cece::FireUtcTime("2022-07-01T00:00:00Z"),
                                    cece::FireUtcTime("2022-07-01T01:00:00Z"), cece::FireCutoffMode::Fixed, grid}),
            species, {"product"}, {}, {10, 6, 4, 2, 20}, {cece::FireQualityState::Observed}, {1}, {std::string(64, 'a')},
            std::nullopt, std::nullopt, cece::FireParticulateOverlap{{"overlap", "1", std::string(64, 'c')},
                {{cece::FireBulkSpecies::PM25, "total"}, {cece::FireBulkSpecies::OC, "component"}, {cece::FireBulkSpecies::BC, "component"}}},
            std::nullopt, std::nullopt, {"product"}});
        const auto output = cece::MapFireBulkInventory(inventory, profile, {
            cece::FireBulkSpecies::BC, cece::FireBulkSpecies::OC, cece::FireBulkSpecies::SO2, cece::FireBulkSpecies::NH3});
        ASSERT_EQ(output.fields.size(), 6);
        const std::vector<std::string> names{"BCphobic", "BCphilic", "BRCphobic", "BRCphilic", "SO2", "NH3"};
        const std::vector<double> expected{8, 2, 3, 3, 4, 2};
        for (std::size_t i = 0; i < names.size(); ++i) {
            EXPECT_EQ(output.fields[i].target_species, names[i]);
            EXPECT_DOUBLE_EQ(output.values[i], expected[i]);
        }
        EXPECT_THROW(cece::MapFireBulkInventory(inventory, profile, {cece::FireBulkSpecies::PM25}), std::invalid_argument);
    }
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
    rejects([](YAML::Node& profile) { profile["mapping_factor_semantics"] = "normalize_coefficients"; });
}

TEST(test_fire_speciation, NoxMolarEquivalentPartitionConservesNitrogenRatherThanMass) {
    const std::string source = std::string(CECE_SOURCE_DIR) + "/tests/data/fire/synthetic_target_profile.yaml";
    auto yaml = YAML::LoadFile(source);
    yaml["assets"][0]["path"] = (std::filesystem::path(source).parent_path() / "synthetic_target_asset.txt").string();
    yaml["nox_partition"]["partition_basis"] = "molar_equivalent";
    yaml["nox_partition"]["no_mass_fraction"] = 0.9 * 30.0 / 46.0;
    yaml["nox_partition"]["no2_mass_fraction"] = 0.1;
    yaml["typed_mappings"][1]["factor"] = 0.9 * 30.0 / 46.0;
    yaml["typed_mappings"][2]["factor"] = 0.1;

    char name[] = "/tmp/cece-fire-nox-profile-XXXXXX";
    const int descriptor = mkstemp(name);
    ASSERT_GE(descriptor, 0);
    close(descriptor);
    struct Cleanup {
        const char* path;
        ~Cleanup() { std::remove(path); }
    } cleanup{name};
    const auto write_profile = [&] { std::ofstream(name) << YAML::Dump(yaml); };
    write_profile();
    const auto profile = cece::LoadFireTargetProfile(name, "SYNTHETIC", "test-v1");
    ASSERT_TRUE(profile.nox_partition);
    EXPECT_EQ(profile.nox_partition->partition_basis, cece::FireNoxPartitionBasis::MolarEquivalent);

    auto grid = std::make_shared<const cece::FireGrid>(cece::FireGridDefinition{
        "nox-grid", std::string(64, 'b'), 1, 1, {0}, {0}, {{{-1, 1, 1, -1}}}, {{{-1, -1, 1, 1}}}, {25}, "EPSG:4326",
        cece::FireLongitudeConvention::Minus180To180});
    const cece::FireProfileIdentity component{"synthetic", "1", std::string(64, 'c')};
    cece::FireBulkInventoryData data{
        cece::FireInputIdentity({1, "synthetic", "product", "algorithm", "delivery", std::string(64, 'a'),
                                 cece::FireUtcTime("2022-07-01T00:00:00Z"), cece::FireUtcTime("2022-07-01T01:00:00Z"),
                                 cece::FireUtcTime("2022-07-01T00:00:00Z"), cece::FireUtcTime("2022-07-01T00:00:00Z"),
                                 cece::FireUtcTime("2022-07-01T02:00:00Z"), cece::FireCutoffMode::Fixed, grid}),
        {cece::FireBulkSpecies::NOX}, {"component"}, {{"component", {component, component}}}, {46},
        {cece::FireQualityState::Observed}, {1}, {std::string(64, 'a')}, cece::FireNoxReferenceBasis::NO2, std::nullopt,
        std::nullopt, std::nullopt, std::nullopt};
    const auto mapped = cece::MapFireBulkInventory(cece::FireBulkInventory(data), profile, {cece::FireBulkSpecies::NOX});
    ASSERT_EQ(mapped.values.size(), 2);
    EXPECT_NEAR(mapped.values[0], 27.0, 1e-12);
    EXPECT_NEAR(mapped.values[1], 4.6, 1e-12);
    EXPECT_NEAR(mapped.values[0] / 30.0 + mapped.values[1] / 46.0, 1.0, 1e-12);
    data.nox_reference_basis = cece::FireNoxReferenceBasis::NO;
    EXPECT_THROW(cece::MapFireBulkInventory(cece::FireBulkInventory(data), profile, {cece::FireBulkSpecies::NOX}), std::invalid_argument);

    yaml["typed_mappings"][1]["factor"] = 0.9;
    yaml["nox_partition"]["no_mass_fraction"] = 0.9;
    write_profile();
    EXPECT_THROW(cece::LoadFireTargetProfile(name, "SYNTHETIC", "test-v1"), std::invalid_argument);
    yaml["nox_partition"]["reference_basis"] = "NO";
    write_profile();
    EXPECT_THROW(cece::LoadFireTargetProfile(name, "SYNTHETIC", "test-v1"), std::invalid_argument);
}

TEST(test_fire_speciation, IgbpSplittingPreservesSourceMassAndRejectsUnknownClasses) {
    const auto fixture = YAML::LoadFile(std::string(CECE_SOURCE_DIR) + "/tests/data/fire/synthetic_igbp_map.yaml");
    EXPECT_EQ(fixture["schema_version"].as<int>(), 1);
    EXPECT_FALSE(fixture["production_use"].as<bool>());
    EXPECT_EQ(fixture["axis_order"].as<std::vector<std::string>>(), (std::vector<std::string>{"latitude", "longitude"}));
    EXPECT_EQ(fixture["longitude_convention"].as<std::string>(), "minus_180_to_180");
    EXPECT_EQ(fixture["missing_policy"].as<std::string>(), "error");
    EXPECT_EQ(fixture["grid"]["longitude_deg"].as<std::vector<double>>(), (std::vector<double>{-65.1, -65.0}));
    EXPECT_EQ(fixture["grid"]["latitude_deg"].as<std::vector<double>>(), (std::vector<double>{-40.8, -40.7}));
    const auto rows = fixture["grid"]["class_ids"];
    std::vector<int> classes;
    for (const auto& row : rows) {
        for (const auto& value : row) classes.push_back(value.as<int>());
    }
    ASSERT_EQ(classes, (std::vector<int>{1, 10, 12, 14}));
    auto grid = std::make_shared<const cece::FireGrid>(cece::FireGridDefinition{
        "igbp-grid", std::string(64, 'b'), 2, 2, {-65.1, -65.0, -65.1, -65.0}, {-40.8, -40.8, -40.7, -40.7},
        {{{-65.15, -65.05, -65.05, -65.15}, {-65.05, -64.95, -64.95, -65.05},
          {-65.15, -65.05, -65.05, -65.15}, {-65.05, -64.95, -64.95, -65.05}}},
        {{{-40.85, -40.85, -40.75, -40.75}, {-40.85, -40.85, -40.75, -40.75},
          {-40.75, -40.75, -40.65, -40.65}, {-40.75, -40.75, -40.65, -40.65}}},
        {1, 1, 1, 1}, "EPSG:4326", cece::FireLongitudeConvention::Minus180To180});
    const cece::FireProfileIdentity biome{"igbp", "1", std::string(64, 'c')};
    const std::array<cece::FireComponentProfile, 3> profiles{{
        {biome, {"forest", "1", std::string(64, 'd')}},
        {biome, {"grassland", "1", std::string(64, 'e')}},
        {biome, {"cropland", "1", std::string(64, 'f')}},
    }};
    cece::FireBulkInventoryData data{
        cece::FireInputIdentity({1, "synthetic", "product", "algorithm", "delivery", std::string(64, 'a'),
                                 cece::FireUtcTime("2022-07-01T00:00:00Z"), cece::FireUtcTime("2022-07-01T01:00:00Z"),
                                 cece::FireUtcTime("2022-07-01T00:00:00Z"), cece::FireUtcTime("2022-07-01T00:00:00Z"),
                                 cece::FireUtcTime("2022-07-01T02:00:00Z"), cece::FireCutoffMode::Fixed, grid}),
        {cece::FireBulkSpecies::CO}, {"product"}, {}, {1, 2, 3, 4},
        std::vector<cece::FireQualityState>(4, cece::FireQualityState::Observed), {1, 1, 1, 1}, {std::string(64, 'a')},
        std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, {"product"}};
    const cece::FireBulkInventory original(data);
    cece::FireIgbpGrid igbp{std::string(64, 'b'), biome, classes};
    const auto split = cece::SplitFireProductByIgbp(original, igbp, profiles);
    ASSERT_EQ(split.Data().component_ids.size(), 3);
    EXPECT_EQ(split.SpeciesBudgetKg(cece::FireBulkSpecies::CO), 10);
    EXPECT_EQ(split.ComponentMassKg(0, cece::FireBulkSpecies::CO, 0), 1);
    EXPECT_EQ(split.ComponentMassKg(1, cece::FireBulkSpecies::CO, 1), 2);
    EXPECT_EQ(split.ComponentMassKg(2, cece::FireBulkSpecies::CO, 2), 3);
    EXPECT_EQ(split.ComponentMassKg(2, cece::FireBulkSpecies::CO, 3), 4);
    const auto classification = fixture["classification_case"];
    const auto category_ids = classification["class_ids"].as<std::vector<int>>();
    const auto expected_components = classification["expected_components"].as<std::vector<std::string>>();
    ASSERT_EQ(category_ids.size(), 17);
    ASSERT_EQ(expected_components.size(), category_ids.size());
    const std::array<std::string, 3> component_names{"forest", "grassland", "cropland"};
    for (std::size_t index = 0; index < category_ids.size(); ++index) {
        const int category = category_ids[index];
        ASSERT_EQ(category, static_cast<int>(index) + 1);
        igbp.classes.assign(4, category);
        if (expected_components[index] == "water") {
            EXPECT_EQ(category, 17);
            EXPECT_THROW(cece::SplitFireProductByIgbp(original, igbp, profiles), std::invalid_argument);
            continue;
        }
        const auto result = cece::SplitFireProductByIgbp(original, igbp, profiles);
        for (std::size_t component = 0; component < component_names.size(); ++component) {
            const double expected = component_names[component] == expected_components[index] ? 1.0 : 0.0;
            EXPECT_DOUBLE_EQ(result.ComponentMassKg(component, cece::FireBulkSpecies::CO, 0), expected) << category;
        }
        EXPECT_DOUBLE_EQ(result.SpeciesBudgetKg(cece::FireBulkSpecies::CO), 10.0) << category;
    }
    igbp.classes.assign(4, 1);
    igbp.classes[0] = 0;
    EXPECT_THROW(cece::SplitFireProductByIgbp(original, igbp, profiles), std::invalid_argument);
    auto zero_data = data;
    zero_data.mass_kg[0] = 0;
    zero_data.quality_state[0] = cece::FireQualityState::ValidZero;
    const cece::FireBulkInventory zero_inventory(std::move(zero_data));
    EXPECT_THROW(cece::SplitFireProductByIgbp(zero_inventory, igbp, profiles), std::invalid_argument);
    igbp.classes[0] = 17;
    const auto zero_split = cece::SplitFireProductByIgbp(zero_inventory, igbp, profiles);
    EXPECT_EQ(zero_split.Data().quality_state[0], cece::FireQualityState::ValidZero);
    for (std::size_t component = 0; component < 3; ++component) {
        EXPECT_DOUBLE_EQ(zero_split.ComponentMassKg(component, cece::FireBulkSpecies::CO, 0), 0);
    }
    EXPECT_DOUBLE_EQ(zero_split.SpeciesBudgetKg(cece::FireBulkSpecies::CO), 9);
    EXPECT_THROW(cece::SplitFireProductByIgbp(original, igbp, profiles), std::invalid_argument);
    igbp.classes = {17, 10, 12, 14};
    auto mixed_species_data = data;
    mixed_species_data.species.push_back(cece::FireBulkSpecies::SO2);
    mixed_species_data.mass_kg = {0, 2, 3, 4, 1, 0, 0, 0};
    EXPECT_THROW(cece::SplitFireProductByIgbp(cece::FireBulkInventory(mixed_species_data), igbp, profiles), std::invalid_argument);
    mixed_species_data.mass_kg[4] = 0;
    const auto observed_zero_split = cece::SplitFireProductByIgbp(cece::FireBulkInventory(mixed_species_data), igbp, profiles);
    EXPECT_EQ(observed_zero_split.Data().quality_state[0], cece::FireQualityState::Observed);
    for (std::size_t component = 0; component < 3; ++component) {
        EXPECT_DOUBLE_EQ(observed_zero_split.ComponentMassKg(component, cece::FireBulkSpecies::CO, 0), 0);
        EXPECT_DOUBLE_EQ(observed_zero_split.ComponentMassKg(component, cece::FireBulkSpecies::SO2, 0), 0);
    }
    auto target = cece::LoadFireTargetProfile(std::string(CECE_SOURCE_DIR) + "/tests/data/fire/synthetic_target_profile.yaml",
                                               "SYNTHETIC", "test-v1");
    target.mappings.clear();
    target.mapping_factor_semantics = cece::FireMappingFactorSemantics::IndependentCoefficients;
    for (std::size_t component = 0; component < profiles.size(); ++component) {
        target.mappings.push_back({cece::FireBulkSpecies::CO, "FIRE_CO", cece::FireMappingBasis::Mass,
                                   0.1 * (component + 1), "kg", profiles[component].composition});
    }
    const auto mapped = cece::MapFireBulkInventory(observed_zero_split, target, {cece::FireBulkSpecies::CO});
    ASSERT_EQ(mapped.fields.size(), 1);
    for (std::size_t component = 0; component < 3; ++component) {
        EXPECT_DOUBLE_EQ(mapped.values[component * 4], 0);
    }
    EXPECT_NEAR(mapped.budgets[0].output_quantity, 2.5, 1e-12);
    igbp.classes[0] = -1;
    EXPECT_THROW(cece::SplitFireProductByIgbp(original, igbp, profiles), std::invalid_argument);
    igbp.classes[0] = 18;
    EXPECT_THROW(cece::SplitFireProductByIgbp(original, igbp, profiles), std::invalid_argument);
    igbp.classes.pop_back();
    EXPECT_THROW(cece::SplitFireProductByIgbp(original, igbp, profiles), std::invalid_argument);
    igbp.classes.assign(4, 1);
    igbp.source_grid_sha256 = std::string(64, 'd');
    EXPECT_THROW(cece::SplitFireProductByIgbp(original, igbp, profiles), std::invalid_argument);
    igbp.source_grid_sha256 = std::string(64, 'b');
    EXPECT_THROW(cece::SplitFireProductByIgbp(split, igbp, profiles), std::invalid_argument);
}

TEST(test_fire_speciation, IndependentCoefficientsPreserveVerifiedNonUnitFactorSums) {
    const std::string source = std::string(CECE_SOURCE_DIR) + "/tests/data/fire/synthetic_target_profile.yaml";
    const auto asset = (std::filesystem::path(source).parent_path() / "synthetic_target_asset.txt").string();
    auto profile_yaml = YAML::LoadFile(source);
    profile_yaml["assets"][0]["path"] = asset;
    profile_yaml["mapping_factor_semantics"] = "independent_coefficients";
    profile_yaml["typed_mappings"][0]["factor"] = 1.1247;

    char name[] = "/tmp/cece-fire-independent-factors-XXXXXX";
    const int descriptor = mkstemp(name);
    ASSERT_GE(descriptor, 0);
    close(descriptor);
    struct Cleanup {
        const char* path;
        ~Cleanup() { std::remove(path); }
    } cleanup{name};
    std::ofstream(name) << YAML::Dump(profile_yaml);

    const auto profile = cece::LoadFireTargetProfile(name, "SYNTHETIC", "test-v1");
    EXPECT_EQ(profile.mapping_factor_semantics, cece::FireMappingFactorSemantics::IndependentCoefficients);
    EXPECT_DOUBLE_EQ(profile.mappings.front().factor, 1.1247);

    auto grid = std::make_shared<const cece::FireGrid>(cece::FireGridDefinition{
        "independent-factor-grid", std::string(64, 'b'), 1, 1, {0}, {0}, {{{-1, 1, 1, -1}}}, {{{-1, -1, 1, 1}}}, {25}, "EPSG:4326",
        cece::FireLongitudeConvention::Minus180To180});
    const cece::FireProfileIdentity component_profile{"synthetic", "1", std::string(64, 'c')};
    const cece::FireBulkInventory inventory({
        cece::FireInputIdentity({1, "synthetic", "product", "algorithm", "delivery", std::string(64, 'a'),
                                cece::FireUtcTime("2022-07-01T00:00:00Z"), cece::FireUtcTime("2022-07-01T01:00:00Z"),
                                cece::FireUtcTime("2022-07-01T00:00:00Z"), cece::FireUtcTime("2022-07-01T00:00:00Z"),
                                cece::FireUtcTime("2022-07-01T02:00:00Z"), cece::FireCutoffMode::Fixed, grid}),
        {cece::FireBulkSpecies::CO}, {"component"}, {{"component", {component_profile, component_profile}},},
        {2}, {cece::FireQualityState::Observed}, {1}, {std::string(64, 'a')}, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
        std::nullopt});
    const auto mapped = cece::MapFireBulkInventory(inventory, profile, {cece::FireBulkSpecies::CO});
    ASSERT_EQ(mapped.values.size(), 1);
    EXPECT_NEAR(mapped.values.front(), 2.2494, 1e-12);

    auto product_data = inventory.Data();
    product_data.component_profiles.clear();
    product_data.unprofiled_component_ids = {"component"};
    const cece::FireBulkInventory product_inventory(std::move(product_data));
    const auto product_mapped = cece::MapFireBulkInventory(product_inventory, profile, {cece::FireBulkSpecies::CO});
    EXPECT_NEAR(product_mapped.values.front(), 2.2494, 1e-12);
    EXPECT_EQ(product_mapped.unprofiled_component_ids, std::vector<std::string>{"component"});
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

    auto unprofiled_data = inventory.Data();
    unprofiled_data.component_profiles.clear();
    unprofiled_data.unprofiled_component_ids = {"a", "b"};
    const cece::FireBulkInventory unprofiled(std::move(unprofiled_data));
    EXPECT_THROW(cece::MapFireBulkInventory(unprofiled, profile, {cece::FireBulkSpecies::CO}), std::invalid_argument);

    profile_yaml["mapping_factor_semantics"] = "independent_coefficients";
    profile_yaml["typed_mappings"][3]["factor"] = 0.2;
    profile_yaml["typed_mappings"][4]["factor"] = 0.4;
    profile_yaml["typed_mappings"].push_back(YAML::Load(
        "{source_species: CO, target_species: FIRE_CO, basis: mass, factor: 1.0, output_unit: kg}"));
    std::ofstream(name) << YAML::Dump(profile_yaml);
    const auto mixed = cece::LoadFireTargetProfile(name, "SYNTHETIC", "test-v1");
    const auto mixed_output = cece::MapFireBulkInventory(inventory, mixed, {cece::FireBulkSpecies::CO});
    ASSERT_EQ(mixed_output.fields.size(), 3);
    EXPECT_EQ(mixed_output.fields[0].target_species, "FIRE_CO");
    EXPECT_DOUBLE_EQ(mixed_output.budgets[0].output_quantity, 100);
    EXPECT_NEAR(mixed_output.budgets[1].output_quantity, 6, 1e-12);
    EXPECT_NEAR(mixed_output.budgets[2].output_quantity, 28, 1e-12);
    EXPECT_THROW(cece::MapFireBulkInventory(unprofiled, mixed, {cece::FireBulkSpecies::CO}), std::invalid_argument);
    EXPECT_THROW(cece::MapFireBulkInventory(unresolved, mixed, {cece::FireBulkSpecies::CO}), std::invalid_argument);

    profile_yaml["typed_mappings"][3]["target_species"] = "FIRE_CO";
    std::ofstream(name) << YAML::Dump(profile_yaml);
    EXPECT_THROW(cece::LoadFireTargetProfile(name, "SYNTHETIC", "test-v1"), std::invalid_argument);
    profile_yaml["typed_mappings"][3]["target_species"] = "FIRE_CO_A";
    profile_yaml["mapping_factor_semantics"] = "partition";
    std::ofstream(name) << YAML::Dump(profile_yaml);
    EXPECT_THROW(cece::LoadFireTargetProfile(name, "SYNTHETIC", "test-v1"), std::invalid_argument);
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
