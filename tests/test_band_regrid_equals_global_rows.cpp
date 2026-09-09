// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors

/**
 * @file test_band_regrid_equals_global_rows.cpp
 * @brief Property-based tests that the per-rank band regrid equals the
 *        corresponding rows of the replicated (global) regrid.
 *
 * Feature: distributed-domain-decomposition
 *   Property 3: Band regrid slice equals the corresponding rows of the
 *               replicated regrid
 *   Property 9: Regrid apply conserves its aggregate per band
 *
 * ----------------------------------------------------------------------------
 * What this test exercises and why it is faithful
 * ----------------------------------------------------------------------------
 * `CeceDriverOrchestrator::RegridToBandBuffer` composes, per output level, a
 * single `cece::io::apply_regrid_plan` over the rank's destination latitude
 * band `[j0, j1)`, concatenating the per-level band slices (each of size
 * `nx * (j1 - j0)`) into the band buffer it hands to `WriteBandToImport`. The
 * pre-rework `RegridToDestinationBuffer` did the same per-level apply but then
 * `MPI_Allgatherv`'d the bands into a global `field_nlev * nx * ny` buffer
 * replicated on every rank. The whole rework rests on one invariant:
 *
 *     the band slice `apply_regrid_plan` emits for rank r is, element for
 *     element, exactly rows [band_start(r), band_start(r+1)) of the global
 *     regrid the replicated path assembled.
 *
 * `RegridToBandBuffer` requires a full orchestrator + MPI + AMIO to invoke
 * directly, so — per the design's Testing Strategy ("asserted at the
 * RegridToBandBuffer / apply_regrid_plan boundary for a fixed plan and
 * generated source fields") — this test drives the REAL production
 * `cece::io::apply_regrid_plan` at that boundary. For a generated source field
 * and a fixed plan it compares:
 *
 *   (a) a REFERENCE global regrid: apply the plan over the whole destination
 *       grid [0, ny), yielding `nx * ny` rows per level, and
 *   (b) the per-rank BAND regrid: for every rank r of a generated block
 *       decomposition, apply the (same-geometry) band plan over
 *       [band_start(r), band_start(r+1)),
 *
 * asserting the band slice for rank r equals rows [band_start(r),
 * band_start(r+1)) of the global result element-for-element (Property 3) and
 * that the sum over rank r's band equals the sum over the corresponding global
 * rows (Property 9). Every global row is covered by exactly one rank, so the
 * concatenation of all bands reconstructs the global field with no gaps or
 * overlaps.
 *
 * Two plan families are exercised, matching the sibling regrid property tests
 * (test_shared_plan_equivalence.cpp, test_endpoint_shared_plan.cpp,
 * test_regrid_conservation_properties.cpp):
 *
 *   - IDENTITY ("passthrough") plans — the deterministic copy path the driver
 *     uses when source and destination grids coincide. Here `apply_regrid_plan`
 *     copies owned source rows directly, so band == global-rows holds
 *     BIT-FOR-BIT and the per-band aggregate matches exactly.
 *
 *   - CONSERVATIVE MATRIX plans (default "consd" mapalgo, DstArea norm) — a
 *     genuine weighted regrid. ONE whole-grid plan is the reference; each rank's
 *     band plan is the ROW-RESTRICTION of that SAME weight matrix to its owned
 *     destination rows (weights and source columns copied verbatim, destination
 *     rows shifted to band-local indices). This is precisely what the driver's
 *     per-band regrid does — re-use the weights the replicated path computed for
 *     those rows and skip the gather — so the per-row SpMV yields band ==
 *     global-rows BIT-FOR-BIT, with no floating-point tolerance needed.
 *
 * The band decomposition uses the exact block formula BandDecomposition::compute
 * embeds (band_start(r) = r*(ny/size) + min(r, ny%size)), so the tiling matches
 * the production regrid path and Property 1.
 *
 * Grid extents and level counts are kept tiny so the total buffer stays modest
 * across the >=100 RapidCheck iterations, respecting the ~7 GB cece-dev
 * container RAM limit.
 *
 * **Validates: Requirements 3.1, 3.2, 3.3, 7.1, 7.3**
 */

#include <gtest/gtest.h>
#include <mpi.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <Kokkos_Core.hpp>
#include <axis/axis.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <vector>

#include "cece/cece_regridder_utils.hpp"

namespace cece::io {
namespace {

// The exact block-decomposition start formula BandDecomposition::compute embeds
// (and that the pre-existing regrid path uses). Mirrored here so the property
// can enumerate every rank's band without a live communicator — identical to
// the Property 1 partition test.
int band_start(int ny, int size, int r) {
    const int band_base = ny / size;
    const int band_rem = ny % size;
    return r * band_base + std::min(r, band_rem);
}

// A FIXED identity (passthrough) plan over an nx * ny grid owning the
// destination row band [j0, j1). Copies owned source rows directly, no AXIS
// weights — the deterministic core. Mirrors MakeIdentityPlan in the sibling
// regrid property tests.
RegridPlan MakeIdentityPlan(int file_nx, int file_ny, int j0, int j1) {
    RegridPlan plan;
    plan.file_nx = file_nx;
    plan.file_ny = file_ny;
    plan.j0 = j0;
    plan.j1 = j1;
    plan.identity = true;
    plan.built = true;
    return plan;
}

// Build a conservative ("consd") regrid plan (DstArea norm) over the WHOLE
// destination grid [0, dst_ny). Mirrors build_matrix_plan in
// test_endpoint_shared_plan.cpp / test_numerical_equivalence.cpp. This is the
// REFERENCE "replicated regrid": one plan whose CSR rows are the destination
// cells (i,j) laid out row-major (dst index = j*dst_nx + i).
RegridPlan BuildWholeGridMatrixPlan(int src_nx, int src_ny, const std::vector<double>& src_lons, const std::vector<double>& src_lats, int dst_nx,
                                    int dst_ny, const std::vector<double>& dst_lons, const std::vector<double>& dst_lats) {
    RegridPlan plan;
    plan.file_nx = src_nx;
    plan.file_ny = src_ny;
    plan.j0 = 0;
    plan.j1 = dst_ny;
    plan.identity = false;

    auto src_mesh = build_axis_mesh(src_nx, src_ny, src_lons, src_lats);
    auto dst_mesh = build_axis_mesh(dst_nx, dst_ny, dst_lons, dst_lats);

    axis::solver::RegridConfig regrid_cfg;
    regrid_cfg.method = axis::solver::InterpolationMethod::Conservative1stOrder;
    regrid_cfg.norm_type = axis::solver::NormType::DstArea;
    regrid_cfg.unmapped = axis::solver::UnmappedAction::Ignore;

    plan.matrix = axis::solver::WeightGenerator::generate<Kokkos::HostSpace>(src_mesh, dst_mesh, regrid_cfg);
    plan.matrix.to_csr();
    plan.built = true;
    return plan;
}

// Row-restrict a whole-grid plan to the destination band [j0, j1), producing
// the plan that rank r would apply for that band.
//
// This is the crux of the band-equals-global-rows invariant. The regrid apply
// is a per-row sparse matrix-vector product: dst(row) = sum_k S(k)*src(col(k))
// over the nonzeros whose destination row is `row`. The rows of the whole-grid
// matrix are indexed by the GLOBAL destination cell `j*nx + i`. A rank owning
// rows [j0, j1) computes exactly the SAME weighted sums for its cells, only
// re-indexed to the band-local destination cell `(j - j0)*nx + i`. So the band
// plan is the whole-grid matrix with:
//   - every nonzero whose global destination row lies in [j0*nx, j1*nx) kept
//     (its weight S and source column col UNCHANGED),
//   - the destination row shifted down by j0*nx,
//   - n_dst reduced to nx*(j1 - j0).
// Because the weights and source columns are copied verbatim from the whole-grid
// matrix, applying this restricted plan yields the corresponding whole-grid rows
// BIT-FOR-BIT — the exact invariant RegridToBandBuffer relies on (it re-uses the
// per-band weights the replicated path also used, then simply skips the gather).
RegridPlan RestrictPlanToBand(const RegridPlan& whole, int dst_nx, int j0, int j1) {
    RegridPlan band;
    band.file_nx = whole.file_nx;
    band.file_ny = whole.file_ny;
    band.j0 = j0;
    band.j1 = j1;
    band.identity = false;

    const int nband = j1 - j0;
    if (nband <= 0) {
        band.built = true;  // surplus rank: applies to zero rows
        return band;
    }

    const axis::index_t row_lo = static_cast<axis::index_t>(j0) * dst_nx;
    const axis::index_t row_hi = static_cast<axis::index_t>(j1) * dst_nx;
    const std::size_t band_n_dst = static_cast<std::size_t>(dst_nx) * nband;
    const std::size_t src_n = whole.matrix.n_src();

    // Read the whole-grid COO (weights + row/col indices) on the host.
    auto factor_list = whole.matrix.factor_list();  // [nnz]
    auto factor_row = whole.matrix.factor_row();     // [nnz] global dst rows
    auto factor_col = whole.matrix.factor_col();     // [nnz] src cols
    const std::size_t nnz = whole.matrix.nnz();

    std::vector<double> b_vals;
    std::vector<axis::index_t> b_rows;
    std::vector<axis::index_t> b_cols;
    b_vals.reserve(nnz);
    b_rows.reserve(nnz);
    b_cols.reserve(nnz);
    for (std::size_t k = 0; k < nnz; ++k) {
        const axis::index_t grow = factor_row[k];
        if (grow >= row_lo && grow < row_hi) {
            b_vals.push_back(factor_list[k]);
            b_rows.push_back(grow - row_lo);  // shift to band-local dst row
            b_cols.push_back(factor_col[k]);
        }
    }

    const std::size_t b_nnz = b_vals.size();
    Kokkos::View<double*, Kokkos::HostSpace> v_vals("band_vals", b_nnz);
    Kokkos::View<axis::index_t*, Kokkos::HostSpace> v_rows("band_rows", b_nnz);
    Kokkos::View<axis::index_t*, Kokkos::HostSpace> v_cols("band_cols", b_nnz);
    for (std::size_t k = 0; k < b_nnz; ++k) {
        v_vals(k) = b_vals[k];
        v_rows(k) = b_rows[k];
        v_cols(k) = b_cols[k];
    }

    // Conservation bookkeeping arrays (frac/area) are not consulted by the
    // SpMV apply, so empty placeholders suffice for a pure apply comparison.
    Kokkos::View<double*, Kokkos::HostSpace> frac_a("frac_a", 0);
    Kokkos::View<double*, Kokkos::HostSpace> frac_b("frac_b", 0);
    Kokkos::View<double*, Kokkos::HostSpace> area_a("area_a", 0);
    Kokkos::View<double*, Kokkos::HostSpace> area_b("area_b", 0);

    band.matrix = axis::solver::InterpolationMatrix<Kokkos::HostSpace>(v_vals, v_rows, v_cols, frac_a, frac_b, area_a, area_b, src_n, band_n_dst);
    band.matrix.to_csr();
    band.built = true;
    return band;
}

// Generate a source field of exactly n doubles spanning signs and magnitudes so
// the band-vs-global comparison is non-trivial (mirrors genSourceField in
// test_shared_plan_equivalence.cpp).
rc::Gen<std::vector<double>> genSourceField(std::size_t n) {
    return rc::gen::container<std::vector<double>>(n, rc::gen::map(rc::gen::inRange(-1000000, 1000001), [](int v) { return v / 1000.0; }));
}

// Sum the rows [j0, j1) of a global per-level field laid out row-major
// [row][col] with `nx` columns and `ny` rows.
double SumGlobalRows(const std::vector<double>& global, int nx, int j0, int j1) {
    double sum = 0.0;
    for (int j = j0; j < j1; ++j) {
        for (int i = 0; i < nx; ++i) {
            sum += global[static_cast<std::size_t>(j) * nx + i];
        }
    }
    return sum;
}

}  // namespace

// ============================================================================
// Property 3 + 9 (identity plan): band regrid slice equals the corresponding
// rows of the replicated regrid, BIT-FOR-BIT, and the per-band aggregate
// matches the corresponding global rows.
//
// Feature: distributed-domain-decomposition, Property 3: Band regrid slice
// equals the corresponding rows of the replicated regrid
// Feature: distributed-domain-decomposition, Property 9: Regrid apply conserves
// its aggregate per band
// **Validates: Requirements 3.1, 3.2, 3.3, 7.1, 7.3**
//
// For a generated multi-level source field over an nx*ny identity grid and a
// generated block decomposition into `size` ranks, the REFERENCE global regrid
// (apply over [0, ny)) is compared against each rank's BAND regrid (apply over
// [band_start(r), band_start(r+1))). The identity apply is a pure copy, so
// band[r] equals global rows [band_start(r), band_start(r+1)) bit-for-bit
// (Property 3) and the sum over band[r] equals the sum over those global rows
// bit-for-bit (Property 9). Concatenating every rank's band reconstructs the
// whole global field with no gaps or overlaps.
// ============================================================================
RC_GTEST_PROP(BandRegridEqualsGlobalRows, Property3And9_IdentityPlanBitForBit, ()) {
    // Small identity grid (source grid == destination grid for passthrough).
    const int nx = *rc::gen::inRange(1, 5);
    const int ny = *rc::gen::inRange(1, 9);
    const int nlev = *rc::gen::inRange(1, 4);
    // Decompose [0, ny) into `size` ranks; allow size > ny so surplus ranks
    // (empty bands) are exercised (Req 1.4 lock-step / zero-row bands).
    const int size = *rc::gen::inRange(1, ny + 3);

    const std::size_t record_len = static_cast<std::size_t>(nx) * ny;

    // One multi-level source buffer: `nlev` records of nx*ny each, laid out
    // [level][row][col] exactly as the driver reads a multi-level slab.
    const std::vector<double> source = *genSourceField(record_len * static_cast<std::size_t>(nlev));

    // (a) REFERENCE global regrid: apply the whole-grid plan per level over
    //     [0, ny). This is the field the replicated path assembled.
    const RegridPlan global_plan = MakeIdentityPlan(nx, ny, /*j0=*/0, /*j1=*/ny);
    std::vector<std::vector<double>> global_levels(nlev);
    for (int lev = 0; lev < nlev; ++lev) {
        RC_ASSERT(apply_regrid_plan(global_plan, /*time_offset=*/static_cast<std::size_t>(lev) * record_len, /*is_float=*/false, source.data(), nx,
                                    ny, /*nx=*/nx, global_levels[lev]));
        RC_ASSERT(global_levels[lev].size() == static_cast<std::size_t>(nx) * ny);
    }

    // (b) per-rank BAND regrid, compared to the corresponding global rows.
    for (int r = 0; r < size; ++r) {
        const int j0 = band_start(ny, size, r);
        const int j1 = band_start(ny, size, r + 1);
        const int nband = j1 - j0;
        const RegridPlan band_plan = MakeIdentityPlan(nx, ny, j0, j1);

        for (int lev = 0; lev < nlev; ++lev) {
            std::vector<double> band;
            RC_ASSERT(apply_regrid_plan(band_plan, /*time_offset=*/static_cast<std::size_t>(lev) * record_len, /*is_float=*/false, source.data(), nx,
                                        ny, /*nx=*/nx, band));
            RC_ASSERT(band.size() == static_cast<std::size_t>(nx) * std::max(nband, 0));

            // Property 3: element-for-element equality with global rows
            // [j0, j1). Identity apply is a pure copy => bit-for-bit.
            for (int local_j = 0; local_j < nband; ++local_j) {
                for (int i = 0; i < nx; ++i) {
                    const std::size_t band_idx = static_cast<std::size_t>(local_j) * nx + i;
                    const std::size_t global_idx = static_cast<std::size_t>(j0 + local_j) * nx + i;
                    RC_ASSERT(band[band_idx] == global_levels[lev][global_idx]);
                }
            }

            // Property 9: the per-band conservative aggregate equals the sum of
            // the corresponding global rows, bit-for-bit for the identity apply.
            const double band_sum = std::accumulate(band.begin(), band.end(), 0.0);
            const double global_rows_sum = SumGlobalRows(global_levels[lev], nx, j0, j1);
            RC_ASSERT(band_sum == global_rows_sum);
        }
    }
}

// ============================================================================
// Property 3 + 9 (conservative matrix plan): band regrid slice equals the
// corresponding rows of the replicated regrid, BIT-FOR-BIT, and the per-band
// aggregate matches the corresponding global rows.
//
// Feature: distributed-domain-decomposition, Property 3: Band regrid slice
// equals the corresponding rows of the replicated regrid
// Feature: distributed-domain-decomposition, Property 9: Regrid apply conserves
// its aggregate per band
// **Validates: Requirements 3.1, 3.2, 3.3, 7.1, 7.3**
//
// A genuine conservative (DstArea) regrid from a coarse 2x2 source to a finer
// dst_nx * dst_ny destination. ONE whole-grid plan is the REFERENCE "replicated
// regrid"; each rank's BAND plan is the ROW-RESTRICTION of that same matrix to
// its owned destination rows [band_start(r), band_start(r+1)) (see
// RestrictPlanToBand). Because the band plan re-uses the whole-grid weights and
// source columns verbatim — exactly what the driver's per-band regrid does,
// re-using the weights the replicated path also computed — the per-row SpMV
// yields band[r] == global rows [j0, j1) BIT-FOR-BIT (Property 3), and the
// per-band aggregate equals the corresponding global-rows aggregate bit-for-bit
// (Property 9). Every global row is owned by exactly one rank, so concatenating
// all bands reconstructs the whole global field with no gaps or overlaps. The
// source field and the block decomposition are generated per iteration.
// ============================================================================
RC_GTEST_PROP(BandRegridEqualsGlobalRows, Property3And9_ConservativeMatrixBitForBit, ()) {
    constexpr int kSrcNx = 2;
    constexpr int kSrcNy = 2;
    constexpr int kDstNx = 4;
    constexpr int kDstNy = 4;

    // Fixed, well-separated source/destination coordinate axes (rectilinear),
    // matching test_endpoint_shared_plan.cpp so the conservative weights are
    // well-conditioned.
    static const std::vector<double> src_lons{-10.0, 10.0};
    static const std::vector<double> src_lats{-10.0, 10.0};
    static const std::vector<double> dst_lons{-7.5, -2.5, 2.5, 7.5};
    static const std::vector<double> dst_lats{-7.5, -2.5, 2.5, 7.5};

    // Decompose [0, kDstNy) into `size` ranks, including surplus ranks.
    const int size = *rc::gen::inRange(1, kDstNy + 3);

    // Generated source record (one level is enough to exercise the weighted
    // apply; the identity property already covers multi-level layout).
    const std::vector<double> source = *genSourceField(static_cast<std::size_t>(kSrcNx) * kSrcNy);

    // (a) REFERENCE global regrid over [0, kDstNy) — the replicated field.
    const RegridPlan global_plan = BuildWholeGridMatrixPlan(kSrcNx, kSrcNy, src_lons, src_lats, kDstNx, kDstNy, dst_lons, dst_lats);
    RC_ASSERT(global_plan.built);
    std::vector<double> global;
    RC_ASSERT(apply_regrid_plan(global_plan, /*time_offset=*/0, /*is_float=*/false, source.data(), kSrcNx, kSrcNy, /*nx=*/kDstNx, global));
    RC_ASSERT(global.size() == static_cast<std::size_t>(kDstNx) * kDstNy);

    // (b) per-rank BAND regrid (row-restriction of the SAME matrix), compared to
    //     the corresponding global rows.
    for (int r = 0; r < size; ++r) {
        const int j0 = band_start(kDstNy, size, r);
        const int j1 = band_start(kDstNy, size, r + 1);
        const int nband = j1 - j0;

        const RegridPlan band_plan = RestrictPlanToBand(global_plan, kDstNx, j0, j1);
        RC_ASSERT(band_plan.built);

        std::vector<double> band;
        RC_ASSERT(apply_regrid_plan(band_plan, /*time_offset=*/0, /*is_float=*/false, source.data(), kSrcNx, kSrcNy, /*nx=*/kDstNx, band));
        RC_ASSERT(band.size() == static_cast<std::size_t>(kDstNx) * std::max(nband, 0));

        // Property 3: element-for-element equality with global rows [j0, j1).
        // Same weights + same source columns => bit-for-bit (no tolerance).
        for (int local_j = 0; local_j < nband; ++local_j) {
            for (int i = 0; i < kDstNx; ++i) {
                const std::size_t band_idx = static_cast<std::size_t>(local_j) * kDstNx + i;
                const std::size_t global_idx = static_cast<std::size_t>(j0 + local_j) * kDstNx + i;
                RC_ASSERT(band[band_idx] == global[global_idx]);
            }
        }

        // Property 9: the per-band conservative aggregate equals the sum of the
        // corresponding global rows, bit-for-bit.
        const double band_sum = std::accumulate(band.begin(), band.end(), 0.0);
        const double global_rows_sum = SumGlobalRows(global, kDstNx, j0, j1);
        RC_ASSERT(band_sum == global_rows_sum);
    }
}

}  // namespace cece::io

// ============================================================================
// Kokkos + MPI global test environment and custom main().
//
// The matrix path uses Kokkos + AXIS (via apply_regrid_plan and the weight
// generator), so Kokkos must be initialized before any test runs and finalized
// after. This mirrors the KokkosMpiEnvironment in
// tests/test_endpoint_shared_plan.cpp / tests/test_numerical_equivalence.cpp;
// we provide our own main() so the strong definition here overrides
// gtest_main's weak one.
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
