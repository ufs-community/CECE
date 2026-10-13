// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors
//
// Feature: temporal-endpoint-regrid-cache
// Property 4: Endpoint hit/miss decision is rank-invariant
//
// The temporal-endpoint-regrid-cache optimization caches the two destination-
// grid regridded endpoint fields regrid(A) (record i0) and regrid(B) (record
// i1), keyed on the bracket indices (i0, i1) ONLY — never the blend weight. On
// a same-indices/different-weight step the driver blends the cached endpoints
// (1-w)*epA + w*epB and skips both disk reads and both regrids (Tier 2). Under
// MPI this fast path is only safe if EVERY rank makes the SAME endpoint-cache
// hit-or-miss decision for a given step — otherwise one rank would read+regrid
// while a peer skips, stranding the peer in a collective gate the reader still
// enters (Req 4.2, 4.3, 4.5).
//
// Property 4 states the endpoint hit/miss decision is a PURE FUNCTION of the
// rank-invariant resolved Bracket_Indices (i0, i1) plus the rank-invariant
// shape (nx_, ny_, field_nlev). Because all of these are identical on every
// rank, the decision is identical on every rank. This test validates that
// decision mechanism directly, without running the full driver (DAGR / AMIO /
// NetCDF), which is far too heavy for a property test.
//
// In production (src/driver/cece_driver_facade.cpp::AdvanceTime) the Tier-2
// endpoint gate fires when:
//
//   bracket_ready && needs_upper_record && endpoint_cache.valid
//     && endpoint_cache.cached_i0 == bracket.i0
//     && endpoint_cache.cached_i1 == bracket.i1
//     && endpoint_cache.built_nx == nx_
//     && endpoint_cache.built_ny == ny_
//     && endpoint_cache.built_field_nlev == field_nlev
//
// Every input to that predicate is rank-invariant: the resolved bracket comes
// from cadence_record_bracket (a pure function of the config-derived
// cadence/tintalgo, the simulation datetime, file_nt, and step_index — all
// rank-invariant), and nx_/ny_/field_nlev are the same on every rank. This
// test reproduces the SAME resolver production uses (the anonymous-namespace
// helpers in the .cpp have internal linkage and cannot be linked directly),
// derives the endpoint hit/miss boolean on every rank, and then:
//
//   * asserts the resolved bracket (i0, i1, interp-mode) is rank-invariant via
//     MPI_Allreduce(MIN) == MPI_Allreduce(MAX) (mirroring production's per-step
//     collective_int_matches);
//   * derives the Tier-2 endpoint hit/miss boolean on every rank from a shared
//     cached endpoint state, deciding index-equality with the REAL production
//     CeceDriverOrchestrator::bracket_equal (reached through the
//     EndpointCacheTestAccess friend shim) applied to an indices-only bracket,
//     and asserts the boolean is rank-invariant via MPI_Allreduce(MIN)==MAX;
//   * asserts single-rank output equals multi-rank output by comparing every
//     rank's decision against a fixed, rank-count-independent reference table.
//
// The test runs correctly both as a single process (ctest default) and under
// mpirun -np 2: the MPI collectives degrade to no-ops at np=1 while the
// per-rank resolution and reference-table comparison still validate the
// rank-invariance property structurally.
//
// Validates: Requirements 4.2, 4.3

#include <gtest/gtest.h>
#include <mpi.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <tick/tick.hpp>
#include <vector>

#include "cece/cece_driver_facade.hpp"
#include "reuse_decision_common.hpp"

namespace cece::test {

namespace {

// ---------------------------------------------------------------------------
// Rank-invariant Tier-2 endpoint hit/miss decision, reproduced faithfully from
// AdvanceTime's endpoint gate. It is a PURE function of the resolved bracket's
// indices (i0, i1) and the rank-invariant shape (nx, ny, field_nlev) — the blend
// weight is intentionally NOT part of the decision, which is the whole point of
// keying on indices only.
//
// Index equality is decided with the REAL production bracket_equal, applied to
// weight-normalized brackets: because the endpoint key excludes the weight, we
// compare the resolved bracket against the cached indices by zeroing both
// weights so bracket_equal reduces to an (i0, i1) equality — exactly the
// indices-only comparison the endpoint gate performs.
// ---------------------------------------------------------------------------
struct EndpointCacheState {
    bool valid = false;
    int cached_i0 = -1;
    int cached_i1 = -1;
    int built_nx = 0;
    int built_ny = 0;
    int built_field_nlev = 0;
};

bool EndpointHit(const RecordBracket& resolved, const EndpointCacheState& cache, int nx, int ny, int field_nlev) {
    if (!NeedsUpperRecord(resolved)) return false;  // single-record steps never take Tier 2
    if (!cache.valid) return false;
    if (cache.built_nx != nx || cache.built_ny != ny || cache.built_field_nlev != field_nlev) return false;

    // Indices-only comparison via the REAL production bracket_equal: normalize
    // weights to zero so bracket_equal reduces to an (i0, i1) equality, mirroring
    // the endpoint gate's `cached_i0 == bracket.i0 && cached_i1 == bracket.i1`.
    RecordBracket resolved_idx = resolved;
    resolved_idx.weight = 0.0;
    RecordBracket cached_idx;
    cached_idx.i0 = cache.cached_i0;
    cached_idx.i1 = cache.cached_i1;
    cached_idx.weight = 0.0;
    return EndpointCacheTestAccess::Equal(resolved_idx, cached_idx);
}

// ---------------------------------------------------------------------------
// Scenario table. Every field is a rank-invariant input in production, so the
// resolved bracket AND the derived endpoint hit/miss decision must be identical
// on every rank. `expected_hit` pins the single-rank reference so the
// "single-rank output equals multi-rank output" assertion is a concrete
// comparison independent of rank count. The cached endpoint state models the
// endpoints built for the previous month's bracket (records 4 & 5, i.e. a June
// linear bracket), shape nx=8 ny=4 nlev=1.
// ---------------------------------------------------------------------------
constexpr int kNx = 8;
constexpr int kNy = 4;
constexpr int kNlev = 1;

struct Scenario {
    const char* name;
    std::string cadence;
    std::string tintalgo;
    std::string sim_time;
    int file_nt;
    int step_index;
    // Cached endpoint state for this step.
    bool cache_valid;
    int cache_i0;
    int cache_i1;
    int cache_nx;
    int cache_ny;
    int cache_nlev;
    // Pinned reference decision.
    bool expected_hit;
};

const std::vector<Scenario>& Scenarios() {
    static const std::vector<Scenario> table = {
        // Same-indices/different-weight June linear step against a cached (4,5)
        // endpoint of matching shape => Tier-2 HIT (skip read + regrid).
        {"june_same_indices_hit", "monthly", "linear", "2020-06-05T06:00:00", 12, 5, true, 4, 5, kNx, kNy, kNlev, true},
        // Another June linear fine step, still (4,5), different weight => HIT.
        {"june_same_indices_hit2", "monthly", "linear", "2020-06-03T18:00:00", 12, 7, true, 4, 5, kNx, kNy, kNlev, true},
        // Rollover into mid-June (5,6) while cache still holds (4,5) => MISS.
        {"june_rollover_miss", "monthly", "linear", "2020-06-25T00:00:00", 12, 20, true, 4, 5, kNx, kNy, kNlev, false},
        // January linear wraps to (11,0) vs cached (4,5) => MISS.
        {"jan_wrap_miss", "monthly", "linear", "2020-01-01T00:00:00", 12, 0, true, 4, 5, kNx, kNy, kNlev, false},
        // Cache invalid (never built) => MISS even though indices match.
        {"cache_invalid_miss", "monthly", "linear", "2020-06-05T06:00:00", 12, 5, false, 4, 5, kNx, kNy, kNlev, false},
        // Shape mismatch (nx differs) => MISS even though indices match.
        {"shape_mismatch_miss", "monthly", "linear", "2020-06-05T06:00:00", 12, 5, true, 4, 5, kNx + 1, kNy, kNlev, false},
        // Single-record monthly-nearest step (needs_upper_record false) => never
        // a Tier-2 hit, regardless of cache.
        {"single_record_miss", "monthly", "nearest", "2020-06-15T12:00:00", 12, 3, true, 5, 5, kNx, kNy, kNlev, false},
        // Hourly single-record step => MISS (no interpolation).
        {"hourly_single_miss", "hourly", "nearest", "2020-03-01T13:00:00", 24, 4, true, 13, 13, kNx, kNy, kNlev, false},
        // Mid-month anchor: frac==0.5 => weight 0 => single record => MISS.
        {"midmonth_weight0_miss", "monthly", "linear", "2020-06-16T00:00:00", 12, 9, true, 5, 6, kNx, kNy, kNlev, false},
    };
    return table;
}

}  // namespace

// ---------------------------------------------------------------------------
// The endpoint hit/miss decision matches the rank-count-independent reference
// AND is rank-invariant. Because the decision is a pure function of the
// resolved (rank-invariant) bracket indices plus rank-invariant shape, the
// boolean each rank computes at np=N equals the fixed reference (== the np=1
// value). This is the core of Property 4 / Req 4.2, 4.3.
// ---------------------------------------------------------------------------
TEST(EndpointReuseDecision, DecisionMatchesReferenceAndIsRankInvariant) {
    for (const Scenario& s : Scenarios()) {
        const RecordBracket resolved = ResolveBracket(s.cadence, s.tintalgo, s.sim_time, s.file_nt, s.step_index);
        EndpointCacheState cache;
        cache.valid = s.cache_valid;
        cache.cached_i0 = s.cache_i0;
        cache.cached_i1 = s.cache_i1;
        cache.built_nx = s.cache_nx;
        cache.built_ny = s.cache_ny;
        cache.built_field_nlev = s.cache_nlev;

        const bool hit = EndpointHit(resolved, cache, kNx, kNy, kNlev);
        SCOPED_TRACE(std::string("scenario=") + s.name);

        // Single-rank reference: np=1 == np=N because the decision is pure.
        EXPECT_EQ(hit, s.expected_hit);

        // Resolved bracket itself is rank-invariant (mirrors per-step
        // collective_int_matches on i0/i1/interp-mode).
        ExpectRankInvariant(resolved.i0, "bracket.i0");
        ExpectRankInvariant(resolved.i1, "bracket.i1");
        ExpectRankInvariant(NeedsUpperRecord(resolved) ? 1 : 0, "needs_upper_record");

        // And the endpoint decision is identical on every rank — no rank reads
        // while a peer skips.
        ExpectRankInvariant(hit ? 1 : 0, "endpoint hit/miss decision");
    }
}

// ---------------------------------------------------------------------------
// Property 4 (RapidCheck, >=100 iterations): for ANY resolved bracket indices
// and ANY cached endpoint state, the endpoint hit/miss decision is a PURE
// FUNCTION of (i0, i1, nx, ny, field_nlev) — the blend weight never affects it.
// This is the property that makes the decision rank-invariant: since production
// resolves (i0, i1) and (nx, ny, field_nlev) identically on every rank (via the
// per-step collective_int_matches and the rank-invariant shape), a decision
// that depends ONLY on those quantities — and never on any rank-local or
// weight-dependent quantity — is necessarily identical on every rank.
//
// We assert the purity via weight-independence: recomputing the decision with
// an arbitrarily different weight (same indices, same cache, same shape) yields
// the identical boolean. Because the decision is a deterministic pure function
// of rank-invariant inputs, feeding it the same inputs on every rank yields the
// same output on every rank — that cross-rank equality is exercised directly by
// the deterministic scenario-table test above (which drives ExpectRankInvariant
// over rank-invariant inputs). This RapidCheck property is intentionally
// rank-local: RapidCheck seeds its generators independently per rank, so the
// generated inputs are NOT rank-invariant, and issuing an MPI collective over a
// per-rank-random derived value would compare unrelated draws rather than the
// property under test. Purity over the whole input space is the correct and
// sufficient PBT obligation for Property 4.
//
// Feature: temporal-endpoint-regrid-cache, Property 4: Endpoint hit/miss
// decision is rank-invariant
// ---------------------------------------------------------------------------
RC_GTEST_PROP(EndpointReuseDecision, HitDecisionIsWeightIndependent, ()) {
    // Small, realistic index/shape space.
    const int i0 = *rc::gen::inRange(0, 12);
    const int i1 = *rc::gen::inRange(0, 12);
    const double weight = *rc::gen::map(rc::gen::inRange(0, 1000), [](int n) { return n / 1000.0; });
    const int nx = *rc::gen::inRange(1, 16);
    const int ny = *rc::gen::inRange(1, 16);
    const int nlev = *rc::gen::inRange(1, 4);

    RecordBracket resolved;
    resolved.i0 = i0;
    resolved.i1 = i1;
    resolved.weight = weight;
    resolved.valid = true;

    // Arbitrary cached endpoint state.
    EndpointCacheState cache;
    cache.valid = *rc::gen::arbitrary<bool>();
    cache.cached_i0 = *rc::gen::inRange(0, 12);
    cache.cached_i1 = *rc::gen::inRange(0, 12);
    cache.built_nx = *rc::gen::element(nx, *rc::gen::inRange(1, 16));
    cache.built_ny = *rc::gen::element(ny, *rc::gen::inRange(1, 16));
    cache.built_field_nlev = *rc::gen::element(nlev, *rc::gen::inRange(1, 4));

    const bool hit = EndpointHit(resolved, cache, nx, ny, nlev);

    // Weight-independence: a different weight (same indices, same cache, same
    // shape) must not change the decision, since the endpoint key excludes the
    // weight. This purity is exactly what guarantees the decision is identical
    // on every rank (all ranks feed the same rank-invariant (i0, i1) and shape).
    const double other_weight = *rc::gen::map(rc::gen::inRange(0, 1000), [](int n) { return n / 1000.0; });
    RecordBracket resolved_other = resolved;
    resolved_other.weight = other_weight;
    const bool hit_other = EndpointHit(resolved_other, cache, nx, ny, nlev);
    RC_ASSERT(hit == hit_other);
}

}  // namespace cece::test
