// Standalone benchmark: time the regrid APPLY hot loop.
//
// This is a STANDALONE performance benchmark (NOT part of any spec). It
// quantifies the two performance fixes made to `cece::io::apply_regrid_plan`
// (src/driver/cece_regridder_utils.cpp) and the AXIS `apply` path, by building a
// REAL conservative regrid plan (real, non-identity weights) WITHOUT NetCDF and
// timing repeated apply calls.
//
// It constructs source and destination AXIS meshes directly from generated
// uniform lon/lat arrays via `cece::io::build_axis_mesh`, then generates the
// sparse conservative weight matrix exactly as `build_regrid_plan` does
// (Conservative1stOrder / DstArea / Ignore), converts it to CSR, and times
// `iters` calls to `apply_regrid_plan` on a deterministic source field.
//
// Single-rank: the destination band is the whole target grid (j0=0, j1=dst_ny).
//
// IMPORTANT: this harness does NOT read NetCDF and does NOT run the AMIO read
// stage. It synthesizes coordinates and the source field directly, so it
// measures ONLY the weight-apply portion (plus one untimed warm-up + weight
// build), not disk I/O.
//
// Build/run inside the cece-dev container only, e.g.:
//   ./setup.sh -c "cmake --build /work/build -j2 --target benchmark_regrid_apply"
//   ./setup.sh -c "/work/build/benchmark_regrid_apply"

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <axis/axis.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>

#include "cece/cece_regridder_utils.hpp"

namespace {

// Uniform cell-center longitudes over [0, 360) and latitudes over (-90, 90).
std::vector<double> uniform_lons(int nx) {
    std::vector<double> lons(nx);
    const double dx = 360.0 / static_cast<double>(nx);
    for (int i = 0; i < nx; ++i) {
        lons[i] = (static_cast<double>(i) + 0.5) * dx;  // cell centers in [0, 360)
    }
    return lons;
}

std::vector<double> uniform_lats(int ny) {
    std::vector<double> lats(ny);
    const double dy = 180.0 / static_cast<double>(ny);
    for (int j = 0; j < ny; ++j) {
        lats[j] = -90.0 + (static_cast<double>(j) + 0.5) * dy;  // cell centers in (-90, 90)
    }
    return lats;
}

int arg_int(int argc, char** argv, const char* flag, int fallback) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], flag) == 0) {
            return std::atoi(argv[i + 1]);
        }
    }
    return fallback;
}

}  // namespace

int main(int argc, char** argv) {
    // Defaults: modest but representative sizes that fit ~7 GB and finish fast.
    // Source ~180x90, target F90-ish (360x180). All CLI-overridable.
    int src_nx = arg_int(argc, argv, "--src-nx", 180);
    int src_ny = arg_int(argc, argv, "--src-ny", 90);
    int dst_nx = arg_int(argc, argv, "--dst-nx", 360);
    int dst_ny = arg_int(argc, argv, "--dst-ny", 180);
    int iters = arg_int(argc, argv, "--iters", 50);
    if (iters < 1) iters = 1;
    int reps = arg_int(argc, argv, "--reps", 15);
    if (reps < 1) reps = 1;

    Kokkos::initialize(argc, argv);
    {
        std::cout << "[RegridApplyBench] regrid APPLY hot-loop benchmark\n";
        std::cout << "[RegridApplyBench] src=" << src_nx << "x" << src_ny << " dst=" << dst_nx << "x" << dst_ny << " iters=" << iters
                  << " reps=" << reps << "\n"
                  << std::flush;

        // 1/2. Build source and destination meshes from generated uniform grids.
        const std::vector<double> src_lons = uniform_lons(src_nx);
        const std::vector<double> src_lats = uniform_lats(src_ny);
        const std::vector<double> dst_lons = uniform_lons(dst_nx);
        const std::vector<double> dst_lats = uniform_lats(dst_ny);

        auto src_mesh = cece::io::build_axis_mesh(src_nx, src_ny, src_lons, src_lats);
        auto dst_mesh = cece::io::build_axis_mesh(dst_nx, dst_ny, dst_lons, dst_lats);

        // Generate weights exactly as build_regrid_plan does.
        axis::solver::RegridConfig cfg;
        cfg.method = axis::solver::InterpolationMethod::Conservative1stOrder;
        cfg.norm_type = axis::solver::NormType::DstArea;
        cfg.unmapped = axis::solver::UnmappedAction::Ignore;

        cece::io::RegridPlan plan;
        std::cout << "[RegridApplyBench] generating conservative weights...\n" << std::flush;
        plan.matrix = axis::solver::WeightGenerator::generate<Kokkos::HostSpace>(src_mesh, dst_mesh, cfg);
        plan.matrix.to_csr();
        plan.file_nx = src_nx;
        plan.file_ny = src_ny;
        plan.j0 = 0;
        plan.j1 = dst_ny;  // single-rank: destination band is the whole target grid
        plan.identity = false;
        plan.built = true;

        const size_t nnz = plan.matrix.nnz();
        std::cout << "[RegridApplyBench] weights built: nnz=" << nnz << "\n" << std::flush;

        // 3. Deterministic source field buffer of size src_nx*src_ny.
        std::vector<double> src(static_cast<size_t>(src_nx) * src_ny);
        for (size_t k = 0; k < src.size(); ++k) {
            src[k] = 1.0 + 0.5 * std::sin(static_cast<double>(k) * 0.001);
        }

        std::vector<double> local_dst;

        // 4. Warm-up (untimed).
        cece::io::apply_regrid_plan(plan, /*time_offset=*/0, /*is_float=*/false, src.data(), src_nx, src_ny, dst_nx, local_dst);

        // 5. Run `reps` repetitions; each times a loop of `iters` apply calls.
        std::cout << std::fixed << std::setprecision(4);
        std::vector<double> rep_ms_per_apply;
        rep_ms_per_apply.reserve(static_cast<size_t>(reps));
        for (int rep = 0; rep < reps; ++rep) {
            auto t0 = std::chrono::steady_clock::now();
            for (int it = 0; it < iters; ++it) {
                cece::io::apply_regrid_plan(plan, /*time_offset=*/0, /*is_float=*/false, src.data(), src_nx, src_ny, dst_nx, local_dst);
            }
            auto t1 = std::chrono::steady_clock::now();

            const double rep_total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            const double ms_per_apply = rep_total_ms / static_cast<double>(iters);
            rep_ms_per_apply.push_back(ms_per_apply);
            std::cout << "REGRID_APPLY_REP: rep=" << rep << " ms_per_apply=" << ms_per_apply << "\n" << std::flush;
        }

        // Robust statistics across reps: min, median, max.
        std::vector<double> sorted = rep_ms_per_apply;
        std::sort(sorted.begin(), sorted.end());
        const double min_ms = sorted.front();
        const double max_ms = sorted.back();
        const size_t n = sorted.size();
        const double median_ms = (n % 2 == 0) ? 0.5 * (sorted[n / 2 - 1] + sorted[n / 2]) : sorted[n / 2];

        std::cout << "REGRID_APPLY_BENCH: src=" << src_nx << "x" << src_ny << " dst=" << dst_nx << "x" << dst_ny << " nnz=" << nnz
                  << " iters=" << iters << " reps=" << reps << " min=" << min_ms << " median=" << median_ms << " max=" << max_ms << "\n"
                  << std::flush;
    }
    Kokkos::finalize();
    return 0;
}
