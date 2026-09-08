// Standalone benchmark: OVERALL per-step regrid+assemble+ingest speedup of the
// feature/optimize branch vs the upstream/develop baseline.
//
// This is a STANDALONE performance benchmark (NOT part of any spec). It
// reproduces BOTH branches' per-step "regrid-apply -> transpose -> ingest"
// pipeline for one field, FAITHFULLY, in a SINGLE binary built on the current
// branch, and times both against IDENTICAL synthetic inputs so the two code
// paths can be compared apples-to-apples.
//
// The DEVELOP path is an in-harness REPRODUCTION of what develop's code did
// (NOT a git checkout): the apply reproduces develop's axis::solver::apply CSR
// path, which rebuilt the KokkosSparse::CrsMatrix on EVERY call (three View
// allocations + three O(nnz) deep_copies + a fresh graph + CrsMatrix), and the
// assemble/ingest reproduces develop's two-transpose + stream_view populate +
// ingestor round-trip copy-back. The OPTIMIZE path calls the REAL, current
// cece::io::apply_regrid_plan (cached CrsMatrix, no per-call rebuild) and the
// real single-transpose / single-deep_copy assemble path (no stream_view
// populate, no ingestor copy-back).
//
// What is measured (per field, per step):
//   apply    : regrid-apply of one source field -> local_dst
//   assemble : transpose gathered [level][j][i] -> (i,j,level) + ingest
//   total    : apply + assemble
// Plan build (weight generation, once per run) and NetCDF/AMIO read are
// EXCLUDED from all timed loops, exactly as they are once-per-run on BOTH
// branches. Single-rank, host OpenMP, nlev = 1.
//
// Build/run inside the cece-dev container only, e.g.:
//   ./setup.sh -c "cmake --build /work/build -j2 --target benchmark_pipeline_develop_vs_optimize"
//   ./setup.sh -c "/work/build/benchmark_pipeline_develop_vs_optimize"

#include <Kokkos_Core.hpp>
#include <axis/axis.hpp>

// develop's axis::solver::apply CSR path rebuilt a KokkosSparse::CrsMatrix per
// call; reproduce that here. KokkosKernels is now a hard AXIS dependency (found
// or fetched by AXIS's CMake), so KokkosSparse is always available through the
// cece->axis link.
#include <KokkosSparse_CrsMatrix.hpp>
#include <KokkosSparse_spmv.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "cece/cece_compute.hpp"
#include "cece/cece_regridder_utils.hpp"

namespace {

using DualView3D = cece::DualView3D;  // Kokkos::DualView<double***, LayoutLeft>
using DeviceView3D = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>;
using HostField3D = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace>;
// Models CeceDataIngestor::field_cache_ entries (device LayoutLeft View).
using FieldCacheView = Kokkos::View<double***, Kokkos::LayoutLeft>;

std::vector<double> uniform_lons(int nx) {
    std::vector<double> lons(nx);
    const double dx = 360.0 / static_cast<double>(nx);
    for (int i = 0; i < nx; ++i) {
        lons[i] = (static_cast<double>(i) + 0.5) * dx;
    }
    return lons;
}

std::vector<double> uniform_lats(int ny) {
    std::vector<double> lats(ny);
    const double dy = 180.0 / static_cast<double>(ny);
    for (int j = 0; j < ny; ++j) {
        lats[j] = -90.0 + (static_cast<double>(j) + 0.5) * dy;
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

double median_of(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    if (n == 0) return 0.0;
    return (n % 2 == 0) ? 0.5 * (v[n / 2 - 1] + v[n / 2]) : v[n / 2];
}

// ─────────────────────────────────────────────────────────────────────────────
// DEVELOP apply: reproduce develop's apply_regrid_plan + develop's axis apply.
// develop's single-arg apply CSR path rebuilt the KokkosSparse::CrsMatrix on
// every call: allocate row_map/entries/vals Views, deep_copy the cached CSR
// arrays into them (O(nnz)), build a graph_t + fresh CrsMatrix, then spmv.
// ─────────────────────────────────────────────────────────────────────────────
void DEVELOP_apply(const cece::io::RegridPlan& plan, const std::vector<double>& src, int file_nx, int file_ny, int nx,
                   std::vector<double>& local_dst) {
    const int nband = plan.j1 - plan.j0;
    local_dst.assign(static_cast<size_t>(nx) * std::max(nband, 0), 0.0);
    if (nband <= 0) return;

    // develop's apply_regrid_plan allocated a src_field View and filled it
    // element-by-element in the double path (widening loop), then a dst_field.
    const size_t src_len = static_cast<size_t>(file_nx) * file_ny;
    Kokkos::View<double*, Kokkos::HostSpace> src_field("src_field", src_len);
    for (int j = 0; j < file_ny; ++j) {
        for (int i = 0; i < file_nx; ++i) {
            const size_t idx = static_cast<size_t>(j) * file_nx + i;
            src_field(idx) = src[idx];
        }
    }
    const size_t dst_len = static_cast<size_t>(nx) * nband;
    Kokkos::View<double*, Kokkos::HostSpace> dst_field("dst_field", dst_len);

    // ── develop's axis::solver::apply CSR path (per-call CrsMatrix rebuild) ──
    using MemorySpace = Kokkos::HostSpace;
    using index_t = axis::index_t;
    using exec_space = MemorySpace::execution_space;
    using device_t = Kokkos::Device<exec_space, MemorySpace>;
    using crs_matrix_t = KokkosSparse::CrsMatrix<double, index_t, device_t, void, index_t>;
    using graph_t = typename crs_matrix_t::staticcrsgraph_type;

    const auto row_ptr = plan.matrix.row_ptr();
    const auto col_idx = plan.matrix.col_idx();
    const auto csr_vals = plan.matrix.csr_values();

    Kokkos::View<index_t*, MemorySpace> row_map_nc("row_map", row_ptr.extent(0));
    Kokkos::deep_copy(row_map_nc, row_ptr);
    Kokkos::View<index_t*, MemorySpace> entries_nc("entries", col_idx.extent(0));
    Kokkos::deep_copy(entries_nc, col_idx);

    graph_t graph(entries_nc, row_map_nc);

    Kokkos::View<double*, MemorySpace> vals_nc("vals", csr_vals.extent(0));
    Kokkos::deep_copy(vals_nc, csr_vals);

    crs_matrix_t A("develop_spmv", static_cast<index_t>(plan.matrix.n_src()), vals_nc, graph);

    Kokkos::View<const double*, MemorySpace, Kokkos::MemoryTraits<Kokkos::Unmanaged>> x_view(src_field.data(), src_field.extent(0));
    Kokkos::View<double*, MemorySpace, Kokkos::MemoryTraits<Kokkos::Unmanaged>> y_view(dst_field.data(), dst_field.extent(0));
    KokkosSparse::spmv("N", 1.0, A, x_view, 0.0, y_view);
    Kokkos::fence("develop::apply::complete");

    // develop copied dst_field back into local_dst element-by-element.
    for (size_t k = 0; k < dst_len; ++k) {
        local_dst[k] = dst_field(k);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// OPTIMIZE apply: the REAL current cece::io::apply_regrid_plan (cached CSR).
// ─────────────────────────────────────────────────────────────────────────────
void OPTIMIZE_apply(const cece::io::RegridPlan& plan, const std::vector<double>& src, int file_nx, int file_ny, int nx,
                    std::vector<double>& local_dst) {
    cece::io::apply_regrid_plan(plan, /*time_offset=*/0, /*is_float=*/false, src.data(), file_nx, file_ny, nx, local_dst);
}

// Single-rank gather exactly as AssembleReplicatedField does: std::copy the
// local_dst band into full_destination[level*nx*ny + j0*nx ...]. nlev=1.
void gather_full_destination(const std::vector<double>& local_dst, const cece::io::RegridPlan& plan, int nx, int ny,
                             std::vector<double>& full_destination) {
    const size_t target_spatial = static_cast<size_t>(nx) * ny;
    full_destination.assign(target_spatial, 0.0);  // field_nlev = 1
    std::copy(local_dst.begin(), local_dst.end(), full_destination.begin() + static_cast<size_t>(plan.j0) * nx);
}

// ─────────────────────────────────────────────────────────────────────────────
// DEVELOP assemble/ingest: two transposes (stream mirror + core mirror) each
// with its own deep_copy into a DualView3D, modify_device + sync_host on core,
// THEN the ingestor round-trip: deep_copy core host buffer into a field_cache
// LayoutLeft device View (models SetField), then deep_copy field_cache back
// into the core device view (models IngestEmissionsInline copy-back) + fence.
// ─────────────────────────────────────────────────────────────────────────────
void DEVELOP_assemble_ingest(const std::vector<double>& full_destination, int nx, int ny, int nlev, DualView3D& core, DualView3D& stream,
                             FieldCacheView& field_cache) {
    const size_t target_spatial = static_cast<size_t>(nx) * ny;

    // Transpose #1: into the stream_view host mirror, then deep_copy into the
    // stream_view DualView3D.
    auto stream_host = stream.view_host();
    for (int level = 0; level < nlev; ++level) {
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                stream_host(i, j, level) = full_destination[static_cast<size_t>(level) * target_spatial + static_cast<size_t>(j) * nx + i];
            }
        }
    }
    Kokkos::deep_copy(stream.view_device(), stream_host);

    // Transpose #2: into the core host mirror (identical index math), then
    // deep_copy into the core DualView3D + modify_device + sync_host.
    auto core_host = core.view_host();
    for (int level = 0; level < nlev; ++level) {
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                core_host(i, j, level) = full_destination[static_cast<size_t>(level) * target_spatial + static_cast<size_t>(j) * nx + i];
            }
        }
    }
    Kokkos::deep_copy(core.view_device(), core_host);
    core.modify_device();
    core.sync_host();

    // Ingestor round-trip: SetField deep_copies the core host buffer into the
    // field_cache device View; IngestEmissionsInline copy-back deep_copies the
    // field_cache back into the core device view.
    Kokkos::deep_copy(field_cache, core.view_host());
    Kokkos::deep_copy(core.view_device(), field_cache);
    Kokkos::fence("develop::ingest::complete");
}

// ─────────────────────────────────────────────────────────────────────────────
// OPTIMIZE assemble/ingest: ONE transpose into a single host buffer, one
// deep_copy into the core import DualView3D + modify_device + sync_host. No
// stream_view populate, no ingestor copy-back.
// ─────────────────────────────────────────────────────────────────────────────
void OPTIMIZE_assemble_ingest(const std::vector<double>& full_destination, int nx, int ny, int nlev, DualView3D& core) {
    const size_t target_spatial = static_cast<size_t>(nx) * ny;
    HostField3D transposed_host("assembled_field_host", nx, ny, nlev);
    for (int level = 0; level < nlev; ++level) {
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                transposed_host(i, j, level) = full_destination[static_cast<size_t>(level) * target_spatial + static_cast<size_t>(j) * nx + i];
            }
        }
    }
    Kokkos::deep_copy(core.view_device(), transposed_host);
    core.modify_device();
    core.sync_host();
}

}  // namespace

int main(int argc, char** argv) {
    int src_nx = arg_int(argc, argv, "--src-nx", 360);
    int src_ny = arg_int(argc, argv, "--src-ny", 180);
    int dst_nx = arg_int(argc, argv, "--dst-nx", 1440);
    int dst_ny = arg_int(argc, argv, "--dst-ny", 720);
    int nvars = arg_int(argc, argv, "--nvars", 2);
    if (nvars < 1) nvars = 1;
    int nsteps = arg_int(argc, argv, "--nsteps", 6);
    if (nsteps < 1) nsteps = 1;
    int iters = arg_int(argc, argv, "--iters", 10);
    if (iters < 1) iters = 1;
    int reps = arg_int(argc, argv, "--reps", 15);
    if (reps < 1) reps = 1;
    const int nlev = 1;

    Kokkos::initialize(argc, argv);
    {
        std::cout << "[PipelineBench] develop-vs-optimize per-step regrid+assemble+ingest\n";
        std::cout << "[PipelineBench] src=" << src_nx << "x" << src_ny << " dst=" << dst_nx << "x" << dst_ny << " nvars=" << nvars
                  << " nsteps=" << nsteps << " iters=" << iters << " reps=" << reps << " nlev=" << nlev << "\n"
                  << std::flush;

        // ── Plan build (once per run, EXCLUDED from timing on both branches) ──
        const std::vector<double> src_lons = uniform_lons(src_nx);
        const std::vector<double> src_lats = uniform_lats(src_ny);
        const std::vector<double> dst_lons = uniform_lons(dst_nx);
        const std::vector<double> dst_lats = uniform_lats(dst_ny);

        auto src_mesh = cece::io::build_axis_mesh(src_nx, src_ny, src_lons, src_lats);
        auto dst_mesh = cece::io::build_axis_mesh(dst_nx, dst_ny, dst_lons, dst_lats);

        axis::solver::RegridConfig cfg;
        cfg.method = axis::solver::InterpolationMethod::Conservative1stOrder;
        cfg.norm_type = axis::solver::NormType::DstArea;
        cfg.unmapped = axis::solver::UnmappedAction::Ignore;

        cece::io::RegridPlan plan;
        std::cout << "[PipelineBench] generating conservative weights...\n" << std::flush;
        plan.matrix = axis::solver::WeightGenerator::generate<Kokkos::HostSpace>(src_mesh, dst_mesh, cfg);
        plan.matrix.to_csr();
        plan.file_nx = src_nx;
        plan.file_ny = src_ny;
        plan.j0 = 0;
        plan.j1 = dst_ny;  // single-rank: destination band is the whole target grid
        plan.identity = false;
        plan.built = true;

        const size_t nnz = plan.matrix.nnz();
        std::cout << "[PipelineBench] weights built: nnz=" << nnz << "\n" << std::flush;

        // ── Identical synthetic source field (one per field, reused) ──
        std::vector<double> src(static_cast<size_t>(src_nx) * src_ny);
        for (size_t k = 0; k < src.size(); ++k) {
            src[k] = 1.0 + 0.5 * std::sin(static_cast<double>(k) * 0.001);
        }

        const int nfields = nvars * nsteps;

        // Persistent per-var core DualViews (both variants); per-var stream +
        // field_cache only for develop.
        std::vector<DualView3D> core_opt;
        std::vector<DualView3D> core_dev;
        std::vector<DualView3D> stream_dev;
        std::vector<FieldCacheView> cache_dev;
        core_opt.reserve(nvars);
        core_dev.reserve(nvars);
        stream_dev.reserve(nvars);
        cache_dev.reserve(nvars);
        for (int v = 0; v < nvars; ++v) {
            core_opt.emplace_back("core_opt_" + std::to_string(v), dst_nx, dst_ny, nlev);
            core_dev.emplace_back("core_dev_" + std::to_string(v), dst_nx, dst_ny, nlev);
            stream_dev.emplace_back("stream_dev_" + std::to_string(v), dst_nx, dst_ny, nlev);
            cache_dev.emplace_back(FieldCacheView("cache_dev_" + std::to_string(v), dst_nx, dst_ny, nlev));
        }

        std::vector<double> local_dst;
        std::vector<double> full_destination;

        std::cout << std::fixed << std::setprecision(4);

        // ── Warm-up (untimed), one per variant ──
        for (int v = 0; v < nvars; ++v) {
            DEVELOP_apply(plan, src, src_nx, src_ny, dst_nx, local_dst);
            gather_full_destination(local_dst, plan, dst_nx, dst_ny, full_destination);
            DEVELOP_assemble_ingest(full_destination, dst_nx, dst_ny, nlev, core_dev[v], stream_dev[v], cache_dev[v]);

            OPTIMIZE_apply(plan, src, src_nx, src_ny, dst_nx, local_dst);
            gather_full_destination(local_dst, plan, dst_nx, dst_ny, full_destination);
            OPTIMIZE_assemble_ingest(full_destination, dst_nx, dst_ny, nlev, core_opt[v]);
        }

        // ── Timing: per variant, three separate timed loops (apply-only,
        // assemble-only, total) so we can attribute where time goes. ──
        std::vector<double> dev_apply, dev_asm, dev_total;
        std::vector<double> opt_apply, opt_asm, opt_total;
        dev_apply.reserve(reps);
        dev_asm.reserve(reps);
        dev_total.reserve(reps);
        opt_apply.reserve(reps);
        opt_asm.reserve(reps);
        opt_total.reserve(reps);

        const double denom = static_cast<double>(iters) * static_cast<double>(nfields);

        for (int rep = 0; rep < reps; ++rep) {
            // ---- DEVELOP: apply-only ----
            {
                auto t0 = std::chrono::steady_clock::now();
                for (int it = 0; it < iters; ++it) {
                    for (int v = 0; v < nvars; ++v) {
                        for (int s = 0; s < nsteps; ++s) {
                            DEVELOP_apply(plan, src, src_nx, src_ny, dst_nx, local_dst);
                        }
                    }
                }
                auto t1 = std::chrono::steady_clock::now();
                dev_apply.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count() / denom);
            }
            // ---- DEVELOP: assemble-only (feed the same gathered buffer) ----
            {
                DEVELOP_apply(plan, src, src_nx, src_ny, dst_nx, local_dst);
                gather_full_destination(local_dst, plan, dst_nx, dst_ny, full_destination);
                auto t0 = std::chrono::steady_clock::now();
                for (int it = 0; it < iters; ++it) {
                    for (int v = 0; v < nvars; ++v) {
                        for (int s = 0; s < nsteps; ++s) {
                            DEVELOP_assemble_ingest(full_destination, dst_nx, dst_ny, nlev, core_dev[v], stream_dev[v], cache_dev[v]);
                        }
                    }
                }
                auto t1 = std::chrono::steady_clock::now();
                dev_asm.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count() / denom);
            }
            // ---- DEVELOP: total (full pipeline per field) ----
            {
                auto t0 = std::chrono::steady_clock::now();
                for (int it = 0; it < iters; ++it) {
                    for (int v = 0; v < nvars; ++v) {
                        for (int s = 0; s < nsteps; ++s) {
                            DEVELOP_apply(plan, src, src_nx, src_ny, dst_nx, local_dst);
                            gather_full_destination(local_dst, plan, dst_nx, dst_ny, full_destination);
                            DEVELOP_assemble_ingest(full_destination, dst_nx, dst_ny, nlev, core_dev[v], stream_dev[v], cache_dev[v]);
                        }
                    }
                }
                auto t1 = std::chrono::steady_clock::now();
                dev_total.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count() / denom);
            }

            // ---- OPTIMIZE: apply-only ----
            {
                auto t0 = std::chrono::steady_clock::now();
                for (int it = 0; it < iters; ++it) {
                    for (int v = 0; v < nvars; ++v) {
                        for (int s = 0; s < nsteps; ++s) {
                            OPTIMIZE_apply(plan, src, src_nx, src_ny, dst_nx, local_dst);
                        }
                    }
                }
                auto t1 = std::chrono::steady_clock::now();
                opt_apply.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count() / denom);
            }
            // ---- OPTIMIZE: assemble-only ----
            {
                OPTIMIZE_apply(plan, src, src_nx, src_ny, dst_nx, local_dst);
                gather_full_destination(local_dst, plan, dst_nx, dst_ny, full_destination);
                auto t0 = std::chrono::steady_clock::now();
                for (int it = 0; it < iters; ++it) {
                    for (int v = 0; v < nvars; ++v) {
                        for (int s = 0; s < nsteps; ++s) {
                            OPTIMIZE_assemble_ingest(full_destination, dst_nx, dst_ny, nlev, core_opt[v]);
                        }
                    }
                }
                auto t1 = std::chrono::steady_clock::now();
                opt_asm.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count() / denom);
            }
            // ---- OPTIMIZE: total ----
            {
                auto t0 = std::chrono::steady_clock::now();
                for (int it = 0; it < iters; ++it) {
                    for (int v = 0; v < nvars; ++v) {
                        for (int s = 0; s < nsteps; ++s) {
                            OPTIMIZE_apply(plan, src, src_nx, src_ny, dst_nx, local_dst);
                            gather_full_destination(local_dst, plan, dst_nx, dst_ny, full_destination);
                            OPTIMIZE_assemble_ingest(full_destination, dst_nx, dst_ny, nlev, core_opt[v]);
                        }
                    }
                }
                auto t1 = std::chrono::steady_clock::now();
                opt_total.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count() / denom);
            }

            std::cout << "PIPE_BENCH_REP: rep=" << rep << " dev_total=" << median_of({dev_total.back()})
                      << " opt_total=" << median_of({opt_total.back()}) << "\n"
                      << std::flush;
        }

        auto emit_phase = [](const char* variant, const char* phase, const std::vector<double>& v, const std::string& extra) {
            std::vector<double> s = v;
            std::sort(s.begin(), s.end());
            std::cout << "PIPE_BENCH: variant=" << variant << "  phase=" << phase << "  " << extra << "min=" << s.front()
                      << " median=" << median_of(v) << " max=" << s.back() << "\n"
                      << std::flush;
        };

        emit_phase("develop", "apply", dev_apply, "nnz=" + std::to_string(nnz) + " ");
        emit_phase("develop", "assemble", dev_asm, "");
        emit_phase("develop", "total", dev_total, "");
        emit_phase("optimize", "apply", opt_apply, "nnz=" + std::to_string(nnz) + " ");
        emit_phase("optimize", "assemble", opt_asm, "");
        emit_phase("optimize", "total", opt_total, "");

        const double dev_total_med = median_of(dev_total);
        const double opt_total_med = median_of(opt_total);
        const double dev_apply_med = median_of(dev_apply);
        const double opt_apply_med = median_of(opt_apply);
        const double dev_asm_med = median_of(dev_asm);
        const double opt_asm_med = median_of(opt_asm);

        const double speedup_total = opt_total_med > 0.0 ? dev_total_med / opt_total_med : 0.0;
        const double speedup_apply = opt_apply_med > 0.0 ? dev_apply_med / opt_apply_med : 0.0;
        const double speedup_asm = opt_asm_med > 0.0 ? dev_asm_med / opt_asm_med : 0.0;

        std::cout << "PIPE_BENCH: speedup_total_median=" << speedup_total << "\n" << std::flush;
        std::cout << "PIPE_BENCH: speedup_apply_median=" << speedup_apply << " speedup_assemble_median=" << speedup_asm << "\n" << std::flush;
    }
    Kokkos::finalize();
    return 0;
}
