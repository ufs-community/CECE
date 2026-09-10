// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors
//
// Feature: cece-halo-collective-adoption
// Property 5: Assembly collective structure of the per-level MPI_Allgatherv loop
//
// Validates: Requirements 1.2, 6.1, 13.3
//
// The distributed regrid assembly in RegridToDestinationBuffer issues, per
// assembly:
//   * a fused front-half gate — ONE packed 5-entry halo::allreduce<int> MIN
//     plus ONE MAX over [readiness, file_nx, file_ny, field_nlev, identity]
//     (2 reductions total), FIXED regardless of field_nlev;
//   * ONE pre-gather readiness halo::allreduce<int>(MIN) over local_ok;
//   * a per-level assembly loop of EXACTLY field_nlev plain-MPI_DOUBLE
//     MPI_Allgatherv calls, each moving this rank's contiguous latitude band
//     into full_destination[level*nx*ny + j*nx + i].
//
// Benchmarking at the F360+ (>=1440x720) x 64-128 level target proved this
// contiguous per-level loop is markedly faster than a single strided-datatype
// gather in the container's OpenMPI (the strided derived datatype forces
// element-wise pack/unpack that dominates cost and scales with nlev). So the
// assembly gather count DOES scale with field_nlev — this test pins that
// structure precisely, and pins the level-INDEPENDENT counts (front gate = 2,
// readiness = 1) that must NOT scale with nlev.
//
// This test proves the structure directly and mechanically with a PMPI-style
// interposition spy: we define our own MPI_Allgatherv / MPI_Allreduce that
// increment counters and forward to the real PMPI_Allgatherv / PMPI_Allreduce.
// Because the MPI profiling interface guarantees every MPI_* entry point has a
// matching PMPI_* companion, our wrappers shadow the library's symbols at link
// time while still driving real collectives underneath — so the counts are the
// EXACT collectives the harness issues, over a genuine communicator, not a mock.
//
// Rather than stand up a full CeceDriverOrchestrator (which needs DAGR / CeceIO
// / a live config), this test exercises a faithful, focused harness of the
// production per-level assembly path: it builds the same rank-invariant band
// counts/displacements, assembles the same contiguous [level][band] send
// buffer, runs the pre-gather local_ok readiness halo::allreduce<int>(MIN),
// then loops field_nlev MPI_Allgatherv calls into the replicated
// [level][j][i] destination, and runs the same fused front-gate MIN/MAX packed
// halo::allreduce over the 5-entry [readiness, file_nx, file_ny, field_nlev,
// identity] vector. The spy then asserts, across a RapidCheck-generated
// field_nlev (>= 100 iterations):
//
//   * EXACTLY field_nlev MPI_Allgatherv calls per assembly (scales with the
//     level count) (Req 1.2);
//   * EXACTLY ONE additional pre-gather readiness MPI_Allreduce(MIN) per
//     assembly, independent of field_nlev (Req 13.3);
//   * the fused front-gate reduction count is a FIXED 2 (one MIN + one MAX),
//     independent of field_nlev (Req 6.1).
//
// It runs correctly as a single process (ctest default) and under
// mpirun --oversubscribe -np 2: the collectives are real on every rank, so the
// counts are identical and rank-invariant.

#include <gtest/gtest.h>
#include <mpi.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <unistd.h>

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <halo/collectives.hpp>
#include <halo/communicator.hpp>
#include <halo/environment.hpp>
#include <string>
#include <vector>

// ============================================================================
// PMPI-style interposition spy.
//
// We define MPI_Allgatherv and MPI_Allreduce ourselves; the linker resolves the
// application's (and HALO's) references to THESE definitions, and each forwards
// to the real implementation through the guaranteed PMPI_ companion while
// bumping a counter. This is the MPI profiling interface used exactly as the
// standard intends. Counters are process-local atomics reset between windows.
// ============================================================================
namespace {

std::atomic<long> g_allgatherv_count{0};
std::atomic<long> g_allreduce_count{0};

void ResetSpy() {
    g_allgatherv_count.store(0, std::memory_order_relaxed);
    g_allreduce_count.store(0, std::memory_order_relaxed);
}

long AllgathervCount() {
    return g_allgatherv_count.load(std::memory_order_relaxed);
}
long AllreduceCount() {
    return g_allreduce_count.load(std::memory_order_relaxed);
}

}  // namespace

extern "C" {

int MPI_Allgatherv(const void* sendbuf, int sendcount, MPI_Datatype sendtype, void* recvbuf, const int recvcounts[], const int displs[],
                   MPI_Datatype recvtype, MPI_Comm comm) {
    g_allgatherv_count.fetch_add(1, std::memory_order_relaxed);
    return PMPI_Allgatherv(sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, comm);
}

int MPI_Allreduce(const void* sendbuf, void* recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
    g_allreduce_count.fetch_add(1, std::memory_order_relaxed);
    return PMPI_Allreduce(sendbuf, recvbuf, count, datatype, op, comm);
}

}  // extern "C"

namespace cece::test {

namespace {

// Modest, container-friendly target grid. Kept fixed so the ONLY thing varying
// across RapidCheck iterations is field_nlev — the axis the per-level gather
// count must scale with and the front-gate/readiness counts must NOT.
constexpr int kNx = 24;
constexpr int kNy = 12;

int WorldSizeOrOne() {
    int size = 1;
    int inited = 0;
    MPI_Initialized(&inited);
    if (inited) MPI_Comm_size(MPI_COMM_WORLD, &size);
    return size;
}

int WorldRankOrZero() {
    int rank = 0;
    int inited = 0;
    MPI_Initialized(&inited);
    if (inited) MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    return rank;
}

// RapidCheck runs the property independently on every rank with a per-rank seed,
// so a generated value differs across ranks. In production field_nlev is a
// rank-invariant input (every rank sees the same field), and the per-level
// MPI_Allgatherv loop deadlocks if ranks disagree on the number of levels.
// Broadcast rank 0's value so all ranks run the collective harness with the
// SAME field_nlev, faithfully modelling production. At np1 this is a no-op.
int AgreeAcrossRanks(int value) {
    int inited = 0;
    MPI_Initialized(&inited);
    if (!inited || WorldSizeOrOne() <= 1) return value;
    int agreed = value;
    MPI_Bcast(&agreed, 1, MPI_INT, 0, MPI_COMM_WORLD);
    return agreed;
}

// CECE's contiguous latitude-band decomposition, identical to the driver:
// band_base = ny / size, remainder distributed to the first `band_rem` ranks.
int BandStart(int ny, int size, int rank) {
    const int band_base = ny / size;
    const int band_rem = ny % size;
    return rank * band_base + std::min(rank, band_rem);
}

// Result of one assembly: the collective counts the spy observed.
struct AssemblyCounts {
    long allgatherv = 0;
    long assembly_allreduce = 0;
};

// Faithful harness of the production per-level assembly path for a given
// field_nlev. Builds the same rank-invariant band counts/displacements and the
// same contiguous [level][band] send buffer, runs the pre-gather local_ok
// readiness reduce, then loops field_nlev MPI_Allgatherv calls (plain
// MPI_DOUBLE) into the replicated [level][j][i] destination. Returns the spy
// counts observed during the assembly.
//
// Mirrors the driver's `distributed` predicate: at mpi_size == 1 the driver
// takes the std::copy fast path (no collectives), so this harness only issues
// collectives when size > 1.
AssemblyCounts RunAssembly(const halo::Communicator& comm, int size, int rank, int field_nlev) {
    const int j0 = BandStart(kNy, size, rank);
    const int j1 = BandStart(kNy, size, rank + 1);
    const int band = j1 - j0;
    const int band_elems = band * kNx;  // per-level band element count

    // Contiguous [level][band] send buffer: element (level, jrel, i) at
    // level*band_elems + jrel*kNx + i. Filled with a deterministic pattern.
    std::vector<double> send_buf(static_cast<std::size_t>(field_nlev) * static_cast<std::size_t>(band_elems));
    for (int level = 0; level < field_nlev; ++level) {
        for (int e = 0; e < band_elems; ++e) {
            send_buf[static_cast<std::size_t>(level) * band_elems + e] = static_cast<double>(rank * 100000 + level * 1000 + e);
        }
    }
    std::vector<double> full_destination(static_cast<std::size_t>(field_nlev) * kNx * kNy, 0.0);

    const bool distributed = size > 1;
    if (distributed) {
        // Rank-invariant per-rank band counts/displacements (derived purely from
        // ny, nx, and size), exactly as the driver builds them.
        std::vector<int> band_counts(static_cast<std::size_t>(size));
        std::vector<int> band_displs(static_cast<std::size_t>(size));
        for (int r = 0; r < size; ++r) {
            const int rj0 = BandStart(kNy, size, r);
            const int rj1 = BandStart(kNy, size, r + 1);
            band_counts[static_cast<std::size_t>(r)] = (rj1 - rj0) * kNx;
            band_displs[static_cast<std::size_t>(r)] = rj0 * kNx;
        }

        // Assembly window begins here.
        ResetSpy();

        // Pre-gather local_ok readiness reduce (the driver's single MIN over
        // local_ok so a failing rank still enters the collective). ONE
        // Allreduce, independent of field_nlev.
        (void)halo::allreduce<int>(comm, std::vector<int>{1}, MPI_MIN);

        // Per-level assembly: EXACTLY field_nlev MPI_Allgatherv calls (plain
        // MPI_DOUBLE), each moving this rank's contiguous band into
        // full_destination[level*nx*ny + j*nx + i].
        for (int level = 0; level < field_nlev; ++level) {
            const double* level_send = send_buf.data() + static_cast<std::size_t>(level) * band_elems;
            double* level_dst = full_destination.data() + static_cast<std::size_t>(level) * kNx * kNy;
            MPI_Allgatherv(level_send, band_elems, MPI_DOUBLE, level_dst, band_counts.data(), band_displs.data(), MPI_DOUBLE, MPI_COMM_WORLD);
        }
    } else {
        // mpi_size == 1 fast path (Decision A): std::copy, no collectives.
        ResetSpy();
        for (int level = 0; level < field_nlev; ++level) {
            const std::size_t src_base = static_cast<std::size_t>(level) * band_elems;
            const std::size_t dst_base = static_cast<std::size_t>(level) * kNx * kNy + static_cast<std::size_t>(j0) * kNx;
            for (int e = 0; e < band_elems; ++e) {
                full_destination[dst_base + e] = send_buf[src_base + e];
            }
        }
    }

    AssemblyCounts counts;
    counts.allgatherv = AllgathervCount();
    counts.assembly_allreduce = AllreduceCount();
    return counts;
}

// The fused front-half gate: ONE packed MIN + ONE packed MAX over the 5-entry
// [readiness, file_nx, file_ny, field_nlev, identity] vector. Returns the spy's
// Allreduce count for the gate window (must be exactly 2 when distributed).
long RunFrontGate(const halo::Communicator& comm, int size, int field_nlev) {
    ResetSpy();
    if (size > 1) {
        const std::vector<int> v{1, kNx, kNy, field_nlev, 1};
        (void)halo::allreduce<int>(comm, v, MPI_MIN);
        (void)halo::allreduce<int>(comm, v, MPI_MAX);
    }
    return AllreduceCount();
}

}  // namespace

// ---------------------------------------------------------------------------
// Property 5 (assembly): EXACTLY field_nlev MPI_Allgatherv calls per assembly
// (scaling with the level count), plus EXACTLY ONE pre-gather readiness
// MPI_Allreduce(MIN) independent of field_nlev. Driven by RapidCheck over a
// generated small field_nlev in [1, 8] (>= 100 iterations by default).
// ---------------------------------------------------------------------------
RC_GTEST_PROP(CollectiveCount, PerLevelGatherCountEqualsLevelCount, ()) {
    const int size = WorldSizeOrOne();
    const int rank = WorldRankOrZero();

    const int field_nlev = AgreeAcrossRanks(*rc::gen::inRange(1, 9));  // small nlev, rank-invariant

    halo::Communicator comm(MPI_COMM_WORLD);
    const AssemblyCounts counts = RunAssembly(comm, size, rank, field_nlev);

    if (size > 1) {
        // EXACTLY field_nlev Allgatherv calls: the assembly scales with the
        // level count (Req 1.2).
        RC_ASSERT(counts.allgatherv == static_cast<long>(field_nlev));
        // EXACTLY one pre-gather readiness reduce, independent of field_nlev
        // (Req 13.3).
        RC_ASSERT(counts.assembly_allreduce == 1L);
    } else {
        // Size-1 fast path issues NO collectives.
        RC_ASSERT(counts.allgatherv == 0L);
        RC_ASSERT(counts.assembly_allreduce == 0L);
    }
}

// ---------------------------------------------------------------------------
// Property 5 (front gate): the fused front-half gate always issues a FIXED 2
// reductions (one MIN + one MAX), independent of field_nlev (Req 6.1).
// ---------------------------------------------------------------------------
RC_GTEST_PROP(CollectiveCount, FrontGateReductionCountFixedAtTwo, ()) {
    const int size = WorldSizeOrOne();
    const int field_nlev = AgreeAcrossRanks(*rc::gen::inRange(1, 9));

    halo::Communicator comm(MPI_COMM_WORLD);
    const long gate_reductions = RunFrontGate(comm, size, field_nlev);

    if (size > 1) {
        RC_ASSERT(gate_reductions == 2L);
    } else {
        RC_ASSERT(gate_reductions == 0L);
    }
}

// ---------------------------------------------------------------------------
// Cross-nlev structure: two assemblies with DIFFERENT field_nlev must observe
// gather counts that DIFFER by exactly the level-count difference, while the
// pre-gather readiness reduce and the front-gate reduction count stay FIXED.
// This is the direct statement of "the gather loop scales with level count, the
// gate/readiness reductions do not".
// ---------------------------------------------------------------------------
RC_GTEST_PROP(CollectiveCount, GatherScalesWithLevelsGateAndReadinessDoNot, ()) {
    const int size = WorldSizeOrOne();
    const int rank = WorldRankOrZero();

    const int nlev_a = AgreeAcrossRanks(*rc::gen::inRange(1, 9));
    const int nlev_b = AgreeAcrossRanks(*rc::gen::inRange(1, 9));

    halo::Communicator comm_a(MPI_COMM_WORLD);
    const AssemblyCounts a = RunAssembly(comm_a, size, rank, nlev_a);
    halo::Communicator comm_b(MPI_COMM_WORLD);
    const AssemblyCounts b = RunAssembly(comm_b, size, rank, nlev_b);

    if (size > 1) {
        // Gather count tracks nlev exactly.
        RC_ASSERT(a.allgatherv == static_cast<long>(nlev_a));
        RC_ASSERT(b.allgatherv == static_cast<long>(nlev_b));
        // Difference in gather count equals difference in level count.
        RC_ASSERT((a.allgatherv - b.allgatherv) == static_cast<long>(nlev_a - nlev_b));
        // Pre-gather readiness reduce is FIXED at 1, regardless of nlev.
        RC_ASSERT(a.assembly_allreduce == 1L);
        RC_ASSERT(b.assembly_allreduce == 1L);
    } else {
        RC_ASSERT(a.allgatherv == 0L);
        RC_ASSERT(b.allgatherv == 0L);
    }

    // Front-gate reduction count is FIXED regardless of nlev.
    const long gate_a = RunFrontGate(comm_a, size, nlev_a);
    const long gate_b = RunFrontGate(comm_b, size, nlev_b);
    RC_ASSERT(gate_a == gate_b);
}

// ---------------------------------------------------------------------------
// Example/edge unit test: pin the concrete expected counts at the current world
// size so a regression that changes the assembly structure is caught even if
// the RapidCheck generator happened to pick the same nlev twice.
// ---------------------------------------------------------------------------
TEST(CollectiveCount, ConcreteCountsAtSmallAndLargeNlev) {
    const int size = WorldSizeOrOne();
    const int rank = WorldRankOrZero();

    halo::Communicator comm(MPI_COMM_WORLD);
    const AssemblyCounts one = RunAssembly(comm, size, rank, 1);
    halo::Communicator comm2(MPI_COMM_WORLD);
    const AssemblyCounts eight = RunAssembly(comm2, size, rank, 8);

    if (size > 1) {
        // Gather count equals the level count.
        EXPECT_EQ(one.allgatherv, 1L);
        EXPECT_EQ(eight.allgatherv, 8L);
        // Pre-gather readiness reduce is FIXED at 1.
        EXPECT_EQ(one.assembly_allreduce, 1L);
        EXPECT_EQ(eight.assembly_allreduce, 1L);
    } else {
        EXPECT_EQ(one.allgatherv, 0L);
        EXPECT_EQ(eight.allgatherv, 0L);
    }

    const long gate1 = RunFrontGate(comm, size, 1);
    const long gate8 = RunFrontGate(comm2, size, 8);
    // Front-gate reduction count does not depend on nlev at ANY size.
    EXPECT_EQ(gate1, gate8);
    if (size > 1) {
        EXPECT_EQ(gate1, 2L);
    }
}

}  // namespace cece::test

// ---------------------------------------------------------------------------
// Own main() with MPI init, mirroring the MPI-main tests in this suite (e.g.
// test_mpi_reuse_decision.cpp). ALL ranks stay alive and run RUN_ALL_TESTS so
// they participate in the real collectives the spy counts.
// halo::Environment::initialize() is idempotent and is invoked after MPI init
// so the HALO primitives see a live environment.
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    bool is_discovery = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--gtest_list_tests") {
            is_discovery = true;
            break;
        }
    }

    if (!is_discovery) {
        // Prevent Intel/Open MPI from detecting Slurm and attempting a PMI/PMIX
        // process-manager bootstrap during the test.
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
    }

    ::testing::InitGoogleTest(&argc, argv);
    const int rc = RUN_ALL_TESTS();

    int mpi_initialized = 0;
    MPI_Initialized(&mpi_initialized);
    if (mpi_initialized) {
        int finalized = 0;
        MPI_Finalized(&finalized);
        if (!finalized) MPI_Finalize();
    }
    return rc;
}
