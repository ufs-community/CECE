// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include "support/cece_fire_test_fixtures.hpp"

namespace {
using namespace cece::test::fire;

TEST(test_fire_runtime, FixtureDeviceRoundTripRetainsEachSpeciesBudget) {
    const auto grid = MakeGrid(2, 2, {25.0, 50.0, 75.0, 100.0});
    const auto areas = Kokkos::create_mirror_view_and_copy(Kokkos::HostSpace{}, grid.area_m2);
    EXPECT_DOUBLE_EQ(areas(1, 0), 75.0);
    std::vector<double> masses;
    for (double total : kExpectedMassKg) {
        for (double fraction : {0.1, 0.2, 0.3, 0.4}) {
            masses.push_back(total * fraction);
        }
    }
    const auto field = MakeMassField(grid, masses);
    for (std::size_t s = 0; s < kSpecies.size(); ++s) {
        EXPECT_NEAR(SpeciesBudgetKg(field, s), kExpectedMassKg[s], kRelativeTolerance * kExpectedMassKg[s]);
    }
}

TEST(test_fire_runtime, FixtureRejectsInvalidGridAndMassRatherThanFillingZeros) {
    EXPECT_THROW(MakeGrid(0, 1, {}), std::invalid_argument);
    EXPECT_THROW(MakeGrid(2, 2, {1.0}), std::invalid_argument);
    EXPECT_THROW(MakeGrid(1, 1, {0.0}), std::invalid_argument);
    EXPECT_THROW(MakeGrid(1, 1, {std::numeric_limits<double>::quiet_NaN()}), std::invalid_argument);
    const auto grid = MakeGrid(1, 1, {1.0});
    EXPECT_THROW(MakeMassField(grid, {}), std::invalid_argument);
    std::vector<double> masses(kSpecies.size(), 0.0);
    const auto zero = MakeMassField(grid, masses);
    EXPECT_NEAR(SpeciesBudgetKg(zero, 0), 0.0, kZeroAbsoluteTolerance);
    EXPECT_THROW(SpeciesBudgetKg(zero, kSpecies.size()), std::invalid_argument);
    masses[0] = -1.0;
    EXPECT_THROW(MakeMassField(grid, masses), std::invalid_argument);
    masses[0] = std::numeric_limits<double>::infinity();
    EXPECT_THROW(MakeMassField(grid, masses), std::invalid_argument);
}
}  // namespace
