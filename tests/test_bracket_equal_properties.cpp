/**
 * @file test_bracket_equal_properties.cpp
 * @brief Property-based tests for CeceDriverOrchestrator::bracket_equal.
 *
 * Feature: driver-io-regrid-perf, Property 5: Bracket-equality reflexivity and
 * field sensitivity
 *
 * bracket_equal(a, b) returns true iff a.i0 == b.i0 && a.i1 == b.i1 &&
 * std::fabs(a.weight - b.weight) <= kBracketWeightTol (1e-12). The `valid`
 * field is intentionally NOT compared.
 *
 * Properties tested:
 *   - Reflexivity: bracket_equal(b, b) is always true.
 *   - Sensitivity: a bracket differing in i0 (nonzero delta) compares false;
 *     differing in i1 (nonzero delta) compares false; differing in weight by
 *     more than the tolerance compares false; a weight difference within the
 *     tolerance (same i0/i1) still compares equal.
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <cmath>

#include "cece/cece_driver_facade.hpp"

namespace cece {

// ============================================================================
// Test-only friend shim.
//
// bracket_equal and kBracketWeightTol are private static members of
// CeceDriverOrchestrator. This struct is declared a friend inside the class
// (see include/cece/cece_driver_facade.hpp, Task 8.5) so the tests below can
// invoke the private static helper without altering its signature, logic, or
// visibility. It does not touch any production code path.
// ============================================================================
struct BracketEqualTestAccess {
    static bool Equal(const RecordBracket& a, const RecordBracket& b) {
        return CeceDriverOrchestrator::bracket_equal(a, b);
    }

    static constexpr double Tol() {
        return CeceDriverOrchestrator::kBracketWeightTol;
    }
};

namespace {

// Generate an arbitrary RecordBracket. i0/i1 span a modest index range; weight
// spans the natural [0, 1] blend range plus a little slack; valid is arbitrary
// (it must not affect the comparison).
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
// Property 5a: Reflexivity
// Feature: driver-io-regrid-perf, Property 5: Bracket-equality reflexivity and field sensitivity
// **Validates: Requirements 3.5**
//
// For any generated RecordBracket b, bracket_equal(b, b) is always true.
// ============================================================================
RC_GTEST_PROP(BracketEqualProperty, Property5_Reflexivity, ()) {
    const RecordBracket b = *genBracket();
    RC_ASSERT(BracketEqualTestAccess::Equal(b, b));
}

// ============================================================================
// Property 5b: Sensitivity to i0
// Feature: driver-io-regrid-perf, Property 5: Bracket-equality reflexivity and field sensitivity
// **Validates: Requirements 3.5**
//
// A bracket differing from b only in i0 (by a nonzero delta) compares false.
// ============================================================================
RC_GTEST_PROP(BracketEqualProperty, Property5_SensitivityI0, ()) {
    const RecordBracket b = *genBracket();

    // Nonzero delta in [-1000, 1000], excluding 0.
    const int delta = *rc::gen::suchThat(rc::gen::inRange(-1000, 1001), [](int d) { return d != 0; });

    RecordBracket other = b;
    other.i0 = b.i0 + delta;

    RC_ASSERT(!BracketEqualTestAccess::Equal(b, other));
}

// ============================================================================
// Property 5c: Sensitivity to i1
// Feature: driver-io-regrid-perf, Property 5: Bracket-equality reflexivity and field sensitivity
// **Validates: Requirements 3.5**
//
// A bracket differing from b only in i1 (by a nonzero delta) compares false.
// ============================================================================
RC_GTEST_PROP(BracketEqualProperty, Property5_SensitivityI1, ()) {
    const RecordBracket b = *genBracket();

    const int delta = *rc::gen::suchThat(rc::gen::inRange(-1000, 1001), [](int d) { return d != 0; });

    RecordBracket other = b;
    other.i1 = b.i1 + delta;

    RC_ASSERT(!BracketEqualTestAccess::Equal(b, other));
}

// ============================================================================
// Property 5d: Sensitivity to weight beyond tolerance
// Feature: driver-io-regrid-perf, Property 5: Bracket-equality reflexivity and field sensitivity
// **Validates: Requirements 3.5**
//
// A bracket differing from b only in weight by MORE than kBracketWeightTol
// compares false (same i0/i1).
// ============================================================================
RC_GTEST_PROP(BracketEqualProperty, Property5_SensitivityWeightBeyondTolerance, ()) {
    const RecordBracket b = *genBracket();
    const double tol = BracketEqualTestAccess::Tol();

    // A strictly-greater-than-tolerance magnitude, and a sign.
    // Add tol so the offset always exceeds it; scale up to keep it well clear.
    const double extra = *rc::gen::map(rc::gen::inRange(1, 1000001), [](int n) { return n / 1000.0; });  // (0.001, 1000]
    const double magnitude = tol + extra;                                                                // strictly > tol
    const bool positive = *rc::gen::arbitrary<bool>();
    const double offset = positive ? magnitude : -magnitude;

    RecordBracket other = b;
    other.weight = b.weight + offset;

    // Guard against floating-point catastrophe: the realized difference must
    // still exceed the tolerance for this property to be meaningful.
    RC_PRE(std::fabs(other.weight - b.weight) > tol);

    RC_ASSERT(!BracketEqualTestAccess::Equal(b, other));
}

// ============================================================================
// Property 5e: Weight difference within tolerance still compares equal
// Feature: driver-io-regrid-perf, Property 5: Bracket-equality reflexivity and field sensitivity
// **Validates: Requirements 3.5**
//
// Given identical i0/i1, a weight difference of AT MOST kBracketWeightTol
// still compares equal.
// ============================================================================
RC_GTEST_PROP(BracketEqualProperty, Property5_WeightWithinToleranceIsEqual, ()) {
    const RecordBracket b = *genBracket();
    const double tol = BracketEqualTestAccess::Tol();

    // A fraction in [0, 1] of the tolerance, with an arbitrary sign, so the
    // realized offset magnitude is <= tol.
    const double frac = *rc::gen::map(rc::gen::inRange(0, 1001), [](int n) { return n / 1000.0; });  // [0, 1]
    const bool positive = *rc::gen::arbitrary<bool>();
    const double offset = (positive ? 1.0 : -1.0) * frac * tol;

    RecordBracket other = b;
    other.i0 = b.i0;  // identical indices
    other.i1 = b.i1;
    other.weight = b.weight + offset;

    // Only assert equality when the realized difference is truly within tol
    // (the addition itself can round; keep the property well-defined).
    RC_PRE(std::fabs(other.weight - b.weight) <= tol);

    RC_ASSERT(BracketEqualTestAccess::Equal(b, other));
}

}  // namespace cece
