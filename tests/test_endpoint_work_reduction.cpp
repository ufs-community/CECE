// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors
//
// Feature: temporal-endpoint-regrid-cache, Property 2: Reads and regrids scale
// with distinct index-sets, not steps
//
// **Validates: Requirements 2.1, 2.2, 3.1, 3.2**
//
// ----------------------------------------------------------------------------
// What this test exercises and why it is faithful
// ----------------------------------------------------------------------------
// The temporal-endpoint-regrid-cache optimization caches the two destination-
// grid regridded endpoint fields regrid(A) (record i0) and regrid(B) (record
// i1) keyed on the bracket INDICES (i0, i1) only, never the weight. Because a
// run of consecutive timesteps that all resolve the same (i0, i1) but drifting
// weight w now hits the endpoint cache (Tier 2), those steps perform NO
// read_slab and NO regrid-front (RegridToDestinationBuffer) work — only the
// cheap destination-grid blend. Reads and regrids therefore scale with the
// number of DISTINCT interpolating index-sets, not with the number of steps.
//
// The full AdvanceTime path (MPI/DAGR/AMIO) is far too heavy to stand up in a
// unit test, so this test validates the DECISION LADDER that gates read+regrid,
// which is exactly what determines whether work is performed or skipped. This is
// the sibling of tests/test_cache_hit_skips_work.cpp (Property "Cache-hit steps
// do no read and no regrid apply"): that test counts work on the single-buffer
// slice cache; this test counts work across a MULTI-STEP bracket sequence run
// through the full Tier 1 -> Tier 2 -> Tier 3 endpoint ladder.
//
// The ladder reproduced here mirrors the production ladder in
// src/driver/cece_driver_facade.cpp (design.md "Control-flow integration in
// AdvanceTime"):
//
//     bracket_ready = collective_int_matches(i0) && (i1) && (interp-mode)  // every step
//     needs_upper_record = (i1 != i0 && weight > 0.0)
//     // Tier 1: exact slice-cache hit (indices AND weight) -> reuse, no work
//     if (bracket_ready && slice.valid && bracket_equal(bracket, slice.last_bracket)) ...
//     // Tier 2: endpoint-cache hit (same indices, valid, shape match) -> blend only
//     else if (bracket_ready && needs_upper_record && ep.valid
//              && ep.cached_i0 == i0 && ep.cached_i1 == i1
//              && ep.built_nx == nx && ep.built_ny == ny && ep.built_field_nlev == nlev) ...
//     // Tier 3: interpolation miss / rollover -> read i0, read i1, regrid i0, regrid i1
//     else if (bracket_ready && needs_upper_record) ...
//     // Tier 3: single record -> read i0, regrid i0
//     else if (bracket_ready) ...
//
// It reproduces that ladder EXACTLY, against the REAL production types
// (cece::RecordBracket, cece::SliceCacheEntry, cece::EndpointCacheEntry) and the
// REAL production comparison (cece::CeceDriverOrchestrator::bracket_equal,
// reached through the EndpointCacheTestAccess friend declared in the class). A
// WorkSpy counts read_slab and regrid-front (RegridToDestinationBuffer)
// invocations; executing the faithful ladder over generated bracket sequences
// and asserting on the counters directly validates Req 2.1, 2.2, 3.1, 3.2 at the
// decision level.
//
// ----------------------------------------------------------------------------
// Access approach
// ----------------------------------------------------------------------------
// SliceCacheEntry, EndpointCacheEntry, and RecordBracket are already public
// structs in namespace cece, so no friend access is needed for them. Only
// bracket_equal is private, reached via the dedicated
// `friend struct EndpointCacheTestAccess;` added to CeceDriverOrchestrator
// (see include/cece/cece_driver_facade.hpp, Task 4.3). This exercises the
// production comparison directly, not a copy. No production signature, logic, or
// visibility is changed. No Kokkos/MPI is needed, so this uses the shared
// GTest::gtest_main like the test_cache_hit_skips_work sibling.

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <cmath>
#include <cstddef>
#include <set>
#include <utility>
#include <vector>

#include "cece/cece_driver_facade.hpp"

namespace cece {

// ============================================================================
// Test-only friend shim for the private static bracket_equal helper. Declared a
// friend inside CeceDriverOrchestrator (see
// include/cece/cece_driver_facade.hpp, Task 4.3). Exercises the production
// comparison directly, not a copy.
// ============================================================================
struct EndpointCacheTestAccess {
    static bool Equal(const RecordBracket& a, const RecordBracket& b) {
        return CeceDriverOrchestrator::bracket_equal(a, b);
    }
};

namespace {

// Fixed rank-invariant destination-grid shape for the reproduced ladder. Kept
// tiny so the blended buffers stay small across the >=100 RapidCheck iterations
// (well within the ~7 GB cece-dev container budget).
constexpr int kFieldNlev = 1;
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
// read_slab / RegridToDestinationBuffer the production path would perform.
// Uses the REAL bracket_equal for the Tier 1 exact-match gate.
void RunTierLadderStep(const RecordBracket& bracket, bool bracket_ready, SliceCacheEntry& slice_cache, EndpointCacheEntry& endpoint_cache,
                       WorkSpy& spy) {
    const bool needs_upper_record = (bracket.i1 != bracket.i0 && bracket.weight > 0.0);

    // ---- Tier 1: exact slice-cache hit (indices AND weight) — no work ----
    if (bracket_ready && slice_cache.valid && EndpointCacheTestAccess::Equal(bracket, slice_cache.last_bracket)) {
        // Reuse slice_cache.ingest_buffer: no read, no regrid, no blend recompute.
        return;
    }

    // ---- Tier 2: endpoint-cache hit (same indices, different weight) — blend only ----
    if (bracket_ready && needs_upper_record && endpoint_cache.valid && endpoint_cache.cached_i0 == bracket.i0 &&
        endpoint_cache.cached_i1 == bracket.i1 && endpoint_cache.built_nx == kNx && endpoint_cache.built_ny == kNy &&
        endpoint_cache.built_field_nlev == kFieldNlev) {
        // NO read_slab, NO apply_regrid_plan on this step: blend cached endpoints.
        std::vector<double> blended(kBufferSize);
        const double w = bracket.weight;
        for (std::size_t k = 0; k < kBufferSize; ++k) {
            blended[k] = (1.0 - w) * endpoint_cache.endpoint_i0[k] + w * endpoint_cache.endpoint_i1[k];
        }
        // Refresh slice cache so an immediate exact repeat re-hits Tier 1.
        slice_cache.last_bracket = bracket;
        slice_cache.ingest_buffer = blended;
        slice_cache.ingest_size = blended.size();
        slice_cache.valid = true;
        return;
    }

    // ---- Tier 3: interpolation miss / rollover — rebuild both endpoints ----
    if (bracket_ready && needs_upper_record) {
        // read_slab(i0) -> srcA ; read_slab(i1) -> srcB
        spy.read_count += 2;
        // RegridToDestinationBuffer(i0) -> epA ; RegridToDestinationBuffer(i1) -> epB
        spy.regrid_front_count += 2;

        // Endpoint buffers are deterministic functions of the record index so a
        // Tier 2 re-hit of the same indices reproduces byte-identical endpoints
        // (mirrors regrid(record) being a pure function of the record).
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
        // read_slab(i0) -> src ; AssembleReplicatedField (one regrid-front).
        spy.read_count += 1;
        spy.regrid_front_count += 1;

        std::vector<double> ingest(kBufferSize);
        for (std::size_t k = 0; k < kBufferSize; ++k) {
            ingest[k] = static_cast<double>(bracket.i0) + static_cast<double>(k);
        }
        slice_cache.last_bracket = bracket;
        slice_cache.ingest_buffer = ingest;
        slice_cache.ingest_size = ingest.size();
        slice_cache.valid = true;
        return;
    }

    // Not bracket_ready: production skips this step entirely (no read/regrid,
    // no cache mutation). Nothing to do.
}

// Generate one "run": several consecutive steps that ALL resolve the same
// interpolating index-set (i0, i1) with i1 != i0, differing only in the blend
// weight w so every step needs_upper_record. Returns the brackets for the run.
rc::Gen<std::vector<RecordBracket>> genConstantIndexRun() {
    return rc::gen::apply(
        [](int i0, int gap, const std::vector<int>& weight_permilles) {
            const int i1 = i0 + gap;  // gap >= 1 so i1 != i0
            std::vector<RecordBracket> run;
            run.reserve(weight_permilles.size());
            for (int permille : weight_permilles) {
                RecordBracket b;
                b.i0 = i0;
                b.i1 = i1;
                // weight in (0, 1] so needs_upper_record is always true for the run.
                b.weight = static_cast<double>(permille) / 1000.0;
                b.valid = true;
                run.push_back(b);
            }
            return run;
        },
        rc::gen::inRange(0, 500), rc::gen::inRange(1, 50), rc::gen::container<std::vector<int>>(rc::gen::inRange(1, 1001)));
}

}  // namespace

// ============================================================================
// Property 2a: A single constant-index run does one read+regrid PAIR per
// endpoint, regardless of how many steps the run has.
//
// Feature: temporal-endpoint-regrid-cache, Property 2: Reads and regrids scale
// with distinct index-sets, not steps
// **Validates: Requirements 2.1, 2.2, 3.1, 3.2**
//
// For a contiguous run of steps that all resolve the same interpolating
// (i0, i1) with drifting weight, the first step is a Tier 3 miss (2 reads +
// 2 regrid-fronts, one per endpoint) and every subsequent step is a Tier 2
// endpoint-cache hit (0 reads, 0 regrids). Total reads == 2 and total
// regrid-fronts == 2 no matter how long the run is (Req 2.1, 2.2, 3.1, 3.2).
// ============================================================================
RC_GTEST_PROP(EndpointWorkReduction, ConstantIndexRun_OnePairPerEndpoint, ()) {
    const std::vector<RecordBracket> run = *genConstantIndexRun();
    RC_PRE(!run.empty());

    SliceCacheEntry slice_cache;        // valid == false (first touch)
    EndpointCacheEntry endpoint_cache;  // valid == false (first touch)
    WorkSpy spy;

    for (const RecordBracket& b : run) {
        RunTierLadderStep(b, /*bracket_ready=*/true, slice_cache, endpoint_cache, spy);
    }

    // Exactly one read+regrid PAIR per endpoint for the single distinct
    // index-set: 2 reads and 2 regrid-fronts across the whole run, regardless
    // of how many steps the run has (work does NOT grow per-step).
    RC_ASSERT(spy.read_count == 2);
    RC_ASSERT(spy.regrid_front_count == 2);
}

// ============================================================================
// Property 2b: Across a sequence of runs, reads and regrids scale with the
// number of DISTINCT interpolating index-sets, not the number of steps.
//
// Feature: temporal-endpoint-regrid-cache, Property 2: Reads and regrids scale
// with distinct index-sets, not steps
// **Validates: Requirements 2.1, 2.2, 3.1, 3.2**
//
// Concatenate several constant-index runs (each with many steps) into one
// bracket sequence. The endpoint cache holds exactly one (i0, i1) pair at a
// time, so crossing into a new index-set is a Tier 3 rebuild (2 reads + 2
// regrids) and staying within an index-set is Tier 2 (0 work). The total reads
// and total regrid-fronts each equal 2 x (number of index-set BOUNDARIES
// crossed) and are bounded by 2 x (number of runs) — independent of the total
// step count.
// ============================================================================
RC_GTEST_PROP(EndpointWorkReduction, MultipleRuns_ScaleWithDistinctIndexSets, ()) {
    // 1..6 runs, each a constant-index run of >=1 step.
    const int num_runs = *rc::gen::inRange(1, 7);
    std::vector<std::vector<RecordBracket>> runs;
    runs.reserve(static_cast<std::size_t>(num_runs));
    for (int r = 0; r < num_runs; ++r) {
        std::vector<RecordBracket> run = *genConstantIndexRun();
        if (run.empty()) {
            continue;
        }
        runs.push_back(std::move(run));
    }
    RC_PRE(!runs.empty());

    SliceCacheEntry slice_cache;
    EndpointCacheEntry endpoint_cache;
    WorkSpy spy;

    // Count how many times the resolved index-set (i0, i1) CHANGES from the
    // currently-cached endpoint index-set as the sequence is processed. Each
    // such change forces a Tier 3 rebuild (2 reads + 2 regrid-fronts); steps
    // that keep the cached index-set are Tier 2 (0 work).
    int rebuilds = 0;
    int cached_i0 = -1;
    int cached_i1 = -1;
    std::size_t total_steps = 0;

    for (const std::vector<RecordBracket>& run : runs) {
        for (const RecordBracket& b : run) {
            ++total_steps;
            const bool same_as_cached = (b.i0 == cached_i0 && b.i1 == cached_i1);
            if (!same_as_cached) {
                ++rebuilds;
                cached_i0 = b.i0;
                cached_i1 = b.i1;
            }
            RunTierLadderStep(b, /*bracket_ready=*/true, slice_cache, endpoint_cache, spy);
        }
    }

    // Reads/regrids equal exactly 2 x (index-set rebuilds), NOT 2 x steps.
    RC_ASSERT(spy.read_count == 2 * rebuilds);
    RC_ASSERT(spy.regrid_front_count == 2 * rebuilds);

    // The distinct number of interpolating index-sets in the whole sequence is
    // an upper bound on the rebuild count (a rebuild happens only when the
    // resolved index-set differs from the one currently cached). Reads/regrids
    // are therefore bounded by 2 x distinct-index-sets and never by 2 x steps.
    std::set<std::pair<int, int>> distinct_index_sets;
    for (const std::vector<RecordBracket>& run : runs) {
        for (const RecordBracket& b : run) {
            distinct_index_sets.insert({b.i0, b.i1});
        }
    }
    RC_ASSERT(rebuilds >= 1);
    RC_ASSERT(rebuilds <= static_cast<int>(total_steps));
    RC_ASSERT(spy.read_count <= 2 * static_cast<int>(total_steps));
    // Work scales with distinct index-sets, not steps: whenever any index-set is
    // revisited across steps (the common case with multi-step runs), the read
    // count is strictly below the naive per-step count of 2 x steps.
    if (static_cast<std::size_t>(rebuilds) < total_steps) {
        RC_ASSERT(spy.read_count < 2 * static_cast<int>(total_steps));
    }
}

// ============================================================================
// Property 2c: Weight drift within a fixed index-set never adds work.
//
// Feature: temporal-endpoint-regrid-cache, Property 2: Reads and regrids scale
// with distinct index-sets, not steps
// **Validates: Requirements 2.1, 2.2, 3.1, 3.2**
//
// This is the exact hourly-on-monthly scenario the feature targets: one fixed
// interpolating index-set with the weight drifting every step. After the first
// (Tier 3) step, every additional step — no matter how many, no matter the
// weight — adds 0 reads and 0 regrids. This directly asserts the "reads/regrids
// do not grow per-step" clause of Req 3.1/3.2.
// ============================================================================
RC_GTEST_PROP(EndpointWorkReduction, WeightDriftWithinIndexSet_NoPerStepGrowth, ()) {
    const int i0 = *rc::gen::inRange(0, 500);
    const int i1 = i0 + *rc::gen::inRange(1, 50);
    const int extra_steps = *rc::gen::inRange(1, 40);  // 1..39 drifting-weight steps after the first

    SliceCacheEntry slice_cache;
    EndpointCacheEntry endpoint_cache;
    WorkSpy spy;

    auto make_step = [&](double w) {
        RecordBracket b;
        b.i0 = i0;
        b.i1 = i1;
        b.weight = w;
        b.valid = true;
        return b;
    };

    // Step 0: Tier 3 miss populates both endpoints.
    RunTierLadderStep(make_step(0.25), /*bracket_ready=*/true, slice_cache, endpoint_cache, spy);
    RC_ASSERT(spy.read_count == 2);
    RC_ASSERT(spy.regrid_front_count == 2);

    // Subsequent drifting-weight steps: each is a distinct weight (beyond the
    // slice-cache exact-match tolerance) so it is Tier 2, never Tier 1 or Tier 3.
    for (int s = 0; s < extra_steps; ++s) {
        const double w = static_cast<double>(s + 1) / static_cast<double>(extra_steps + 2);
        RunTierLadderStep(make_step(w), /*bracket_ready=*/true, slice_cache, endpoint_cache, spy);
    }

    // No matter how many drifting-weight steps followed, work stayed at one
    // read+regrid pair per endpoint.
    RC_ASSERT(spy.read_count == 2);
    RC_ASSERT(spy.regrid_front_count == 2);
}

}  // namespace cece
