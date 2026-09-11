// Before/after ingest/assembly micro-benchmark harness
// (ingest-copy-consolidation, Task 1.1; Requirements 6.1, 6.5)
//
// This harness isolates the transpose/copy phase of AssembleReplicatedField
// (src/driver/cece_driver_facade.cpp, ~lines 688-720) — the phase this spec
// consolidates — and measures its per-step wall time in isolation, exactly as
// the ad-hoc isolated-phase micro-benchmark used during the investigation did.
//
// The measured phase takes an already-assembled `full_destination` laid out
// [level][j][i] (level*nx*ny + j*nx + i, the fixed MPI_Allgatherv gather layout,
// which this spec MUST NOT modify) and populates two live consumers, both
// LayoutLeft DualView3D (i, j, level):
//   * the CeceIO stream_view, and
//   * the core import field (import_state.fields[var]).
//
// Two variants are timed against the *same* inputs so before/after numbers sit
// side by side in a stable, comparable format:
//   * BEFORE (baseline): two independent transpose loops, each into its own
//     create_mirror_view + deep_copy (mirrors steps 2 and 3 verbatim).
//   * AFTER (consolidated): one transpose into a single shared host buffer,
//     deep_copy'd into both consumers (the Tier 1 target of this spec).
//
// Nothing upstream of `full_destination` (regrid, band decomposition,
// MPI_Allgatherv) is touched: the harness synthesizes `full_destination`
// directly so the assembly/copy phase is measured in isolation, single-rank.
//
// Grid sizes (2D-emission fields, nlev=1 by default to respect the ~7 GB
// container RAM limit):
//   * 1 degree   : 360  x 180
//   * F720       : 2880 x 1440
//   * 0.1 degree : 3600 x 1800
//
// Output format (one block per grid, machine-parseable lines prefixed
// INGEST_BENCH:) so the closing benchmark (Task 11.2) can record after-numbers
// next to the recorded baseline:
//
//   INGEST_BENCH: grid=<label> nx=<nx> ny=<ny> nlev=<nlev> nvars=<n> variant=before ms_per_step=<v>
//   INGEST_BENCH: grid=<label> nx=<nx> ny=<ny> nlev=<nlev> nvars=<n> variant=after  ms_per_step=<v>

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

struct GridCase {
    std::string label;
    int nx;
    int ny;
};

// Synthesize an already-assembled full_destination laid out [level][j][i]
// (level*nx*ny + j*nx + i), exactly the layout MPI_Allgatherv produces. Values
// are deterministic and span negatives / zeros / large magnitudes so the copy
// path is exercised over a representative range.
std::vector<double> MakeFullDestination(int nx, int ny, int nlev) {
    const size_t spatial = static_cast<size_t>(nx) * static_cast<size_t>(ny);
    std::vector<double> full(static_cast<size_t>(nlev) * spatial, 0.0);
    for (int level = 0; level < nlev; ++level) {
        double* layer = full.data() + static_cast<size_t>(level) * spatial;
        for (size_t k = 0; k < spatial; ++k) {
            // Cheap, deterministic, wide-magnitude filler.
            const double base = static_cast<double>((k * 2654435761u + level * 40503u) % 100003u);
            layer[k] = (base - 50000.0) * 1.0e-3;
        }
    }
    return full;
}

// BEFORE (baseline): two independent transpose loops, each into its own
// create_mirror_view + deep_copy. Mirrors AssembleReplicatedField steps 2 + 3.
double TimeVariantBefore(const std::vector<double>& full_destination, int nx, int ny, int nlev, DualView3D& stream, DualView3D& core, int iters) {
    const size_t target_spatial = static_cast<size_t>(nx) * static_cast<size_t>(ny);
    auto stream_view = stream.view_device();
    auto core_view = core.view_device();

    // Warm-up (not timed): amortize first-touch/allocation of mirror views.
    {
        auto stream_host = Kokkos::create_mirror_view(stream_view);
        auto core_host = Kokkos::create_mirror_view(core_view);
        (void)stream_host;
        (void)core_host;
    }

    const auto t0 = std::chrono::steady_clock::now();
    for (int it = 0; it < iters; ++it) {
        // Transpose #1 -> stream_view
        auto stream_host = Kokkos::create_mirror_view(stream_view);
        for (int level = 0; level < nlev; ++level) {
            for (int j = 0; j < ny; ++j) {
                for (int i = 0; i < nx; ++i) {
                    stream_host(i, j, level) = full_destination[static_cast<size_t>(level) * target_spatial + static_cast<size_t>(j) * nx + i];
                }
            }
        }
        Kokkos::deep_copy(stream_view, stream_host);

        // Transpose #2 -> core import field (authoritative write)
        auto core_host = Kokkos::create_mirror_view(core_view);
        for (int level = 0; level < nlev; ++level) {
            for (int j = 0; j < ny; ++j) {
                for (int i = 0; i < nx; ++i) {
                    core_host(i, j, level) = full_destination[static_cast<size_t>(level) * target_spatial + static_cast<size_t>(j) * nx + i];
                }
            }
        }
        Kokkos::deep_copy(core_view, core_host);
        core.modify_device();
        core.sync_host();
    }
    Kokkos::fence();
    const auto t1 = std::chrono::steady_clock::now();
    const double total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return total_ms / static_cast<double>(iters);
}

// AFTER (consolidated): one transpose into a single shared host buffer,
// deep_copy'd into both consumers. The Tier 1 target of this spec.
double TimeVariantAfter(const std::vector<double>& full_destination, int nx, int ny, int nlev, DualView3D& stream, DualView3D& core, int iters) {
    const size_t target_spatial = static_cast<size_t>(nx) * static_cast<size_t>(ny);
    auto stream_view = stream.view_device();
    auto core_view = core.view_device();

    // Warm-up (not timed).
    {
        auto host_buf = Kokkos::create_mirror_view(core_view);
        (void)host_buf;
    }

    const auto t0 = std::chrono::steady_clock::now();
    for (int it = 0; it < iters; ++it) {
        // Single transpose into one shared host buffer (i, j, level).
        auto host_buf = Kokkos::create_mirror_view(core_view);
        for (int level = 0; level < nlev; ++level) {
            for (int j = 0; j < ny; ++j) {
                for (int i = 0; i < nx; ++i) {
                    host_buf(i, j, level) = full_destination[static_cast<size_t>(level) * target_spatial + static_cast<size_t>(j) * nx + i];
                }
            }
        }
        // Authoritative write to the core import field.
        Kokkos::deep_copy(core_view, host_buf);
        core.modify_device();
        core.sync_host();
        // Feed stream_view from the same single host buffer (no second transpose).
        Kokkos::deep_copy(stream_view, host_buf);
    }
    Kokkos::fence();
    const auto t1 = std::chrono::steady_clock::now();
    const double total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return total_ms / static_cast<double>(iters);
}

void RunGrid(const GridCase& g, int nlev, int nvars, int iters) {
    const std::vector<double> full = MakeFullDestination(g.nx, g.ny, nlev);

    // Allocate the two consumer DualViews once per grid (reused across vars).
    DualView3D stream("stream_view", g.nx, g.ny, nlev);
    DualView3D core("core_import", g.nx, g.ny, nlev);

    // Sum per-variable step time so the reported ms_per_step reflects the
    // modest variable count for the grid (kept small to fit ~7 GB RAM).
    double before_ms = 0.0;
    double after_ms = 0.0;
    for (int v = 0; v < nvars; ++v) {
        before_ms += TimeVariantBefore(full, g.nx, g.ny, nlev, stream, core, iters);
        after_ms += TimeVariantAfter(full, g.nx, g.ny, nlev, stream, core, iters);
    }

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "[IngestBench] grid=" << g.label << " (" << g.nx << "x" << g.ny << ", nlev=" << nlev << ", nvars=" << nvars << ", iters=" << iters
              << ")\n";
    std::cout << "[IngestBench]   before (2 transposes): " << before_ms << " ms/step\n";
    std::cout << "[IngestBench]   after  (1 transpose)  : " << after_ms << " ms/step\n";

    // Machine-parseable lines (stable, comparable format).
    std::cout << "INGEST_BENCH: grid=" << g.label << " nx=" << g.nx << " ny=" << g.ny << " nlev=" << nlev << " nvars=" << nvars
              << " variant=before ms_per_step=" << before_ms << "\n";
    std::cout << "INGEST_BENCH: grid=" << g.label << " nx=" << g.nx << " ny=" << g.ny << " nlev=" << nlev << " nvars=" << nvars
              << " variant=after ms_per_step=" << after_ms << "\n";
    std::cout << std::flush;
}

}  // namespace

int main(int argc, char** argv) {
    // Defaults: 2D-emission fields (nlev=1), a modest variable count, and a
    // small iteration count so all three grids fit comfortably in the ~7 GB
    // container and finish quickly. All overridable on the command line.
    int nlev = 1;
    int nvars = 4;
    int iters = 5;
    std::string only_grid;  // empty => run all three

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--nlev" && i + 1 < argc) {
            nlev = std::atoi(argv[++i]);
        } else if (arg == "--nvars" && i + 1 < argc) {
            nvars = std::atoi(argv[++i]);
        } else if (arg == "--iters" && i + 1 < argc) {
            iters = std::atoi(argv[++i]);
        } else if (arg == "--grid" && i + 1 < argc) {
            only_grid = argv[++i];
        }
    }

    if (nlev < 1) nlev = 1;
    if (nvars < 1) nvars = 1;
    if (iters < 1) iters = 1;

    const std::vector<GridCase> grids = {
        {"1deg", 360, 180},
        {"F720", 2880, 1440},
        {"0.1deg", 3600, 1800},
    };

    Kokkos::initialize(argc, argv);
    {
        std::cout << "[IngestBench] ingest-copy-consolidation assembly/copy micro-benchmark\n";
        std::cout << "[IngestBench] nlev=" << nlev << " nvars=" << nvars << " iters=" << iters << "\n";
        for (const auto& g : grids) {
            if (!only_grid.empty() && only_grid != g.label) continue;
            RunGrid(g, nlev, nvars, iters);
        }
    }
    Kokkos::finalize();
    return 0;
}
