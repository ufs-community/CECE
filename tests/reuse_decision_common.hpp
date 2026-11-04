#pragma once
#ifndef CECE_REUSE_DECISION_COMMON_HPP
#define CECE_REUSE_DECISION_COMMON_HPP

#include "cece/cece_driver_facade.hpp"

namespace cece {

// ============================================================================
// Test-only shim: reach the private static production bracket_equal through the
// EndpointCacheTestAccess friend declared on CeceDriverOrchestrator (Task 4.3),
// so the endpoint index-equality check is derived from REAL production logic,
// not a copy. Declared in namespace cece to match the
// `friend struct EndpointCacheTestAccess;` declaration in
// include/cece/cece_driver_facade.hpp. It changes no production signature or
// visibility.
// ============================================================================
struct EndpointCacheTestAccess {
    static bool Equal(const RecordBracket& a, const RecordBracket& b) {
        return CeceDriverOrchestrator::bracket_equal(a, b);
    }
};

}  // namespace cece

namespace cece::test {

namespace {

// ---------------------------------------------------------------------------
// Faithful reproduction of the production bracket resolver — mirrors, line for
// line, the anonymous-namespace helpers parse_sim_datetime and
// cadence_record_bracket in src/driver/cece_driver_facade.cpp plus the legacy
// step-index fallback AdvanceTime applies when the cadence yields no valid
// bracket. Those helpers have internal linkage and cannot be linked from a test
// TU, but they are PURE functions of rank-invariant inputs, so reproducing them
// exactly is sufficient to validate the cross-rank decision mechanism. The
// scenario table below additionally pins the expected decision, so any drift
// between this reproduction and production would surface as a failing
// reference-table assertion rather than silently passing.
// ---------------------------------------------------------------------------

struct SimDateTime {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int day_of_week = 0;
    bool valid = false;
};

SimDateTime ParseSimDateTime(const std::string& iso8601) {
    SimDateTime dt;
    try {
        const tick::Date_Time tdt = tick::parse_iso8601(iso8601);
        dt.year = tdt.year;
        dt.month = tdt.month;
        dt.day = tdt.day;
        dt.hour = tdt.hour;

        const std::int64_t nanos = tick::Gregorian_Calendar::to_time_point(tdt).nanos();
        std::int64_t days = nanos / tick::nanos_per_day;
        if (nanos < 0 && nanos % tick::nanos_per_day != 0) --days;  // floor toward -inf
        dt.day_of_week = static_cast<int>(((days + 4) % 7 + 7) % 7);
        dt.valid = true;
    } catch (const std::exception&) {
        dt = SimDateTime{};
    }
    return dt;
}

RecordBracket CadenceRecordBracket(const std::string& cadence, const std::string& tintalgo, const SimDateTime& dt, int file_nt) {
    RecordBracket br;
    if (cadence.empty() || !dt.valid) return br;

    std::string c = cadence;
    std::transform(c.begin(), c.end(), c.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    std::string algo = tintalgo;
    std::transform(algo.begin(), algo.end(), algo.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    const bool linear = (algo == "linear");

    auto clamp_idx = [&](int idx) {
        if (file_nt > 0 && idx >= file_nt) idx = file_nt - 1;
        if (idx < 0) idx = 0;
        return idx;
    };

    if (c == "hourly") {
        br.i0 = br.i1 = clamp_idx(dt.hour);
        br.valid = true;
    } else if (c == "weekly") {
        br.i0 = br.i1 = clamp_idx(dt.day_of_week);
        br.valid = true;
    } else if (c == "monthly") {
        const int m = dt.month - 1;
        if (!linear) {
            br.i0 = br.i1 = clamp_idx(m);
            br.valid = true;
            return br;
        }
        const int dim = tick::Gregorian_Calendar::days_in_month(dt.year, dt.month);
        const double frac = (static_cast<double>(dt.day - 1) + dt.hour / 24.0) / static_cast<double>(dim);
        const int nrec = (file_nt > 0) ? file_nt : 12;
        if (frac >= 0.5) {
            br.i0 = m % nrec;
            br.i1 = (m + 1) % nrec;
            br.weight = frac - 0.5;
        } else {
            br.i0 = (m - 1 + nrec) % nrec;
            br.i1 = m % nrec;
            br.weight = frac + 0.5;
        }
        br.valid = true;
    }
    return br;
}

// Resolve the bracket exactly as AdvanceTime does: cadence resolver, then the
// legacy step-index fallback when the cadence produced no valid bracket.
RecordBracket ResolveBracket(const std::string& cadence, const std::string& tintalgo, const std::string& sim_time, int file_nt, int step_index) {
    const SimDateTime dt = ParseSimDateTime(sim_time);
    RecordBracket bracket = CadenceRecordBracket(cadence, tintalgo, dt, file_nt);
    if (!bracket.valid) {
        const int t_idx = (file_nt > 0) ? (step_index % file_nt) : 0;
        bracket.i0 = bracket.i1 = t_idx;
        bracket.weight = 0.0;
    }
    return bracket;
}

// Production's interp-mode flag: needs an upper record only when i1 != i0 and
// the blend weight is strictly positive. (Tier 2 only ever fires when this is
// true — a single-record step takes the unchanged Tier-3 single path.)
bool NeedsUpperRecord(const RecordBracket& b) {
    return (b.i1 != b.i0 && b.weight > 0.0);
}

int WorldSize() {
    int size = 1;
    int inited = 0;
    MPI_Initialized(&inited);
    if (inited) MPI_Comm_size(MPI_COMM_WORLD, &size);
    return size;
}

// Assert every rank agrees on `value` via MIN==MAX allreduce (mirrors
// production's collective_int_matches). At np=1 this is a no-op that trivially
// holds, keeping the single-process path valid.
void ExpectRankInvariant(int value, const std::string& what) {
    int inited = 0;
    MPI_Initialized(&inited);
    if (!inited || WorldSize() <= 1) return;  // np=1: nothing to compare, trivially invariant.

    int minv = 0;
    int maxv = 0;
    ASSERT_EQ(MPI_Allreduce(&value, &minv, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD), MPI_SUCCESS);
    ASSERT_EQ(MPI_Allreduce(&value, &maxv, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD), MPI_SUCCESS);
    EXPECT_EQ(minv, maxv) << what << " differs across ranks (min " << minv << ", max " << maxv << ")";
}
}  // namespace
}  // namespace cece::test

#endif
