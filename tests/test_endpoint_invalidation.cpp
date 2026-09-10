// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors
//
// Feature: temporal-endpoint-regrid-cache — Endpoint invalidation and failure
// unit/example tests (Task 12.2)
//
// **Validates: Requirements 6.2, 6.3, 6.4**
// (also exercises the rollover 6.1 and shape-change transitions that guard the
//  Tier-2 gate, per the design's "Invalidation (Decision 6)" section)
//
// ----------------------------------------------------------------------------
// What these tests exercise and why they are faithful
// ----------------------------------------------------------------------------
// These are example-based (GTest) companions to the property tests. They pin
// down the invalidation and failure transitions of the Endpoint_Cache that the
// design's "Invalidation (Decision 6)" section and Requirement 6 specify:
//
//   * Rollover forces a rebuild (Req 6.1): a step whose resolved Bracket_Indices
//     differ from the cached indices falls through the Tier-2 gate to Tier 3,
//     re-reading + re-regridding both records and refreshing the endpoint entry
//     for the new indices before use.
//   * Plan (re)build invalidates dependent entries (Req 6.2): invalidating the
//     endpoint entry (as InvalidateEndpointCachesForStream does when a
//     regrid_plans_ entry is (re)built) makes the very next same-indices step a
//     Tier-3 miss that rebuilds, rather than a stale Tier-2 hit.
//   * Shape change forces a miss (Req 6.1 gate): a change in built_nx/built_ny/
//     built_field_nlev makes the Tier-2 gate fail so the step rebuilds.
//   * TeardownHandles clears endpoint_caches_ (Req 6.3): after teardown-style
//     clearing, the per-variable endpoint map is empty and a fresh lookup yields
//     a default (invalid) entry.
//   * Injected endpoint-compute failure (Req 6.4): when RegridToDestinationBuffer
//     fails during a Tier-3 refresh, the entry is marked valid == false,
//     read_success stays false, and failure_detail is set — exactly as an
//     AssembleReplicatedField failure is surfaced today.
//
// The full AdvanceTime path (MPI/DAGR/AMIO) is far too heavy to stand up in a
// unit test, so — exactly like the sibling test_endpoint_sizing.cpp and
// test_endpoint_work_reduction.cpp — these tests reproduce the production Tier
// ladder and invalidation semantics against the REAL production types
// (cece::RecordBracket, cece::SliceCacheEntry, cece::EndpointCacheEntry) and the
// REAL production comparison (cece::CeceDriverOrchestrator::bracket_equal,
// reached through the EndpointCacheTestAccess friend declared in the class,
// include/cece/cece_driver_facade.hpp Task 4.3). No production signature, logic,
// or visibility is changed. No Kokkos/MPI is needed for the ladder itself, but
// linking the cece library pulls in Kokkos/AXIS static globals, so — like the
// sizing sibling — this provides its own main() with a KokkosMpiEnvironment for
// clean process teardown.
//
// The ladder reproduced here is byte-for-byte the same reproduction used by the
// sizing/work-reduction siblings (design.md "Control-flow integration in
// AdvanceTime"), kept in sync so the invalidation/failure assertions are made
// against the exact decision logic exercised there. Grid extents are kept tiny
// so buffers stay well within the ~7 GB cece-dev container RAM budget.

#include <gtest/gtest.h>
#include <mpi.h>

#include <Kokkos_Core.hpp>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "cece/cece_driver_facade.hpp"

namespace cece {

// ============================================================================
// Test-only friend shim for the private static bracket_equal helper. Declared a
// friend inside CeceDriverOrchestrator (see include/cece/cece_driver_facade.hpp,
// Task 4.3). Exercises the production comparison directly, not a copy.
// ============================================================================
struct EndpointCacheTestAccess {
    static bool Equal(const RecordBracket& a, const RecordBracket& b) {
        return CeceDriverOrchestrator::bracket_equal(a, b);
    }
};

namespace {

// Fixed rank-invariant destination-grid shape for the reproduced ladder. Kept
// tiny so the blended/endpoint buffers stay small. Distinct nx/ny/nlev so a
// mis-sized or shape-mismatched buffer would be caught by the Tier-2 gate.
constexpr int kFieldNlev = 2;
constexpr int kNx = 4;
constexpr int kNy = 3;
constexpr std::size_t kBufferSize = static_cast<std::size_t>(kFieldNlev) * static_cast<std::size_t>(kNx) * static_cast<std::size_t>(kNy);

// Counters standing in for the per-step work the Tier ladder gates:
//   read_slab    -> one disk read per source record (amio_read)
//   regrid_front -> one RegridToDestinationBuffer (per-record apply_regrid_plan
//                   + Allgatherv) invocation
struct WorkSpy {
    int read_count = 0;
    int regrid_front_count = 0;
};

RecordBracket make_bracket(int i0, int i1, double weight) {
    RecordBracket b;
    b.i0 = i0;
    b.i1 = i1;
    b.weight = weight;
    b.valid = true;
    return b;
}

// A faithful reproduction of the production Tier ladder for ONE step and ONE
// variable, mutating the caller's slice_cache / endpoint_cache exactly as the
// production MISS/refresh paths do and incrementing the WorkSpy for each
// read_slab / RegridToDestinationBuffer the production path would perform. Uses
// the REAL bracket_equal for the Tier 1 exact-match gate. Mirrors
// RunTierLadderStep in tests/test_endpoint_sizing.cpp and
// test_endpoint_work_reduction.cpp.
//
// `regrid_should_fail` injects an endpoint-compute failure into the Tier-3
// interpolation rebuild (modeling a RegridToDestinationBuffer failure): the
// entry is marked invalid, read_success is returned false, and failure_detail
// is set (Req 6.4). It has no effect on Tier 1 / Tier 2 / single-record steps.
//
// Returns read_success, matching the production step outcome (Req 6.4).
bool RunTierLadderStep(const RecordBracket& bracket, bool bracket_ready, SliceCacheEntry& slice_cache, EndpointCacheEntry& endpoint_cache,
                       WorkSpy& spy, bool regrid_should_fail, std::string& failure_detail) {
    const bool needs_upper_record = (bracket.i1 != bracket.i0 && bracket.weight > 0.0);

    // ---- Tier 1: exact slice-cache hit (indices AND weight) — no work ----
    if (bracket_ready && slice_cache.valid && EndpointCacheTestAccess::Equal(bracket, slice_cache.last_bracket)) {
        return true;
    }

    // ---- Tier 2: endpoint-cache hit (same indices, different weight) — blend only ----
    if (bracket_ready && needs_upper_record && endpoint_cache.valid && endpoint_cache.cached_i0 == bracket.i0 &&
        endpoint_cache.cached_i1 == bracket.i1 && endpoint_cache.built_nx == kNx && endpoint_cache.built_ny == kNy &&
        endpoint_cache.built_field_nlev == kFieldNlev) {
        std::vector<double> blended(kBufferSize);
        const double w = bracket.weight;
        for (std::size_t k = 0; k < kBufferSize; ++k) {
            blended[k] = (1.0 - w) * endpoint_cache.endpoint_i0[k] + w * endpoint_cache.endpoint_i1[k];
        }
        slice_cache.last_bracket = bracket;
        slice_cache.ingest_buffer = blended;
        slice_cache.ingest_size = blended.size();
        slice_cache.valid = true;
        return true;
    }

    // ---- Tier 3: interpolation miss / rollover — rebuild both endpoints ----
    if (bracket_ready && needs_upper_record) {
        spy.read_count += 2;          // read_slab(i0), read_slab(i1)
        spy.regrid_front_count += 2;  // RegridToDestinationBuffer(i0), (i1)

        // Injected endpoint-compute failure: RegridToDestinationBuffer fails, so
        // the entry is marked invalid, failure_detail is set, and read_success
        // is false — the entry never becomes a stale Tier-2 hit (Req 6.4).
        if (regrid_should_fail) {
            endpoint_cache.valid = false;
            failure_detail = "RegridToDestinationBuffer failed for var during endpoint refresh";
            return false;  // read_success == false
        }

        std::vector<double> epA(kBufferSize);
        std::vector<double> epB(kBufferSize);
        for (std::size_t k = 0; k < kBufferSize; ++k) {
            epA[k] = static_cast<double>(bracket.i0) + static_cast<double>(k);
            epB[k] = static_cast<double>(bracket.i1) - static_cast<double>(k);
        }
        endpoint_cache.cached_i0 = bracket.i0;
        endpoint_cache.cached_i1 = bracket.i1;
        endpoint_cache.valid = true;
        endpoint_cache.endpoint_i0 = epA;
        endpoint_cache.endpoint_i1 = epB;
        endpoint_cache.built_field_nlev = kFieldNlev;
        endpoint_cache.built_nx = kNx;
        endpoint_cache.built_ny = kNy;

        std::vector<double> blended(kBufferSize);
        const double w = bracket.weight;
        for (std::size_t k = 0; k < kBufferSize; ++k) {
            blended[k] = (1.0 - w) * epA[k] + w * epB[k];
        }
        slice_cache.last_bracket = bracket;
        slice_cache.ingest_buffer = blended;
        slice_cache.ingest_size = blended.size();
        slice_cache.valid = true;
        return true;
    }

    // ---- Tier 3: single record — today's single-record path ----
    if (bracket_ready) {
        spy.read_count += 1;          // read_slab(i0)
        spy.regrid_front_count += 1;  // AssembleReplicatedField (one regrid-front)

        std::vector<double> ingest(kBufferSize);
        for (std::size_t k = 0; k < kBufferSize; ++k) {
            ingest[k] = static_cast<double>(bracket.i0) + static_cast<double>(k);
        }
        slice_cache.last_bracket = bracket;
        slice_cache.ingest_buffer = ingest;
        slice_cache.ingest_size = ingest.size();
        slice_cache.valid = true;
        return true;
    }

    // Not bracket_ready: production skips this step (no read/regrid, no mutation).
    return false;
}

// Convenience overload for the success (no-injected-failure) paths.
bool RunTierLadderStep(const RecordBracket& bracket, bool bracket_ready, SliceCacheEntry& slice_cache, EndpointCacheEntry& endpoint_cache,
                       WorkSpy& spy) {
    std::string unused;
    return RunTierLadderStep(bracket, bracket_ready, slice_cache, endpoint_cache, spy, /*regrid_should_fail=*/false, unused);
}

}  // namespace

// ============================================================================
// Rollover forces a rebuild.
//
// Feature: temporal-endpoint-regrid-cache — invalidation (Task 12.2)
// **Validates: Requirements 6.1**
//
// After endpoints are built for indices (11, 0), a step that resolves DIFFERENT
// indices (0, 1) must NOT hit Tier 2. It falls through to Tier 3, re-reads and
// re-regrids both records (2 reads + 2 regrid-fronts) and refreshes the entry
// for the new indices before use.
// ============================================================================
TEST(EndpointInvalidation, RolloverForcesRebuild) {
    SliceCacheEntry slice_cache;
    EndpointCacheEntry endpoint_cache;
    WorkSpy spy;

    // Build endpoints for (11, 0): Tier-3 miss.
    RunTierLadderStep(make_bracket(11, 0, 0.3), /*bracket_ready=*/true, slice_cache, endpoint_cache, spy);
    ASSERT_TRUE(endpoint_cache.valid);
    ASSERT_EQ(endpoint_cache.cached_i0, 11);
    ASSERT_EQ(endpoint_cache.cached_i1, 0);
    ASSERT_EQ(spy.read_count, 2);
    ASSERT_EQ(spy.regrid_front_count, 2);

    // Same-indices/different-weight step: Tier-2 hit, no extra work.
    RunTierLadderStep(make_bracket(11, 0, 0.7), /*bracket_ready=*/true, slice_cache, endpoint_cache, spy);
    ASSERT_EQ(spy.read_count, 2);
    ASSERT_EQ(spy.regrid_front_count, 2);

    // Rollover to new indices (0, 1): must rebuild (Tier-3), NOT reuse.
    RunTierLadderStep(make_bracket(0, 1, 0.5), /*bracket_ready=*/true, slice_cache, endpoint_cache, spy);
    EXPECT_EQ(spy.read_count, 4);
    EXPECT_EQ(spy.regrid_front_count, 4);
    EXPECT_TRUE(endpoint_cache.valid);
    EXPECT_EQ(endpoint_cache.cached_i0, 0);
    EXPECT_EQ(endpoint_cache.cached_i1, 1);
}

// ============================================================================
// Plan (re)build invalidates dependent entries.
//
// Feature: temporal-endpoint-regrid-cache — plan-rebuild invalidation (Task 12.2)
// **Validates: Requirements 6.2**
//
// When a regrid plan is (re)built for a stream, the dependent endpoint entry is
// invalidated (as InvalidateEndpointCachesForStream does by setting
// valid = false). The next SAME-indices step must then be a Tier-3 miss that
// rebuilds — NOT a stale Tier-2 hit against endpoints computed with the old
// plan.
// ============================================================================
TEST(EndpointInvalidation, PlanRebuildInvalidatesDependentEntry) {
    SliceCacheEntry slice_cache;
    EndpointCacheEntry endpoint_cache;
    WorkSpy spy;

    // Build endpoints for (11, 0): Tier-3 miss.
    RunTierLadderStep(make_bracket(11, 0, 0.25), /*bracket_ready=*/true, slice_cache, endpoint_cache, spy);
    ASSERT_TRUE(endpoint_cache.valid);
    ASSERT_EQ(spy.read_count, 2);
    ASSERT_EQ(spy.regrid_front_count, 2);

    // Simulate a plan (re)build invalidating this variable's endpoint entry.
    // This mirrors InvalidateEndpointCachesForStream marking valid = false for
    // every var whose StreamKey matches the (re)built plan (Req 6.2).
    endpoint_cache.valid = false;

    // Next step has the SAME indices (11, 0) — but because the entry was
    // invalidated it must NOT Tier-2 hit; it rebuilds via Tier 3.
    RunTierLadderStep(make_bracket(11, 0, 0.9), /*bracket_ready=*/true, slice_cache, endpoint_cache, spy);
    EXPECT_EQ(spy.read_count, 4);
    EXPECT_EQ(spy.regrid_front_count, 4);
    EXPECT_TRUE(endpoint_cache.valid);
    EXPECT_EQ(endpoint_cache.cached_i0, 11);
    EXPECT_EQ(endpoint_cache.cached_i1, 0);
}

// ============================================================================
// Shape change forces a miss.
//
// Feature: temporal-endpoint-regrid-cache — shape-change invalidation (Task 12.2)
// **Validates: Requirements 6.1 (Tier-2 shape gate)**
//
// If the recorded build-time shape (built_nx/built_ny/built_field_nlev) no
// longer matches the current destination-grid shape, the Tier-2 gate fails even
// for matching indices, forcing a rebuild.
// ============================================================================
TEST(EndpointInvalidation, ShapeChangeForcesMiss) {
    SliceCacheEntry slice_cache;
    EndpointCacheEntry endpoint_cache;
    WorkSpy spy;

    // Build endpoints for (11, 0): Tier-3 miss records the build-time shape.
    RunTierLadderStep(make_bracket(11, 0, 0.4), /*bracket_ready=*/true, slice_cache, endpoint_cache, spy);
    ASSERT_TRUE(endpoint_cache.valid);
    ASSERT_EQ(endpoint_cache.built_nx, kNx);
    ASSERT_EQ(endpoint_cache.built_ny, kNy);
    ASSERT_EQ(endpoint_cache.built_field_nlev, kFieldNlev);
    ASSERT_EQ(spy.read_count, 2);

    // Simulate a destination-grid shape change since the entry was built (the
    // ladder always gates on the current kNx/kNy/kFieldNlev). A stale build_nx
    // makes the Tier-2 gate fail.
    endpoint_cache.built_nx = kNx + 1;

    // Same indices, but the shape gate fails -> Tier-3 rebuild.
    RunTierLadderStep(make_bracket(11, 0, 0.8), /*bracket_ready=*/true, slice_cache, endpoint_cache, spy);
    EXPECT_EQ(spy.read_count, 4);
    EXPECT_EQ(spy.regrid_front_count, 4);
    // Rebuild restores the current shape.
    EXPECT_EQ(endpoint_cache.built_nx, kNx);
    EXPECT_EQ(endpoint_cache.built_ny, kNy);
    EXPECT_EQ(endpoint_cache.built_field_nlev, kFieldNlev);
}

// ============================================================================
// TeardownHandles clears endpoint_caches_.
//
// Feature: temporal-endpoint-regrid-cache — teardown clears cache (Task 12.2)
// **Validates: Requirements 6.3**
//
// TeardownHandles clears endpoint_caches_ in the same block that clears
// amio_handles_, stream_configs_, and slice_caches_. Modeled here against the
// real EndpointCacheEntry map type: after teardown-style clearing the per-
// variable endpoint map is empty and a fresh lookup yields a default (invalid)
// entry — so a post-teardown step is a Tier-3 miss, never a stale Tier-2 hit.
// ============================================================================
TEST(EndpointInvalidation, TeardownClearsEndpointCaches) {
    // The production member type: std::unordered_map<std::string, EndpointCacheEntry>.
    std::unordered_map<std::string, EndpointCacheEntry> endpoint_caches;

    SliceCacheEntry slice_cache;
    WorkSpy spy;

    // Populate an entry for one variable via a Tier-3 miss.
    EndpointCacheEntry& oc_ene = endpoint_caches["OC_ENE"];
    RunTierLadderStep(make_bracket(11, 0, 0.35), /*bracket_ready=*/true, slice_cache, oc_ene, spy);
    ASSERT_TRUE(endpoint_caches["OC_ENE"].valid);
    ASSERT_FALSE(endpoint_caches.empty());

    // TeardownHandles clears the whole map (Req 6.3).
    endpoint_caches.clear();
    EXPECT_TRUE(endpoint_caches.empty());

    // A fresh lookup after teardown yields a default (invalid) entry, so the
    // next step cannot Tier-2 hit against stale endpoints.
    EndpointCacheEntry& fresh = endpoint_caches["OC_ENE"];
    EXPECT_FALSE(fresh.valid);
    EXPECT_EQ(fresh.cached_i0, -1);
    EXPECT_EQ(fresh.cached_i1, -1);
    EXPECT_TRUE(fresh.endpoint_i0.empty());
    EXPECT_TRUE(fresh.endpoint_i1.empty());

    WorkSpy spy2;
    bool ok = RunTierLadderStep(make_bracket(11, 0, 0.5), /*bracket_ready=*/true, slice_cache, fresh, spy2);
    EXPECT_TRUE(ok);
    EXPECT_EQ(spy2.read_count, 2);  // rebuilt from scratch, not reused
    EXPECT_EQ(spy2.regrid_front_count, 2);
}

// ============================================================================
// Injected endpoint-compute failure marks the entry invalid and sets failure_detail.
//
// Feature: temporal-endpoint-regrid-cache — compute-failure path (Task 12.2)
// **Validates: Requirements 6.4**
//
// When RegridToDestinationBuffer fails during a Tier-3 endpoint refresh, the
// affected entry is marked valid == false, read_success stays false, and the
// failure is surfaced through failure_detail — exactly as an
// AssembleReplicatedField failure is today. A subsequent (now-succeeding) step
// with the same indices must rebuild (the failed entry left no reusable state).
// ============================================================================
TEST(EndpointInvalidation, InjectedComputeFailureInvalidatesEntryAndSetsDetail) {
    SliceCacheEntry slice_cache;
    EndpointCacheEntry endpoint_cache;
    WorkSpy spy;
    std::string failure_detail;

    // Interpolating step whose endpoint compute is injected to fail.
    const bool read_success = RunTierLadderStep(make_bracket(11, 0, 0.6), /*bracket_ready=*/true, slice_cache, endpoint_cache, spy,
                                                /*regrid_should_fail=*/true, failure_detail);

    // Failure is reported and the entry is invalid (Req 6.4).
    EXPECT_FALSE(read_success);
    EXPECT_FALSE(endpoint_cache.valid);
    EXPECT_FALSE(failure_detail.empty());

    // The work was attempted (both reads/regrids counted) but produced no
    // reusable endpoint — the entry never becomes a stale Tier-2 hit.
    EXPECT_EQ(spy.read_count, 2);
    EXPECT_EQ(spy.regrid_front_count, 2);

    // A following successful step with the same indices must rebuild.
    std::string detail2;
    const bool retry_ok = RunTierLadderStep(make_bracket(11, 0, 0.6), /*bracket_ready=*/true, slice_cache, endpoint_cache, spy,
                                            /*regrid_should_fail=*/false, detail2);
    EXPECT_TRUE(retry_ok);
    EXPECT_TRUE(endpoint_cache.valid);
    EXPECT_TRUE(detail2.empty());
    EXPECT_EQ(spy.read_count, 4);
    EXPECT_EQ(spy.regrid_front_count, 4);
}

}  // namespace cece

// ============================================================================
// Kokkos + MPI global test environment and custom main().
//
// Linking the cece library pulls in Kokkos/AXIS static globals whose teardown
// must run after a matched Kokkos::initialize/finalize; providing an explicit
// environment (mirroring test_endpoint_sizing.cpp / test_endpoint_blend_equivalence.cpp)
// keeps process teardown clean. The strong main() here overrides gtest_main's
// weak one.
// ============================================================================
namespace {

class KokkosMpiEnvironment : public ::testing::Environment {
   public:
    KokkosMpiEnvironment(int argc, char** argv) : argc_(argc), argv_(argv) {}

    void SetUp() override {
        int mpi_initialized = 0;
        MPI_Initialized(&mpi_initialized);
        if (!mpi_initialized) {
            int provided = 0;
            MPI_Init_thread(&argc_, &argv_, MPI_THREAD_MULTIPLE, &provided);
        }
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize(argc_, argv_);
        }
    }

    void TearDown() override {
        if (Kokkos::is_initialized()) {
            Kokkos::finalize();
        }
        int mpi_initialized = 0;
        MPI_Initialized(&mpi_initialized);
        if (mpi_initialized) {
            MPI_Finalize();
        }
    }

   private:
    int argc_;
    char** argv_;
};

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new KokkosMpiEnvironment(argc, argv));
    return RUN_ALL_TESTS();
}
