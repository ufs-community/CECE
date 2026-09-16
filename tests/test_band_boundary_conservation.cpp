// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors
//
// Feature: distributed-domain-decomposition
// Property 10: Band conservative regrid matches global rows at band boundaries
//              on non-uniform latitude grids.
//
// A rank-local latitude band [j0, j1) must reproduce the corresponding rows of
// the global conservative regrid EXACTLY, including its first/last (boundary)
// rows. The risk is StructuredGrid::synthesize_corners extrapolating the band's
// outer latitude edges one-sidedly from band-local centers, which is correct on
// a uniform grid but wrong on a non-uniform one (the true edge is the midpoint
// to the neighbour row owned by an adjacent rank), changing conservative overlap
// areas at the seam. The production path build_regrid_plan pins globally-correct
// corners via build_band_mesh_with_global_corners; this test drives that REAL
// production helper against the global regrid and asserts bit-for-bit equality
// on a deliberately NON-UNIFORM latitude grid.
//
// Validates: Requirements 3.1, 3.2, 7.1

#include <gtest/gtest.h>
#include <mpi.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <axis/solver/weight_generator.hpp>
#include <cmath>
#include <vector>

#include "cece/cece_regridder_utils.hpp"

namespace cece::io {
namespace {

int band_start(int ny, int size, int r) {
    const int band_base = ny / size;
    const int band_rem = ny % size;
    return r * band_base + std::min(r, band_rem);
}

RegridPlan MakePlanFromMeshes(int src_nx, int src_ny, axis::topology::UnstructuredMesh<Kokkos::HostSpace> src_mesh,
                              axis::topology::UnstructuredMesh<Kokkos::HostSpace> dst_mesh, int j0, int j1) {
    RegridPlan plan;
    plan.file_nx = src_nx;
    plan.file_ny = src_ny;
    plan.j0 = j0;
    plan.j1 = j1;
    plan.identity = false;
    axis::solver::RegridConfig cfg;
    cfg.method = axis::solver::InterpolationMethod::Conservative1stOrder;
    cfg.norm_type = axis::solver::NormType::DstArea;
    cfg.unmapped = axis::solver::UnmappedAction::Ignore;
    plan.matrix = axis::solver::WeightGenerator::generate<Kokkos::HostSpace>(src_mesh, dst_mesh, cfg);
    plan.matrix.to_csr();
    plan.built = true;
    return plan;
}

// Non-uniform latitude centers over [-90, 90] with varying cell widths.
std::vector<double> nonuniform_lats(int n, double phase) {
    std::vector<double> e(n + 1);
    e[0] = 0.0;
    for (int j = 0; j < n; ++j) {
        double w = 1.0 + 0.8 * std::sin(phase + 3.0 * (j + 0.5) / n) + 0.5 * std::cos(1.7 * j / n);
        if (w < 0.2) w = 0.2;
        e[j + 1] = e[j] + w;
    }
    double span = e[n] - e[0];
    std::vector<double> c(n);
    for (int j = 0; j < n; ++j) {
        double lo = -90.0 + 180.0 * (e[j] - e[0]) / span;
        double hi = -90.0 + 180.0 * (e[j + 1] - e[0]) / span;
        c[j] = 0.5 * (lo + hi);
    }
    return c;
}

std::vector<double> uniform(int n, double lo, double hi) {
    std::vector<double> c(n);
    for (int i = 0; i < n; ++i) c[i] = lo + (hi - lo) * (i + 0.5) / n;
    return c;
}

void check_band_equals_global(int src_nx, int src_ny, int dst_nx, int dst_ny, const std::vector<double>& src_lons,
                              const std::vector<double>& src_lats, const std::vector<double>& dst_lons, const std::vector<double>& dst_lats,
                              int size) {
    std::vector<double> src(static_cast<size_t>(src_nx) * src_ny);
    for (size_t k = 0; k < src.size(); ++k) src[k] = 1.0 + 0.37 * static_cast<double>(k) - 0.013 * static_cast<double>(k * k % 17);

    // Global reference: whole-grid band with globally-correct corners == whole grid.
    auto src_mesh_g = build_axis_mesh(src_nx, src_ny, src_lons, src_lats);
    auto dst_mesh_g = build_band_mesh_with_global_corners(dst_nx, 0, dst_ny, dst_lons, dst_lats);
    RegridPlan gplan = MakePlanFromMeshes(src_nx, src_ny, std::move(src_mesh_g), std::move(dst_mesh_g), 0, dst_ny);
    std::vector<double> global;
    RC_ASSERT(apply_regrid_plan(gplan, 0, false, src.data(), src_nx, src_ny, dst_nx, global));

    for (int r = 0; r < size; ++r) {
        const int j0 = band_start(dst_ny, size, r);
        const int j1 = band_start(dst_ny, size, r + 1);
        const int nband = j1 - j0;
        if (nband <= 0) continue;
        auto src_mesh_b = build_axis_mesh(src_nx, src_ny, src_lons, src_lats);
        auto dst_mesh_b = build_band_mesh_with_global_corners(dst_nx, j0, j1, dst_lons, dst_lats);
        RegridPlan bplan = MakePlanFromMeshes(src_nx, src_ny, std::move(src_mesh_b), std::move(dst_mesh_b), j0, j1);
        std::vector<double> band;
        RC_ASSERT(apply_regrid_plan(bplan, 0, false, src.data(), src_nx, src_ny, dst_nx, band));
        for (int jr = 0; jr < nband; ++jr) {
            for (int i = 0; i < dst_nx; ++i) {
                const double b = band[static_cast<size_t>(jr) * dst_nx + i];
                const double g = global[static_cast<size_t>(j0 + jr) * dst_nx + i];
                RC_ASSERT(std::fabs(b - g) <= 1e-12);
            }
        }
    }
}

}  // namespace

// Property 10 (non-uniform): band regrid == global rows, all bands, boundaries included.
// Feature: distributed-domain-decomposition, Property 10: Band conservative regrid
// matches global rows at band boundaries on non-uniform latitude grids.
// **Validates: Requirements 3.1, 3.2, 7.1**
RC_GTEST_PROP(BandBoundaryConservation, NonUniformBandEqualsGlobal, ()) {
    const int src_nx = *rc::gen::inRange(2, 5);
    const int src_ny = *rc::gen::inRange(3, 9);
    const int dst_nx = *rc::gen::inRange(2, 6);
    const int dst_ny = *rc::gen::inRange(4, 13);
    const int size = *rc::gen::inRange(1, dst_ny + 2);
    const double phase = *rc::gen::map(rc::gen::inRange(0, 628), [](int v) { return v / 100.0; });

    const std::vector<double> src_lons = uniform(src_nx, -180.0, 180.0);
    const std::vector<double> dst_lons = uniform(dst_nx, -180.0, 180.0);
    const std::vector<double> src_lats = nonuniform_lats(src_ny, phase);
    const std::vector<double> dst_lats = nonuniform_lats(dst_ny, phase + 0.5);

    check_band_equals_global(src_nx, src_ny, dst_nx, dst_ny, src_lons, src_lats, dst_lons, dst_lats, size);
}

// Fixed regression case that reproduced the original ~1% boundary error.
TEST(BandBoundaryConservation, NonUniformFixedCase) {
    const int src_nx = 2, src_ny = 10, dst_nx = 4, dst_ny = 20;
    const std::vector<double> src_lons = uniform(src_nx, -180.0, 180.0);
    const std::vector<double> dst_lons = uniform(dst_nx, -180.0, 180.0);
    const std::vector<double> src_lats = nonuniform_lats(src_ny, 0.0);
    const std::vector<double> dst_lats = nonuniform_lats(dst_ny, 0.5);
    std::vector<double> src(static_cast<size_t>(src_nx) * src_ny);
    for (size_t k = 0; k < src.size(); ++k) src[k] = 1.0 + 0.37 * static_cast<double>(k);

    auto sg = build_axis_mesh(src_nx, src_ny, src_lons, src_lats);
    auto dg = build_band_mesh_with_global_corners(dst_nx, 0, dst_ny, dst_lons, dst_lats);
    RegridPlan g;
    g.file_nx = src_nx;
    g.file_ny = src_ny;
    g.j0 = 0;
    g.j1 = dst_ny;
    g.identity = false;
    axis::solver::RegridConfig cfg;
    cfg.method = axis::solver::InterpolationMethod::Conservative1stOrder;
    cfg.norm_type = axis::solver::NormType::DstArea;
    cfg.unmapped = axis::solver::UnmappedAction::Ignore;
    g.matrix = axis::solver::WeightGenerator::generate<Kokkos::HostSpace>(sg, dg, cfg);
    g.matrix.to_csr();
    g.built = true;
    std::vector<double> global;
    ASSERT_TRUE(apply_regrid_plan(g, 0, false, src.data(), src_nx, src_ny, dst_nx, global));

    const int j0 = 6, j1 = 14;
    auto sb = build_axis_mesh(src_nx, src_ny, src_lons, src_lats);
    auto db = build_band_mesh_with_global_corners(dst_nx, j0, j1, dst_lons, dst_lats);
    RegridPlan b;
    b.file_nx = src_nx;
    b.file_ny = src_ny;
    b.j0 = j0;
    b.j1 = j1;
    b.identity = false;
    b.matrix = axis::solver::WeightGenerator::generate<Kokkos::HostSpace>(sb, db, cfg);
    b.matrix.to_csr();
    b.built = true;
    std::vector<double> band;
    ASSERT_TRUE(apply_regrid_plan(b, 0, false, src.data(), src_nx, src_ny, dst_nx, band));

    double maxd = 0.0;
    for (int jr = 0; jr < (j1 - j0); ++jr)
        for (int i = 0; i < dst_nx; ++i) maxd = std::max(maxd, std::fabs(band[(size_t)jr * dst_nx + i] - global[(size_t)(j0 + jr) * dst_nx + i]));
    EXPECT_LE(maxd, 1e-12) << "band boundary rows diverge from global regrid on non-uniform grid";
}

}  // namespace cece::io

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    Kokkos::initialize(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    int rc = RUN_ALL_TESTS();
    Kokkos::finalize();
    MPI_Finalize();
    return rc;
}
