// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <limits>
#include <vector>

#include "cece/cece_fire_input.hpp"
#include "support/cece_fire_test_fixtures.hpp"

namespace {
using namespace cece::test::fire;

constexpr std::string_view kAbcSha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

cece::FireBulkInventory ProviderInventory(std::string content_hash = std::string(kAbcSha256), std::string start = "2022-07-01T00:00:00Z",
                                          std::string end = "2022-07-01T01:00:00Z", std::string cutoff = "2022-07-02T00:00:00Z") {
    auto grid = std::make_shared<const cece::FireGrid>(cece::FireGridDefinition{"provider-grid",
                                                                                std::string(64, 'b'),
                                                                                1,
                                                                                1,
                                                                                {0.0},
                                                                                {0.0},
                                                                                {{{-1, 1, 1, -1}}},
                                                                                {{{-1, -1, 1, 1}}},
                                                                                {25.0},
                                                                                "EPSG:4326",
                                                                                cece::FireLongitudeConvention::Minus180To180});
    const cece::FireProfileIdentity profile{"synthetic", "1", std::string(64, 'c')};
    return cece::FireBulkInventory(
        {cece::FireInputIdentity({1, "source", "product-1", "algorithm-1", "delivery-1", std::move(content_hash), cece::FireUtcTime(start),
                                  cece::FireUtcTime(end), cece::FireUtcTime("2022-07-01T00:00:00Z"), cece::FireUtcTime("2022-07-01T00:00:00Z"),
                                  cece::FireUtcTime(cutoff), cece::FireCutoffMode::Fixed, grid}),
         {cece::FireBulkSpecies::CO},
         {"component"},
         {{"component", {profile, profile}}},
         {5.0},
         {cece::FireQualityState::Observed},
         {1.0},
         {std::string(64, 'd')},
         std::nullopt,
         std::nullopt,
         std::nullopt,
         std::nullopt,
         std::nullopt});
}

cece::FireCacheContext ProviderContext() {
    return {cece::FireInputMode::Product,
            {"quality", "1", std::string(64, 'e')},
            std::nullopt,
            std::nullopt,
            {"target", "1", std::string(64, 'f')},
            std::string(64, 'a'),
            std::nullopt,
            cece::FireTimeAllocation::Uniform,
            std::nullopt};
}

struct ProviderFile {
    std::string path;
    ProviderFile() {
        char name[] = "/tmp/cece-fire-provider-XXXXXX";
        const int descriptor = mkstemp(name);
        if (descriptor < 0) throw std::runtime_error("Cannot create provider test file");
        close(descriptor);
        path = name;
        Write("abc");
    }
    ~ProviderFile() {
        std::remove(path.c_str());
    }
    void Write(std::string_view bytes) const {
        std::ofstream output(path, std::ios::binary);
        output << bytes;
        if (!output) throw std::runtime_error("Cannot write provider test file");
    }
};

TEST(test_fire_runtime, ContentVerificationUsesKnownSha256AndReadsChangedFiles) {
    EXPECT_EQ(cece::FireContentSha256(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(cece::FireContentSha256("abc"), kAbcSha256);
    EXPECT_EQ(cece::FireContentSha256(std::string(1000000, 'a')), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    const std::string binary("a\0b", 3);
    EXPECT_NE(cece::FireContentSha256(binary), cece::FireContentSha256("a"));
    ProviderFile file;
    EXPECT_EQ(cece::FireFileSha256(file.path), kAbcSha256);
    file.Write(std::string(1000000, 'a'));
    EXPECT_EQ(cece::FireFileSha256(file.path), cece::FireContentSha256(std::string(1000000, 'a')));
    EXPECT_THROW(cece::FireFileSha256(file.path + ".missing"), std::runtime_error);
}

TEST(test_fire_runtime, PreparedAndCoupledProvidersShareCacheAndLifecycle) {
    ProviderFile file;
    cece::FireIntervalProvider prepared(cece::FireInputProvider::Prepared, {1, 1, 1, 8192}, {"run", "revision"});
    cece::FireIntervalProvider coupled(cece::FireInputProvider::Coupled, {1, 1, 1, 8192}, {"run", "revision"});
    EXPECT_EQ(prepared.Status(0).state, cece::FireIntervalState::Evicted);
    prepared.StagePrepared(0, ProviderInventory(), ProviderContext(), file.path);
    coupled.StageCoupled(0, ProviderInventory(), ProviderContext(), "abc");
    const auto key = prepared.Status(0).cache_identity;
    EXPECT_EQ(key, coupled.Status(0).cache_identity);
    EXPECT_EQ(prepared.Status(0).state, cece::FireIntervalState::Ready);
    EXPECT_DOUBLE_EQ(prepared.Ready(0).SpeciesBudgetKg(cece::FireBulkSpecies::CO), 5.0);
    EXPECT_THROW(prepared.Evict(0), std::logic_error);
    EXPECT_THROW(prepared.Consume(0, "stale-key"), std::invalid_argument);
    prepared.Consume(0, key);
    EXPECT_EQ(prepared.Status(0).state, cece::FireIntervalState::Consumed);
    EXPECT_THROW(prepared.Ready(0), std::logic_error);
    EXPECT_THROW(prepared.Consume(0, key), std::logic_error);
    prepared.Evict(0);
    EXPECT_EQ(prepared.Status(0).state, cece::FireIntervalState::Evicted);
    EXPECT_THROW(prepared.StagePrepared(0, ProviderInventory(), ProviderContext(), file.path), std::invalid_argument);
    EXPECT_EQ(prepared.Status(0).state, cece::FireIntervalState::Rejected);
    EXPECT_FALSE(prepared.Status(0).rejection_reason.empty());
}

TEST(test_fire_runtime, FailedIntegrityCannotLeaveAReadyOrZeroFilledInterval) {
    ProviderFile file;
    cece::FireIntervalProvider provider(cece::FireInputProvider::Prepared, {1, 1, 1, 8192}, {"run", "revision"});
    provider.StagePrepared(0, ProviderInventory(), ProviderContext(), file.path);
    file.Write("changed");
    EXPECT_THROW(provider.StagePrepared(0, ProviderInventory(), ProviderContext(), file.path), std::runtime_error);
    EXPECT_EQ(provider.Status(0).state, cece::FireIntervalState::Rejected);
    EXPECT_FALSE(provider.Status(0).rejection_reason.empty());
    EXPECT_THROW(provider.Ready(0), std::logic_error);
    provider.Evict(0);
    EXPECT_THROW(provider.StagePrepared(0, ProviderInventory(), ProviderContext(), file.path + ".missing"), std::runtime_error);
    EXPECT_EQ(provider.Status(0).state, cece::FireIntervalState::Rejected);
    EXPECT_THROW(provider.StageCoupled(0, ProviderInventory(), ProviderContext(), "abc"), std::invalid_argument);
}

TEST(test_fire_runtime, ProviderBoundsAndUnconsumedRevisionsAreExplicit) {
    EXPECT_THROW((cece::FireIntervalProvider(cece::FireInputProvider::Coupled, {0, 1, 1, 8192}, {"run", "r"})), std::invalid_argument);
    cece::FireIntervalProvider provider(cece::FireInputProvider::Coupled, {2, 1, 1, 8192}, {"run", "r"});
    EXPECT_THROW(provider.Status(2), std::out_of_range);
    provider.StageCoupled(0, ProviderInventory(), ProviderContext(), "abc");
    const auto old_key = provider.Status(0).cache_identity;
    auto context = ProviderContext();
    context.quality_rules.version = "2";
    provider.StageCoupled(0, ProviderInventory(), context, "abc");
    EXPECT_NE(provider.Status(0).cache_identity, old_key);
    EXPECT_THROW(provider.Consume(0, old_key), std::invalid_argument);
    EXPECT_THROW(provider.StageCoupled(1, ProviderInventory(), context, "abc"), std::invalid_argument);
    EXPECT_EQ(provider.Status(0).state, cece::FireIntervalState::Ready);
    provider.StageCoupled(1, ProviderInventory(std::string(kAbcSha256), "2022-07-01T01:00:00Z", "2022-07-01T02:00:00Z"), context, "abc");
    EXPECT_THROW(provider.Consume(1, provider.Status(1).cache_identity), std::invalid_argument);
    provider.Consume(0, provider.Status(0).cache_identity);
    provider.Consume(1, provider.Status(1).cache_identity);
    EXPECT_EQ(provider.SlotCount(), 2);
    EXPECT_THROW(provider.StageCoupled(0, ProviderInventory(), context, "abc"), std::logic_error);
    cece::FireIntervalProvider tiny(cece::FireInputProvider::Coupled, {1, 1, 1, 1}, {"r", "r"});
    EXPECT_THROW(tiny.StageCoupled(0, ProviderInventory(), context, "abc"), std::invalid_argument);
    EXPECT_EQ(tiny.Status(0).state, cece::FireIntervalState::Rejected);
}

TEST(test_fire_runtime, CacheIdentityIncludesPoliciesProfilesSourcesAndRevision) {
    cece::FireIntervalProvider provider(cece::FireInputProvider::Coupled, {1, 1, 1, 8192}, {"run", "r"});
    const auto original = ProviderContext();
    provider.StageCoupled(0, ProviderInventory(), original, "abc");
    const auto key = provider.Status(0).cache_identity;
    auto changed = original;
    changed.target_profile.content_sha256 = std::string(64, '1');
    provider.StageCoupled(0, ProviderInventory(), changed, "abc");
    EXPECT_NE(provider.Status(0).cache_identity, key);
    changed = original;
    changed.source_manifest_sha256 = std::string(64, '2');
    provider.StageCoupled(0, ProviderInventory(), changed, "abc");
    EXPECT_NE(provider.Status(0).cache_identity, key);
    changed = original;
    changed.product_time_allocation = cece::FireTimeAllocation::SourceResolved;
    provider.StageCoupled(0, ProviderInventory(), changed, "abc");
    EXPECT_NE(provider.Status(0).cache_identity, key);
    changed = original;
    changed.scaling_profile = cece::FireProfileIdentity{"scaling", "1", std::string(64, '3')};
    provider.StageCoupled(0, ProviderInventory(), changed, "abc");
    EXPECT_NE(provider.Status(0).cache_identity, key);
    provider.StageCoupled(0, ProviderInventory(std::string(kAbcSha256), "2022-07-01T00:00:00Z", "2022-07-01T01:00:00Z", "2022-07-03T00:00:00Z"),
                          original, "abc");
    EXPECT_NE(provider.Status(0).cache_identity, key);
    cece::FireIntervalProvider revision(cece::FireInputProvider::Coupled, {1, 1, 1, 8192}, {"run", "r2"});
    revision.StageCoupled(0, ProviderInventory(), original, "abc");
    EXPECT_NE(revision.Status(0).cache_identity, key);
}

TEST(test_fire_runtime, ProviderEnforcesCutoffMonotonicityAndRestoredCommitWatermark) {
    cece::FireIntervalProvider provider(cece::FireInputProvider::Coupled, {1, 1, 1, 8192}, {"run", "r"});
    provider.StageCoupled(0, ProviderInventory(), ProviderContext(), "abc");
    provider.Consume(0, provider.Status(0).cache_identity);
    const auto checkpoint = provider.Checkpoint();
    provider.Evict(0);
    EXPECT_THROW(
        provider.StageCoupled(0, ProviderInventory(std::string(kAbcSha256), "2022-07-01T01:00:00Z", "2022-07-01T02:00:00Z", "2022-07-01T12:00:00Z"),
                              ProviderContext(), "abc"),
        std::invalid_argument);
    cece::FireIntervalProvider restarted(cece::FireInputProvider::Coupled, {1, 1, 1, 8192}, {"run", "r"}, checkpoint);
    EXPECT_THROW(restarted.StageCoupled(0, ProviderInventory(), ProviderContext(), "abc"), std::invalid_argument);
    EXPECT_THROW((cece::FireIntervalProvider(cece::FireInputProvider::Coupled, {1, 1, 1, 8192}, {"different", "r"}, checkpoint)),
                 std::invalid_argument);
}

TEST(test_fire_runtime, ProviderAcceptsOutOfOrderUnconsumedDeliveriesAndCanonicalizesSourceSets) {
    cece::FireIntervalProvider provider(cece::FireInputProvider::Coupled, {2, 1, 1, 8192}, {"run", "r"});
    provider.StageCoupled(1, ProviderInventory(std::string(kAbcSha256), "2022-07-01T01:00:00Z", "2022-07-01T02:00:00Z"), ProviderContext(), "abc");
    auto data = ProviderInventory().Data();
    data.accepted_source_sha256.push_back(std::string(64, 'e'));
    provider.StageCoupled(0, cece::FireBulkInventory(data), ProviderContext(), "abc");
    const auto key = provider.Status(0).cache_identity;
    std::reverse(data.accepted_source_sha256.begin(), data.accepted_source_sha256.end());
    provider.StageCoupled(0, cece::FireBulkInventory(data), ProviderContext(), "abc");
    EXPECT_EQ(provider.Status(0).cache_identity, key);
    data.accepted_source_sha256[0] = std::string(64, 'f');
    provider.StageCoupled(0, cece::FireBulkInventory(data), ProviderContext(), "abc");
    EXPECT_NE(provider.Status(0).cache_identity, key);
    data = ProviderInventory(cece::FireContentSha256("revised")).Data();
    provider.StageCoupled(0, cece::FireBulkInventory(data), ProviderContext(), "revised");
    EXPECT_NE(provider.Status(0).cache_identity, key);
    provider.Consume(0, provider.Status(0).cache_identity);
    provider.Consume(1, provider.Status(1).cache_identity);
}

TEST(test_fire_runtime, ProviderRejectsInvalidProfilesSchedulesAndLogicalPayloadBounds) {
    cece::FireIntervalProvider provider(cece::FireInputProvider::Coupled, {1, 1, 1, 8192}, {"run", "r"});
    auto context = ProviderContext();
    context.target_profile.content_sha256 = "not-a-hash";
    EXPECT_THROW(provider.StageCoupled(0, ProviderInventory(), context, "abc"), std::invalid_argument);
    context = ProviderContext();
    context.cutoff_schedule_sha256 = std::string(64, 'a');
    EXPECT_THROW(provider.StageCoupled(0, ProviderInventory(), context, "abc"), std::invalid_argument);
    context = ProviderContext();
    context.product_time_allocation = cece::FireTimeAllocation::Profile;
    EXPECT_THROW(provider.StageCoupled(0, ProviderInventory(), context, "abc"), std::invalid_argument);
    context.product_time_profile = cece::FireProfileIdentity{"temporal", "1", std::string(64, 'e')};
    provider.StageCoupled(0, ProviderInventory(), context, "abc");
    const auto profile_key = provider.Status(0).cache_identity;
    context.product_time_profile->version = "2";
    provider.StageCoupled(0, ProviderInventory(), context, "abc");
    EXPECT_NE(profile_key, provider.Status(0).cache_identity);
    context = ProviderContext();
    context.mode = cece::FireInputMode::DeriveFrp;
    context.product_time_allocation.reset();
    EXPECT_THROW(provider.StageCoupled(0, ProviderInventory(), context, "abc"), std::invalid_argument);
    context.scientific_profile = cece::FireProfileIdentity{"science", "1", std::string(64, 'e')};
    provider.StageCoupled(0, ProviderInventory(), context, "abc");
    auto data = ProviderInventory().Data();
    data.component_ids.push_back("extra");
    data.component_profiles.emplace("extra", data.component_profiles.at("component"));
    data.mass_kg.push_back(1.0);
    EXPECT_THROW(provider.StageCoupled(0, cece::FireBulkInventory(data), context, "abc"), std::invalid_argument);
    data = ProviderInventory().Data();
    auto grid = data.identity.Metadata().grid_definition->Definition();
    grid.nx = 2;
    grid.longitude_deg.push_back(0);
    grid.latitude_deg.push_back(0);
    grid.longitude_bounds_deg.push_back(grid.longitude_bounds_deg.front());
    grid.latitude_bounds_deg.push_back(grid.latitude_bounds_deg.front());
    grid.area_m2.push_back(25);
    auto identity = data.identity.Metadata();
    identity.grid_definition = std::make_shared<const cece::FireGrid>(std::move(grid));
    data.identity = cece::FireInputIdentity(std::move(identity));
    data.mass_kg.push_back(1);
    data.coverage_fraction.push_back(1);
    data.quality_state.push_back(cece::FireQualityState::Observed);
    EXPECT_THROW(provider.StageCoupled(0, cece::FireBulkInventory(data), context, "abc"), std::invalid_argument);
    EXPECT_EQ(provider.Status(0).state, cece::FireIntervalState::Rejected);
}

TEST(test_fire_runtime, PreparedHostCutoffsRequireScheduleIdentityAndCheckpointPairs) {
    ProviderFile file;
    cece::FireIntervalProvider provider(cece::FireInputProvider::Prepared, {1, 1, 1, 8192}, {"run", "r"});
    auto data = ProviderInventory().Data();
    auto identity = data.identity.Metadata();
    identity.cutoff_mode = cece::FireCutoffMode::HostSupplied;
    data.identity = cece::FireInputIdentity(identity);
    EXPECT_THROW(provider.StagePrepared(0, cece::FireBulkInventory(data), ProviderContext(), file.path), std::invalid_argument);
    auto context = ProviderContext();
    context.cutoff_schedule_sha256 = std::string(64, 'a');
    provider.StagePrepared(0, cece::FireBulkInventory(data), context, file.path);
    const auto key = provider.Status(0).cache_identity;
    context.cutoff_schedule_sha256 = std::string(64, 'b');
    provider.StagePrepared(0, cece::FireBulkInventory(data), context, file.path);
    EXPECT_NE(key, provider.Status(0).cache_identity);
    auto checkpoint = provider.Checkpoint();
    checkpoint.committed_end = cece::FireUtcTime("2022-07-01T01:00:00Z");
    EXPECT_THROW((cece::FireIntervalProvider(cece::FireInputProvider::Prepared, {1, 1, 1, 8192}, {"run", "r"}, checkpoint)), std::invalid_argument);
}

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
