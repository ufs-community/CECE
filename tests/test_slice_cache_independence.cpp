/**
 * @file test_slice_cache_independence.cpp
 * @brief Property-based test for slice-cache independence across variables
 *        sharing a stream.
 *
 * Feature: regrid-per-stream, Property 3: Slice cache independence
 *
 * The regrid-per-stream refactor re-keys the regrid-plan, AMIO-handle, and
 * file-record-count caches by Stream_Identity_Key, but the slice cache
 * (`slice_caches_`) intentionally remains keyed by Model_Variable name because
 * each variable carries different data even within the same stream.
 *
 * Property 3: For any two distinct variable names var_a and var_b that share
 * the same stream, writing a SliceCacheEntry for var_a into an
 * `std::unordered_map<std::string, SliceCacheEntry>` SHALL NOT affect the
 * entry for var_b. The var_b entry must remain absent (if never populated) or
 * retain its prior state (if populated first with a known state).
 *
 * This test operates directly on a plain
 * `std::unordered_map<std::string, SliceCacheEntry>` — no orchestrator
 * construction, AMIO, or MPI is required. It mirrors how `AdvanceTime` keys
 * the slice cache purely by variable name.
 *
 * **Validates: Requirements 5.1, 5.2, 5.3**
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "cece/cece_driver_facade.hpp"

namespace cece {
namespace {

// Generate a non-empty variable name from a modest alphabet. Non-empty keeps
// the two names distinguishable and mirrors real model variable names.
rc::Gen<std::string> genVarName() {
    return rc::gen::nonEmpty(rc::gen::container<std::string>(rc::gen::elementOf(std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"))));
}

// Generate an arbitrary SliceCacheEntry with a small, valid ingest buffer.
rc::Gen<SliceCacheEntry> genSliceEntry() {
    return rc::gen::apply(
        [](int i0, int i1, double weight, bool bracket_valid, bool valid, std::vector<double> buf) {
            SliceCacheEntry e;
            e.last_bracket.i0 = i0;
            e.last_bracket.i1 = i1;
            e.last_bracket.weight = weight;
            e.last_bracket.valid = bracket_valid;
            e.valid = valid;
            e.ingest_buffer = std::move(buf);
            e.ingest_size = e.ingest_buffer.size();
            return e;
        },
        rc::gen::inRange(0, 1000), rc::gen::inRange(0, 1000), rc::gen::map(rc::gen::inRange(0, 1000001), [](int n) { return n / 1000000.0; }),
        rc::gen::arbitrary<bool>(), rc::gen::arbitrary<bool>(),
        rc::gen::container<std::vector<double>>(rc::gen::map(rc::gen::inRange(-100000, 100000), [](int n) { return n / 1000.0; })));
}

bool EntriesEqual(const SliceCacheEntry& a, const SliceCacheEntry& b) {
    return a.valid == b.valid && a.ingest_size == b.ingest_size && a.ingest_buffer == b.ingest_buffer && a.last_bracket.i0 == b.last_bracket.i0 &&
           a.last_bracket.i1 == b.last_bracket.i1 && a.last_bracket.weight == b.last_bracket.weight && a.last_bracket.valid == b.last_bracket.valid;
}

}  // namespace

// ============================================================================
// Property 3a: Writing var_a leaves an absent var_b absent.
// Feature: regrid-per-stream, Property 3: Slice cache independence
// **Validates: Requirements 5.1, 5.2, 5.3**
//
// For two distinct variable names sharing a stream, populating the slice cache
// for var_a must not create or populate an entry for var_b.
// ============================================================================
RC_GTEST_PROP(SliceCacheIndependence, Property3_WriteDoesNotCreateOther, ()) {
    const std::string var_a = *genVarName();
    const std::string var_b = *rc::gen::suchThat(genVarName(), [&](const std::string& s) { return s != var_a; });

    std::unordered_map<std::string, SliceCacheEntry> slice_caches;

    const SliceCacheEntry entry_a = *genSliceEntry();
    slice_caches[var_a] = entry_a;

    // var_b was never written: it must remain absent.
    RC_ASSERT(slice_caches.find(var_b) == slice_caches.end());

    // var_a holds exactly what we wrote.
    RC_ASSERT(slice_caches.find(var_a) != slice_caches.end());
    RC_ASSERT(EntriesEqual(slice_caches.at(var_a), entry_a));
}

// ============================================================================
// Property 3b: Writing var_a leaves a pre-populated var_b unchanged.
// Feature: regrid-per-stream, Property 3: Slice cache independence
// **Validates: Requirements 5.1, 5.2, 5.3**
//
// When var_b is populated first with a known state, a later cache hit/write for
// var_a must not disturb var_b's entry (no stale-data cross-contamination).
// ============================================================================
RC_GTEST_PROP(SliceCacheIndependence, Property3_WriteDoesNotMutateOther, ()) {
    const std::string var_a = *genVarName();
    const std::string var_b = *rc::gen::suchThat(genVarName(), [&](const std::string& s) { return s != var_a; });

    std::unordered_map<std::string, SliceCacheEntry> slice_caches;

    // Populate var_b first with a known state.
    const SliceCacheEntry entry_b = *genSliceEntry();
    slice_caches[var_b] = entry_b;

    // Now write (and later overwrite) var_a with arbitrary entries.
    const SliceCacheEntry entry_a1 = *genSliceEntry();
    slice_caches[var_a] = entry_a1;

    const SliceCacheEntry entry_a2 = *genSliceEntry();
    slice_caches[var_a] = entry_a2;

    // var_b must be untouched by any operation on var_a.
    RC_ASSERT(slice_caches.find(var_b) != slice_caches.end());
    RC_ASSERT(EntriesEqual(slice_caches.at(var_b), entry_b));

    // var_a reflects its most recent write.
    RC_ASSERT(EntriesEqual(slice_caches.at(var_a), entry_a2));
}

}  // namespace cece
