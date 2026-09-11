// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors
//
// Feature: distributed-domain-decomposition
// Property 4: Output_Gather reconstructs the global field with no gaps or overlaps
//
// The Standalone_Writer's Output_Gather (src/driver/cece_standalone_writer.cpp,
// WriteTimeStep Step 8) assembles the global NetCDF field from the rank-local,
// BAND-LOCAL export views. Each rank owns a contiguous latitude band
// [band_start(r), band_start(r+1)) of the Global_Grid; its export host view is
// nx x ny_local x nz (LayoutLeft, element (i, jrel, k) at
// i + jrel*nx + k*nx*ny_local). The writer:
//
//   1. packs the band host view into a contiguous per-level send buffer laid
//      out [level][jrel][i]  (element (k, jrel, i) at k*(ny_local*nx) + jrel*nx + i);
//   2. issues one MPI_Gatherv PER LEVEL into a global [level][j][i] buffer on
//      rank 0, using recvcounts row_counts[r]*nx and displs row_displs[r]*nx
//      (plain MPI_DOUBLE);
//   3. MPI_Bcasts the assembled global field so every rank holds it.
//
// This test validates the pack/place LOGIC of that gather — that reassembling
// every rank's band into its global rows reconstructs the original global field
// element-for-element, with every global row written exactly once (no gaps, no
// overlaps), including the surplus-rank (ny_local == 0) case.
//
//   * PURE RapidCheck property (>= 100 iters): over generated (nx, ny, nz, size),
//     synthesize a global [k][j][i] reference field, split it into per-rank
//     bands via the exact block decomposition, pack each band into the
//     [level][jrel][i] send layout, then REPLICATE the gather's placement
//     in-process (place rank r's band into rows [band_start(r), band_start(r+1))
//     of a fresh global buffer using row_counts[r]*nx / row_displs[r]*nx scaled
//     per level) and assert the reconstructed field equals the reference, with a
//     write-count map proving every element is written exactly once.
//   * SMALL multi-rank MPI variant: replicate the actual per-level MPI_Gatherv
//     with row_counts*nx / row_displs*nx into a rank-0 global buffer over
//     MPI_COMM_WORLD and assert it equals the row-placed reference. Runs at
//     np1 (Gatherv degrades to a local copy) and under mpirun -np {2,3}.
//
// The block decomposition mirrors cece::BandDecomposition::compute exactly:
//   band_base    = ny / size
//   band_rem     = ny % size
//   band_start(r)= r*band_base + min(r, band_rem)
// so a surplus rank (size > ny) has band_start(r) == band_start(r+1) == ny and
// contributes a zero-count band.
//
// Own main() with MPI init (MPI-main pattern, mirrors
// tests/test_mpi_reuse_decision.cpp / tests/test_halo_gather_equivalence.cpp):
// does NOT link GTest::gtest_main; all ranks stay alive and run RUN_ALL_TESTS so
// every rank enters the MPI collectives. Grids are tiny (nx,ny,nz small) for the
// ~7 GB container and -np 3 oversubscription.
//
// **Validates: Requirements 6.1, 6.2, 6.3, 6.5**

#include <gtest/gtest.h>
#include <mpi.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "cece/cece_band_decomposition.hpp"

namespace cece {
namespace {

int WorldRank() {
    int rank = 0;
    int inited = 0;
    MPI_Initialized(&inited);
    if (inited) MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    return rank;
}

int WorldSize() {
    int size = 1;
    int inited = 0;
    MPI_Initialized(&inited);
    if (inited) MPI_Comm_size(MPI_COMM_WORLD, &size);
    return size;
}

// The exact block decomposition used by cece::BandDecomposition::compute and the
// production regrid/gather path: rank r owns rows [band_start(r), band_start(r+1)).
int BandStart(int rank, int ny, int size) {
    const int base = ny / size;
    const int rem = ny % size;
    return rank * base + std::min(rank, rem);
}

// Build a BandDecomposition-shaped row_counts/row_displs table for `size` ranks
// over `ny` rows, mirroring BandDecomposition::compute's fields. Surplus ranks
// (size > ny) get row_counts[r] == 0.
struct RowTable {
    std::vector<int> row_counts;
    std::vector<int> row_displs;
};

RowTable MakeRowTable(int ny, int size) {
    RowTable t;
    t.row_counts.resize(static_cast<std::size_t>(size));
    t.row_displs.resize(static_cast<std::size_t>(size));
    for (int r = 0; r < size; ++r) {
        const int j0 = BandStart(r, ny, size);
        const int j1 = BandStart(r + 1, ny, size);
        t.row_counts[static_cast<std::size_t>(r)] = j1 - j0;
        t.row_displs[static_cast<std::size_t>(r)] = j0;
    }
    return t;
}

// Deterministic global reference field value at (k, j, i). Encodes all three
// indices so a misplaced band (wrong row, wrong level, wrong column) is caught.
double RefValue(int k, int j, int i, int nx) {
    return static_cast<double>(k) * 1.0e6 + static_cast<double>(j) * static_cast<double>(nx) + static_cast<double>(i) + 0.5;
}

// Pack a rank's band of the global reference into the writer's per-level send
// layout [level][jrel][i]: element (k, jrel, i) at k*(ny_local*nx) + jrel*nx + i.
// jrel indexes rows within the band; global row is j0 + jrel.
std::vector<double> PackBandSendBuffer(int nx, int nz, int j0, int ny_local) {
    const std::size_t band_level_elems = static_cast<std::size_t>(nx) * ny_local;
    std::vector<double> send_buf(static_cast<std::size_t>(nz) * band_level_elems, 0.0);
    for (int k = 0; k < nz; ++k) {
        for (int jrel = 0; jrel < ny_local; ++jrel) {
            const int j = j0 + jrel;
            for (int i = 0; i < nx; ++i) {
                const std::size_t idx = static_cast<std::size_t>(k) * band_level_elems + static_cast<std::size_t>(jrel) * nx + i;
                send_buf[idx] = RefValue(k, j, i, nx);
            }
        }
    }
    return send_buf;
}

// Broadcast a single int from rank 0 so every rank uses rank-invariant dims
// (production feeds these from rank-invariant config; RapidCheck draws them on
// rank 0 only, since each rank has an independent generator).
int BcastInt(int value) {
    if (WorldSize() > 1) {
        MPI_Bcast(&value, 1, MPI_INT, 0, MPI_COMM_WORLD);
    }
    return value;
}

}  // namespace

// ============================================================================
// Property 4 (PURE): reconstructing every rank's packed band into its global
// rows yields the original global field exactly, with each element written
// exactly once — no gaps, no overlaps. Includes surplus ranks (size > ny ->
// zero-count bands).
//
// Feature: distributed-domain-decomposition, Property 4: Output_Gather
// reconstructs the global field with no gaps or overlaps
// **Validates: Requirements 6.1, 6.2, 6.3, 6.5**
//
// This replicates the writer's placement math in-process: for each rank r we
// pack its band (rows [band_start(r), band_start(r+1))) into the [level][jrel][i]
// send layout, then place it into a fresh global [level][j][i] buffer using the
// SAME per-level scaled counts/displs the gather uses
// (recvcounts = row_counts[r]*nx, displs = row_displs[r]*nx, per level). A
// parallel write-count buffer records how many times each global element is
// written; the property asserts the reconstructed field equals the reference
// AND every element's write count is exactly 1.
// ============================================================================
RC_GTEST_PROP(OutputGatherReconstruction, Property4_PureReassemblyEqualsReference, ()) {
    // Tiny rank-invariant dims. `size` includes values > ny so surplus,
    // zero-count bands are exercised.
    const int nx = *rc::gen::inRange(1, 9);    // 1..8
    const int ny = *rc::gen::inRange(0, 13);   // 0..12 (0 exercises an all-empty grid)
    const int nz = *rc::gen::inRange(1, 5);    // 1..4
    const int size = *rc::gen::inRange(1, 9);  // 1..8 ranks (may exceed ny -> surplus ranks)

    const RowTable table = MakeRowTable(ny, size);

    // The reference global field [k][j][i] and a per-element write-count map.
    const std::size_t global_level_elems = static_cast<std::size_t>(nx) * ny;
    const std::size_t global_total = static_cast<std::size_t>(nz) * global_level_elems;
    std::vector<double> reference(global_total, 0.0);
    for (int k = 0; k < nz; ++k) {
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                reference[static_cast<std::size_t>(k) * global_level_elems + static_cast<std::size_t>(j) * nx + i] = RefValue(k, j, i, nx);
            }
        }
    }

    // Reconstruct by placing every rank's packed band. `write_count` proves the
    // no-gaps/no-overlaps property (each element written exactly once).
    std::vector<double> reconstructed(global_total, 0.0);
    std::vector<int> write_count(global_total, 0);

    for (int r = 0; r < size; ++r) {
        const int j0 = table.row_displs[static_cast<std::size_t>(r)];
        const int ny_local = table.row_counts[static_cast<std::size_t>(r)];
        // Surplus rank: zero-count band contributes nothing (Req 6.5).
        const std::vector<double> send_buf = PackBandSendBuffer(nx, nz, j0, ny_local);

        // Per-level scaled counts/displs, exactly as the writer's Output_Gather:
        // recvcount = row_counts[r]*nx, displ = row_displs[r]*nx (Req 6.3).
        const int recvcount = ny_local * nx;
        const int recvdispl = j0 * nx;
        const std::size_t band_level_elems = static_cast<std::size_t>(nx) * ny_local;

        for (int k = 0; k < nz; ++k) {
            const double* level_send = send_buf.data() + static_cast<std::size_t>(k) * band_level_elems;
            double* level_recv = reconstructed.data() + static_cast<std::size_t>(k) * global_level_elems;
            int* level_wc = write_count.data() + static_cast<std::size_t>(k) * global_level_elems;
            for (int e = 0; e < recvcount; ++e) {
                level_recv[recvdispl + e] = level_send[e];
                level_wc[recvdispl + e] += 1;
            }
        }
    }

    // No gaps / no overlaps: every element written exactly once.
    for (std::size_t idx = 0; idx < global_total; ++idx) {
        RC_ASSERT(write_count[idx] == 1);
    }
    // Element-for-element equality with the reference (Req 6.1, 6.2, 6.3).
    RC_ASSERT(reconstructed.size() == reference.size());
    for (std::size_t idx = 0; idx < global_total; ++idx) {
        RC_ASSERT(reconstructed[idx] == reference[idx]);
    }
}

// ============================================================================
// Property 4 (MPI): the ACTUAL per-level MPI_Gatherv assembly reconstructs the
// global field. Each rank packs its own band of the shared global reference
// into the [level][jrel][i] send layout and issues nz MPI_Gatherv calls into a
// rank-0 global [level][j][i] buffer, using recvcounts row_counts[r]*nx and
// displs row_displs[r]*nx — exactly the writer's Output_Gather. Rank 0 asserts
// the assembled field equals the row-placed reference.
//
// Feature: distributed-domain-decomposition, Property 4: Output_Gather
// reconstructs the global field with no gaps or overlaps
// **Validates: Requirements 6.1, 6.2, 6.3, 6.5**
//
// Runs at np1 (Gatherv degrades to a single local copy of this rank's whole-grid
// band) and under mpirun -np {2,3}. Surplus ranks (size > ny) send a zero-count
// band (Req 6.5). Every rank issues exactly nz Gatherv calls in the same order,
// so the collective sequence is identical across ranks (Req 6.6-adjacent).
// ============================================================================
RC_GTEST_PROP(OutputGatherReconstruction, Property4_MpiGathervEqualsReference, ()) {
    const int size = WorldSize();
    const int rank = WorldRank();

    // Rank-invariant tiny dims. Ensure ny >= 0; allow ny < size so a surplus,
    // zero-count band occurs when size > ny.
    const int nx = BcastInt(*rc::gen::inRange(1, 7));   // 1..6
    const int ny = BcastInt(*rc::gen::inRange(0, 11));  // 0..10
    const int nz = BcastInt(*rc::gen::inRange(1, 4));   // 1..3

    const RowTable table = MakeRowTable(ny, size);
    const int j0 = table.row_displs[static_cast<std::size_t>(rank)];
    const int ny_local = table.row_counts[static_cast<std::size_t>(rank)];

    // This rank's packed [level][jrel][i] send buffer over the shared reference.
    const std::vector<double> send_buf = PackBandSendBuffer(nx, nz, j0, ny_local);
    const std::size_t band_level_elems = static_cast<std::size_t>(nx) * ny_local;
    const int sendcount = static_cast<int>(band_level_elems);

    // Per-level recvcounts/displs on rank 0 (Req 6.3), scaled by nx.
    std::vector<int> recvcounts;
    std::vector<int> recvdispls;
    if (rank == 0) {
        recvcounts.resize(static_cast<std::size_t>(size));
        recvdispls.resize(static_cast<std::size_t>(size));
        for (int r = 0; r < size; ++r) {
            recvcounts[static_cast<std::size_t>(r)] = table.row_counts[static_cast<std::size_t>(r)] * nx;
            recvdispls[static_cast<std::size_t>(r)] = table.row_displs[static_cast<std::size_t>(r)] * nx;
        }
    }

    const std::size_t global_level_elems = static_cast<std::size_t>(nx) * ny;
    std::vector<double> global_field;
    if (rank == 0) {
        global_field.assign(static_cast<std::size_t>(nz) * global_level_elems, 0.0);
    }

    // One MPI_Gatherv PER LEVEL, exactly mirroring the writer. Every rank issues
    // nz calls (surplus ranks with ny_local == 0 send sendcount 0).
    for (int k = 0; k < nz; ++k) {
        const double* level_send = send_buf.data() + static_cast<std::size_t>(k) * band_level_elems;
        double* level_recv = (rank == 0) ? (global_field.data() + static_cast<std::size_t>(k) * global_level_elems) : nullptr;
        const int gather_rc = MPI_Gatherv(level_send, sendcount, MPI_DOUBLE, level_recv, (rank == 0) ? recvcounts.data() : nullptr,
                                          (rank == 0) ? recvdispls.data() : nullptr, MPI_DOUBLE, 0, MPI_COMM_WORLD);
        RC_ASSERT(gather_rc == MPI_SUCCESS);
    }

    // Rank 0 holds the authoritative assembled field: compare against the
    // row-placed reference element-for-element.
    if (rank == 0) {
        for (int k = 0; k < nz; ++k) {
            for (int j = 0; j < ny; ++j) {
                for (int i = 0; i < nx; ++i) {
                    const std::size_t idx = static_cast<std::size_t>(k) * global_level_elems + static_cast<std::size_t>(j) * nx + i;
                    RC_ASSERT(global_field[idx] == RefValue(k, j, i, nx));
                }
            }
        }
    }
}

}  // namespace cece

// ---------------------------------------------------------------------------
// Own main() with MPI init (MPI-main pattern, mirrors
// tests/test_mpi_reuse_decision.cpp / tests/test_halo_gather_equivalence.cpp):
// ALL ranks stay alive and run RUN_ALL_TESTS so every rank enters the MPI
// collectives the MPI property issues. Kokkos is not needed (pure host/MPI
// logic). Slurm/PMI env is scrubbed so a plain mpirun -np N works in the
// container without a batch scheduler.
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
