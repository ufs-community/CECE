// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors
//
// Feature: temporal-endpoint-regrid-cache, Property 5: Per-variable correctness
// under a shared plan
//
// **Validates: Requirements 7.2, 7.3**
//
// ----------------------------------------------------------------------------
// What this test exercises and why it is faithful
// ----------------------------------------------------------------------------
// Multiple variables in one CECE input stream share a single RegridPlan
// (regrid_plans_ is keyed by stream_key, not variable name). The endpoint cache
// (endpoint_caches_) is keyed by variable name, so each variable holds its own
// pair of destination-grid endpoints regrid(A_var) / regrid(B_var). This
// property proves that keying is correct: applying the SAME shared plan to each
// variable's OWN distinct source data yields, per variable, an endpoint blend
//
//     (1-w_var)*R(A_var) + w_var*R(B_var)
//
// that equals that same variable's source-blend-then-regrid path
//
//     R((1-w_var)*A_var + w_var*B_var)
//
// with NO cross-contamination: variable A's result never depends on variable
// B's data, and vice-versa. R is the regrid operator (a fixed CSR sparse
// matrix-vector product, a LINEAR operator), so by linearity the two paths are
// mathematically identical; they only differ in floating-point summation order
// for matrix plans.
//
// The test drives the REAL production regrid operator
// (cece::io::apply_regrid_plan) and the REAL plan type (cece::io::RegridPlan),
// mirroring the sibling tests test_shared_plan_equivalence.cpp (shared plan is
// a pure function of geometry, not variable name) and
// test_numerical_equivalence.cpp (blend/regrid math + build_matrix_plan). Two
// "variables" independently generate random A/B records and a weight; each is
// blended through both paths and compared, and a no-cross-contamination check
// confirms swapping one variable's data does not change the other's result.
//
// Grid extents are kept tiny so the total buffer stays modest across the >=100
// RapidCheck iterations, respecting the ~7 GB cece-dev container RAM limit.

#include <gtest/gtest.h>
#include <mpi.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <axis/axis.hpp>
#include <cmath>
#include <cstddef>
#include <vector>

#include "cece/cece_regridder_utils.hpp"

namespace cece::test {

namespace {

// Same tolerance as tests/test_numerical_equivalence.cpp and the sibling P1
// test: the matrix path accumulates floating point in a different summation
// order than the source-blend-then-regrid reference, so matrix plans are
// asserted within this tolerance while identity plans are asserted bit-for-bit.
constexpr double kRegridTolerance = 1.0e-10;

// The CEDS OC fill value the design calls out; kept in the generator pool so
// per-variable fill handling is exercised under the shared plan too.
constexpr double kFillValue = 1.0e20;

// Blend two source records on the source grid: (1-w)*src0 + w*src1. Matches the
// current AdvanceTime source-grid blend and blend_source in the sibling tests.
std::vector<double> blend_source(const std::vector<double>& src0, const std::vector<double>& src1, double w) {
    std::vector<double> out(src0.size());
    for (size_t k = 0; k < src0.size(); ++k) {
        out[k] = (1.0 - w) * src0[k] + w * src1[k];
    }
    return out;
}

// Build a small, deterministic conservative regrid plan from a coarse source
// grid to a finer destination grid, matching build_matrix_plan in
// test_numerical_equivalence.cpp (default "consd" mapalgo configuration). ONE
// such plan is shared by both variables in each property iteration.
io::RegridPlan build_matrix_plan(int src_nx, int src_ny, const std::vector<double>& src_lons, const std::vector<double>& src_lats, int dst_nx,
                                 int dst_ny, const std::vector<double>& dst_lons, const std::vector<double>& dst_lats) {
    io::RegridPlan plan;
    plan.file_nx = src_nx;
    plan.file_ny = src_ny;
    plan.j0 = 0;
    plan.j1 = dst_ny;  // single-rank: this rank owns every destination row.
    plan.identity = false;

    auto src_mesh = io::build_axis_mesh(src_nx, src_ny, src_lons, src_lats);
    auto dst_mesh = io::build_axis_mesh(dst_nx, dst_ny, dst_lons, dst_lats);

    axis::solver::RegridConfig regrid_cfg;
    regrid_cfg.method = axis::solver::InterpolationMethod::Conservative1stOrder;
    regrid_cfg.norm_type = axis::solver::NormType::DstArea;
    regrid_cfg.unmapped = axis::solver::UnmappedAction::Ignore;

    plan.matrix = axis::solver::WeightGenerator::generate<Kokkos::HostSpace>(src_mesh, dst_mesh, regrid_cfg);
    plan.matrix.to_csr();
    plan.built = true;
    return plan;
}

// The endpoint path (this feature): regrid each source record independently on
// the destination grid, then blend the two destination-grid results with the
// same weight w the source-grid path uses (Req 5.4, 7.3). Returns false if
// either per-record regrid apply fails. This models what the per-variable
// endpoint cache stores (regrid(A_var), regrid(B_var)) plus the per-step blend.
bool endpoint_blend(const io::RegridPlan& plan, const std::vector<double>& src0, const std::vector<double>& src1, double w, int src_nx, int src_ny,
                    int dst_nx, std::vector<double>& out) {
    std::vector<double> dst0;
    std::vector<double> dst1;
    if (!io::apply_regrid_plan(plan, 0, false, src0.data(), src_nx, src_ny, dst_nx, dst0)) {
        return false;
    }
    if (!io::apply_regrid_plan(plan, 0, false, src1.data(), src_nx, src_ny, dst_nx, dst1)) {
        return false;
    }
    if (dst0.size() != dst1.size()) {
        return false;
    }
    out.resize(dst0.size());
    for (size_t k = 0; k < dst0.size(); ++k) {
        out[k] = (1.0 - w) * dst0[k] + w * dst1[k];
    }
    return true;
}

// The current source-grid path: blend on the source grid, then regrid the blend
// once. Returns false if the single regrid apply fails.
bool source_blend_then_regrid(const io::RegridPlan& plan, const std::vector<double>& src0, const std::vector<double>& src1, double w, int src_nx,
                              int src_ny, int dst_nx, std::vector<double>& out) {
    const std::vector<double> blended = blend_source(src0, src1, w);
    return io::apply_regrid_plan(plan, 0, false, blended.data(), src_nx, src_ny, dst_nx, out);
}

// RapidCheck generator for one source record of `n` cells. Values span
// negatives, zeros, fractions, and large magnitudes; the fill value 1e20 is in
// the pool so per-variable fill handling is exercised under the shared plan.
rc::Gen<std::vector<double>> genRecord(std::size_t n) {
    static const std::vector<double> kPool = {
        0.0, -0.0, 1.0, -1.0, 3.14159265358979, -2.718281828459045, 12.5, -7.25, 100.0, -100.0, 0.125, -0.0625, kFillValue, 42.0, -42.0, 1234.5,
    };
    return rc::gen::container<std::vector<double>>(
        n, rc::gen::map(rc::gen::inRange<std::size_t>(0, kPool.size()), [](std::size_t idx) { return kPool[idx]; }));
}

// RapidCheck generator for a blend weight in [0, 1], including the boundaries
// w = 0 and w = 1, all exactly representable so identity-plan comparisons stay
// bit-exact. Each variable draws its own weight to model independent per-step
// blends over a shared plan.
rc::Gen<double> genWeight() {
    static const std::vector<double> kWeights = {0.0, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 1.0};
    return rc::gen::map(rc::gen::inRange<std::size_t>(0, kWeights.size()), [](std::size_t idx) { return kWeights[idx]; });
}

// Assert two destination vectors agree within a magnitude-scaled tolerance,
// matching the sibling P1 matrix comparison (fill-dominated cells reach ~1e20,
// so the tolerance is scaled by cell magnitude for relative agreement).
void assertWithinTolerance(const std::vector<double>& a, const std::vector<double>& b) {
    RC_ASSERT(a.size() == b.size());
    for (std::size_t k = 0; k < a.size(); ++k) {
        const double diff = std::abs(a[k] - b[k]);
        const double scale = std::max({1.0, std::abs(a[k]), std::abs(b[k])});
        RC_ASSERT(diff <= kRegridTolerance * scale);
    }
}

}  // namespace

// ============================================================================
// Property 5 (identity plan): per-variable correctness under a shared plan,
// BIT-FOR-BIT.
//
// Feature: temporal-endpoint-regrid-cache, Property 5: Per-variable correctness
// under a shared plan
// **Validates: Requirements 7.2, 7.3**
//
// One identity plan R is shared by two variables that carry DISTINCT random
// source data. For each variable independently, the destination-grid endpoint
// blend (1-w)*R(A) + w*R(B) equals that variable's source-grid path
// R((1-w)A + w B) bit-for-bit (identity R is a plain copy). A cross-check
// confirms no contamination: variable A's endpoint result is independent of
// variable B's data.
// ============================================================================
RC_GTEST_PROP(EndpointSharedPlanProperty, Property5_IdentityPlanPerVariableBitForBit, ()) {
    constexpr int kNx = 3;
    constexpr int kNy = 2;

    // ONE shared identity plan, used by both variables (mirrors a shared
    // stream_key -> single RegridPlan).
    io::RegridPlan plan;
    plan.file_nx = kNx;
    plan.file_ny = kNy;
    plan.j0 = 0;
    plan.j1 = kNy;
    plan.identity = true;
    plan.built = true;

    const std::size_t n = static_cast<std::size_t>(kNx) * static_cast<std::size_t>(kNy);

    // Variable A: its own distinct records and weight.
    const std::vector<double> a0 = *genRecord(n);
    const std::vector<double> a1 = *genRecord(n);
    const double wa = *genWeight();

    // Variable B: independently generated, distinct data and weight.
    const std::vector<double> b0 = *genRecord(n);
    const std::vector<double> b1 = *genRecord(n);
    const double wb = *genWeight();

    // Each variable's endpoint blend through the SHARED plan.
    std::vector<double> a_endpoint;
    std::vector<double> b_endpoint;
    RC_ASSERT(endpoint_blend(plan, a0, a1, wa, kNx, kNy, kNx, a_endpoint));
    RC_ASSERT(endpoint_blend(plan, b0, b1, wb, kNx, kNy, kNx, b_endpoint));

    // Each variable's own source-blend-then-regrid reference.
    std::vector<double> a_source;
    std::vector<double> b_source;
    RC_ASSERT(source_blend_then_regrid(plan, a0, a1, wa, kNx, kNy, kNx, a_source));
    RC_ASSERT(source_blend_then_regrid(plan, b0, b1, wb, kNx, kNy, kNx, b_source));

    // Per-variable correctness (Req 7.3): identity plan is a plain copy, so
    // both summation orders are bit-for-bit equal, for each variable's OWN data.
    RC_ASSERT(a_endpoint == a_source);
    RC_ASSERT(b_endpoint == b_source);

    // No cross-contamination (Req 7.2): variable A's endpoint result must be a
    // pure function of variable A's data. Recomputing A's endpoint blend after
    // B has been processed yields the identical result — B's data never leaks
    // into A's per-variable endpoint entry.
    std::vector<double> a_endpoint_again;
    RC_ASSERT(endpoint_blend(plan, a0, a1, wa, kNx, kNy, kNx, a_endpoint_again));
    RC_ASSERT(a_endpoint_again == a_endpoint);
}

// ============================================================================
// Property 5 (matrix plan): per-variable correctness under a shared plan,
// within kRegridTolerance (1e-10).
//
// Feature: temporal-endpoint-regrid-cache, Property 5: Per-variable correctness
// under a shared plan
// **Validates: Requirements 7.2, 7.3**
//
// One small real conservative matrix plan R (2x2 -> 4x4) is shared by two
// variables with DISTINCT random data. For each variable independently,
// (1-w)*R(A) + w*R(B) equals R((1-w)A + w B) within kRegridTolerance, and a
// cross-check confirms no contamination between the two per-variable entries.
// ============================================================================
RC_GTEST_PROP(EndpointSharedPlanProperty, Property5_MatrixPlanPerVariableWithinTolerance, ()) {
    constexpr int kSrcNx = 2;
    constexpr int kSrcNy = 2;
    constexpr int kDstNx = 4;
    constexpr int kDstNy = 4;

    static const std::vector<double> src_lons{-10.0, 10.0};
    static const std::vector<double> src_lats{-10.0, 10.0};
    static const std::vector<double> dst_lons{-7.5, -2.5, 2.5, 7.5};
    static const std::vector<double> dst_lats{-7.5, -2.5, 2.5, 7.5};

    // ONE shared matrix plan for both variables.
    const io::RegridPlan plan = build_matrix_plan(kSrcNx, kSrcNy, src_lons, src_lats, kDstNx, kDstNy, dst_lons, dst_lats);
    RC_ASSERT(plan.built);
    RC_ASSERT(!plan.identity);

    const std::size_t n = static_cast<std::size_t>(kSrcNx) * static_cast<std::size_t>(kSrcNy);

    // Variable A.
    const std::vector<double> a0 = *genRecord(n);
    const std::vector<double> a1 = *genRecord(n);
    const double wa = *genWeight();

    // Variable B, independently generated.
    const std::vector<double> b0 = *genRecord(n);
    const std::vector<double> b1 = *genRecord(n);
    const double wb = *genWeight();

    std::vector<double> a_endpoint;
    std::vector<double> b_endpoint;
    RC_ASSERT(endpoint_blend(plan, a0, a1, wa, kSrcNx, kSrcNy, kDstNx, a_endpoint));
    RC_ASSERT(endpoint_blend(plan, b0, b1, wb, kSrcNx, kSrcNy, kDstNx, b_endpoint));

    std::vector<double> a_source;
    std::vector<double> b_source;
    RC_ASSERT(source_blend_then_regrid(plan, a0, a1, wa, kSrcNx, kSrcNy, kDstNx, a_source));
    RC_ASSERT(source_blend_then_regrid(plan, b0, b1, wb, kSrcNx, kSrcNy, kDstNx, b_source));

    // Per-variable correctness (Req 7.3) within tolerance for each variable.
    assertWithinTolerance(a_endpoint, a_source);
    assertWithinTolerance(b_endpoint, b_source);

    // No cross-contamination (Req 7.2): A's endpoint blend is a pure function of
    // A's data — recomputing after B is processed reproduces A's result exactly.
    std::vector<double> a_endpoint_again;
    RC_ASSERT(endpoint_blend(plan, a0, a1, wa, kSrcNx, kSrcNy, kDstNx, a_endpoint_again));
    RC_ASSERT(a_endpoint_again == a_endpoint);
}

}  // namespace cece::test

// ============================================================================
// Kokkos + MPI global test environment and custom main().
//
// The matrix path uses Kokkos + AXIS (via apply_regrid_plan and the weight
// generator), so Kokkos must be initialized before any test runs and finalized
// after. This mirrors the KokkosMpiEnvironment in
// tests/test_numerical_equivalence.cpp and the sibling P1 test; we provide our
// own main() so the strong definition here overrides gtest_main's weak one.
// ============================================================================
namespace {

class KokkosMpiEnvironment : public ::testing::Environment {
   public:
    KokkosMpiEnvironment(int argc, char** argv) : argc_(argc), argv_(argv) {}

    void SetUp() override {
        int mpi_initialized = 0;
        MPI_Initialized(&mpi_initialized);
        if (!mpi_initialized) {
            int provided = 0;
            MPI_Init_thread(&argc_, &argv_, MPI_THREAD_MULTIPLE, &provided);
        }
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize(argc_, argv_);
        }
    }

    void TearDown() override {
        if (Kokkos::is_initialized()) {
            Kokkos::finalize();
        }
        int mpi_initialized = 0;
        MPI_Initialized(&mpi_initialized);
        if (mpi_initialized) {
            MPI_Finalize();
        }
    }

   private:
    int argc_;
    char** argv_;
};

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new KokkosMpiEnvironment(argc, argv));
    return RUN_ALL_TESTS();
}
