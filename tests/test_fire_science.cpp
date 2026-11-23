// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

#include "cece/cece_fire_input.hpp"
#include "support/cece_fire_test_fixtures.hpp"

TEST(test_fire_science, ApproximateSwathAreaHandlesDateLineLatitudeAndDeletedPixels) {
    std::vector<double> longitude{179.99, -180.0, -179.99, 179.99, -180.0, -179.99, 179.99, -180.0, -179.99};
    const std::vector<double> latitude{59.99, 59.99, 59.99, 60.0, 60.0, 60.0, 60.01, 60.01, 60.01};
    std::vector<std::uint8_t> deleted(9, 0);
    deleted[0] = 1;
    const auto areas = cece::FireApproximateSwathPixelAreas(3, 3, longitude, latitude, deleted);
    ASSERT_EQ(areas.size(), 9);
    EXPECT_DOUBLE_EQ(areas[0], 0);
    constexpr double radius_m = 6371008.8;
    const double radians = 0.01 * std::numbers::pi / 180.0;
    const double expected = radius_m * radius_m * radians * radians * 0.5;
    EXPECT_NEAR(areas[4], expected, expected * 1.0e-6);
    EXPECT_NEAR(areas[5], expected, expected * 1.0e-6);
    longitude[0] = std::numeric_limits<double>::quiet_NaN();
    const auto areas_without_deleted_geolocation = cece::FireApproximateSwathPixelAreas(3, 3, longitude, latitude, deleted);
    EXPECT_DOUBLE_EQ(areas_without_deleted_geolocation[0], 0);
    EXPECT_NEAR(areas_without_deleted_geolocation[4], expected, expected * 1.0e-6);
}

TEST(test_fire_science, ApproximateSwathAreaRejectsMissingOrDegenerateGeometry) {
    std::vector<double> longitude{0, 1, 0, 1};
    std::vector<double> latitude{0, 0, 1, 1};
    std::vector<std::uint8_t> deleted(4, 0);
    EXPECT_THROW(cece::FireApproximateSwathPixelAreas(2, 2, {0}, latitude, deleted), std::invalid_argument);
    longitude[0] = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(cece::FireApproximateSwathPixelAreas(2, 2, longitude, latitude, deleted), std::invalid_argument);
    longitude = {0, 0, 0, 0};
    EXPECT_THROW(cece::FireApproximateSwathPixelAreas(2, 2, longitude, latitude, deleted), std::invalid_argument);
}

TEST(test_fire_science, SyntheticReferenceArithmeticIsInternallyConsistent) {
    const auto fixture = YAML::LoadFile(std::string(CECE_SOURCE_DIR) + "/tests/data/fire/synthetic_interval.yaml");
    const auto input = fixture["inputs"];
    const auto expected = fixture["expected"];
    const double energy_mj = input["frp_mw"].as<double>() * input["duration_seconds"].as<double>();
    const double dry_kg = energy_mj * input["combustion_kg_per_mj"].as<double>();
    EXPECT_DOUBLE_EQ(energy_mj, expected[0]["values"][0].as<double>());
    EXPECT_DOUBLE_EQ(dry_kg, expected[1]["values"][0].as<double>());
    ASSERT_EQ(input["species"].size(), cece::test::fire::kSpecies.size());
    ASSERT_EQ(input["emission_factors_kg_per_kg"].size(), cece::test::fire::kSpecies.size());
    ASSERT_EQ(expected[2]["values"].size(), cece::test::fire::kSpecies.size());
    ASSERT_EQ(expected[3]["values"].size(), cece::test::fire::kSpecies.size());
    for (std::size_t s = 0; s < cece::test::fire::kSpecies.size(); ++s) {
        EXPECT_EQ(input["species"][s].as<std::string>(), cece::test::fire::kSpecies[s]);
        const double mass = dry_kg * input["emission_factors_kg_per_kg"][s].as<double>();
        EXPECT_NEAR(mass, expected[2]["values"][s].as<double>(), 1.0e-12);
        EXPECT_DOUBLE_EQ(expected[2]["values"][s].as<double>(), cece::test::fire::kExpectedMassKg[s]);
        const double flux = mass / input["area_m2"].as<double>() / input["duration_seconds"].as<double>();
        EXPECT_NEAR(flux, expected[3]["values"][s].as<double>(), 1.0e-12);
        EXPECT_DOUBLE_EQ(input["emission_factors_kg_per_kg"][s].as<double>(), cece::test::fire::kSyntheticFactors.emission_kg_per_kg[s]);
    }
    EXPECT_DOUBLE_EQ(input["combustion_kg_per_mj"].as<double>(), cece::test::fire::kSyntheticFactors.combustion_kg_per_mj);
}

TEST(test_fire_science, AtbdForestIllustrationHasConsistentPrecedenceAndDimensionalReference) {
    const auto fixture = YAML::LoadFile(std::string(CECE_SOURCE_DIR) + "/tests/data/fire/rave_atbd_forest_reference.yaml");
    EXPECT_EQ(fixture["kind"].as<std::string>(), "synthetic");
    EXPECT_FALSE(fixture["production_use"].as<bool>());
    const auto input = fixture["inputs"];
    const auto expected = fixture["expected"];
    ASSERT_EQ(expected.size(), 4);
    EXPECT_EQ(expected[0]["units"].as<std::string>(), "MW");
    EXPECT_EQ(expected[1]["units"].as<std::string>(), "MJ");
    EXPECT_EQ(expected[2]["units"].as<std::string>(), "kg");
    EXPECT_EQ(expected[3]["units"].as<std::string>(), "kg");
    const auto abi = input["abi_calibrated_frp_mw"].as<std::vector<double>>();
    const auto viirs = input["viirs_corrected_frp_mw"].as<std::vector<double>>();
    const auto available = input["viirs_available"].as<std::vector<bool>>();
    const auto fused = expected[0]["values"].as<std::vector<double>>();
    ASSERT_EQ(abi.size(), 6);
    ASSERT_EQ(viirs.size(), abi.size());
    ASSERT_EQ(available.size(), abi.size());
    ASSERT_EQ(fused.size(), abi.size());
    EXPECT_EQ(input["slot_duration_seconds"].as<int>(), 600);
    EXPECT_DOUBLE_EQ(input["combustion_coefficient_kg_per_mj"].as<double>(), 0.368);
    for (std::size_t i = 0; i < abi.size(); ++i) {
        EXPECT_DOUBLE_EQ(fused[i], available[i] ? viirs[i] : abi[i]) << i;
    }
    double fre = 0;
    for (const double power : fused) fre += power * input["slot_duration_seconds"].as<double>();
    EXPECT_DOUBLE_EQ(fre, expected[1]["values"][0].as<double>());
    const double dry_matter = fre * input["combustion_coefficient_kg_per_mj"].as<double>();
    EXPECT_NEAR(dry_matter, expected[2]["values"][0].as<double>(), 1e-10);
    const auto names = input["species"].as<std::vector<std::string>>();
    const auto factors = input["forest_mean_emission_factors_g_per_kg"].as<std::vector<double>>();
    const auto masses = expected[3]["values"].as<std::vector<double>>();
    ASSERT_EQ(names.size(), cece::test::fire::kSpecies.size());
    ASSERT_EQ(factors.size(), names.size());
    ASSERT_EQ(masses.size(), names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        EXPECT_EQ(names[i], cece::test::fire::kSpecies[i]);
        EXPECT_NEAR(dry_matter * factors[i] / 1000.0, masses[i], 1e-9) << names[i];
    }
}

TEST(test_fire_science, SyntheticElevenSpeciesFluxNormalizesToIndependentMasses) {
    const auto fixture = YAML::LoadFile(std::string(CECE_SOURCE_DIR) + "/tests/data/fire/synthetic_interval.yaml");
    const auto grid = std::make_shared<const cece::FireGrid>(cece::FireGridDefinition{
        "synthetic-science", std::string(64, 'b'), 1, 1, {0}, {0}, {{{-1, 1, 1, -1}}}, {{{-1, -1, 1, 1}}},
        {fixture["inputs"]["area_m2"].as<double>()}, "EPSG:4326", cece::FireLongitudeConvention::Minus180To180});
    cece::FireProductEmissionsIntervalData data({"synthetic", "1", "test",
        cece::FireInputIdentity({1, "synthetic", "1", "test", "test", std::string(64, 'a'),
            cece::FireUtcTime("2022-07-01T00:00:00Z"), cece::FireUtcTime("2022-07-01T00:01:40Z"),
            cece::FireUtcTime("2022-07-01T00:00:00Z"), cece::FireUtcTime("2022-07-01T00:00:00Z"),
            cece::FireUtcTime("2022-07-01T00:01:40Z"), cece::FireCutoffMode::Fixed, grid})});
    data.quality_state = {cece::FireQualityState::Observed};
    data.coverage_fraction = {1};
    const auto names = fixture["inputs"]["species"];
    const auto fluxes = fixture["expected"][3]["values"];
    const auto masses = fixture["expected"][2]["values"];
    ASSERT_EQ(names.size(), 11);
    ASSERT_EQ(fluxes.size(), names.size());
    ASSERT_EQ(masses.size(), names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto species = cece::FireSpeciesFromName(names[i].as<std::string>());
        data.species.push_back(species);
        data.species_availability.emplace(species, cece::FireProductSpeciesAvailability::Available);
        data.quantity_semantics.emplace(species, cece::FireProductQuantitySemantics{cece::FireProductQuantityKind::Flux, "kg m-2 s-1"});
        data.source_values.push_back(fluxes[i].as<double>());
    }
    const cece::FireProductEmissionsInterval product(data);
    ASSERT_EQ(product.MassKg().size(), names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        EXPECT_NEAR(product.MassKg()[i], masses[i].as<double>(), 1e-12) << names[i].as<std::string>();
    }
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto species = data.species[i];
        data.quantity_semantics.at(species) = {cece::FireProductQuantityKind::MeanRate, "kg s-1"};
        data.source_values[i] = masses[i].as<double>() / 100.0;
    }
    const cece::FireProductEmissionsInterval rate_product(data);
    for (std::size_t i = 0; i < names.size(); ++i) {
        EXPECT_NEAR(rate_product.MassKg()[i], masses[i].as<double>(), 1e-12) << names[i].as<std::string>();
    }
    data.quantity_semantics.at(data.species[0]).units = "kg m-2 s-1";
    EXPECT_THROW(cece::FireProductEmissionsInterval{data}, std::invalid_argument);
    data.quantity_semantics.at(data.species[0]).units = "kg s-1";
    data.quality_state[0] = cece::FireQualityState::ValidZero;
    EXPECT_THROW(cece::FireProductEmissionsInterval{data}, std::invalid_argument);
    std::fill(data.source_values.begin(), data.source_values.end(), 0.0);
    const cece::FireProductEmissionsInterval zero_product(data);
    EXPECT_EQ(zero_product.Data().quality_state[0], cece::FireQualityState::ValidZero);
    for (const double mass : zero_product.MassKg()) EXPECT_DOUBLE_EQ(mass, 0.0);
    data.quality_state[0] = cece::FireQualityState::Unavailable;
    EXPECT_THROW(cece::FireProductEmissionsInterval{data}, std::invalid_argument);
}
