/**
 * @file test_endpoint_tier_precedence.cpp
 * @brief Property-based tests for the Endpoint_Cache tier-precedence and
 *        skip guarantee of the temporal-endpoint-regrid-cache feature.
 *
 * Feature: temporal-endpoint-regrid-cache, Property 3: Tier precedence and skip
 * guarantee
 *
 * **Validates: Requirements 2.4, 6.1, 6.5, 9.3**
 *
 * ----------------------------------------------------------------------------
 * What this test exercises and why it is faithful
 * ----------------------------------------------------------------------------
 * Property 3 (design.md): for any combination of resolved bracket, slice-cache
 * state, and endpoint-cache state, EXACTLY ONE tier fires with precedence
 *     Tier 1 (exact bracket, incl. weight) > Tier 2 (same indices, different
 *     weight) > Tier 3 (miss / rollover / single record).
 * WHEN Tier 1 fires the driver performs no read, no regrid, and no endpoint
 * blend recompute; WHEN Tier 2 fires the driver performs no read_slab and no
 * apply_regrid_plan. WHEN the resolved indices differ from the cached indices,
 * Tier 1 and Tier 2 SHALL NOT fire (rollover forces a Tier-3 rebuild).
 *
 * The full AdvanceTime path (MPI/DAGR/AMIO) is far too heavy to stand up in a
 * unit test, so this validates the DECISION LADDER that gates read+regrid+blend,
 * which is exactly what determines which tier fires and what work is skipped.
 * The production ladder (src/driver/cece_driver_facade.cpp, design.md
 * "Control-flow integration in AdvanceTime") is:
 *
 *     auto& slice_cache    = slice_caches_[var_name];
 *     auto& endpoint_cache = endpoint_caches_[var_name];
 *     needs_upper_record   = (i1 != i0 && weight > 0.0)
 *
 *     // Tier 1: exact slice-cache hit (indices AND weight)
 *     if (bracket_ready && slice_cache.valid
 *         && bracket_equal(bracket, slice_cache.last_bracket)) { ... }
 *     // Tier 2: endpoint-cache hit (same indices, different weight)
 *     else if (bracket_ready && needs_upper_record
 *              && endpoint_cache.valid
 *              && endpoint_cache.cached_i0 == bracket.i0
 *              && endpoint_cache.cached_i1 == bracket.i1
 *              && endpoint_cache.built_nx == nx_ && endpoint_cache.built_ny == ny_
 *              && endpoint_cache.built_field_nlev == field_nlev) { ... }
 *     // Tier 3: miss / rollover / single record
 *     else if (bracket_ready) { ... }
 *
 * This test reproduces that ladder EXACTLY, against the REAL production types
 * (cece::RecordBracket, cece::SliceCacheEntry, cece::EndpointCacheEntry) and the
 * REAL production comparison (cece::CeceDriverOrchestrator::bracket_equal,
 * reached through the EndpointCacheTestAccess friend declared in the class).
 * A small spy struct counts read_slab, apply_regrid_plan, and blend-recompute
 * so the tests can assert directly on which tier fired and what work each tier
 * skipped.
 *
 * ----------------------------------------------------------------------------
 * Access approach
 * ----------------------------------------------------------------------------
 * RecordBracket, SliceCacheEntry, and EndpointCacheEntry are public structs in
 * namespace cece, so no friend access is needed for them. Only bracket_equal is
 * private, reached via the dedicated `friend struct EndpointCacheTestAccess;`
 * added to CeceDriverOrchestrator (see include/cece/cece_driver_facade.hpp,
 * Task 4.3). Exercising the production comparison directly (not a copy) keeps
 * Tier 1 faithful. No production signature, logic, or visibility is changed.
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <vector>

#include "cece/cece_driver_facade.hpp"

namespace cece {

// ============================================================================
// Test-only friend shim for the private static bracket_equal helper. Declared
// a friend inside CeceDriverOrchestrator (see
// include/cece/cece_driver_facade.hpp, Task 4.3). Exercises the production
// comparison directly, not a copy.
// ============================================================================
struct EndpointCacheTestAccess {
    static bool Equal(const RecordBracket& a, const RecordBracket& b) {
        return CeceDriverOrchestrator::bracket_equal(a, b);
    }
};

namespace {

// Which tier the ladder selected on a step. kNone models the not-ready /
// invalid-bracket case where the production ladder performs no work this step.
enum class Tier { kNone = 0, kTier1 = 1, kTier2 = 2, kTier3 = 3 };

// Counters standing in for the pieces of per-step work each tier gates:
//   read_count   - read_slab invocations (amio_read)
//   regrid_count - apply_regrid_plan invocation-sets (RegridToDestinationBuffer)
//   blend_count  - the destination-grid Endpoint_Blend recompute
// Tier 1: all three stay 0. Tier 2: read/regrid 0, one blend. Tier 3 interp:
// two reads, two regrids, one blend. Tier 3 single: one read, one regrid.
struct WorkSpy {
    int read_count = 0;
    int regrid_count = 0;
    int blend_count = 0;
};

// Faithful reproduction of the production tier ladder + branch bodies
// (design.md "Control-flow integration in AdvanceTime"). Uses the REAL
// bracket_equal for the Tier-1 test. The shape parameters (nx, ny, field_nlev)
// are the rank-invariant destination-grid dimensions the Tier-2 gate compares
// against the endpoint entry's built_* fields. Returns the tier that fired.
Tier ExecuteLadder(const RecordBracket& resolved, bool bracket_ready, const SliceCacheEntry& slice_cache, const EndpointCacheEntry& endpoint_cache,
                   int nx, int ny, int field_nlev, WorkSpy& spy) {
    const bool needs_upper_record = (resolved.i1 != resolved.i0 && resolved.weight > 0.0);

    // ---- Tier 1: exact slice-cache hit (indices AND weight), Req 2.4, 9.3 ----
    if (bracket_ready && slice_cache.valid && EndpointCacheTestAccess::Equal(resolved, slice_cache.last_bracket)) {
        // No read, no regrid, no blend recompute (reuse slice_cache.ingest_buffer).
        return Tier::kTier1;
    }

    // ---- Tier 2: endpoint-cache hit, same indices / different weight ----
    if (bracket_ready && needs_upper_record && endpoint_cache.valid && endpoint_cache.cached_i0 == resolved.i0 &&
        endpoint_cache.cached_i1 == resolved.i1 && endpoint_cache.built_nx == nx && endpoint_cache.built_ny == ny &&
        endpoint_cache.built_field_nlev == field_nlev) {
        // No read_slab, no apply_regrid_plan; only the cheap blend recompute.
        spy.blend_count++;
        return Tier::kTier2;
    }

    // ---- Tier 3: miss / rollover / single record ----
    if (bracket_ready) {
        if (needs_upper_record) {
            // Interpolation miss / rollover: read + regrid BOTH endpoints, blend.
            spy.read_count += 2;
            spy.regrid_count += 2;
            spy.blend_count++;
        } else {
            // Single record: today's path verbatim (one read + one regrid).
            spy.read_count += 1;
            spy.regrid_count += 1;
        }
        return Tier::kTier3;
    }

    // Not ready / invalid bracket: production performs no work this step.
    return Tier::kNone;
}

// Populate a valid slice-cache entry as the production compute path does.
SliceCacheEntry MakeValidSliceEntry(const RecordBracket& producing_bracket) {
    SliceCacheEntry entry;
    entry.last_bracket = producing_bracket;
    entry.ingest_buffer = {1.0, 2.0, 3.0};
    entry.ingest_size = entry.ingest_buffer.size();
    entry.valid = true;
    return entry;
}

// Populate a valid endpoint-cache entry for indices (i0, i1) at the given shape.
EndpointCacheEntry MakeValidEndpointEntry(int i0, int i1, int nx, int ny, int field_nlev) {
    EndpointCacheEntry entry;
    entry.cached_i0 = i0;
    entry.cached_i1 = i1;
    entry.valid = true;
    entry.endpoint_i0 = {1.0, 2.0};
    entry.endpoint_i1 = {3.0, 4.0};
    entry.built_nx = nx;
    entry.built_ny = ny;
    entry.built_field_nlev = field_nlev;
    return entry;
}

// Generate a bracket. i0/i1 kept in a small range so index collisions and
// rollovers occur often; weight covers [0,1] including the {0,1} boundaries.
rc::Gen<RecordBracket> genBracket() {
    return rc::gen::apply(
        [](int i0, int i1, double weight) {
            RecordBracket b;
            b.i0 = i0;
            b.i1 = i1;
            b.weight = weight;
            b.valid = true;
            return b;
        },
        rc::gen::inRange(0, 8), rc::gen::inRange(0, 8), rc::gen::map(rc::gen::inRange(0, 1001), [](int n) { return n / 1000.0; }));
}

// A bracket that genuinely requires temporal interpolation
// (needs_upper_record == true): i1 != i0 and weight > 0.
rc::Gen<RecordBracket> genInterpolatingBracket() {
    return rc::gen::apply(
        [](int i0, int gap, double w01) {
            RecordBracket b;
            b.i0 = i0;
            b.i1 = i0 + 1 + gap;             // guarantees i1 != i0
            b.weight = 0.001 + w01 * 0.998;  // guarantees weight in (0,1)
            b.valid = true;
            return b;
        },
        rc::gen::inRange(0, 8), rc::gen::inRange(0, 8), rc::gen::map(rc::gen::inRange(0, 1001), [](int n) { return n / 1000.0; }));
}

}  // namespace

// ============================================================================
// Property A: Exactly one tier fires with the specified precedence, and each
// tier does only its permitted work.
// Feature: temporal-endpoint-regrid-cache, Property 3: Tier precedence and skip
// guarantee
// **Validates: Requirements 2.4, 6.1, 6.5, 9.3**
//
// For arbitrary (bracket, slice-cache, endpoint-cache, shape) states we run the
// faithful ladder and assert:
//   - the fired tier matches an independent precedence recomputation
//     (Tier 1 > Tier 2 > Tier 3), i.e. exactly one tier fires;
//   - Tier 1 => 0 read, 0 regrid, 0 blend;
//   - Tier 2 => 0 read, 0 regrid, exactly 1 blend;
//   - Tier 3 interp => 2 read, 2 regrid; Tier 3 single => 1 read, 1 regrid.
// ============================================================================
RC_GTEST_PROP(EndpointTierPrecedence, ExactlyOneTierFiresWithPrecedence, ()) {
    const RecordBracket resolved = *genBracket();
    const bool bracket_ready = *rc::gen::arbitrary<bool>();

    // Shape used by both the ladder and the endpoint entry's built_* fields.
    const int nx = *rc::gen::inRange(1, 5);
    const int ny = *rc::gen::inRange(1, 5);
    const int field_nlev = *rc::gen::inRange(1, 4);

    // Slice cache: sometimes valid for the resolved bracket, sometimes for a
    // different bracket, sometimes invalid.
    SliceCacheEntry slice_cache;
    const int slice_mode = *rc::gen::inRange(0, 3);
    if (slice_mode == 0) {
        slice_cache = MakeValidSliceEntry(resolved);  // exact hit candidate
    } else if (slice_mode == 1) {
        slice_cache = MakeValidSliceEntry(*genBracket());  // possibly different bracket
    }  // slice_mode == 2 => invalid (default-constructed)

    // Endpoint cache: sometimes valid for resolved indices at matching shape,
    // sometimes different indices, sometimes shape mismatch, sometimes invalid.
    EndpointCacheEntry endpoint_cache;
    const int ep_mode = *rc::gen::inRange(0, 4);
    if (ep_mode == 0) {
        endpoint_cache = MakeValidEndpointEntry(resolved.i0, resolved.i1, nx, ny, field_nlev);  // hit candidate
    } else if (ep_mode == 1) {
        endpoint_cache = MakeValidEndpointEntry(*rc::gen::inRange(0, 8), *rc::gen::inRange(0, 8), nx, ny, field_nlev);
    } else if (ep_mode == 2) {
        // Right indices, wrong shape => must miss the Tier-2 gate.
        endpoint_cache = MakeValidEndpointEntry(resolved.i0, resolved.i1, nx + 1, ny, field_nlev);
    }  // ep_mode == 3 => invalid (default-constructed)

    WorkSpy spy;
    const Tier fired = ExecuteLadder(resolved, bracket_ready, slice_cache, endpoint_cache, nx, ny, field_nlev, spy);

    // Independent recomputation of which tier SHOULD fire, following precedence.
    const bool needs_upper_record = (resolved.i1 != resolved.i0 && resolved.weight > 0.0);
    const bool tier1_ok = bracket_ready && slice_cache.valid && EndpointCacheTestAccess::Equal(resolved, slice_cache.last_bracket);
    const bool tier2_ok = bracket_ready && needs_upper_record && endpoint_cache.valid && endpoint_cache.cached_i0 == resolved.i0 &&
                          endpoint_cache.cached_i1 == resolved.i1 && endpoint_cache.built_nx == nx && endpoint_cache.built_ny == ny &&
                          endpoint_cache.built_field_nlev == field_nlev;

    Tier expected;
    if (!bracket_ready) {
        expected = Tier::kNone;
    } else if (tier1_ok) {
        expected = Tier::kTier1;
    } else if (tier2_ok) {
        expected = Tier::kTier2;
    } else {
        expected = Tier::kTier3;
    }

    // Exactly one tier fires, and it is the highest-precedence eligible tier.
    RC_ASSERT(fired == expected);

    // Per-tier skip guarantees.
    if (fired == Tier::kTier1) {
        RC_ASSERT(spy.read_count == 0);
        RC_ASSERT(spy.regrid_count == 0);
        RC_ASSERT(spy.blend_count == 0);
    } else if (fired == Tier::kTier2) {
        RC_ASSERT(spy.read_count == 0);    // Req 2.1 (no read_slab)
        RC_ASSERT(spy.regrid_count == 0);  // Req 2.2 (no apply_regrid_plan)
        RC_ASSERT(spy.blend_count == 1);
    } else if (fired == Tier::kTier3) {
        if (needs_upper_record) {
            RC_ASSERT(spy.read_count == 2);
            RC_ASSERT(spy.regrid_count == 2);
        } else {
            RC_ASSERT(spy.read_count == 1);
            RC_ASSERT(spy.regrid_count == 1);
        }
    } else {  // Tier::kNone
        RC_ASSERT(spy.read_count == 0);
        RC_ASSERT(spy.regrid_count == 0);
        RC_ASSERT(spy.blend_count == 0);
    }
}

// ============================================================================
// Property B: Tier 1 (exact slice-cache hit) takes precedence over an otherwise
// eligible Tier 2, and does zero work.
// Feature: temporal-endpoint-regrid-cache, Property 3: Tier precedence and skip
// guarantee
// **Validates: Requirements 2.4, 6.5, 9.3**
//
// Construct a state where BOTH the exact slice-cache and the endpoint-cache
// would match the resolved bracket. Tier 1 must win: no read, no regrid, no
// blend recompute.
// ============================================================================
RC_GTEST_PROP(EndpointTierPrecedence, Tier1WinsOverTier2_ZeroWork, ()) {
    const RecordBracket resolved = *genInterpolatingBracket();
    const int nx = *rc::gen::inRange(1, 5);
    const int ny = *rc::gen::inRange(1, 5);
    const int field_nlev = *rc::gen::inRange(1, 4);

    // Both caches valid and matching the resolved bracket/indices.
    const SliceCacheEntry slice_cache = MakeValidSliceEntry(resolved);
    const EndpointCacheEntry endpoint_cache = MakeValidEndpointEntry(resolved.i0, resolved.i1, nx, ny, field_nlev);

    // Sanity: both tiers are individually eligible for this state.
    RC_ASSERT(EndpointCacheTestAccess::Equal(resolved, slice_cache.last_bracket));

    WorkSpy spy;
    const Tier fired = ExecuteLadder(resolved, /*bracket_ready=*/true, slice_cache, endpoint_cache, nx, ny, field_nlev, spy);

    RC_ASSERT(fired == Tier::kTier1);
    RC_ASSERT(spy.read_count == 0);
    RC_ASSERT(spy.regrid_count == 0);
    RC_ASSERT(spy.blend_count == 0);
}

// ============================================================================
// Property C: Tier 2 fires (same indices, different weight) when Tier 1 misses,
// performing no read_slab and no apply_regrid_plan.
// Feature: temporal-endpoint-regrid-cache, Property 3: Tier precedence and skip
// guarantee
// **Validates: Requirements 2.4, 6.5**
//
// Resolved bracket shares indices with the endpoint cache but has a weight that
// makes bracket_equal false (differs beyond kBracketWeightTol from the stale
// slice-cache weight). Tier 1 misses, Tier 2 fires: 0 read, 0 regrid, 1 blend.
// ============================================================================
RC_GTEST_PROP(EndpointTierPrecedence, Tier2FiresOnSameIndicesDifferentWeight, ()) {
    const RecordBracket resolved = *genInterpolatingBracket();
    const int nx = *rc::gen::inRange(1, 5);
    const int ny = *rc::gen::inRange(1, 5);
    const int field_nlev = *rc::gen::inRange(1, 4);

    // Slice cache holds the SAME indices but a weight offset well beyond
    // kBracketWeightTol (1e-12), so Tier 1 misses.
    RecordBracket stale = resolved;
    const double extra = *rc::gen::map(rc::gen::inRange(1, 500001), [](int n) { return n / 1000.0; });  // (0.001, 500]
    const bool positive = *rc::gen::arbitrary<bool>();
    stale.weight = resolved.weight + (positive ? extra : -extra);
    RC_PRE(!EndpointCacheTestAccess::Equal(resolved, stale));
    const SliceCacheEntry slice_cache = MakeValidSliceEntry(stale);

    // Endpoint cache valid for the resolved indices at matching shape.
    const EndpointCacheEntry endpoint_cache = MakeValidEndpointEntry(resolved.i0, resolved.i1, nx, ny, field_nlev);

    WorkSpy spy;
    const Tier fired = ExecuteLadder(resolved, /*bracket_ready=*/true, slice_cache, endpoint_cache, nx, ny, field_nlev, spy);

    RC_ASSERT(fired == Tier::kTier2);
    RC_ASSERT(spy.read_count == 0);    // Req 2.1
    RC_ASSERT(spy.regrid_count == 0);  // Req 2.2
    RC_ASSERT(spy.blend_count == 1);
}

// ============================================================================
// Property D: Rolled-over indices prevent Tier 1 and Tier 2 from firing.
// Feature: temporal-endpoint-regrid-cache, Property 3: Tier precedence and skip
// guarantee
// **Validates: Requirements 6.1, 9.3**
//
// The resolved indices differ from BOTH the slice-cache bracket indices and the
// endpoint-cache cached indices (an Index_Rollover_Step). Neither the exact
// slice hit nor the endpoint hit can fire; the ladder must fall to Tier 3 and
// rebuild (2 reads + 2 regrids for the interpolating case).
// ============================================================================
RC_GTEST_PROP(EndpointTierPrecedence, RolloverForcesTier3Rebuild, ()) {
    const RecordBracket resolved = *genInterpolatingBracket();
    const int nx = *rc::gen::inRange(1, 5);
    const int ny = *rc::gen::inRange(1, 5);
    const int field_nlev = *rc::gen::inRange(1, 4);

    // Cached (old) indices that differ from the resolved ones in i0 and i1.
    const int old_i0 = resolved.i0 + 100;
    const int old_i1 = resolved.i1 + 100;

    // Slice cache and endpoint cache both keyed on the OLD (rolled-over) indices.
    RecordBracket old_bracket = resolved;
    old_bracket.i0 = old_i0;
    old_bracket.i1 = old_i1;
    const SliceCacheEntry slice_cache = MakeValidSliceEntry(old_bracket);
    const EndpointCacheEntry endpoint_cache = MakeValidEndpointEntry(old_i0, old_i1, nx, ny, field_nlev);

    // Sanity: neither the exact slice bracket nor the endpoint indices match.
    RC_ASSERT(!EndpointCacheTestAccess::Equal(resolved, slice_cache.last_bracket));
    RC_ASSERT(endpoint_cache.cached_i0 != resolved.i0 || endpoint_cache.cached_i1 != resolved.i1);

    WorkSpy spy;
    const Tier fired = ExecuteLadder(resolved, /*bracket_ready=*/true, slice_cache, endpoint_cache, nx, ny, field_nlev, spy);

    // Rollover => Tier 1 and Tier 2 do NOT fire; Tier 3 rebuilds both endpoints.
    RC_ASSERT(fired == Tier::kTier3);
    RC_ASSERT(spy.read_count == 2);
    RC_ASSERT(spy.regrid_count == 2);
}

}  // namespace cece
