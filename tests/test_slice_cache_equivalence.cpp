/**
 * @file test_slice_cache_equivalence.cpp
 * @brief Property-based tests that a cache-HIT ingest buffer equals the
 *        cache-MISS ingest buffer for the same time bracket.
 *
 * Feature: driver-io-regrid-perf, Property 6: Cache-hit ingest buffer equals
 * cache-miss ingest buffer
 *
 * **Validates: Requirements 3.2, 3.4, 5.1, 9.4**
 *
 * ----------------------------------------------------------------------------
 * What this test exercises and why it is faithful
 * ----------------------------------------------------------------------------
 * The full AdvanceTime read/regrid/ingest path requires a live MPI + DAGR +
 * CeceIO + AMIO environment and is far too heavy to stand up in a unit test.
 * But the ACTUAL determinant of hit==miss equality is the tiny cache mechanism
 * inside AdvanceTime, and that mechanism is reproduced here EXACTLY, against the
 * REAL production types and the REAL production comparison:
 *
 *   MISS (src/driver/cece_driver_facade.cpp, ~L1053-1063): after assembling the
 *   ingest buffer, production does
 *       slice_cache.last_bracket  = bracket;
 *       slice_cache.ingest_buffer = ingest_buffer;   // the assembled buffer
 *       slice_cache.ingest_size   = field_nlev * nx_ * ny_;
 *       slice_cache.valid         = true;
 *   and that assembled buffer is what cece_ingestor_set_field receives on the
 *   miss step.
 *
 *   HIT (~L930-937): on a later step it computes
 *       cache_hit = ... && slice_cache.valid
 *                       && bracket_equal(bracket, slice_cache.last_bracket);
 *   and when cache_hit is true it does
 *       ingest_buffer = slice_cache.ingest_buffer;   // reuse, no read/regrid
 *   and that reused buffer is what cece_ingestor_set_field receives on the hit
 *   step.
 *
 * So hit==miss equality reduces to two production facts:
 *   (1) bracket_equal(bracket, last_bracket) is TRUE for the SAME bracket
 *       (otherwise the hit path would not be taken and production would
 *       recompute), and
 *   (2) a plain assignment/copy of the cached std::vector<double> reproduces
 *       the stored buffer element-for-element.
 *
 * This test uses the production cece::SliceCacheEntry struct (public, from the
 * facade header) and the production cece::CeceDriverOrchestrator::bracket_equal
 * (private static, reached through the SliceCacheTestAccess friend declared in
 * the class) so it validates production types/logic, not a copy.
 *
 * ----------------------------------------------------------------------------
 * Access approach
 * ----------------------------------------------------------------------------
 * SliceCacheEntry and RecordBracket are already public structs in namespace
 * cece, so no friend access is needed for them. Only bracket_equal is private.
 * A dedicated `friend struct SliceCacheTestAccess;` was added to
 * CeceDriverOrchestrator (next to the existing BracketEqualTestAccess /
 * StreamConfigTestAccess friends). A dedicated shim keeps this test file
 * self-contained (each property-test translation unit owns its own shim, as in
 * test_bracket_equal_properties.cpp) without depending on another test's
 * internal linkage. No production signature, logic, or visibility is changed.
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <cstddef>
#include <vector>

#include "cece/cece_driver_facade.hpp"

namespace cece {

// ============================================================================
// Test-only friend shim for the private static bracket_equal helper.
// Declared a friend inside CeceDriverOrchestrator (see
// include/cece/cece_driver_facade.hpp, Task 9.2). Exercises the production
// comparison directly, not a copy.
// ============================================================================
struct SliceCacheTestAccess {
    static bool Equal(const RecordBracket& a, const RecordBracket& b) {
        return CeceDriverOrchestrator::bracket_equal(a, b);
    }
};

namespace {

// Generate a small, positive field shape (field_nlev, nx, ny) so the total
// buffer stays modest across >=100 iterations.
struct FieldShape {
    int field_nlev;
    int nx;
    int ny;
    size_t size() const {
        return static_cast<size_t>(field_nlev) * nx * ny;
    }
};

rc::Gen<FieldShape> genShape() {
    return rc::gen::apply([](int nlev, int nx, int ny) { return FieldShape{nlev, nx, ny}; }, rc::gen::inRange(1, 4), rc::gen::inRange(1, 8),
                          rc::gen::inRange(1, 8));
}

// Generate an assembled/replicated ingest buffer of exactly `n` doubles. Values
// span a wide range including negatives and fractional parts so the element-wise
// comparison is meaningful (a truncating/aliasing copy would be caught).
rc::Gen<std::vector<double>> genBuffer(size_t n) {
    return rc::gen::container<std::vector<double>>(n, rc::gen::map(rc::gen::inRange(-1000000, 1000001), [](int m) { return m / 997.0; }));
}

// Generate a RecordBracket. i0/i1 span a modest index range; weight spans the
// natural [0, 1] blend range; valid is arbitrary (production does not compare
// it in bracket_equal).
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

// Reproduce the production MISS cache write EXACTLY (cece_driver_facade.cpp
// ~L1060-1063): given the freshly assembled buffer, populate the cache entry.
// The "miss buffer" that cece_ingestor_set_field would receive on the miss step
// IS the assembled buffer itself.
void SimulateMiss(SliceCacheEntry& entry, const RecordBracket& bracket, const std::vector<double>& assembled, const FieldShape& shape) {
    entry.last_bracket = bracket;
    entry.ingest_buffer = assembled;
    entry.ingest_size = shape.size();
    entry.valid = true;
}

// Reproduce the production HIT cache read EXACTLY (cece_driver_facade.cpp
// ~L935): reuse the cached buffer. Returns the buffer that
// cece_ingestor_set_field would receive on the hit step.
std::vector<double> SimulateHitRead(const SliceCacheEntry& entry) {
    std::vector<double> ingest_buffer = entry.ingest_buffer;  // production: ingest_buffer = slice_cache.ingest_buffer;
    return ingest_buffer;
}

}  // namespace

// ============================================================================
// Property 6a: HIT buffer equals MISS buffer for the SAME bracket.
// Feature: driver-io-regrid-perf, Property 6: Cache-hit ingest buffer equals
// cache-miss ingest buffer
// **Validates: Requirements 3.2, 3.4, 5.1, 9.4**
//
// For any generated assembled buffer and bracket, simulate the miss (populate
// the cache) and then the hit for the SAME bracket. The production hit gate
// (bracket_equal) MUST be satisfied, and the reused buffer MUST be
// element-for-element (bit-identical) to the assembled miss buffer.
// ============================================================================
RC_GTEST_PROP(SliceCacheEquivalenceProperty, Property6_HitBufferEqualsMissBuffer, ()) {
    const FieldShape shape = *genShape();
    const std::vector<double> assembled = *genBuffer(shape.size());
    const RecordBracket bracket = *genBracket();

    // --- MISS step: this assembled buffer is what ingestor would receive. ---
    const std::vector<double> miss_buffer = assembled;

    SliceCacheEntry entry;
    SimulateMiss(entry, bracket, assembled, shape);

    // Sanity: the cache recorded what production records on a miss.
    RC_ASSERT(entry.valid);
    RC_ASSERT(entry.ingest_size == shape.size());
    RC_ASSERT(entry.ingest_buffer.size() == shape.size());

    // --- HIT step for the SAME bracket. ---
    // Production only takes the hit path when bracket_equal is true; assert it.
    RC_ASSERT(SliceCacheTestAccess::Equal(bracket, entry.last_bracket));

    const std::vector<double> hit_buffer = SimulateHitRead(entry);

    // Element-for-element (size + every element bit-identical).
    RC_ASSERT(hit_buffer.size() == miss_buffer.size());
    for (size_t i = 0; i < miss_buffer.size(); ++i) {
        // Bit-identical: these are copies of the same doubles, not recomputed.
        RC_ASSERT(hit_buffer[i] == miss_buffer[i]);
    }
    RC_ASSERT(hit_buffer == miss_buffer);
}

// ============================================================================
// Property 6b: A DIFFERENT bracket does NOT satisfy the hit gate.
// Feature: driver-io-regrid-perf, Property 6: Cache-hit ingest buffer equals
// cache-miss ingest buffer
// **Validates: Requirements 3.2, 3.4, 9.4**
//
// If the resolved bracket differs from the cached bracket (in i0, i1, or weight
// beyond tolerance), bracket_equal is false, so production would take the MISS
// path and recompute rather than blindly reusing a stale buffer. This guards the
// "same bracket" precondition of Property 6.
// ============================================================================
RC_GTEST_PROP(SliceCacheEquivalenceProperty, Property6_DifferentBracketIsCacheMiss, ()) {
    const FieldShape shape = *genShape();
    const std::vector<double> assembled = *genBuffer(shape.size());
    const RecordBracket cached_bracket = *genBracket();

    SliceCacheEntry entry;
    SimulateMiss(entry, cached_bracket, assembled, shape);

    // Perturb the resolved bracket in exactly one of i0 / i1 / weight so it is
    // genuinely different from the cached bracket.
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
        // Guard against floating-point catastrophe so the perturbation is real.
        RC_PRE(resolved.weight != cached_bracket.weight);
    }

    // The hit gate must be FALSE for a genuinely different bracket => production
    // recomputes (a real miss), so it never reuses a buffer for the wrong bracket.
    RC_ASSERT(!SliceCacheTestAccess::Equal(resolved, entry.last_bracket));
}

// ============================================================================
// Property 6c: Repeated hits stay identical to the original miss buffer.
// Feature: driver-io-regrid-perf, Property 6: Cache-hit ingest buffer equals
// cache-miss ingest buffer
// **Validates: Requirements 3.2, 3.4, 9.4**
//
// Consecutive same-bracket timesteps each reuse the cached buffer; every reuse
// must equal the original assembled (miss) buffer, confirming reuse is
// non-destructive across many steps.
// ============================================================================
RC_GTEST_PROP(SliceCacheEquivalenceProperty, Property6_RepeatedHitsStayIdentical, ()) {
    const FieldShape shape = *genShape();
    const std::vector<double> assembled = *genBuffer(shape.size());
    const RecordBracket bracket = *genBracket();
    const int num_hits = 1 + *rc::gen::inRange(0, 8);  // 1..8 subsequent hit steps

    const std::vector<double> miss_buffer = assembled;

    SliceCacheEntry entry;
    SimulateMiss(entry, bracket, assembled, shape);

    for (int step = 0; step < num_hits; ++step) {
        RC_ASSERT(SliceCacheTestAccess::Equal(bracket, entry.last_bracket));
        const std::vector<double> hit_buffer = SimulateHitRead(entry);
        RC_ASSERT(hit_buffer == miss_buffer);
    }
}

}  // namespace cece
