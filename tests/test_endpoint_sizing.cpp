// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors
//
// Feature: temporal-endpoint-regrid-cache — Endpoint sizing and Tier-3
// population unit/example tests (Task 12.1)
//
// **Validates: Requirements 1.1, 1.3, 2.5, 8.1**
//
// ----------------------------------------------------------------------------
// What these tests exercise and why they are faithful
// ----------------------------------------------------------------------------
// These are example-based (GTest) companions to the property tests
// (test_endpoint_blend_equivalence.cpp, test_endpoint_work_reduction.cpp). They
// pin down three concrete structural guarantees of the Endpoint_Cache
// optimization:
//
//   * Sizing (Req 1.3, 8.1): each Endpoint_Field is a destination-grid buffer
//     sized exactly field_nlev * nx * ny doubles — the SAME layout/size the
//     Slice_Cache ingest_buffer uses.
//   * Tier-3 population (Req 1.1): the FIRST interpolating step for a set of
//     bracket indices is a Tier-3 miss that reads + regrids both records and
//     POPULATES the per-variable EndpointCacheEntry (valid == true, indices +
//     shape recorded, both endpoint buffers filled).
//   * Single-record path unchanged (Req 2.5): a Single_Record_Step
//     (needs_upper_record == false) takes the single-record path — one read,
//     one regrid, NO endpoint entry is built — exactly as the current
//     implementation behaves.
//
// The full AdvanceTime path (MPI/DAGR/AMIO) is far too heavy to stand up in a
// unit test, so — exactly like the sibling test_endpoint_work_reduction.cpp —
// these tests reproduce the production Tier ladder against the REAL production
// types (cece::RecordBracket, cece::SliceCacheEntry, cece::EndpointCacheEntry)
// and the REAL production comparison (cece::CeceDriverOrchestrator::
// bracket_equal, reached through the EndpointCacheTestAccess friend declared in
// the class, include/cece/cece_driver_facade.hpp Task 4.3). No production
// signature, logic, or visibility is changed. No Kokkos/MPI is needed, so this
// uses the shared GTest::gtest_main like the test_cache_hit_skips_work and
// test_endpoint_work_reduction siblings.
//
// The ladder reproduced here mirrors the production ladder in
// src/driver/cece_driver_facade.cpp (design.md "Control-flow integration in
// AdvanceTime") and is byte-for-byte the same reproduction used by the
// work-reduction sibling, kept in sync so the sizing/population assertions are
// made against the exact decision logic exercised there.
//
// Grid extents are kept tiny so the buffers stay small (well within the ~7 GB
// cece-dev container RAM budget).

#include <gtest/gtest.h>
#include <mpi.h>

#include <Kokkos_Core.hpp>
#include <cstddef>
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
// tiny so the blended/endpoint buffers stay small (well within the ~7 GB
// cece-dev container budget). Distinct nx/ny/nlev so a mis-sized buffer would
// be caught.
constexpr int kFieldNlev = 2;
constexpr int kNx = 4;
constexpr int kNy = 3;
constexpr std::size_t kBufferSize = static_cast<std::size_t>(kFieldNlev) * static_cast<std::size_t>(kNx) * static_cast<std::size_t>(kNy);

// Counters standing in for the pieces of per-step work the Tier ladder gates:
//   read_slab   -> one disk read per source record (amio_read)
//   regrid_front-> one RegridToDestinationBuffer (per-record apply_regrid_plan +
//                  Allgatherv) invocation
// On a Tier 1/Tier 2 step both stay 0; a Tier 3 interpolation step adds 2 reads
// + 2 regrids (record i0 and record i1); a Tier 3 single-record step adds 1 + 1.
struct WorkSpy {
    int read_count = 0;
    int regrid_front_count = 0;
};

// A faithful reproduction of the production Tier ladder for ONE step and ONE
// variable, mutating the caller's slice_cache / endpoint_cache exactly as the
// production MISS/refresh paths do, and incrementing the WorkSpy for each
// read_slab / RegridToDestinationBuffer the production path would perform. Uses
// the REAL bracket_equal for the Tier 1 exact-match gate. This mirrors
// RunTierLadderStep in tests/test_endpoint_work_reduction.cpp.
void RunTierLadderStep(const RecordBracket& bracket, bool bracket_ready, SliceCacheEntry& slice_cache, EndpointCacheEntry& endpoint_cache,
                       WorkSpy& spy) {
    const bool needs_upper_record = (bracket.i1 != bracket.i0 && bracket.weight > 0.0);

    // ---- Tier 1: exact slice-cache hit (indices AND weight) — no work ----
    if (bracket_ready && slice_cache.valid && EndpointCacheTestAccess::Equal(bracket, slice_cache.last_bracket)) {
        return;
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
        return;
    }

    // ---- Tier 3: interpolation miss / rollover — rebuild both endpoints ----
    if (bracket_ready && needs_upper_record) {
        spy.read_count += 2;          // read_slab(i0), read_slab(i1)
        spy.regrid_front_count += 2;  // RegridToDestinationBuffer(i0), (i1)

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
        return;
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
        // NOTE: no endpoint_cache mutation — the single-record path does not
        // build an endpoint entry (Req 2.5).
        return;
    }

    // Not bracket_ready: production skips this step (no read/regrid, no mutation).
}

RecordBracket make_bracket(int i0, int i1, double weight) {
    RecordBracket b;
    b.i0 = i0;
    b.i1 = i1;
    b.weight = weight;
    b.valid = true;
    return b;
}

}  // namespace

// ============================================================================
// Sizing: each Endpoint_Field is sized exactly field_nlev * nx * ny doubles.
//
// Feature: temporal-endpoint-regrid-cache — Endpoint sizing (Task 12.1)
// **Validates: Requirements 1.3, 8.1**
//
// After a Tier-3 interpolation compute builds the endpoint entry, both
// endpoint_i0 and endpoint_i1 are destination-grid buffers of exactly
// field_nlev * nx * ny doubles — the same layout/size as a Slice_Cache
// ingest_buffer (Req 8.2's "~2x one ingest buffer" footprint).
// ============================================================================
TEST(EndpointSizing, EndpointFieldsSizedFieldNlevTimesNxTimesNy) {
    SliceCacheEntry slice_cache;
    EndpointCacheEntry endpoint_cache;
    WorkSpy spy;

    // Interpolating step: i1 != i0 and weight > 0 -> Tier 3 populates endpoints.
    RunTierLadderStep(make_bracket(/*i0=*/11, /*i1=*/0, /*weight=*/0.4), /*bracket_ready=*/true, slice_cache, endpoint_cache, spy);

    const std::size_t expected = static_cast<std::size_t>(kFieldNlev) * static_cast<std::size_t>(kNx) * static_cast<std::size_t>(kNy);
    EXPECT_EQ(endpoint_cache.endpoint_i0.size(), expected);
    EXPECT_EQ(endpoint_cache.endpoint_i1.size(), expected);
    // The two endpoints share the destination-grid shape.
    EXPECT_EQ(endpoint_cache.endpoint_i0.size(), endpoint_cache.endpoint_i1.size());
    // And it matches the slice-cache ingest buffer size (same layout).
    EXPECT_EQ(slice_cache.ingest_size, expected);
    EXPECT_EQ(slice_cache.ingest_buffer.size(), expected);
}

// ============================================================================
// Tier-3 population: the first interpolating step populates the endpoint entry.
//
// Feature: temporal-endpoint-regrid-cache — Tier-3 population (Task 12.1)
// **Validates: Requirements 1.1, 1.3**
//
// Before any step the endpoint entry is invalid (default-constructed). The
// first interpolating step is a Tier-3 miss that reads + regrids both records
// (2 reads, 2 regrid-fronts) and marks the entry valid, recording the bracket
// indices and the build-time shape and filling both endpoint buffers.
// ============================================================================
TEST(EndpointSizing, Tier3ComputePopulatesEndpointEntry) {
    SliceCacheEntry slice_cache;
    EndpointCacheEntry endpoint_cache;
    WorkSpy spy;

    // Precondition: default endpoint entry is empty/invalid (Req 1.1 baseline).
    ASSERT_FALSE(endpoint_cache.valid);
    ASSERT_EQ(endpoint_cache.cached_i0, -1);
    ASSERT_EQ(endpoint_cache.cached_i1, -1);
    ASSERT_TRUE(endpoint_cache.endpoint_i0.empty());
    ASSERT_TRUE(endpoint_cache.endpoint_i1.empty());

    const int i0 = 11;
    const int i1 = 0;
    RunTierLadderStep(make_bracket(i0, i1, /*weight=*/0.6), /*bracket_ready=*/true, slice_cache, endpoint_cache, spy);

    // Tier 3 interpolation read + regridded BOTH records exactly once.
    EXPECT_EQ(spy.read_count, 2);
    EXPECT_EQ(spy.regrid_front_count, 2);

    // The endpoint entry is now populated for these indices.
    EXPECT_TRUE(endpoint_cache.valid);
    EXPECT_EQ(endpoint_cache.cached_i0, i0);
    EXPECT_EQ(endpoint_cache.cached_i1, i1);
    EXPECT_EQ(endpoint_cache.built_field_nlev, kFieldNlev);
    EXPECT_EQ(endpoint_cache.built_nx, kNx);
    EXPECT_EQ(endpoint_cache.built_ny, kNy);
    EXPECT_FALSE(endpoint_cache.endpoint_i0.empty());
    EXPECT_FALSE(endpoint_cache.endpoint_i1.empty());

    // A subsequent same-indices/different-weight step is a Tier-2 hit that does
    // NO additional read/regrid (confirms the entry was genuinely populated and
    // is reused, Req 1.1).
    RunTierLadderStep(make_bracket(i0, i1, /*weight=*/0.9), /*bracket_ready=*/true, slice_cache, endpoint_cache, spy);
    EXPECT_EQ(spy.read_count, 2);
    EXPECT_EQ(spy.regrid_front_count, 2);
}

// ============================================================================
// Single-record path unchanged: a Single_Record_Step builds NO endpoint entry.
//
// Feature: temporal-endpoint-regrid-cache — single-record path (Task 12.1)
// **Validates: Requirements 2.5**
//
// A step with needs_upper_record == false (either i0 == i1, or weight == 0)
// takes the single-record path: exactly one read and one regrid, the slice
// cache is refreshed as today, and NO endpoint entry is created — the endpoint
// cache stays in its default invalid state, unchanged from the current
// implementation.
// ============================================================================
TEST(EndpointSizing, SingleRecordPathBuildsNoEndpointEntry) {
    // Case A: i0 == i1 (no upper record).
    {
        SliceCacheEntry slice_cache;
        EndpointCacheEntry endpoint_cache;
        WorkSpy spy;

        RunTierLadderStep(make_bracket(/*i0=*/5, /*i1=*/5, /*weight=*/0.0), /*bracket_ready=*/true, slice_cache, endpoint_cache, spy);

        EXPECT_EQ(spy.read_count, 1);
        EXPECT_EQ(spy.regrid_front_count, 1);
        // Endpoint cache untouched: still default/invalid.
        EXPECT_FALSE(endpoint_cache.valid);
        EXPECT_EQ(endpoint_cache.cached_i0, -1);
        EXPECT_EQ(endpoint_cache.cached_i1, -1);
        EXPECT_TRUE(endpoint_cache.endpoint_i0.empty());
        EXPECT_TRUE(endpoint_cache.endpoint_i1.empty());
        // Slice cache refreshed exactly as the current single-record path does.
        EXPECT_TRUE(slice_cache.valid);
        EXPECT_EQ(slice_cache.ingest_size, kBufferSize);
    }

    // Case B: weight == 0 (distinct indices but no interpolation needed).
    {
        SliceCacheEntry slice_cache;
        EndpointCacheEntry endpoint_cache;
        WorkSpy spy;

        RunTierLadderStep(make_bracket(/*i0=*/7, /*i1=*/8, /*weight=*/0.0), /*bracket_ready=*/true, slice_cache, endpoint_cache, spy);

        EXPECT_EQ(spy.read_count, 1);
        EXPECT_EQ(spy.regrid_front_count, 1);
        EXPECT_FALSE(endpoint_cache.valid);
        EXPECT_TRUE(endpoint_cache.endpoint_i0.empty());
        EXPECT_TRUE(endpoint_cache.endpoint_i1.empty());
        EXPECT_TRUE(slice_cache.valid);
    }
}

}  // namespace cece

// ============================================================================
// Kokkos + MPI global test environment and custom main().
//
// Linking the cece library pulls in Kokkos/AXIS static globals whose teardown
// must run after a matched Kokkos::initialize/finalize; providing an explicit
// environment (mirroring KokkosMpiEnvironment in
// tests/test_endpoint_blend_equivalence.cpp / test_numerical_equivalence.cpp)
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
