// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors
//
// Feature: temporal-endpoint-regrid-cache, Property 1: Endpoint-blend equals
// source-blend-then-regrid
//
// **Validates: Requirements 1.4, 5.1, 5.2, 5.3, 5.4**
//
// ----------------------------------------------------------------------------
// What this test exercises and why it is faithful
// ----------------------------------------------------------------------------
// The temporal-endpoint-regrid-cache optimization replaces the current
// source-grid path "blend two source records then regrid the blend once"
//
//     R((1-w)*A + w*B)
//
// with the destination-grid endpoint path "regrid each record, then blend the
// two destination-grid results"
//
//     (1-w)*R(A) + w*R(B)
//
// where R is the regrid operator (a fixed CSR sparse matrix-vector product, a
// LINEAR operator). By linearity these are mathematically identical; the two
// paths only differ in floating-point summation order for matrix plans.
//
// This test validates that identity directly against the REAL production regrid
// operator (`cece::io::apply_regrid_plan`) and the REAL plan type
// (`cece::io::RegridPlan`), for:
//   * an identity plan (R is a plain cell copy) -> the two paths are bit-for-bit
//     equal, and
//   * a small conservative matrix plan (2x2 -> 4x4) -> the two paths agree
//     within kRegridTolerance (1e-10), the same tolerance the sibling
//     test_numerical_equivalence.cpp uses.
//
// RapidCheck generates random source records A, B and a blend weight w in
// [0, 1] (including the boundaries w = 0 and w = 1, i.e. single-record
// fidelity), and injects 1e20 fill values into some A/B elements to verify the
// current path's raw-double, fill-blind linearity is preserved by the endpoint
// path. This mirrors the helpers and math in test_numerical_equivalence.cpp;
// see that file for the commutativity rationale this feature relies on.
//
// Grid extents are kept tiny so the total buffer stays modest across the >=100
// RapidCheck iterations, respecting the ~7 GB cece-dev container RAM limit.

#include <gtest/gtest.h>
#include <mpi.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <Kokkos_Core.hpp>
#include <axis/axis.hpp>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

#include "cece/cece_regridder_utils.hpp"

namespace cece::test {

namespace {

// Same tolerance as tests/test_numerical_equivalence.cpp: the matrix path
// accumulates floating point in a different summation order than the
// source-blend-then-regrid reference, so matrix plans are asserted within this
// tolerance while identity plans are asserted bit-for-bit.
constexpr double kRegridTolerance = 1.0e-10;

// The CEDS OC fill value the design calls out (Req 5, fill-handling
// verification). Injected into some A/B elements so the property proves the
// endpoint path preserves the current path's raw-double linear handling of
// fill values.
constexpr double kFillValue = 1.0e20;

// Blend two source records on the source grid: (1-w)*src0 + w*src1. This is the
// exact expression the current AdvanceTime path uses before the single regrid
// apply (matches blend_source in test_numerical_equivalence.cpp).
std::vector<double> blend_source(const std::vector<double>& src0, const std::vector<double>& src1, double w) {
    std::vector<double> out(src0.size());
    for (size_t k = 0; k < src0.size(); ++k) {
        out[k] = (1.0 - w) * src0[k] + w * src1[k];
    }
    return out;
}

// Build a small, deterministic conservative regrid plan from a coarse source
// grid to a finer destination grid, matching build_matrix_plan in
// test_numerical_equivalence.cpp (default "consd" mapalgo configuration).
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
// same weight w the source-grid path uses (Req 5.4). Returns false if either
// per-record regrid apply fails.
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

// RapidCheck generator for one source record of `n` cells. Values are drawn
// from a representative pool that spans negatives, zeros, fractions, and large
// magnitudes so the equivalence check is meaningful. The fill value 1e20 is in
// the pool so it is exercised across records/cells (Req 5 fill handling).
rc::Gen<std::vector<double>> genRecord(std::size_t n) {
    static const std::vector<double> kPool = {
        0.0, -0.0, 1.0, -1.0, 3.14159265358979, -2.718281828459045, 12.5, -7.25, 100.0, -100.0, 0.125, -0.0625, kFillValue, 42.0, -42.0, 1234.5,
    };
    return rc::gen::container<std::vector<double>>(
        n, rc::gen::map(rc::gen::inRange<std::size_t>(0, kPool.size()), [](std::size_t idx) { return kPool[idx]; }));
}

// RapidCheck generator for a blend weight in [0, 1]. Uses a discrete set that
// explicitly includes the boundaries w = 0 and w = 1 (single-record fidelity,
// Req 5.2) plus interior weights, all exactly representable so identity-plan
// comparisons stay bit-exact.
rc::Gen<double> genWeight() {
    static const std::vector<double> kWeights = {0.0, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 1.0};
    return rc::gen::map(rc::gen::inRange<std::size_t>(0, kWeights.size()), [](std::size_t idx) { return kWeights[idx]; });
}

}  // namespace

// ============================================================================
// Property 1 (identity plan): endpoint-blend equals source-blend-then-regrid
// BIT-FOR-BIT.
//
// Feature: temporal-endpoint-regrid-cache, Property 1: Endpoint-blend equals
// source-blend-then-regrid
// **Validates: Requirements 1.4, 5.1, 5.2, 5.3, 5.4**
//
// For an identity plan R (a plain cell copy), for any two source records A, B
// and any weight w in [0, 1] (incl. w in {0, 1} and injected 1e20 fills), the
// destination-grid endpoint blend (1-w)*R(A) + w*R(B) equals the source-grid
// path R((1-w)A + w B) bit-for-bit.
// ============================================================================
RC_GTEST_PROP(EndpointBlendEquivalenceProperty, Property1_IdentityPlanBitForBit, ()) {
    constexpr int kNx = 3;
    constexpr int kNy = 2;

    io::RegridPlan plan;
    plan.file_nx = kNx;
    plan.file_ny = kNy;
    plan.j0 = 0;
    plan.j1 = kNy;
    plan.identity = true;
    plan.built = true;

    const std::size_t n = static_cast<std::size_t>(kNx) * static_cast<std::size_t>(kNy);
    const std::vector<double> src0 = *genRecord(n);
    const std::vector<double> src1 = *genRecord(n);
    const double w = *genWeight();

    std::vector<double> endpoint_dst;
    std::vector<double> source_dst;
    RC_ASSERT(endpoint_blend(plan, src0, src1, w, kNx, kNy, kNx, endpoint_dst));
    RC_ASSERT(source_blend_then_regrid(plan, src0, src1, w, kNx, kNy, kNx, source_dst));

    RC_ASSERT(endpoint_dst.size() == source_dst.size());
    // Identity plan is a plain copy, so both summation orders are bit-for-bit
    // equal (Req 5.1, 5.2, 5.3). This holds at w = 0 and w = 1 too.
    for (std::size_t k = 0; k < endpoint_dst.size(); ++k) {
        RC_ASSERT(endpoint_dst[k] == source_dst[k]);
    }
    RC_ASSERT(endpoint_dst == source_dst);
}

// ============================================================================
// Property 1 (matrix plan): endpoint-blend equals source-blend-then-regrid
// within kRegridTolerance (1e-10).
//
// Feature: temporal-endpoint-regrid-cache, Property 1: Endpoint-blend equals
// source-blend-then-regrid
// **Validates: Requirements 1.4, 5.1, 5.2, 5.3, 5.4**
//
// For a small real conservative matrix plan R (2x2 -> 4x4), for any two source
// records A, B and any weight w in [0, 1] (incl. w in {0, 1} and injected 1e20
// fills), (1-w)*R(A) + w*R(B) equals R((1-w)A + w B) within kRegridTolerance.
// The tolerance (not bit-identity) is required because the matrix SpMV
// accumulates in a different summation order between the two paths.
// ============================================================================
RC_GTEST_PROP(EndpointBlendEquivalenceProperty, Property1_MatrixPlanWithinTolerance, ()) {
    constexpr int kSrcNx = 2;
    constexpr int kSrcNy = 2;
    constexpr int kDstNx = 4;
    constexpr int kDstNy = 4;

    // Rebuilt once per iteration; extents are tiny so this stays cheap and well
    // within the container RAM budget across >=100 iterations.
    static const std::vector<double> src_lons{-10.0, 10.0};
    static const std::vector<double> src_lats{-10.0, 10.0};
    static const std::vector<double> dst_lons{-7.5, -2.5, 2.5, 7.5};
    static const std::vector<double> dst_lats{-7.5, -2.5, 2.5, 7.5};

    const io::RegridPlan plan = build_matrix_plan(kSrcNx, kSrcNy, src_lons, src_lats, kDstNx, kDstNy, dst_lons, dst_lats);
    RC_ASSERT(plan.built);
    RC_ASSERT(!plan.identity);

    const std::size_t n = static_cast<std::size_t>(kSrcNx) * static_cast<std::size_t>(kSrcNy);
    const std::vector<double> src0 = *genRecord(n);
    const std::vector<double> src1 = *genRecord(n);
    const double w = *genWeight();

    std::vector<double> endpoint_dst;
    std::vector<double> source_dst;
    RC_ASSERT(endpoint_blend(plan, src0, src1, w, kSrcNx, kSrcNy, kDstNx, endpoint_dst));
    RC_ASSERT(source_blend_then_regrid(plan, src0, src1, w, kSrcNx, kSrcNy, kDstNx, source_dst));

    RC_ASSERT(endpoint_dst.size() == source_dst.size());
    for (std::size_t k = 0; k < endpoint_dst.size(); ++k) {
        const double diff = std::abs(endpoint_dst[k] - source_dst[k]);
        // Fill-dominated cells reach ~1e20 magnitude; scale the tolerance by the
        // cell magnitude so the check remains meaningful (relative agreement)
        // rather than demanding 1e-10 absolute agreement on 1e20 values.
        const double scale = std::max({1.0, std::abs(endpoint_dst[k]), std::abs(source_dst[k])});
        RC_ASSERT(diff <= kRegridTolerance * scale);
    }
}

}  // namespace cece::test

// ============================================================================
// Kokkos + MPI global test environment and custom main().
//
// The matrix path uses Kokkos + AXIS (via apply_regrid_plan and the weight
// generator), so Kokkos must be initialized before any test runs and finalized
// after. This mirrors the KokkosMpiEnvironment in
// tests/test_numerical_equivalence.cpp; we provide our own main() so the strong
// definition here overrides gtest_main's weak one.
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
