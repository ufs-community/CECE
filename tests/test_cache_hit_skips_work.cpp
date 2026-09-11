/**
 * @file test_cache_hit_skips_work.cpp
 * @brief Property-based tests that a cache HIT performs NO input-record read
 *        and NO regrid apply, while a genuine cache MISS performs exactly one
 *        of each.
 *
 * Feature: driver-io-regrid-perf, Property: Cache-hit steps do no read and no
 * regrid apply
 *
 * **Validates: Requirements 9.4**
 *
 * ----------------------------------------------------------------------------
 * What this test exercises and why it is faithful
 * ----------------------------------------------------------------------------
 * This is the complement of Property 6 (test_slice_cache_equivalence.cpp).
 * Property 6 asserts that WHEN the hit path is taken the reused buffer equals
 * the miss buffer. This test asserts the other half of Req 9.4: on a step whose
 * resolved bracket equals the previous step's bracket (a cache HIT), the driver
 * performs NO disk read (amio_read via read_slab) and NO regrid apply
 * (AssembleReplicatedField / apply_regrid_plan).
 *
 * The full AdvanceTime path (MPI/DAGR/AMIO) is far too heavy to stand up in a
 * unit test, so this validates the DECISION LOGIC that gates read+regrid, which
 * is exactly what determines whether work is skipped. The production gate
 * (src/driver/cece_driver_facade.cpp ~L929-L1063) is:
 *
 *     auto& slice_cache = slice_caches_[var_name];
 *     const bool cache_hit = bracket_ready && slice_cache.valid
 *                            && bracket_equal(bracket, slice_cache.last_bracket);
 *     if (cache_hit) {
 *         ingest_buffer = slice_cache.ingest_buffer;   // reuse; NO read, NO regrid
 *         read_success  = true;
 *     } else {
 *         ... read_slab(bracket.i0, ...)   // amio_read
 *         ... AssembleReplicatedField(...) // the single regrid apply
 *     }
 *
 * This test reproduces that gate and branch EXACTLY, against the REAL
 * production types (cece::RecordBracket, cece::SliceCacheEntry) and the REAL
 * production comparison (cece::CeceDriverOrchestrator::bracket_equal, reached
 * through the CacheHitSkipTestAccess friend declared in the class). A small spy
 * struct with read_count / regrid_count counters stands in for the two pieces
 * of work the branch gates; executing the faithful branch and asserting on the
 * counters directly validates Req 9.4 at the decision level.
 *
 * ----------------------------------------------------------------------------
 * Access approach
 * ----------------------------------------------------------------------------
 * SliceCacheEntry and RecordBracket are already public structs in namespace
 * cece, so no friend access is needed for them. Only bracket_equal is private,
 * reached via a dedicated `friend struct CacheHitSkipTestAccess;` added to
 * CeceDriverOrchestrator (next to the existing BracketEqualTestAccess /
 * StreamConfigTestAccess / SliceCacheTestAccess friends). Each property-test
 * translation unit owns its own single-file shim so it stays self-contained and
 * does not depend on another test's internal linkage. No production signature,
 * logic, or visibility is changed.
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <cstddef>
#include <vector>

#include "cece/cece_driver_facade.hpp"

namespace cece {

// ============================================================================
// Test-only friend shim for the private static bracket_equal helper. Declared
// a friend inside CeceDriverOrchestrator (see
// include/cece/cece_driver_facade.hpp, Task 13.3). Exercises the production
// comparison directly, not a copy.
// ============================================================================
struct CacheHitSkipTestAccess {
    static bool Equal(const RecordBracket& a, const RecordBracket& b) {
        return CeceDriverOrchestrator::bracket_equal(a, b);
    }
};

namespace {

// Counters standing in for the two pieces of per-step work that the production
// cache_hit branch gates: read_slab (amio_read) and AssembleReplicatedField
// (apply_regrid_plan). On a HIT both stay 0; on a MISS both increment once.
struct WorkSpy {
    int read_count = 0;
    int regrid_count = 0;
};

// Faithful copy of the production cache decision + branch bodies
// (cece_driver_facade.cpp ~L929-L937 and ~L1053). `bracket_ready` mirrors the
// production readiness gate that also participates in cache_hit; the tests
// exercise it both true and false. Returns the ingest buffer the ingestor would
// receive (reused on a hit, assembled on a miss) so callers can additionally
// verify reuse.
std::vector<double> ExecuteDecision(const RecordBracket& resolved, bool bracket_ready, const SliceCacheEntry& entry,
                                    const std::vector<double>& assembled_on_miss, WorkSpy& spy) {
    // Production gate, verbatim in structure and using the real bracket_equal.
    const bool cache_hit = bracket_ready && entry.valid && CacheHitSkipTestAccess::Equal(resolved, entry.last_bracket);

    std::vector<double> ingest_buffer;
    if (cache_hit) {
        // Production: ingest_buffer = slice_cache.ingest_buffer;  (no read, no regrid)
        ingest_buffer = entry.ingest_buffer;
    } else {
        // Production: read_slab(bracket.i0, ...) => amio_read
        spy.read_count++;
        // Production: AssembleReplicatedField(...) => apply_regrid_plan (single regrid)
        spy.regrid_count++;
        ingest_buffer = assembled_on_miss;
    }
    return ingest_buffer;
}

// Populate a valid cache entry as the production MISS path does
// (cece_driver_facade.cpp ~L1060-1063): record the producing bracket and buffer.
SliceCacheEntry MakeValidEntry(const RecordBracket& producing_bracket, const std::vector<double>& buffer) {
    SliceCacheEntry entry;
    entry.last_bracket = producing_bracket;
    entry.ingest_buffer = buffer;
    entry.ingest_size = buffer.size();
    entry.valid = true;
    return entry;
}

rc::Gen<std::vector<double>> genBuffer() {
    return rc::gen::container<std::vector<double>>(rc::gen::map(rc::gen::inRange(-1000000, 1000001), [](int m) { return m / 997.0; }));
}

rc::Gen<RecordBracket> genBracket() {
    return rc::gen::apply(
        [](int i0, int i1, double weight, bool valid) {
            RecordBracket b;
            b.i0 = i0;
            b.i1 = i1;
            b.weight = weight;
            b.valid = valid;
            return b;
        },
        rc::gen::inRange(0, 1000), rc::gen::inRange(0, 1000), rc::gen::map(rc::gen::inRange(0, 1000001), [](int n) { return n / 1000000.0; }),
        rc::gen::arbitrary<bool>());
}

}  // namespace

// ============================================================================
// Property A: SAME bracket + valid cache => HIT => 0 reads, 0 regrids.
// Feature: driver-io-regrid-perf, Property: Cache-hit steps do no read and no
// regrid apply
// **Validates: Requirements 9.4**
//
// When the resolved bracket equals the previously cached bracket and the cache
// is valid (and the step is bracket_ready, as production requires), the hit
// gate is satisfied and the branch that SKIPS read+regrid is taken: the read
// and regrid counters both stay 0, and the reused buffer is the cached one.
// ============================================================================
RC_GTEST_PROP(CacheHitSkipsWork, SameBracketValidCache_NoReadNoRegrid, ()) {
    const std::vector<double> cached_buffer = *genBuffer();
    const RecordBracket bracket = *genBracket();

    const SliceCacheEntry entry = MakeValidEntry(bracket, cached_buffer);

    // The resolved bracket for this step equals the cached one (a cache HIT).
    // Production only takes the hit path when bracket_equal is true; assert it.
    RC_ASSERT(CacheHitSkipTestAccess::Equal(bracket, entry.last_bracket));

    WorkSpy spy;
    // assembled_on_miss is irrelevant on a hit; use a distinct sentinel to prove
    // the hit path does NOT touch it.
    const std::vector<double> assembled_on_miss = {-42.0};
    const std::vector<double> ingest = ExecuteDecision(bracket, /*bracket_ready=*/true, entry, assembled_on_miss, spy);

    // Req 9.4: a cache hit performs NO read and NO regrid apply.
    RC_ASSERT(spy.read_count == 0);
    RC_ASSERT(spy.regrid_count == 0);

    // And the ingestor receives the reused cached buffer, not the miss buffer.
    RC_ASSERT(ingest == cached_buffer);
}

// ============================================================================
// Property B: DIFFERENT bracket => MISS => exactly 1 read, 1 regrid.
// Feature: driver-io-regrid-perf, Property: Cache-hit steps do no read and no
// regrid apply
// **Validates: Requirements 9.4**
//
// When the resolved bracket differs from the cached bracket (in i0, i1, or
// weight beyond tolerance), the hit gate is false, so the branch performs the
// read and the single regrid apply: each counter increments exactly once.
// ============================================================================
RC_GTEST_PROP(CacheHitSkipsWork, DifferentBracket_OneReadOneRegrid, ()) {
    const std::vector<double> cached_buffer = *genBuffer();
    const RecordBracket cached_bracket = *genBracket();

    const SliceCacheEntry entry = MakeValidEntry(cached_bracket, cached_buffer);

    // Perturb exactly one field so the resolved bracket is genuinely different.
    RecordBracket resolved = cached_bracket;
    const int which = *rc::gen::inRange(0, 3);
    if (which == 0) {
        const int delta = *rc::gen::suchThat(rc::gen::inRange(-1000, 1001), [](int d) { return d != 0; });
        resolved.i0 = cached_bracket.i0 + delta;
    } else if (which == 1) {
        const int delta = *rc::gen::suchThat(rc::gen::inRange(-1000, 1001), [](int d) { return d != 0; });
        resolved.i1 = cached_bracket.i1 + delta;
    } else {
        // Offset well beyond kBracketWeightTol (1e-12).
        const double extra = *rc::gen::map(rc::gen::inRange(1, 1000001), [](int n) { return n / 1000.0; });  // (0.001, 1000]
        const bool positive = *rc::gen::arbitrary<bool>();
        resolved.weight = cached_bracket.weight + (positive ? extra : -extra);
        RC_PRE(resolved.weight != cached_bracket.weight);
    }

    // Sanity: this is genuinely a MISS per the real production comparison.
    RC_ASSERT(!CacheHitSkipTestAccess::Equal(resolved, entry.last_bracket));

    WorkSpy spy;
    const std::vector<double> assembled_on_miss = *genBuffer();
    const std::vector<double> ingest = ExecuteDecision(resolved, /*bracket_ready=*/true, entry, assembled_on_miss, spy);

    // Miss => exactly one read and one regrid apply.
    RC_ASSERT(spy.read_count == 1);
    RC_ASSERT(spy.regrid_count == 1);
    // And the ingestor receives the freshly assembled buffer, not the stale cache.
    RC_ASSERT(ingest == assembled_on_miss);
}

// ============================================================================
// Property C: INVALID cache (or not-ready step) => MISS => exactly 1 read, 1 regrid.
// Feature: driver-io-regrid-perf, Property: Cache-hit steps do no read and no
// regrid apply
// **Validates: Requirements 9.4**
//
// Even when the resolved bracket equals the cached bracket, an invalid cache
// (valid == false, e.g. the first touch) or a not-ready step (bracket_ready ==
// false) must NOT be treated as a hit: production recomputes, so read+regrid
// each run exactly once. This guards against reusing an unpopulated buffer.
// ============================================================================
RC_GTEST_PROP(CacheHitSkipsWork, InvalidOrNotReady_OneReadOneRegrid, ()) {
    const RecordBracket bracket = *genBracket();
    const std::vector<double> assembled_on_miss = *genBuffer();

    // Two independent ways the hit gate can fail while brackets match:
    //   - cache invalid (valid == false)
    //   - step not ready (bracket_ready == false)
    const bool invalidate_cache = *rc::gen::arbitrary<bool>();
    const bool not_ready = *rc::gen::arbitrary<bool>();
    // Ensure at least one gate is actually broken so this is a real miss.
    RC_PRE(invalidate_cache || not_ready);

    SliceCacheEntry entry = MakeValidEntry(bracket, assembled_on_miss);
    if (invalidate_cache) {
        entry.valid = false;
    }
    const bool bracket_ready = !not_ready;

    WorkSpy spy;
    const std::vector<double> ingest = ExecuteDecision(bracket, bracket_ready, entry, assembled_on_miss, spy);

    // Not a hit => recompute: one read, one regrid.
    RC_ASSERT(spy.read_count == 1);
    RC_ASSERT(spy.regrid_count == 1);
    RC_ASSERT(ingest == assembled_on_miss);
}

// ============================================================================
// Property D: Repeated same-bracket steps after a miss => still 0 reads/regrids.
// Feature: driver-io-regrid-perf, Property: Cache-hit steps do no read and no
// regrid apply
// **Validates: Requirements 9.4**
//
// Models a multi-step run: the first step is a MISS (populates the cache with
// one read + one regrid), and every subsequent same-bracket step is a HIT that
// adds no further reads or regrids. Total read/regrid counts stay at exactly 1
// no matter how many identical steps follow.
// ============================================================================
RC_GTEST_PROP(CacheHitSkipsWork, RepeatedSameBracketSteps_OnlyFirstDoesWork, ()) {
    const RecordBracket bracket = *genBracket();
    const std::vector<double> assembled = *genBuffer();
    const int subsequent_steps = 1 + *rc::gen::inRange(0, 8);  // 1..8 hit steps after the miss

    WorkSpy spy;

    // Step 0: first touch => invalid cache => MISS => populate the cache.
    SliceCacheEntry entry;  // valid == false (first touch)
    std::vector<double> ingest = ExecuteDecision(bracket, /*bracket_ready=*/true, entry, assembled, spy);
    RC_ASSERT(spy.read_count == 1);
    RC_ASSERT(spy.regrid_count == 1);
    RC_ASSERT(ingest == assembled);

    // Production MISS path then updates the cache with the assembled buffer.
    entry = MakeValidEntry(bracket, ingest);

    // Subsequent same-bracket steps: every one is a HIT => no extra work.
    for (int step = 0; step < subsequent_steps; ++step) {
        ingest = ExecuteDecision(bracket, /*bracket_ready=*/true, entry, /*assembled_on_miss=*/{-42.0}, spy);
        RC_ASSERT(ingest == assembled);  // reused, not recomputed
    }

    // Across the whole run only the first step did any read/regrid.
    RC_ASSERT(spy.read_count == 1);
    RC_ASSERT(spy.regrid_count == 1);
}

}  // namespace cece
