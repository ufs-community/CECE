// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors
//
// Feature: driver-io-regrid-perf
// Property 8: Reuse decision is identical across all MPI ranks
//
// The driver-io-regrid-perf optimization skips redundant reads/regrids when the
// resolved time bracket is unchanged between timesteps. Under MPI this is only
// safe if EVERY rank resolves the SAME time bracket and therefore makes the
// SAME reuse-vs-recompute (cache hit/miss) decision — otherwise one rank could
// read while another skips, deadlocking the collective gates or silently
// corrupting the ingested field.
//
// Running the full driver (DAGR/AMIO/NetCDF) across ranks is far too heavy for
// an integration test, so this test validates the DECISION MECHANISM that
// production relies on. In production
// (src/driver/cece_driver_facade.cpp::AdvanceTime) the read/skip decision is
// driven by two pieces of logic:
//
//   1. Bracket resolution: cadence_record_bracket(cadence, tintalgo, sim_dt,
//      file_nt) with a legacy step-index fallback
//      (t_idx = step_index % file_nt) when the cadence yields no valid bracket.
//      Every input to this resolver — the config-derived cadence/tintalgo, the
//      simulation datetime, file_nt, and step_index — is rank-invariant, so the
//      resolved (i0, i1, weight, interp-mode) is a pure function of rank-
//      invariant data and MUST be identical on every rank.
//
//   2. The per-step cross-rank confirmation: production runs
//      collective_int_matches on bracket.i0, bracket.i1, and the interp-mode
//      flag EVERY step, and derives the hit/miss decision from a prior bracket
//      via CeceDriverOrchestrator::bracket_equal.
//
// This test faithfully reproduces the SAME resolver production uses (it is a
// pure function of rank-invariant inputs — the anonymous-namespace helpers in
// the .cpp cannot be linked directly), computes the bracket independently on
// every rank, and then:
//
//   * asserts rank-invariance directly via MPI_Allreduce(MIN) == MPI_Allreduce(MAX)
//     over i0, i1, and the interp-mode flag (mirroring production's
//     collective_int_matches), so every rank resolves an identical bracket;
//   * derives the hit/miss boolean on every rank from a shared prior bracket
//     using the REAL production CeceDriverOrchestrator::bracket_equal (reached
//     through the CrossRankReuseTestAccess friend shim) and asserts the boolean
//     is identical across ranks via MPI_Allreduce — no rank reads while another
//     skips;
//   * asserts single-rank output equals multi-rank output by comparing every
//     rank's resolved bracket against a fixed expected reference table that is
//     independent of rank count (the bracket at np=1 equals the bracket each
//     rank computes at np=N for the same inputs).
//
// The test runs correctly both as a single process (ctest default) and under
// mpirun -np 2 / -np 4: the MPI collectives degrade to no-ops at np=1 while the
// per-rank resolution and the reference-table comparison still validate the
// rank-invariance property structurally.
//
// Validates: Requirements 6.3, 6.4, 6.5

#include <gtest/gtest.h>
#include <mpi.h>
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

using CrossRankReuseTestAccess = cece::EndpointCacheTestAccess;

namespace cece::test {

namespace {

// Production's interp-mode flag: needs an upper record only when i1 != i0 and
// the blend weight is strictly positive.
constexpr inline int InterpMode(const RecordBracket& b) {
    return NeedsUpperRecord(b) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Scenario table. Every field (cadence, tintalgo, sim_time, file_nt,
// step_index) is a rank-invariant input in production, so the resolved bracket
// must be identical on every rank. `expected_*` pin the single-rank reference
// values so the "single-rank output equals multi-rank output" assertion is a
// concrete comparison independent of rank count.
// ---------------------------------------------------------------------------
struct Scenario {
    const char* name;
    std::string cadence;
    std::string tintalgo;
    std::string sim_time;
    int file_nt;
    int step_index;
    int expected_i0;
    int expected_i1;
    int expected_interp_mode;
};

const std::vector<Scenario>& Scenarios() {
    static const std::vector<Scenario> table = {
        // Legacy step-index cycling (no cadence): t_idx = step % file_nt.
        {"cycle_step0_nt12", "", "nearest", "2020-06-15T00:00:00", 12, 0, 0, 0, 0},
        {"cycle_step7_nt12", "", "nearest", "2020-06-15T00:00:00", 12, 7, 7, 7, 0},
        {"cycle_step13_nt12", "", "nearest", "2020-06-15T00:00:00", 12, 13, 1, 1, 0},
        {"cycle_step5_nt1", "", "nearest", "2020-06-15T00:00:00", 1, 5, 0, 0, 0},
        // Hourly cadence: nearest hour-of-day record.
        {"hourly_00Z", "hourly", "nearest", "2020-03-01T00:00:00", 24, 0, 0, 0, 0},
        {"hourly_13Z", "hourly", "nearest", "2020-03-01T13:00:00", 24, 3, 13, 13, 0},
        {"hourly_23Z", "hourly", "nearest", "2020-03-01T23:00:00", 24, 9, 23, 23, 0},
        // Weekly cadence: nearest day-of-week record. 2020-01-01 is a Wednesday
        // (index 3, 0=Sunday); 2020-01-05 is a Sunday (index 0).
        {"weekly_wed", "weekly", "nearest", "2020-01-01T06:00:00", 7, 2, 3, 3, 0},
        {"weekly_sun", "weekly", "nearest", "2020-01-05T06:00:00", 7, 4, 0, 0, 0},
        // Monthly nearest: month-1 record.
        {"monthly_nearest_jun", "monthly", "nearest", "2020-06-15T12:00:00", 12, 1, 5, 5, 0},
        {"monthly_nearest_jan", "monthly", "nearest", "2020-01-10T00:00:00", 12, 8, 0, 0, 0},
        // Monthly linear, mid-month convention. June (m=5), day 25 of 30 =>
        // frac = 24/30 = 0.8 >= 0.5 => i0=5, i1=6, interp on.
        {"monthly_linear_jun25", "monthly", "linear", "2020-06-25T00:00:00", 12, 2, 5, 6, 1},
        // June day 1 => frac = 0/30 = 0 < 0.5 => i0=4, i1=5, interp on.
        {"monthly_linear_jun01", "monthly", "linear", "2020-06-01T00:00:00", 12, 6, 4, 5, 1},
        // Jan day 1 => frac = 0 < 0.5 => wraps: i0=(0-1+12)%12=11, i1=0, interp on.
        {"monthly_linear_jan01", "monthly", "linear", "2020-01-01T00:00:00", 12, 0, 11, 0, 1},
        // Monthly linear exactly at mid-month anchor => frac==0.5 => i0=m, i1=m+1,
        // weight 0 => interp OFF (single read). June has 30 days; day 16 hour 0 =>
        // frac = 15/30 = 0.5.
        {"monthly_linear_midmonth", "monthly", "linear", "2020-06-16T00:00:00", 12, 3, 5, 6, 0},
    };
    return table;
}

}  // namespace

// ---------------------------------------------------------------------------
// Every rank resolves an IDENTICAL bracket (i0, i1, interp-mode) for the same
// rank-invariant inputs. Asserted directly with MPI_Allreduce(MIN)==MAX, which
// is exactly what production's collective_int_matches enforces per step. This
// is the core of Property 8 / Req 6.4.
// ---------------------------------------------------------------------------
TEST(MpiReuseDecision, BracketIdenticalAcrossRanks) {
    for (const Scenario& s : Scenarios()) {
        const RecordBracket b = ResolveBracket(s.cadence, s.tintalgo, s.sim_time, s.file_nt, s.step_index);
        SCOPED_TRACE(std::string("scenario=") + s.name);
        ExpectRankInvariant(b.i0, "bracket.i0");
        ExpectRankInvariant(b.i1, "bracket.i1");
        ExpectRankInvariant(InterpMode(b), "bracket interp-mode");
        // The weight is derived deterministically from rank-invariant sim time;
        // scale it into an integer so the same MIN==MAX check covers it too.
        const int weight_scaled = static_cast<int>(std::llround(b.weight * 1e9));
        ExpectRankInvariant(weight_scaled, "bracket.weight (scaled)");
    }
}

// ---------------------------------------------------------------------------
// Single-rank output equals multi-rank output: because the resolved bracket is
// a rank-invariant function of the inputs, the bracket each rank computes at
// np=N equals the fixed reference (== the np=1 value) for the same inputs.
// Comparing against a table that does not depend on rank count encodes the
// "np=1 == np=N" guarantee (Req 6.5).
// ---------------------------------------------------------------------------
TEST(MpiReuseDecision, BracketMatchesRankIndependentReference) {
    for (const Scenario& s : Scenarios()) {
        const RecordBracket b = ResolveBracket(s.cadence, s.tintalgo, s.sim_time, s.file_nt, s.step_index);
        SCOPED_TRACE(std::string("scenario=") + s.name);
        EXPECT_EQ(b.i0, s.expected_i0);
        EXPECT_EQ(b.i1, s.expected_i1);
        EXPECT_EQ(InterpMode(b), s.expected_interp_mode);
        // And this reference must itself be the same on every rank.
        ExpectRankInvariant(b.i0, "reference i0");
        ExpectRankInvariant(b.i1, "reference i1");
    }
}

// ---------------------------------------------------------------------------
// The reuse-vs-recompute (cache hit/miss) decision is identical on every rank.
// Each rank derives the boolean from a shared prior bracket using the REAL
// production bracket_equal, then we allreduce the boolean and assert MIN==MAX
// so no rank reads while another skips (Req 6.3).
//
// The prior bracket for each scenario is the bracket resolved for the PREVIOUS
// scenario in the table, giving a realistic mix of genuine hits (same bracket)
// and misses (different bracket) across the sequence.
// ---------------------------------------------------------------------------
TEST(MpiReuseDecision, ReuseDecisionIdenticalAcrossRanks) {
    const std::vector<Scenario>& table = Scenarios();
    // Seed the prior bracket with an obviously-different sentinel so step 0 is a
    // genuine miss on every rank.
    RecordBracket prior;
    prior.i0 = -1;
    prior.i1 = -1;
    prior.weight = 0.0;
    prior.valid = true;

    for (const Scenario& s : table) {
        const RecordBracket resolved = ResolveBracket(s.cadence, s.tintalgo, s.sim_time, s.file_nt, s.step_index);
        // Production gate: cache hit iff a valid prior bracket compares equal
        // under the REAL bracket_equal.
        const bool cache_hit = prior.valid && CrossRankReuseTestAccess::Equal(resolved, prior);
        SCOPED_TRACE(std::string("scenario=") + s.name);
        ExpectRankInvariant(cache_hit ? 1 : 0, "cache hit/miss decision");
        prior = resolved;
    }
}

// ---------------------------------------------------------------------------
// A repeated-bracket sequence must yield a HIT on every rank on the repeat, and
// that HIT decision must be identical across ranks. This mirrors the production
// intent that consecutive fine timesteps resolving to the same bracket reuse
// the cache in lock-step. Uses the REAL bracket_equal.
// ---------------------------------------------------------------------------
TEST(MpiReuseDecision, RepeatedBracketIsHitInLockStep) {
    // Same monthly-nearest scenario at two different fine timesteps that resolve
    // to the same June record — a genuine cache HIT.
    const RecordBracket first = ResolveBracket("monthly", "nearest", "2020-06-05T00:00:00", 12, 10);
    const RecordBracket second = ResolveBracket("monthly", "nearest", "2020-06-25T00:00:00", 12, 11);

    // Both resolve to the same record (June => index 5).
    ASSERT_EQ(first.i0, 5);
    ASSERT_EQ(second.i0, 5);

    const bool hit = first.valid && CrossRankReuseTestAccess::Equal(second, first);
    EXPECT_TRUE(hit) << "repeated bracket should be a cache hit under production bracket_equal";
    ExpectRankInvariant(hit ? 1 : 0, "repeated-bracket hit decision");

    // And a genuinely different bracket (July) must be a MISS, in lock-step.
    const RecordBracket different = ResolveBracket("monthly", "nearest", "2020-07-05T00:00:00", 12, 12);
    ASSERT_EQ(different.i0, 6);
    const bool miss = !(first.valid && CrossRankReuseTestAccess::Equal(different, first));
    EXPECT_TRUE(miss) << "different bracket must be a cache miss under production bracket_equal";
    ExpectRankInvariant(miss ? 1 : 0, "different-bracket miss decision");
}

}  // namespace cece::test
