// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <stdexcept>
#include <string>

#include "cece/cece_fire_input.hpp"
#include "support/cece_fire_test_fixtures.hpp"

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
    data.quality_state[0] = cece::FireQualityState::ValidZero;
    EXPECT_THROW(cece::FireProductEmissionsInterval{data}, std::invalid_argument);
    std::fill(data.source_values.begin(), data.source_values.end(), 0.0);
    EXPECT_NO_THROW(cece::FireProductEmissionsInterval{data});
    data.quality_state[0] = cece::FireQualityState::Unavailable;
    EXPECT_THROW(cece::FireProductEmissionsInterval{data}, std::invalid_argument);
}
