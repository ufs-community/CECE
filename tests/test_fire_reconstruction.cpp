// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "cece/cece_fire_input.hpp"

TEST(test_fire_reconstruction, InputAgeUsesCutoffAndAcquisitionAcrossUtcBoundary) {
    const auto grid = std::make_shared<const cece::FireGrid>(cece::FireGridDefinition{
        "identity-grid", std::string(64, 'b'), 1, 1, {0}, {0}, {{{-1, 1, 1, -1}}}, {{{-1, -1, 1, 1}}},
        {1}, "EPSG:4326", cece::FireLongitudeConvention::Minus180To180});
    cece::FireIdentityMetadata data{1, "synthetic", "1", "test", "test", std::string(64, 'a'),
        cece::FireUtcTime("2022-12-31T23:50:00Z"), cece::FireUtcTime("2023-01-01T00:10:00Z"),
        cece::FireUtcTime("2022-12-31T23:55:00Z"), cece::FireUtcTime("2023-01-01T00:05:00Z"),
        cece::FireUtcTime("2023-01-01T00:15:00Z"), cece::FireCutoffMode::Fixed, grid};
    const cece::FireInputIdentity identity(data);
    EXPECT_EQ(identity.DurationSeconds(), 1200);
    EXPECT_EQ(identity.ObservationAgeSeconds(), 1200);

    data.availability_cutoff = cece::FireUtcTime("2023-01-01T00:05:00Z");
    EXPECT_EQ(cece::FireInputIdentity(data).ObservationAgeSeconds(), 600);
    data.availability_cutoff = cece::FireUtcTime("2023-01-01T00:04:59Z");
    EXPECT_THROW(cece::FireInputIdentity{data}, std::invalid_argument);
    data.availability_cutoff = cece::FireUtcTime("2023-01-01T00:15:00Z");
    data.available_at = cece::FireUtcTime("2022-12-31T23:54:59Z");
    EXPECT_THROW(cece::FireInputIdentity{data}, std::invalid_argument);
    data.available_at = cece::FireUtcTime("2023-01-01T00:05:00Z");
    data.cutoff_mode = cece::FireCutoffMode::Unspecified;
    EXPECT_THROW(cece::FireInputIdentity{data}, std::invalid_argument);
}
