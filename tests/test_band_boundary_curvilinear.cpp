// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors
//
// Feature: distributed-domain-decomposition
// Property 11: Curvilinear band conservative regrid matches global rows at band
//              boundaries.
//
// A rank-local latitude band [j0, j1) of a CURVILINEAR destination grid must
// reproduce the corresponding rows of the global conservative regrid EXACTLY,
// including its first/last (boundary) rows. A curvilinear boundary corner depends
// on neighbour centers in BOTH longitude and latitude, so building the band mesh
// from a band-local center slice makes AXIS extrapolate the outer corners
// one-sidedly — wrong on a non-uniform / curvilinear grid and inconsistent across
// the rank seam. The production path build_band_mesh_curvilinear_with_global_corners
// derives the band's 2-D corners from the FULL global center arrays via the shared
// AXIS kernel (axis::topology::synthesize_band_corners), so the band matches the
// global rows. This test drives that REAL production helper against the global
// curvilinear regrid and asserts bit-for-bit equality, and additionally confirms
// the naive slice-based band mesh DIVERGES (so the fix is load-bearing).
//
// Validates: Requirements 9.1, 9.2, 9.4, 9.5

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

// A curvilinear (2-D) destination center field of shape ny x nx flattened
// row-major (index j*nx + i). Longitude and latitude both vary in i and j, and
// the latitude spacing is non-uniform, so a band's outer corners cannot be
// recovered from a band-local slice.
struct CurvGrid {
    int nx, ny;
    std::vector<double> lon, lat;
};

CurvGrid make_curvilinear(int nx, int ny, double phase) {
    CurvGrid g;
    g.nx = nx;
    g.ny = ny;
    g.lon.assign(static_cast<size_t>(nx) * ny, 0.0);
    g.lat.assign(static_cast<size_t>(nx) * ny, 0.0);
    for (int j = 0; j < ny; ++j) {
        // Non-uniform latitude base with a longitude-dependent warp.
        double tj = (j + 0.5) / ny;
        double lat_base = -80.0 + 160.0 * (tj + 0.05 * std::sin(phase + 3.0 * tj));
        for (int i = 0; i < nx; ++i) {
            double ti = (i + 0.5) / nx;
            double lon = -180.0 + 360.0 * ti;
            double lat = lat_base + 4.0 * std::cos(2.0 * M_PI * ti + phase);
            g.lon[static_cast<size_t>(j) * nx + i] = lon;
            g.lat[static_cast<size_t>(j) * nx + i] = lat;
        }
    }
    return g;
}

std::vector<double> src_rect_lons(int n) {
    std::vector<double> c(n);
    for (int i = 0; i < n; ++i) c[i] = -180.0 + 360.0 * (i + 0.5) / n;
    return c;
}
std::vector<double> src_rect_lats(int n) {
    std::vector<double> c(n);
    for (int j = 0; j < n; ++j) c[j] = -80.0 + 160.0 * (j + 0.5) / n;
    return c;
}

// Build a band mesh the OLD (buggy) way: a StructuredGrid from the band-local
// center slice, letting AXIS extrapolate the outer corners. Used only to confirm
// the fix is load-bearing.
axis::topology::UnstructuredMesh<Kokkos::HostSpace> naive_band_mesh(int nx, int j0, int j1, const std::vector<double>& flat_lon,
                                                                    const std::vector<double>& flat_lat) {
    std::vector<double> blon(flat_lon.begin() + static_cast<size_t>(j0) * nx, flat_lon.begin() + static_cast<size_t>(j1) * nx);
    std::vector<double> blat(flat_lat.begin() + static_cast<size_t>(j0) * nx, flat_lat.begin() + static_cast<size_t>(j1) * nx);
    return build_axis_mesh(nx, j1 - j0, blon, blat);
}

void check_curv_band_equals_global(int src_nx, int src_ny, const CurvGrid& dst, int size) {
    const int nx = dst.nx, ny = dst.ny;
    std::vector<double> src(static_cast<size_t>(src_nx) * src_ny);
    for (size_t k = 0; k < src.size(); ++k) src[k] = 1.0 + 0.37 * static_cast<double>(k) - 0.013 * static_cast<double>(k * k % 17);

    // Global reference: whole curvilinear grid built the way build_regrid_plan
    // would for a single rank (AXIS-native synthesize_corners).
    auto src_mesh_g = build_axis_mesh(src_nx, src_ny, src_rect_lons(src_nx), src_rect_lats(src_ny));
    auto dst_mesh_g = build_axis_mesh(nx, ny, dst.lon, dst.lat);
    RegridPlan gplan = MakePlanFromMeshes(src_nx, src_ny, std::move(src_mesh_g), std::move(dst_mesh_g), 0, ny);
    std::vector<double> global;
    RC_ASSERT(apply_regrid_plan(gplan, 0, false, src.data(), src_nx, src_ny, nx, global));

    for (int r = 0; r < size; ++r) {
        const int j0 = band_start(ny, size, r);
        const int j1 = band_start(ny, size, r + 1);
        const int nband = j1 - j0;
        if (nband <= 0) continue;
        std::vector<double> blon(dst.lon.begin() + static_cast<size_t>(j0) * nx, dst.lon.begin() + static_cast<size_t>(j1) * nx);
        std::vector<double> blat(dst.lat.begin() + static_cast<size_t>(j0) * nx, dst.lat.begin() + static_cast<size_t>(j1) * nx);
        auto src_mesh_b = build_axis_mesh(src_nx, src_ny, src_rect_lons(src_nx), src_rect_lats(src_ny));
        auto dst_mesh_b = build_band_mesh_curvilinear_with_global_corners(nx, j0, j1, dst.lon, dst.lat, blon, blat);
        RegridPlan bplan = MakePlanFromMeshes(src_nx, src_ny, std::move(src_mesh_b), std::move(dst_mesh_b), j0, j1);
        std::vector<double> band;
        RC_ASSERT(apply_regrid_plan(bplan, 0, false, src.data(), src_nx, src_ny, nx, band));
        for (int jr = 0; jr < nband; ++jr) {
            for (int i = 0; i < nx; ++i) {
                const double b = band[static_cast<size_t>(jr) * nx + i];
                const double g = global[static_cast<size_t>(j0 + jr) * nx + i];
                RC_ASSERT(std::fabs(b - g) <= 1e-12);
            }
        }
    }
}

}  // namespace

// Property 11 (curvilinear): band regrid == global rows, all bands, boundaries
// included.
// Feature: distributed-domain-decomposition, Property 11: Curvilinear band
// conservative regrid matches global rows at band boundaries.
// **Validates: Requirements 9.1, 9.2, 9.4, 9.5**
RC_GTEST_PROP(BandBoundaryConservation, CurvilinearBandEqualsGlobal, ()) {
    const int src_nx = *rc::gen::inRange(2, 5);
    const int src_ny = *rc::gen::inRange(3, 9);
    const int dst_nx = *rc::gen::inRange(2, 6);
    const int dst_ny = *rc::gen::inRange(4, 13);
    const int size = *rc::gen::inRange(1, dst_ny + 2);
    const double phase = *rc::gen::map(rc::gen::inRange(0, 628), [](int v) { return v / 100.0; });

    const CurvGrid dst = make_curvilinear(dst_nx, dst_ny, phase);
    check_curv_band_equals_global(src_nx, src_ny, dst, size);
}

// Fixed regression case: a curvilinear band that reproduces the seam divergence
// without the global-corner fix, and matches with it.
TEST(BandBoundaryConservation, CurvilinearFixedCase) {
    const int src_nx = 3, src_ny = 8, dst_nx = 4, dst_ny = 12;
    const CurvGrid dst = make_curvilinear(dst_nx, dst_ny, 1.1);
    std::vector<double> src(static_cast<size_t>(src_nx) * src_ny);
    for (size_t k = 0; k < src.size(); ++k) src[k] = 1.0 + 0.37 * static_cast<double>(k);

    auto sg = build_axis_mesh(src_nx, src_ny, src_rect_lons(src_nx), src_rect_lats(src_ny));
    auto dg = build_axis_mesh(dst_nx, dst_ny, dst.lon, dst.lat);
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

    const int j0 = 5, j1 = 9;
    std::vector<double> blon(dst.lon.begin() + static_cast<size_t>(j0) * dst_nx, dst.lon.begin() + static_cast<size_t>(j1) * dst_nx);
    std::vector<double> blat(dst.lat.begin() + static_cast<size_t>(j0) * dst_nx, dst.lat.begin() + static_cast<size_t>(j1) * dst_nx);

    // Fixed path: globally-consistent corners.
    auto sb = build_axis_mesh(src_nx, src_ny, src_rect_lons(src_nx), src_rect_lats(src_ny));
    auto db = build_band_mesh_curvilinear_with_global_corners(dst_nx, j0, j1, dst.lon, dst.lat, blon, blat);
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
        for (int i = 0; i < dst_nx; ++i)
            maxd = std::max(maxd, std::fabs(band[static_cast<size_t>(jr) * dst_nx + i] - global[static_cast<size_t>(j0 + jr) * dst_nx + i]));
    EXPECT_LE(maxd, 1e-12) << "curvilinear band boundary rows diverge from global regrid";

    // Naive slice-based band mesh (the pre-fix approach) must DIVERGE at a seam,
    // proving the global-corner fix is load-bearing.
    auto sn = build_axis_mesh(src_nx, src_ny, src_rect_lons(src_nx), src_rect_lats(src_ny));
    auto dn = naive_band_mesh(dst_nx, j0, j1, dst.lon, dst.lat);
    RegridPlan n;
    n.file_nx = src_nx;
    n.file_ny = src_ny;
    n.j0 = j0;
    n.j1 = j1;
    n.identity = false;
    n.matrix = axis::solver::WeightGenerator::generate<Kokkos::HostSpace>(sn, dn, cfg);
    n.matrix.to_csr();
    n.built = true;
    std::vector<double> nband;
    ASSERT_TRUE(apply_regrid_plan(n, 0, false, src.data(), src_nx, src_ny, dst_nx, nband));
    double naive_maxd = 0.0;
    for (int jr = 0; jr < (j1 - j0); ++jr)
        for (int i = 0; i < dst_nx; ++i)
            naive_maxd =
                std::max(naive_maxd, std::fabs(nband[static_cast<size_t>(jr) * dst_nx + i] - global[static_cast<size_t>(j0 + jr) * dst_nx + i]));
    EXPECT_GT(naive_maxd, 1e-9) << "expected the naive slice-based band mesh to diverge (fix is load-bearing)";
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
