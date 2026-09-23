// End-to-end per-step ingest/assembly benchmark, ex5-shaped, F360 target grid.
//
// This is a STANDALONE performance benchmark (not part of any spec). It measures
// the end-to-end per-step ingest/assembly speedup of the ingest-copy-consolidation
// refactor by reproducing the transpose/copy/ingest phase of
// `CeceDriverOrchestrator::AssembleReplicatedField`
// (src/driver/cece_driver_facade.cpp, ~lines 557-731) — the phase the
// consolidation actually changed — driven in an ex5-shaped per-step loop.
//
// It models the shape of examples/cece_config_ex5.yaml (2 species/variables
// MACCITY_CO + MACCITY_NO, 6 hourly steps from 00:00 to 06:00 at 3600s,
// 2D-emission fields with nlev=1) scaled up to an F360 Gaussian target grid
// (nx=1440=4*360, ny=720=2*360).
//
// IMPORTANT: this harness does NOT read NetCDF and does NOT run the DAGR/AMIO
// read stage (those inputs are unavailable in the container). It synthesizes the
// already-assembled `full_destination` directly, so it measures ONLY the
// ingest/assembly/copy portion that AssembleReplicatedField performs after the
// read/regrid stage — not disk I/O.
//
// The single-rank flow modeled (mpi_size==1, distributed_regrid=false):
//   `full_destination` is laid out [level][j][i] = level*nx*ny + j*nx + i (the
//   fixed MPI_Allgatherv gather layout). It is transposed into a LayoutLeft
//   (i, j, level) host buffer, then deep_copy'd into the core import field
//   DualView3D (import_state.fields[var]) with modify_device() + sync_host().
//
// Two variants are timed against identical inputs over the full
// nsteps x nvars loop, each using the REAL cece::DualView3D and real Kokkos
// deep_copy / modify_device / sync_host discipline (no host==device assumption):
//
//   BEFORE (pre-refactor): per (var, step) the driver did MORE work per field:
//     * transpose #1 into its own mirror -> deep_copy into stream_view (the
//       separate CeceIO consumer DualView3D);
//     * transpose #2 (identical index math) into its own mirror -> deep_copy
//       into the core import field + modify_device() + sync_host();
//     * PLUS the ingestor round-trip copy-back: SetField copies full_destination
//       into a field_cache_ DualView3D, then IngestEmissionsInline deep_copy's
//       field_cache_ back into the core import field + Kokkos::fence(). That is a
//       THIRD deep_copy of the same field.
//     => 3 deep_copies + 2 host transposes per field per step.
//
//   AFTER (consolidated): per (var, step) ONE transpose into one shared host
//     buffer -> single deep_copy into the core import field (the sole
//     authoritative write) + modify_device() + sync_host(). No stream_view
//     populate, no ingestor copy-back.
//     => 1 deep_copy + 1 host transpose per field per step.
//
// The persistent core import field DualView3D per variable is kept across steps
// (as production does — import_state.fields[var] persists), plus the
// stream_view / field_cache DualViews the BEFORE path needs.
//
// Output format (stable, machine-parseable lines prefixed EX5_BENCH:):
//   EX5_BENCH: grid=F360 nx=1440 ny=720 nlev=1 nvars=2 nsteps=6 iters=5 variant=before ms_per_step=<v>
//   EX5_BENCH: grid=F360 nx=1440 ny=720 nlev=1 nvars=2 nsteps=6 iters=5 variant=after ms_per_step=<v>
//   EX5_BENCH: grid=F360 ... speedup=<before/after>

#include <Kokkos_Core.hpp>
#include <Kokkos_DualView.hpp>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "cece/cece_compute.hpp"  // cece::DualView3D

namespace {

using cece::DualView3D;
// The ingestor's field_cache_ is a LayoutLeft device View<double***> (see
// CeceDataIngestor::SetField in src/core/cece_data_ingestor.cpp). Model it with
// the same type so the BEFORE copy-back is a like-for-like deep_copy.
using FieldCacheView = Kokkos::View<double***, Kokkos::LayoutLeft>;

// Synthesize an already-assembled full_destination laid out [level][j][i]
// (level*nx*ny + j*nx + i), exactly the layout MPI_Allgatherv produces. Values
// are deterministic and span negatives / zeros / large magnitudes so the copy
// path is exercised over a representative range. The `step` seed varies the
// data every step so the slice-cache read path (cache miss) is modeled each
// step — the expensive path production hits when new hourly data arrives.
std::vector<double> MakeFullDestination(int nx, int ny, int nlev, int var, int step) {
    const size_t spatial = static_cast<size_t>(nx) * static_cast<size_t>(ny);
    std::vector<double> full(static_cast<size_t>(nlev) * spatial, 0.0);
    const unsigned seed = static_cast<unsigned>(var * 2246822519u + step * 3266489917u);
    for (int level = 0; level < nlev; ++level) {
        double* layer = full.data() + static_cast<size_t>(level) * spatial;
        for (size_t k = 0; k < spatial; ++k) {
            const double base = static_cast<double>((k * 2654435761u + level * 40503u + seed) % 100003u);
            layer[k] = (base - 50000.0) * 1.0e-3;
        }
    }
    return full;
}

// One host transpose of full_destination ([level][j][i]) into a LayoutLeft
// (i, j, level) host mirror, exactly as AssembleReplicatedField does:
//   host(i, j, level) = full_destination[level*nx*ny + j*nx + i].
template <typename HostView>
void TransposeInto(HostView& host, const std::vector<double>& full_destination, int nx, int ny, int nlev) {
    const size_t target_spatial = static_cast<size_t>(nx) * static_cast<size_t>(ny);
    for (int level = 0; level < nlev; ++level) {
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                host(i, j, level) = full_destination[static_cast<size_t>(level) * target_spatial + static_cast<size_t>(j) * nx + i];
            }
        }
    }
}

// One (var, step) unit of BEFORE work: 3 deep_copies + 2 host transposes.
void BeforeStep(const std::vector<double>& full_destination, int nx, int ny, int nlev, DualView3D& stream, DualView3D& core,
                FieldCacheView& field_cache) {
    auto stream_view = stream.view_device();
    auto core_view = core.view_device();

    // Transpose #1 -> stream_view (the CeceIO consumer).
    auto stream_host = Kokkos::create_mirror_view(stream_view);
    TransposeInto(stream_host, full_destination, nx, ny, nlev);
    Kokkos::deep_copy(stream_view, stream_host);

    // Transpose #2 (identical index math) -> core import field.
    auto core_host = Kokkos::create_mirror_view(core_view);
    TransposeInto(core_host, full_destination, nx, ny, nlev);
    Kokkos::deep_copy(core_view, core_host);
    core.modify_device();
    core.sync_host();

    // Ingestor round-trip copy-back: SetField deep_copy's the host field into
    // field_cache_, then IngestEmissionsInline deep_copy's field_cache_ back into
    // the core import field + Kokkos::fence(). This is the THIRD deep_copy of the
    // same field. SetField's source is the same transposed host layout.
    Kokkos::deep_copy(field_cache, core_host);  // SetField -> field_cache_
    Kokkos::deep_copy(core_view, field_cache);  // IngestEmissionsInline copy-back
    Kokkos::fence();
}

// One (var, step) unit of AFTER work: 1 deep_copy + 1 host transpose.
void AfterStep(const std::vector<double>& full_destination, int nx, int ny, int nlev, DualView3D& core) {
    auto core_view = core.view_device();

    // Single transpose into one shared host buffer.
    auto host_buf = Kokkos::create_mirror_view(core_view);
    TransposeInto(host_buf, full_destination, nx, ny, nlev);

    // Sole authoritative write to the core import field. No stream_view
    // populate, no ingestor copy-back.
    Kokkos::deep_copy(core_view, host_buf);
    core.modify_device();
    core.sync_host();
}

}  // namespace

int main(int argc, char** argv) {
    // F360 defaults, ex5-shaped. All overridable on the command line.
    const char* grid_label = "F360";
    int nx = 1440;   // 4 * 360
    int ny = 720;    // 2 * 360
    int nlev = 1;    // 2D-emission fields
    int nvars = 2;   // MACCITY_CO, MACCITY_NO
    int nsteps = 6;  // hourly 00:00 -> 06:00 at 3600s
    int iters = 5;   // modest; F360 field ~8.3 MB per DualView fits ~7 GB budget

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--nx" && i + 1 < argc) {
            nx = std::atoi(argv[++i]);
        } else if (arg == "--ny" && i + 1 < argc) {
            ny = std::atoi(argv[++i]);
        } else if (arg == "--nlev" && i + 1 < argc) {
            nlev = std::atoi(argv[++i]);
        } else if (arg == "--nvars" && i + 1 < argc) {
            nvars = std::atoi(argv[++i]);
        } else if (arg == "--nsteps" && i + 1 < argc) {
            nsteps = std::atoi(argv[++i]);
        } else if (arg == "--iters" && i + 1 < argc) {
            iters = std::atoi(argv[++i]);
        }
    }
    if (nx < 1) nx = 1;
    if (ny < 1) ny = 1;
    if (nlev < 1) nlev = 1;
    if (nvars < 1) nvars = 1;
    if (nsteps < 1) nsteps = 1;
    if (iters < 1) iters = 1;

    Kokkos::initialize(argc, argv);
    {
        std::cout << "[Ex5Bench] ex5-shaped end-to-end per-step ingest/assembly benchmark\n";
        std::cout << "[Ex5Bench] reproduces AssembleReplicatedField assembly/ingest phase; no NetCDF / no DAGR-AMIO read stage\n";
        std::cout << "[Ex5Bench] grid=" << grid_label << " nx=" << nx << " ny=" << ny << " nlev=" << nlev << " nvars=" << nvars
                  << " nsteps=" << nsteps << " iters=" << iters << "\n";
        std::cout << std::flush;

        // Persistent per-variable core import fields (as production does:
        // import_state.fields[var] persists across steps). Also the stream_view
        // and per-variable field_cache DualViews/Views the BEFORE path needs.
        std::vector<DualView3D> core_fields;
        std::vector<FieldCacheView> field_caches;
        core_fields.reserve(nvars);
        field_caches.reserve(nvars);
        for (int v = 0; v < nvars; ++v) {
            core_fields.emplace_back("core_import_" + std::to_string(v), nx, ny, nlev);
            field_caches.emplace_back("field_cache_" + std::to_string(v), nx, ny, nlev);
        }
        DualView3D stream("stream_view", nx, ny, nlev);

        // Pre-synthesize inputs for every (var, step) so synthesis cost is not
        // charged to either timed variant. Distinct data per step models the
        // per-step cache-miss/read path.
        std::vector<std::vector<double>> inputs(static_cast<size_t>(nvars) * nsteps);
        for (int v = 0; v < nvars; ++v) {
            for (int s = 0; s < nsteps; ++s) {
                inputs[static_cast<size_t>(v) * nsteps + s] = MakeFullDestination(nx, ny, nlev, v, s);
            }
        }

        auto run_loop = [&](bool before) {
            for (int v = 0; v < nvars; ++v) {
                for (int s = 0; s < nsteps; ++s) {
                    const auto& full = inputs[static_cast<size_t>(v) * nsteps + s];
                    if (before) {
                        BeforeStep(full, nx, ny, nlev, stream, core_fields[v], field_caches[v]);
                    } else {
                        AfterStep(full, nx, ny, nlev, core_fields[v]);
                    }
                }
            }
            Kokkos::fence();
        };

        // Warm-up (not timed): amortize first-touch allocation of mirror views.
        run_loop(true);
        run_loop(false);

        // Time BEFORE over `iters` full nsteps x nvars loops.
        const auto b0 = std::chrono::steady_clock::now();
        for (int it = 0; it < iters; ++it) run_loop(true);
        const auto b1 = std::chrono::steady_clock::now();
        const double before_total_ms = std::chrono::duration<double, std::milli>(b1 - b0).count();

        // Time AFTER over `iters` full nsteps x nvars loops.
        const auto a0 = std::chrono::steady_clock::now();
        for (int it = 0; it < iters; ++it) run_loop(false);
        const auto a1 = std::chrono::steady_clock::now();
        const double after_total_ms = std::chrono::duration<double, std::milli>(a1 - a0).count();

        // "step" = one (var, step) field assembly. Total field-steps per loop
        // is nvars * nsteps; ms_per_step normalizes over all iters * field-steps.
        const double field_steps = static_cast<double>(iters) * nvars * nsteps;
        const double before_ms_per_step = before_total_ms / field_steps;
        const double after_ms_per_step = after_total_ms / field_steps;
        const double speedup = after_ms_per_step > 0.0 ? before_ms_per_step / after_ms_per_step : 0.0;

        std::cout << std::fixed << std::setprecision(4);
        std::cout << "[Ex5Bench] before total=" << before_total_ms << " ms, after total=" << after_total_ms << " ms\n";

        auto emit = [&](const char* variant, double ms_per_step) {
            std::cout << "EX5_BENCH: grid=" << grid_label << " nx=" << nx << " ny=" << ny << " nlev=" << nlev << " nvars=" << nvars
                      << " nsteps=" << nsteps << " iters=" << iters << " variant=" << variant << " ms_per_step=" << ms_per_step << "\n";
        };
        emit("before", before_ms_per_step);
        emit("after", after_ms_per_step);
        std::cout << "EX5_BENCH: grid=" << grid_label << " nx=" << nx << " ny=" << ny << " nlev=" << nlev << " nvars=" << nvars
                  << " nsteps=" << nsteps << " iters=" << iters << " speedup=" << speedup << "\n";
        std::cout << std::flush;
    }
    Kokkos::finalize();
    return 0;
}
