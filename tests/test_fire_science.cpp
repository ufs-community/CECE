// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include <string>

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
