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
// signature, logic, or visibility is changed. Linking the cece library pulls in
// Kokkos/AXIS static globals, so this runs on the shared cece_test_main
// environment (MPI + Kokkos brought up and down around the run).
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
#include "endpoint_common.hpp"

namespace cece {

namespace {

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
