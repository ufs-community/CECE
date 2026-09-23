/**
 * @file test_ingest_no_stale_data.cpp
 * @brief Property-based test that the core import field never carries stale
 *        data from a prior step, including across slice-cache hits.
 *
 * Feature: ingest-copy-consolidation, Property 4: no stale data across steps
 *
 * **Validates: Requirements 1.4**
 *
 * ----------------------------------------------------------------------------
 * Property (design.md, Property 4)
 * ----------------------------------------------------------------------------
 * "For any sequence of per-step source fields — including steps whose resolved
 *  bracket is unchanged and therefore served from the slice cache — the core
 *  import field observed after a step SHALL equal the field assembled for that
 *  step's inputs, never a value carried over from a prior step."
 *
 * ----------------------------------------------------------------------------
 * Why this test is faithful to production
 * ----------------------------------------------------------------------------
 * The full AdvanceTime read/regrid/ingest path needs a live MPI + DAGR + CeceIO
 * + AMIO environment and is far too heavy for a unit test. But the determinant
 * of "no stale data" is the composition of two production mechanisms, and both
 * are reproduced here EXACTLY against the REAL production types:
 *
 *   1. The slice-cache hit/miss decision
 *      (src/driver/cece_driver_facade.cpp, ~L930-937 / ~L1053-1063):
 *        MISS: assemble ingest_buffer for the resolved bracket, then
 *              slice_cache.last_bracket  = bracket;
 *              slice_cache.ingest_buffer = ingest_buffer;   // assembled buffer
 *              slice_cache.ingest_size   = field_nlev * nx_ * ny_;
 *              slice_cache.valid         = true;
 *        HIT (bracket_equal(bracket, last_bracket) && valid):
 *              ingest_buffer = slice_cache.ingest_buffer;   // reuse, no reread
 *      We use the production cece::SliceCacheEntry / cece::RecordBracket structs
 *      and the production private-static cece::CeceDriverOrchestrator::bracket_equal
 *      (reached through the existing SliceCacheTestAccess friend shim), so the
 *      hit/miss gate is production logic, not a copy.
 *
 *   2. The single consolidated transpose that writes the core import field
 *      (AssembleReplicatedField, ~L688-720): the assembled `[level][j][i]`
 *      ingest_buffer is transposed into the LayoutLeft (i, j, level) core import
 *      DualView via the index math
 *          host(i, j, level) = ingest_buffer[level*nx*ny + j*nx + i]
 *      then deep_copy'd into import_state.fields[var]. We reproduce this exact
 *      transpose and write into a REAL cece::DualView3D standing in for
 *      import_state.fields[var], and read it back to assert what a downstream
 *      consumer would observe.
 *
 * The stale-data hazard is the composition: a step that RESOLVES a *different*
 * bracket than the previous step must NOT observe the previous step's field. A
 * step that resolves the SAME bracket (a cache hit) must observe the field that
 * bracket assembled (which, because the read is a pure function of the bracket,
 * equals the field this step's inputs would have assembled) — again never a
 * different prior step's field. This test drives sequences that mix hits
 * (repeated brackets) and misses (new brackets) and asserts the invariant after
 * every step.
 *
 * ----------------------------------------------------------------------------
 * Modelling the per-step inputs
 * ----------------------------------------------------------------------------
 * In production the read+regrid for a resolved bracket is a deterministic,
 * side-effect-free function of that bracket (test_regrid_conservation_properties
 * Property 9a proves apply_regrid_plan is deterministic). So "the field
 * assembled for this step's inputs" is fully determined by the resolved bracket.
 * We model this with a deterministic assemble(bracket) -> buffer function over a
 * small pool of distinct brackets. Repeating a bracket forces a genuine
 * production cache hit (bracket_equal true); a fresh bracket forces a miss.
 * The expected field for a step is therefore assemble(that step's bracket),
 * regardless of hit/miss, and never assemble(a different, prior bracket).
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <Kokkos_Core.hpp>
#include <cstddef>
#include <vector>

#include "cece/cece_compute.hpp"        // DualView3D
#include "cece/cece_driver_facade.hpp"  // SliceCacheEntry, RecordBracket, bracket_equal

namespace cece {

// ============================================================================
// Test-only friend shim for the private static bracket_equal helper. Declared a
// friend inside CeceDriverOrchestrator (SliceCacheTestAccess, see
// include/cece/cece_driver_facade.hpp). Exercises the production comparison
// directly, not a copy. (Same shim struct used by
// tests/test_slice_cache_equivalence.cpp; each property-test translation unit
// owns its own copy, so this is self-contained.)
// ============================================================================
struct SliceCacheTestAccess {
    static bool Equal(const RecordBracket& a, const RecordBracket& b) {
        return CeceDriverOrchestrator::bracket_equal(a, b);
    }
};

namespace {

// A small field shape (field_nlev, nx, ny). Kept modest so the total buffer
// stays small across >=100 iterations x several steps (well within the ~7 GB
// container limit).
struct FieldShape {
    int field_nlev;
    int nx;
    int ny;
    std::size_t size() const {
        return static_cast<std::size_t>(field_nlev) * nx * ny;
    }
};

rc::Gen<FieldShape> genShape() {
    return rc::gen::apply([](int nlev, int nx, int ny) { return FieldShape{nlev, nx, ny}; }, rc::gen::inRange(1, 4), rc::gen::inRange(1, 8),
                          rc::gen::inRange(1, 8));
}

// A distinct bracket carries a distinct integer key so we can deterministically
// derive the field it assembles. i0 doubles as the identity of the bracket in
// this model: distinct keys => distinct brackets (bracket_equal false), the
// same key repeated => the same bracket (bracket_equal true, a cache hit).
RecordBracket MakeBracket(int key) {
    RecordBracket b;
    b.i0 = key;
    b.i1 = key;      // i0 == i1 -> single-read bracket; weight irrelevant
    b.weight = 0.0;  // exactly equal weights so bracket_equal keys off i0/i1
    b.valid = true;
    return b;
}

// The assembled `[level][j][i]` ingest buffer for a given bracket. In production
// this is a deterministic, side-effect-free function of the resolved bracket
// (the read+regrid). Here we make it a deterministic function of the bracket key
// and the shape so that:
//   - the SAME bracket always assembles the SAME buffer (matches a real cache
//     hit reusing the stored buffer), and
//   - DISTINCT brackets assemble DISTINCT buffers (so a stale carry-over from a
//     prior step is detectable).
std::vector<double> AssembleForBracket(int key, const FieldShape& shape) {
    std::vector<double> buf(shape.size());
    for (std::size_t idx = 0; idx < buf.size(); ++idx) {
        // Distinct per (key, idx); the key term guarantees different brackets
        // produce different values at every position.
        buf[idx] = static_cast<double>(key) * 1000.0 + static_cast<double>(idx) + static_cast<double>(key) * 0.5;
    }
    return buf;
}

// Reproduce the production single consolidated transpose + write of the core
// import field (AssembleReplicatedField, ~L688-720). Given the assembled
// `[level][j][i]` ingest buffer, transpose into the (i, j, level) core import
// DualView using the exact production index math and deep_copy it in.
void WriteCoreImportField(DualView3D& core_field, const std::vector<double>& ingest_buffer, const FieldShape& shape) {
    const int nx = shape.nx;
    const int ny = shape.ny;
    const int nlev = shape.field_nlev;
    const std::size_t target_spatial = static_cast<std::size_t>(nx) * ny;

    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> transposed_host("assembled_field_host", nx, ny, nlev);
    for (int level = 0; level < nlev; ++level) {
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                transposed_host(i, j, level) = ingest_buffer[static_cast<std::size_t>(level) * target_spatial + static_cast<std::size_t>(j) * nx + i];
            }
        }
    }

    auto core_view = core_field.view_device();
    Kokkos::deep_copy(core_view, transposed_host);
    core_field.modify_device();
    core_field.sync_host();
}

// Read the core import field back into a flat `[level][j][i]` buffer so it can be
// compared against an expected assembled buffer element-for-element. Uses the
// same index math in reverse.
std::vector<double> ReadCoreImportField(DualView3D& core_field, const FieldShape& shape) {
    const int nx = shape.nx;
    const int ny = shape.ny;
    const int nlev = shape.field_nlev;
    const std::size_t target_spatial = static_cast<std::size_t>(nx) * ny;

    core_field.sync_host();
    auto host = core_field.view_host();

    std::vector<double> flat(shape.size());
    for (int level = 0; level < nlev; ++level) {
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                flat[static_cast<std::size_t>(level) * target_spatial + static_cast<std::size_t>(j) * nx + i] = host(i, j, level);
            }
        }
    }
    return flat;
}

// One production-faithful ingest step. Given the resolved bracket for this step
// and the live slice cache, decide hit vs miss with the REAL production gate,
// obtain the ingest buffer exactly as production would (reuse on hit, assemble
// on miss + repopulate the cache), then write the core import field via the
// consolidated transpose. Returns nothing; the core import field is mutated in
// place, mirroring import_state.fields[var].
void RunIngestStep(int bracket_key, const FieldShape& shape, SliceCacheEntry& slice_cache, DualView3D& core_field) {
    const RecordBracket bracket = MakeBracket(bracket_key);

    // Production hit gate (cece_driver_facade.cpp ~L930-937).
    const bool cache_hit = slice_cache.valid && SliceCacheTestAccess::Equal(bracket, slice_cache.last_bracket);

    std::vector<double> ingest_buffer;
    if (cache_hit) {
        // HIT: reuse the cached buffer, no reread/regrid (production ~L935).
        ingest_buffer = slice_cache.ingest_buffer;
    } else {
        // MISS: assemble for this step's resolved bracket, then repopulate the
        // cache exactly as production does (~L1060-1063).
        ingest_buffer = AssembleForBracket(bracket_key, shape);
        slice_cache.last_bracket = bracket;
        slice_cache.ingest_buffer = ingest_buffer;
        slice_cache.ingest_size = shape.size();
        slice_cache.valid = true;
    }

    // Consolidated transpose + authoritative write of the core import field.
    WriteCoreImportField(core_field, ingest_buffer, shape);
}

}  // namespace

// ============================================================================
// Property 4: No stale data across steps under slice-cache hits.
// Feature: ingest-copy-consolidation, Property 4: no stale data across steps
// **Validates: Requirements 1.4**
//
// Drive a sequence of steps whose brackets are drawn from a small pool, so the
// sequence naturally mixes cache HITS (a repeated bracket) and cache MISSES (a
// fresh bracket). After EVERY step, the core import field MUST equal the field
// assembled for THAT step's bracket (AssembleForBracket(bracket_key)), and MUST
// NOT equal the field assembled for the immediately preceding step's bracket
// whenever that prior bracket differs. This catches any stale carry-over.
// ============================================================================
RC_GTEST_PROP(IngestNoStaleDataProperty, Property4_NoStaleDataAcrossSteps, ()) {
    const FieldShape shape = *genShape();

    // A pool of distinct bracket keys; drawing step keys from this pool makes
    // repeats (hits) and new values (misses) both likely. Small pool + longer
    // sequence => many genuine hits are exercised.
    const int pool_size = *rc::gen::inRange(1, 5);   // 1..4 distinct brackets
    const int num_steps = *rc::gen::inRange(2, 13);  // 2..12 steps
    const auto step_keys = *rc::gen::container<std::vector<int>>(static_cast<std::size_t>(num_steps), rc::gen::inRange(0, pool_size));

    // One live cache + one live core import field for the whole sequence, exactly
    // as a single stream variable keeps one slice cache and one import field
    // across AdvanceTime steps.
    SliceCacheEntry slice_cache;
    DualView3D core_field("import_state_field", shape.nx, shape.ny, shape.field_nlev);

    int prev_key = -1;  // no previous step yet
    bool saw_hit = false;
    bool saw_miss = false;

    for (int step = 0; step < num_steps; ++step) {
        const int key = step_keys[static_cast<std::size_t>(step)];

        // Track whether this step is a hit or miss BEFORE running it, using the
        // same gate, so the test also confirms the sequence exercises both.
        const bool would_hit = slice_cache.valid && SliceCacheTestAccess::Equal(MakeBracket(key), slice_cache.last_bracket);
        if (would_hit) {
            saw_hit = true;
        } else {
            saw_miss = true;
        }

        RunIngestStep(key, shape, slice_cache, core_field);

        // The core import field MUST equal the field assembled for THIS step's
        // bracket — never a value carried from a prior step.
        const std::vector<double> observed = ReadCoreImportField(core_field, shape);
        const std::vector<double> expected_for_this_step = AssembleForBracket(key, shape);

        RC_ASSERT(observed.size() == expected_for_this_step.size());
        RC_ASSERT(observed == expected_for_this_step);

        // And whenever this step's bracket differs from the immediately prior
        // step's bracket, the observed field MUST NOT be the prior step's field
        // (an explicit stale-carry-over check; distinct brackets assemble
        // distinct buffers by construction).
        if (prev_key != -1 && prev_key != key) {
            const std::vector<double> prior_field = AssembleForBracket(prev_key, shape);
            RC_ASSERT(observed != prior_field);
        }

        prev_key = key;
    }

    // Sanity: over the generated sequence we expect to have exercised at least
    // one miss (the first step is always a miss). This keeps the property honest
    // that the miss path is covered; hits are covered whenever a key repeats.
    RC_ASSERT(saw_miss);
    (void)saw_hit;  // hits are opportunistic; not asserted every iteration
}

}  // namespace cece

// ============================================================================
// Kokkos lifecycle. The transpose/write path uses Kokkos HostSpace + DualView
// views, so Kokkos must be initialized. Mirrors the lifecycle used by
// tests/test_regrid_conservation_properties.cpp, minus the MPI dependence (this
// property touches no collectives).
// ============================================================================
class KokkosEnvironment : public ::testing::Environment {
   private:
    int argc_;
    char** argv_;

   public:
    KokkosEnvironment(int argc, char** argv) : argc_(argc), argv_(argv) {}

    void SetUp() override {
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize(argc_, argv_);
        }
    }

    void TearDown() override {
        if (Kokkos::is_initialized()) {
            Kokkos::finalize();
        }
    }
};

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new KokkosEnvironment(argc, argv));
    return RUN_ALL_TESTS();
}
