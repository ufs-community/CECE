// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include <string>

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
