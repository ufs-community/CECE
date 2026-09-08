// Standalone MPI timing harness: develop-vs-optimize gather/assembly pipeline.
//
// This is a STANDALONE performance benchmark (NOT part of any spec). It times
// the DISTRIBUTED regrid-assembly path across np1/np2/np4 with TWO gather
// strategies against IDENTICAL synthetic inputs, so we can report an actual
// MPI wall-time speedup (not just a collective-count reduction):
//
//   DEVELOP  : the pre-adoption pattern — one MPI_Allgatherv PER LEVEL plus two
//              MPI_Allreduce(MIN) per level (a pre-gather readiness gate and a
//              post-gather status sync). Rebuilds the counts/displs each call.
//              => field_nlev gathers + 2*field_nlev reductions per assembly.
//
//   OPTIMIZE : the current adoption — one cached halo::Replicated_Gather_Plan
//              driving a SINGLE halo::gather_replicated (one MPI_Allgatherv with
//              a derived strided datatype), and the fused front-half gate (one
//              packed MIN + one MAX). Plan is built once and reused.
//              => 1 gather + a fixed, level-independent reduction count.
//
// Both variants produce the SAME replicated [level][j][i] field; the harness
// asserts they agree so the timing is apples-to-apples. No NetCDF / no DAGR read
// stage: the rank-invariant source is synthesized and regridded via the REAL
// cece::io::apply_regrid_plan (identity), exactly as the wave-10/11 focused
// harnesses do.
//
// Dims are kept modest (nx/ny/nlev small) for the ~7 GB container and np4
// oversubscription. Run under mpirun --oversubscribe -np {1,2,4}, with
// OMPI_ALLOW_RUN_AS_ROOT[_CONFIRM] set by setup.sh.
//
// Output (machine-parseable, prefixed MPI_PIPE:):
//   MPI_PIPE: np=<n> nx=.. ny=.. nlev=.. variant=develop  ms_per_assembly=<v>
//   MPI_PIPE: np=<n> nx=.. ny=.. nlev=.. variant=optimize ms_per_assembly=<v>
//   MPI_PIPE: np=<n> speedup=<develop/optimize>
//
// Build/run inside the cece-dev container only, e.g.:
//   ./setup.sh -c "cmake --build /work/build -j2 --target benchmark_mpi_pipeline"
//   ./setup.sh -c "mpirun --oversubscribe -np 2 /work/build/benchmark_mpi_pipeline"

#include <mpi.h>
#include <unistd.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <halo/collectives.hpp>
#include <halo/communicator.hpp>
#include <halo/environment.hpp>
#include <halo/gather_replicated.hpp>
#include <halo/replicated_gather_plan.hpp>

#include "cece/cece_regridder_utils.hpp"

// PMPI collective timing hooks (defined in tools/pmpi_allgatherv_timer.cpp).
extern "C" {
void pmpi_allgatherv_reset();
unsigned long long pmpi_allgatherv_calls();
double pmpi_allgatherv_total_ms();
}

namespace {

using cece::io::RegridPlan;
using cece::io::apply_regrid_plan;

int WorldRank() {
    int r = 0;
    int inited = 0;
    MPI_Initialized(&inited);
    if (inited) MPI_Comm_rank(MPI_COMM_WORLD, &r);
    return r;
}
int WorldSize() {
    int s = 1;
    int inited = 0;
    MPI_Initialized(&inited);
    if (inited) MPI_Comm_size(MPI_COMM_WORLD, &s);
    return s;
}

// Production band decomposition (src/driver/cece_driver_facade.cpp).
int BandStart(int rank, int ny, int size) {
    const int base = ny / size;
    const int rem = ny % size;
    return rank * base + std::min(rank, rem);
}

RegridPlan MakeIdentityPlan(int nx, int ny, int j0, int j1) {
    RegridPlan plan;
    plan.file_nx = nx;
    plan.file_ny = ny;
    plan.j0 = j0;
    plan.j1 = j1;
    plan.identity = true;
    plan.built = true;
    return plan;
}

std::optional<halo::Communicator> MakeHaloComm() {
    int inited = 0;
    MPI_Initialized(&inited);
    if (!inited || WorldSize() <= 1) return std::nullopt;
    return std::optional<halo::Communicator>(std::in_place, MPI_COMM_WORLD);
}

int arg_int(int argc, char** argv, const char* flag, int fallback) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], flag) == 0) return std::atoi(argv[i + 1]);
    }
    return fallback;
}

double median_of(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return (n % 2 == 0) ? 0.5 * (v[n / 2 - 1] + v[n / 2]) : v[n / 2];
}

// Build this rank's contiguous [level][band] send buffer via the REAL identity
// apply_regrid_plan over the rank's band.
std::vector<double> BuildSendBuffer(const std::vector<double>& source, int nx, int ny, int nlev, int j0, int j1) {
    const int band_elems = (j1 - j0) * nx;
    const RegridPlan plan = MakeIdentityPlan(nx, ny, j0, j1);
    std::vector<double> send_buf(static_cast<std::size_t>(nlev) * band_elems, 0.0);
    for (int level = 0; level < nlev; ++level) {
        std::vector<double> band;
        apply_regrid_plan(plan, 0, false, source.data(), nx, ny, nx, band);
        std::copy(band.begin(), band.end(), send_buf.begin() + static_cast<std::size_t>(level) * band_elems);
    }
    return send_buf;
}

// ── DEVELOP assembly: one MPI_Allgatherv PER LEVEL + 2 MPI_Allreduce(MIN) per
//    level, rebuilding counts/displs each call. Produces [level][j][i]. ──
std::vector<double> AssembleDevelop(const std::vector<double>& send_buf, int nx, int ny, int nlev, int size, int rank) {
    std::vector<double> full(static_cast<std::size_t>(nlev) * nx * ny, 0.0);
    const int band_elems = (BandStart(rank + 1, ny, size) - BandStart(rank, ny, size)) * nx;

    if (size <= 1) {
        std::copy(send_buf.begin(), send_buf.end(), full.begin());
        return full;
    }

    // Rebuild per-rank counts/displs every assembly (as develop did).
    std::vector<int> counts(size, 0);
    std::vector<int> displs(size, 0);
    for (int r = 0; r < size; ++r) {
        const int rj0 = BandStart(r, ny, size);
        const int rj1 = BandStart(r + 1, ny, size);
        counts[r] = (rj1 - rj0) * nx;
        displs[r] = rj0 * nx;
    }

    for (int level = 0; level < nlev; ++level) {
        // Pre-gather readiness reduction (per level, as develop).
        int local_ready = 1;
        int all_ready = 1;
        MPI_Allreduce(&local_ready, &all_ready, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);

        const double* level_send = send_buf.data() + static_cast<std::size_t>(level) * band_elems;
        double* level_dst = full.data() + static_cast<std::size_t>(level) * nx * ny;
        MPI_Allgatherv(level_send, band_elems, MPI_DOUBLE, level_dst, counts.data(), displs.data(), MPI_DOUBLE,
                       MPI_COMM_WORLD);

        // Post-gather status reduction (per level, as develop).
        int local_ok = 1;
        int all_ok = 1;
        MPI_Allreduce(&local_ok, &all_ok, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
    }
    return full;
}

// ── OPTIMIZE assembly: one cached Replicated_Gather_Plan + one
//    gather_replicated. Plan is passed in (built once, reused). ──
std::vector<double> AssembleOptimize(const std::vector<double>& send_buf, int nx, int ny, int nlev, int size, int rank,
                                     const halo::Replicated_Gather_Plan<double>* plan) {
    std::vector<double> full(static_cast<std::size_t>(nlev) * nx * ny, 0.0);
    if (size <= 1) {
        std::copy(send_buf.begin(), send_buf.end(), full.begin());
        return full;
    }
    Kokkos::View<const double*, Kokkos::HostSpace, Kokkos::MemoryTraits<Kokkos::Unmanaged>> send_view(send_buf.data(),
                                                                                                      send_buf.size());
    Kokkos::View<double*, Kokkos::HostSpace, Kokkos::MemoryTraits<Kokkos::Unmanaged>> dest_view(full.data(), full.size());
    halo::gather_replicated(*plan, send_view, dest_view);
    return full;
}

// ── OPTIMIZE2 assembly (candidate fix): ONE MPI_Allgatherv with PLAIN
//    MPI_DOUBLE into a contiguous [rank][level][band] receive buffer (no strided
//    derived datatype), then a cheap local host reorder into the replicated
//    [level][j][i] layout. Keeps the single-collective count of the optimize
//    path but avoids the element-wise strided pack/unpack that made it slow. ──
std::vector<double> AssembleOptimize2(const std::vector<double>& send_buf, int nx, int ny, int nlev, int size, int rank,
                                      const std::vector<int>& band_rows_per_rank) {
    std::vector<double> full(static_cast<std::size_t>(nlev) * nx * ny, 0.0);
    if (size <= 1) {
        std::copy(send_buf.begin(), send_buf.end(), full.begin());
        return full;
    }

    // Per-rank contiguous chunk = nlev * (rows_r * nx). recvbuf lays out rank
    // blocks back to back, each block being that rank's [level][band].
    std::vector<int> counts(size, 0);
    std::vector<int> displs(size, 0);
    int running = 0;
    for (int r = 0; r < size; ++r) {
        const int rows_r = band_rows_per_rank[r];
        counts[r] = nlev * rows_r * nx;
        displs[r] = running;
        running += counts[r];
    }
    std::vector<double> recv(static_cast<std::size_t>(running), 0.0);

    const int my_rows = band_rows_per_rank[rank];
    const int my_send = nlev * my_rows * nx;
    MPI_Allgatherv(send_buf.data(), my_send, MPI_DOUBLE, recv.data(), counts.data(), displs.data(), MPI_DOUBLE,
                   MPI_COMM_WORLD);

    // Local reorder: for each rank block, each level's band of rows_r*nx doubles
    // is copied contiguously into full[level][j0_r .. j1_r][:]. This is a plain
    // memcpy per (rank, level) — no MPI element-wise packing.
    for (int r = 0; r < size; ++r) {
        const int rows_r = band_rows_per_rank[r];
        if (rows_r == 0) continue;
        const int j0_r = BandStart(r, ny, size);
        const int band_r = rows_r * nx;
        const double* rank_block = recv.data() + displs[r];
        for (int level = 0; level < nlev; ++level) {
            const double* src = rank_block + static_cast<std::size_t>(level) * band_r;
            double* dst = full.data() + static_cast<std::size_t>(level) * nx * ny + static_cast<std::size_t>(j0_r) * nx;
            std::copy(src, src + band_r, dst);
        }
    }
    return full;
}

// ── OPTIMIZE3 assembly (candidate fix, parallel reorder): same single
//    contiguous MPI_Allgatherv as optimize2, but the local [rank][level][band]
//    -> [level][j][i] reorder is a Kokkos host parallel_for over (rank, level)
//    bands instead of a serial std::copy loop. Tests whether parallelizing the
//    ~0.5 GB reorder closes the gap to the per-level loop while keeping ONE
//    collective. ──
std::vector<double> AssembleOptimize3(const std::vector<double>& send_buf, int nx, int ny, int nlev, int size, int rank,
                                      const std::vector<int>& band_rows_per_rank) {
    std::vector<double> full(static_cast<std::size_t>(nlev) * nx * ny, 0.0);
    if (size <= 1) {
        std::copy(send_buf.begin(), send_buf.end(), full.begin());
        return full;
    }

    std::vector<int> counts(size, 0);
    std::vector<int> displs(size, 0);
    std::vector<int> j0_of(size, 0);
    int running = 0;
    for (int r = 0; r < size; ++r) {
        const int rows_r = band_rows_per_rank[r];
        counts[r] = nlev * rows_r * nx;
        displs[r] = running;
        j0_of[r] = BandStart(r, ny, size);
        running += counts[r];
    }
    std::vector<double> recv(static_cast<std::size_t>(running), 0.0);

    const int my_rows = band_rows_per_rank[rank];
    const int my_send = nlev * my_rows * nx;
    MPI_Allgatherv(send_buf.data(), my_send, MPI_DOUBLE, recv.data(), counts.data(), displs.data(), MPI_DOUBLE,
                   MPI_COMM_WORLD);

    // Parallel reorder over the flat set of (rank, level) bands. Each work item
    // copies one contiguous band_r-length row-block. Raw pointers are captured
    // (host-only unmanaged access) so no Kokkos View wrapping overhead per band.
    const double* recv_ptr = recv.data();
    double* full_ptr = full.data();
    const int nsize = size;
    const int levels = nlev;
    const std::size_t plane = static_cast<std::size_t>(nx) * ny;

    // Precompute per-rank offsets once (host vectors captured by value copy into
    // small local arrays through pointers).
    const int* displs_ptr = displs.data();
    const int* rows_ptr = band_rows_per_rank.data();
    const int* j0_ptr = j0_of.data();

    Kokkos::parallel_for(
        "optimize3_reorder", Kokkos::RangePolicy<Kokkos::DefaultHostExecutionSpace>(0, static_cast<std::size_t>(nsize) * levels),
        [=](const std::size_t idx) {
            const int r = static_cast<int>(idx / levels);
            const int level = static_cast<int>(idx % levels);
            const int rows_r = rows_ptr[r];
            if (rows_r == 0) return;
            const int band_r = rows_r * nx;
            const double* src = recv_ptr + displs_ptr[r] + static_cast<std::size_t>(level) * band_r;
            double* dst = full_ptr + static_cast<std::size_t>(level) * plane + static_cast<std::size_t>(j0_ptr[r]) * nx;
            for (int k = 0; k < band_r; ++k) dst[k] = src[k];
        });
    Kokkos::fence();
    return full;
}

}  // namespace

int main(int argc, char** argv) {
    bool is_discovery = false;  // harmless; keeps parity with the test-main pattern
    (void)is_discovery;

    unsetenv("SLURM_JOB_ID");
    unsetenv("SLURM_STEP_ID");
    unsetenv("PMI_RANK");
    unsetenv("PMI_SIZE");
    setenv("I_MPI_HYDRA_BOOTSTRAP", "none", 0);
    setenv("I_MPI_SHM", "disable", 0);

    int mpi_initialized = 0;
    MPI_Initialized(&mpi_initialized);
    if (!mpi_initialized) {
        int provided = 0;
        MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    }
    halo::Environment::initialize();
    if (!Kokkos::is_initialized()) Kokkos::initialize(argc, argv);

    {
        const int nx = arg_int(argc, argv, "--nx", 64);
        const int ny = arg_int(argc, argv, "--ny", 64);
        const int nlev = arg_int(argc, argv, "--nlev", 8);
        const int iters = arg_int(argc, argv, "--iters", 200);
        const int reps = arg_int(argc, argv, "--reps", 11);

        const int size = WorldSize();
        const int rank = WorldRank();
        const int j0 = BandStart(rank, ny, size);
        const int j1 = BandStart(rank + 1, ny, size);

        if (rank == 0) {
            std::cout << "[MpiPipeBench] develop-vs-optimize distributed gather/assembly\n";
            std::cout << "[MpiPipeBench] np=" << size << " nx=" << nx << " ny=" << ny << " nlev=" << nlev
                      << " iters=" << iters << " reps=" << reps << "\n"
                      << std::flush;
        }

        // Rank-invariant source (same on every rank), deterministic.
        std::vector<double> source(static_cast<std::size_t>(nx) * ny, 0.0);
        for (std::size_t k = 0; k < source.size(); ++k) {
            source[k] = static_cast<double>((k * 37 + 11) % 1000) - 500.0 + 0.25 * static_cast<double>(k % 7);
        }

        const std::vector<double> send_buf = BuildSendBuffer(source, nx, ny, nlev, j0, j1);
        const int band_elems = (j1 - j0) * nx;

        // Per-rank band row counts (rank-invariant), for the contiguous
        // optimize2 gather's counts/displs.
        std::vector<int> band_rows_per_rank(size, 0);
        for (int r = 0; r < size; ++r) {
            band_rows_per_rank[r] = BandStart(r + 1, ny, size) - BandStart(r, ny, size);
        }

        // Build the OPTIMIZE plan ONCE (reused every assembly, as production does).
        const std::optional<halo::Communicator> halo_comm = MakeHaloComm();
        std::optional<halo::Replicated_Gather_Plan<double>> plan;
        if (size > 1) {
            plan.emplace(*halo_comm, /*local_band_count=*/band_elems, /*num_levels=*/nlev);
        }

        // Correctness: both variants must produce the same field.
        {
            const std::vector<double> a = AssembleDevelop(send_buf, nx, ny, nlev, size, rank);
            const std::vector<double> b =
                AssembleOptimize(send_buf, nx, ny, nlev, size, rank, plan.has_value() ? &plan.value() : nullptr);
            double maxdiff = 0.0;
            for (std::size_t k = 0; k < a.size(); ++k) maxdiff = std::max(maxdiff, std::abs(a[k] - b[k]));
            const std::vector<double> c = AssembleOptimize2(send_buf, nx, ny, nlev, size, rank, band_rows_per_rank);
            const std::vector<double> d = AssembleOptimize3(send_buf, nx, ny, nlev, size, rank, band_rows_per_rank);
            double maxdiff2 = 0.0;
            double maxdiff3 = 0.0;
            for (std::size_t k = 0; k < a.size(); ++k) {
                maxdiff2 = std::max(maxdiff2, std::abs(a[k] - c[k]));
                maxdiff3 = std::max(maxdiff3, std::abs(a[k] - d[k]));
            }
            double global_maxdiff = maxdiff;
            double global_maxdiff2 = maxdiff2;
            double global_maxdiff3 = maxdiff3;
            if (size > 1) {
                MPI_Allreduce(&maxdiff, &global_maxdiff, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
                MPI_Allreduce(&maxdiff2, &global_maxdiff2, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
                MPI_Allreduce(&maxdiff3, &global_maxdiff3, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
            }
            if (rank == 0) {
                std::cout << "[MpiPipeBench] develop/optimize  max abs diff=" << std::scientific << global_maxdiff << "\n";
                std::cout << "[MpiPipeBench] develop/optimize2 max abs diff=" << std::scientific << global_maxdiff2 << "\n";
                std::cout << "[MpiPipeBench] develop/optimize3 max abs diff=" << std::scientific << global_maxdiff3
                          << std::fixed << "\n"
                          << std::flush;
            }
        }

        // ── Collective-only attribution: reset the PMPI Allgatherv timer,
        //    run ONE assembly of each variant, and report the Allgatherv call
        //    count + total time so the strided single-gather vs per-level loop
        //    collective cost is isolated from the surrounding work. ──
        if (size > 1) {
            pmpi_allgatherv_reset();
            (void)AssembleDevelop(send_buf, nx, ny, nlev, size, rank);
            const unsigned long long dev_calls = pmpi_allgatherv_calls();
            const double dev_coll_ms = pmpi_allgatherv_total_ms();

            pmpi_allgatherv_reset();
            (void)AssembleOptimize(send_buf, nx, ny, nlev, size, rank, plan.has_value() ? &plan.value() : nullptr);
            const unsigned long long opt_calls = pmpi_allgatherv_calls();
            const double opt_coll_ms = pmpi_allgatherv_total_ms();

            double dev_coll_max = dev_coll_ms;
            double opt_coll_max = opt_coll_ms;
            MPI_Allreduce(&dev_coll_ms, &dev_coll_max, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
            MPI_Allreduce(&opt_coll_ms, &opt_coll_max, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
            if (rank == 0) {
                std::cout << std::fixed << std::setprecision(5);
                std::cout << "MPI_PIPE_COLL: np=" << size << " nlev=" << nlev
                          << " develop  allgatherv_calls=" << dev_calls << " allgatherv_ms=" << dev_coll_max << "\n";
                std::cout << "MPI_PIPE_COLL: np=" << size << " nlev=" << nlev
                          << " optimize allgatherv_calls=" << opt_calls << " allgatherv_ms=" << opt_coll_max << "\n"
                          << std::flush;
            }
        }

        // Warm-up (untimed).
        for (int v = 0; v < 2; ++v) {
            (void)AssembleDevelop(send_buf, nx, ny, nlev, size, rank);
            (void)AssembleOptimize(send_buf, nx, ny, nlev, size, rank, plan.has_value() ? &plan.value() : nullptr);
            (void)AssembleOptimize2(send_buf, nx, ny, nlev, size, rank, band_rows_per_rank);
            (void)AssembleOptimize3(send_buf, nx, ny, nlev, size, rank, band_rows_per_rank);
        }

        enum class Variant { Develop, Optimize, Optimize2, Optimize3 };
        auto time_variant = [&](Variant which) {
            std::vector<double> per_rep;
            per_rep.reserve(reps);
            for (int rep = 0; rep < reps; ++rep) {
                MPI_Barrier(size > 1 ? MPI_COMM_WORLD : MPI_COMM_SELF);
                const auto t0 = std::chrono::steady_clock::now();
                for (int it = 0; it < iters; ++it) {
                    double sink = 0.0;
                    if (which == Variant::Develop) {
                        sink = AssembleDevelop(send_buf, nx, ny, nlev, size, rank)[0];
                    } else if (which == Variant::Optimize) {
                        sink = AssembleOptimize(send_buf, nx, ny, nlev, size, rank,
                                                plan.has_value() ? &plan.value() : nullptr)[0];
                    } else if (which == Variant::Optimize2) {
                        sink = AssembleOptimize2(send_buf, nx, ny, nlev, size, rank, band_rows_per_rank)[0];
                    } else {
                        sink = AssembleOptimize3(send_buf, nx, ny, nlev, size, rank, band_rows_per_rank)[0];
                    }
                    volatile double vsink = sink;
                    (void)vsink;
                }
                const auto t1 = std::chrono::steady_clock::now();
                const double local_ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / iters;
                double rep_ms = local_ms;
                if (size > 1) MPI_Allreduce(&local_ms, &rep_ms, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
                per_rep.push_back(rep_ms);
            }
            return median_of(per_rep);
        };

        const double dev_ms = time_variant(Variant::Develop);
        const double opt_ms = time_variant(Variant::Optimize);
        const double opt2_ms = time_variant(Variant::Optimize2);
        const double opt3_ms = time_variant(Variant::Optimize3);

        if (rank == 0) {
            std::cout << std::fixed << std::setprecision(5);
            std::cout << "MPI_PIPE: np=" << size << " nx=" << nx << " ny=" << ny << " nlev=" << nlev
                      << " variant=develop   ms_per_assembly=" << dev_ms << "\n";
            std::cout << "MPI_PIPE: np=" << size << " nx=" << nx << " ny=" << ny << " nlev=" << nlev
                      << " variant=optimize  ms_per_assembly=" << opt_ms << "\n";
            std::cout << "MPI_PIPE: np=" << size << " nx=" << nx << " ny=" << ny << " nlev=" << nlev
                      << " variant=optimize2 ms_per_assembly=" << opt2_ms << "\n";
            std::cout << "MPI_PIPE: np=" << size << " nx=" << nx << " ny=" << ny << " nlev=" << nlev
                      << " variant=optimize3 ms_per_assembly=" << opt3_ms << "\n";
            std::cout << "MPI_PIPE: np=" << size << " speedup(develop/optimize)=" << (opt_ms > 0.0 ? dev_ms / opt_ms : 0.0)
                      << " speedup(develop/optimize2)=" << (opt2_ms > 0.0 ? dev_ms / opt2_ms : 0.0)
                      << " speedup(develop/optimize3)=" << (opt3_ms > 0.0 ? dev_ms / opt3_ms : 0.0)
                      << " speedup(optimize/optimize3)=" << (opt3_ms > 0.0 ? opt_ms / opt3_ms : 0.0) << "\n"
                      << std::flush;
        }

        // Free the plan before finalize (RAII frees the derived datatype).
        plan.reset();
    }

    if (Kokkos::is_initialized()) Kokkos::finalize();
    int finalized = 0;
    MPI_Finalized(&finalized);
    if (!finalized) MPI_Finalize();
    return 0;
}
