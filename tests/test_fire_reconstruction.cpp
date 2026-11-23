// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "cece/cece_fire_input.hpp"

namespace {
cece::FireBulkInventory Inventory(const char* start, const char* end, const char* cutoff) {
    const auto grid = std::make_shared<const cece::FireGrid>(cece::FireGridDefinition{
        "identity-grid", std::string(64, 'b'), 1, 1, {0}, {0}, {{{-1, 1, 1, -1}}}, {{{-1, -1, 1, 1}}},
        {1}, "EPSG:4326", cece::FireLongitudeConvention::Minus180To180});
    const cece::FireProfileIdentity profile{"synthetic", "1", std::string(64, 'c')};
    return cece::FireBulkInventory({
        cece::FireInputIdentity({1, "synthetic", "1", "test", "test", cece::FireContentSha256("abc"),
                                 cece::FireUtcTime(start), cece::FireUtcTime(end), cece::FireUtcTime("2022-12-31T23:55:00Z"),
                                 cece::FireUtcTime("2023-01-01T00:05:00Z"), cece::FireUtcTime(cutoff),
                                 cece::FireCutoffMode::HostSupplied, grid}),
        {cece::FireBulkSpecies::CO}, {"component"}, {{"component", {profile, profile}}}, {1},
        {cece::FireQualityState::Observed}, {1}, {cece::FireContentSha256("abc")},
        std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt});
}

cece::FireCacheContext Context() {
    return {cece::FireInputMode::Product, {"quality", "1", std::string(64, 'd')}, std::nullopt, std::nullopt,
            {"target", "1", std::string(64, 'e')}, std::nullopt, std::nullopt, cece::FireTimeAllocation::Uniform, std::nullopt};
}
}  // namespace

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

TEST(test_fire_reconstruction, ForwardProviderRejectsBackwardCutoffAndAllowsEqualCutoff) {
    cece::FireIntervalProvider provider(cece::FireInputProvider::Coupled, {1, 1, 1, 8192}, {"synthetic", "1"});
    provider.StageCoupled(0, Inventory("2022-12-31T23:50:00Z", "2023-01-01T00:10:00Z", "2023-01-01T00:15:00Z"),
                          Context(), "abc");
    provider.Consume(0, provider.Status(0).cache_identity);
    provider.Evict(0);

    EXPECT_THROW(provider.StageCoupled(0, Inventory("2023-01-01T00:10:00Z", "2023-01-01T00:20:00Z", "2023-01-01T00:14:59Z"),
                                       Context(), "abc"), std::invalid_argument);
    EXPECT_EQ(provider.Status(0).state, cece::FireIntervalState::Rejected);
    EXPECT_NE(provider.Status(0).rejection_reason.find("cutoff"), std::string::npos);
    EXPECT_THROW(provider.Ready(0), std::logic_error);
    ASSERT_TRUE(provider.Checkpoint().committed_cutoff);
    EXPECT_EQ(provider.Checkpoint().committed_cutoff->ToString(), "2023-01-01T00:15:00Z");

    provider.StageCoupled(0, Inventory("2023-01-01T00:10:00Z", "2023-01-01T00:20:00Z", "2023-01-01T00:15:00Z"),
                          Context(), "abc");
    EXPECT_EQ(provider.Status(0).state, cece::FireIntervalState::Ready);
    provider.Consume(0, provider.Status(0).cache_identity);
    ASSERT_TRUE(provider.Checkpoint().committed_end);
    EXPECT_EQ(provider.Checkpoint().committed_end->ToString(), "2023-01-01T00:20:00Z");
}

TEST(test_fire_reconstruction, IdentityAllowsPostIntervalAcquisitionOnlyByCutoff) {
    auto metadata = Inventory("2022-12-31T23:50:00Z", "2023-01-01T00:10:00Z", "2023-01-01T00:20:00Z").Data().identity.Metadata();
    metadata.acquired_at = cece::FireUtcTime("2023-01-01T00:12:00Z");
    metadata.available_at = cece::FireUtcTime("2023-01-01T00:20:00Z");
    const cece::FireInputIdentity retrospective_candidate(metadata);
    EXPECT_EQ(retrospective_candidate.ObservationAgeSeconds(), 480);
    metadata.available_at = cece::FireUtcTime("2023-01-01T00:20:01Z");
    EXPECT_THROW(cece::FireInputIdentity{metadata}, std::invalid_argument);
    metadata.acquired_at = cece::FireUtcTime("2023-01-01T00:20:01Z");
    EXPECT_THROW(cece::FireInputIdentity{metadata}, std::invalid_argument);
}
