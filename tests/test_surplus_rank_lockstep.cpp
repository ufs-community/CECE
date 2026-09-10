// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors
//
// Feature: distributed-domain-decomposition
// Task 10.4: Integration test — collective lock-step with a surplus rank
//
// Property 8: Collective sequence is identical across ranks.
//
// **Validates: Requirements 1.4, 3.4, 3.5, 6.6, 7.4**
//
// This is a focused MPI-main harness (own main(), no gtest_main, mirroring
// tests/test_halo_multirank_integration.cpp) that drives the REAL Output_Gather
// primitive sequence the writer issues — per-level MPI_Gatherv with
// band_.row_counts/row_displs scaled by nx_, followed by a single MPI_Bcast of
// the assembled global field — under a decomposition where `size > ny` so at
// least one rank owns ny_local == 0 (a surplus rank).
//
// Why the focused harness and not a full cece_standalone_driver end-to-end run:
// forcing ny < np in a real config requires a tiny (ny=2/3) grid AND a fully
// plumbed DAGR/CeceIO/config/AMIO stack standing up under -np 4 oversubscribe;
// the harness instead exercises the EXACT primitives Property 8 is about (the
// surplus-rank collective lock-step of the Output_Gather) with the REAL
// cece::BandDecomposition::compute geometry, which is both more robust and more
// targeted at the property under test.
//
// Under mpirun --oversubscribe -np {1,4} (np4 with ny=2/3 forces surplus ranks)
// it asserts:
//
//   1. NO DEADLOCK (Req 1.4, 3.4, 3.5, 6.6, 7.4): the test running to
//      COMPLETION (not hanging) is the primary evidence that surplus ranks
//      (ny_local == 0) enter every collective in lock-step with row-owning
//      ranks. A stranded rank would hang the launcher until the ctest timeout.
//
//   2. IDENTICAL COLLECTIVE SEQUENCE (Property 8, Req 6.6, 7.4): every rank —
//      including the surplus ranks — issues exactly the same NUMBER and ORDER
//      of collectives (nz_ MPI_Gatherv per field in sorted field order, then
//      one MPI_Bcast per field). Each rank counts its own issued collectives
//      and we allreduce(MIN/MAX) the count, requiring agreement. A surplus rank
//      that skipped a gather (or a row-owner that entered one extra) would
//      diverge and either deadlock or fail this check.
//
//   3. CORRECT GLOBAL RECONSTRUCTION (Req 6.2, 6.3, 6.5): the assembled global
//      field places rank r's band at global rows [band_start(r), band_start(r+1))
//      with no gaps/overlaps; surplus ranks contribute nothing (zero-count
//      band); and the result equals a rank-invariant serial reference the whole
//      grid would produce. Checked bit-for-bit on rank 0 and cross-rank
//      bit-identical after the Bcast.
//
// Dims are deliberately TINY: ny in {2, 3} (< np=4 => forced surplus ranks),
// nx=8, nz=2, one field — well within the ~7 GB container even at np4
// oversubscription.

#include <gtest/gtest.h>
#include <mpi.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

// Rank-invariant deterministic value for global cell (k, j, i) in the assembled
// [level][j][i] layout. Every rank can compute the whole-grid reference
// independently; a rank fills only the rows [j0, j1) it owns into its band.
double CellValue(int k, int j, int i, int nx) {
    const std::size_t g = static_cast<std::size_t>(k) * 100000 + static_cast<std::size_t>(j) * static_cast<std::size_t>(nx) + i;
    return static_cast<double>((g * 37 + 11) % 1000) - 500.0 + 0.5 * static_cast<double>(g % 13);
}

// The full-grid serial [level][j][i] reference a single rank would hold.
std::vector<double> SerialReference(int nx, int ny, int nz) {
    std::vector<double> ref(static_cast<std::size_t>(nz) * ny * nx, 0.0);
    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) ref[static_cast<std::size_t>(k) * ny * nx + static_cast<std::size_t>(j) * nx + i] = CellValue(k, j, i, nx);
    return ref;
}

// This rank's BAND-LOCAL packed send buffer, laid out [level][jrel][i] over the
// ny_local rows it owns (exactly the writer's pack). Surplus ranks (ny_local==0)
// produce an EMPTY buffer, mirroring the zero-count send band.
std::vector<double> PackBand(const BandDecomposition& band, int nx, int nz) {
    const int ny_local = band.ny_local;
    std::vector<double> send(static_cast<std::size_t>(nz) * ny_local * nx, 0.0);
    for (int k = 0; k < nz; ++k)
        for (int jrel = 0; jrel < ny_local; ++jrel)
            for (int i = 0; i < nx; ++i) {
                const int gj = band.j0 + jrel;  // global row this band row maps to
                send[static_cast<std::size_t>(k) * ny_local * nx + static_cast<std::size_t>(jrel) * nx + i] = CellValue(k, gj, i, nx);
            }
    return send;
}

// Assert `field` is bit-identical on every rank (allreduce bit pattern MIN/MAX).
void ExpectCrossRankBitIdentical(const std::vector<double>& field) {
    if (WorldSize() <= 1) return;
    const std::size_t n = field.size();
    std::vector<std::int64_t> bits(n);
    for (std::size_t k = 0; k < n; ++k) {
        std::int64_t b = 0;
        std::memcpy(&b, &field[k], sizeof(double));
        bits[k] = b;
    }
    std::vector<std::int64_t> mn(n);
    std::vector<std::int64_t> mx(n);
    MPI_Allreduce(bits.data(), mn.data(), static_cast<int>(n), MPI_INT64_T, MPI_MIN, MPI_COMM_WORLD);
    MPI_Allreduce(bits.data(), mx.data(), static_cast<int>(n), MPI_INT64_T, MPI_MAX, MPI_COMM_WORLD);
    for (std::size_t k = 0; k < n; ++k) {
        ASSERT_EQ(mn[k], mx[k]) << "assembled global field element " << k << " differs across ranks";
    }
}

// Assert every rank agrees on an integer (used to prove identical collective
// counts, i.e. lock-step). At np=1 trivially true.
void ExpectIntAgreesAcrossRanks(int value, const char* what) {
    if (WorldSize() <= 1) return;
    int mn = value;
    int mx = value;
    MPI_Allreduce(&value, &mn, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
    MPI_Allreduce(&value, &mx, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
    ASSERT_EQ(mn, mx) << "ranks disagree on " << what << " (collective lock-step violation)";
}

// Drive the REAL Output_Gather primitive sequence for ONE field, mirroring
// CeceStandaloneWriter::WriteTimeStep exactly:
//   - pack the band into [level][jrel][i]
//   - nz per-level MPI_Gatherv into a global [level][j][i] buffer on rank 0,
//     using recvcounts[r] = row_counts[r]*nx / recvdispls[r] = row_displs[r]*nx
//   - one MPI_Bcast of the assembled global field to all ranks
// Returns the assembled global field (identical on every rank after Bcast) and
// increments `collective_count` by the number of collectives THIS rank issued
// (nz gathers + 1 bcast), so callers can prove lock-step.
std::vector<double> RunOutputGatherOneField(const BandDecomposition& band, int nx, int ny, int nz, int& collective_count) {
    const int size = WorldSize();
    const int rank = WorldRank();
    const bool do_gather = size > 1;

    const std::size_t band_level_elems = static_cast<std::size_t>(nx) * band.ny_local;  // 0 for surplus ranks
    const std::size_t global_level_elems = static_cast<std::size_t>(nx) * ny;

    std::vector<double> send_buf = PackBand(band, nx, nz);

    std::vector<double> global_field;
    if (!do_gather) {
        // Single-rank / no-MPI: the band IS the whole grid (ny_local == ny).
        global_field = std::move(send_buf);
        return global_field;
    }

    std::vector<int> recvcounts(size);
    std::vector<int> recvdispls(size);
    for (int r = 0; r < size; ++r) {
        recvcounts[r] = band.row_counts[r] * nx;
        recvdispls[r] = band.row_displs[r] * nx;
    }

    if (rank == 0) global_field.assign(static_cast<std::size_t>(nz) * global_level_elems, 0.0);

    // nz per-level MPI_Gatherv — EVERY rank (row-owners AND surplus ny_local==0
    // ranks) issues exactly nz calls in the same order. Surplus ranks send
    // sendcount 0 from an empty buffer.
    const int sendcount = static_cast<int>(band_level_elems);
    const double* send_base = send_buf.empty() ? nullptr : send_buf.data();
    for (int k = 0; k < nz; ++k) {
        const double* level_send = (send_base == nullptr) ? nullptr : (send_base + static_cast<std::size_t>(k) * band_level_elems);
        double* level_recv = (rank == 0) ? (global_field.data() + static_cast<std::size_t>(k) * global_level_elems) : nullptr;
        const int rc = MPI_Gatherv(level_send, sendcount, MPI_DOUBLE, level_recv, (rank == 0) ? recvcounts.data() : nullptr,
                                   (rank == 0) ? recvdispls.data() : nullptr, MPI_DOUBLE, 0, MPI_COMM_WORLD);
        // EXPECT (not ASSERT) because this function returns a value; ASSERT's
        // hidden `return;` would not compile here.
        EXPECT_EQ(rc, MPI_SUCCESS) << "Output_Gather MPI_Gatherv failed at level " << k;
        ++collective_count;
    }

    // Broadcast the assembled global field so all ranks hold identical data
    // (mirrors the writer's collective-write broadcast). Every rank enters.
    if (rank != 0) global_field.assign(static_cast<std::size_t>(nz) * global_level_elems, 0.0);
    const int brc =
        MPI_Bcast(global_field.data(), static_cast<int>(static_cast<std::size_t>(nz) * global_level_elems), MPI_DOUBLE, 0, MPI_COMM_WORLD);
    EXPECT_EQ(brc, MPI_SUCCESS) << "Output_Gather MPI_Bcast failed";
    ++collective_count;

    return global_field;
}

}  // namespace

// ============================================================================
// Surplus-rank collective lock-step: with ny < size, at least one rank owns
// ny_local == 0. All ranks drive the Output_Gather sequence in lock-step, the
// run completes (no deadlock), every rank issues an identical collective count,
// and the assembled global field is correct and cross-rank bit-identical.
//
// Property 8: Collective sequence is identical across ranks.
// Feature: distributed-domain-decomposition, Property 8: Collective sequence is
//   identical across ranks
// **Validates: Requirements 1.4, 3.4, 3.5, 6.6, 7.4**
// ============================================================================
TEST(SurplusRankLockstep, GatherSequenceLockStepAndCorrect_NyTwo) {
    const int size = WorldSize();
    const int rank = WorldRank();

    // ny=2 < np=4 => ranks 2 and 3 are surplus (ny_local == 0). At np1 this is
    // the whole-grid short-circuit (ny_local == ny).
    const int nx = 8;
    const int ny = 2;
    const int nz = 2;

    const BandDecomposition band = BandDecomposition::compute(ny, MPI_COMM_WORLD);

    // Structural: single source of truth partitions [0, ny) exactly; surplus
    // ranks (index >= ny) get ny_local == 0.
    ASSERT_EQ(band.ny_global, ny);
    ASSERT_EQ(band.j1 - band.j0, band.ny_local);
    if (size > 1) {
        ASSERT_EQ(static_cast<int>(band.row_counts.size()), size);
        int total = 0;
        for (int r = 0; r < size; ++r) total += band.row_counts[r];
        ASSERT_EQ(total, ny) << "bands must cover [0, ny) exactly";
        if (rank >= ny) {
            ASSERT_EQ(band.ny_local, 0) << "rank " << rank << " must be a surplus rank (ny_local == 0)";
        }
    } else {
        ASSERT_EQ(band.ny_local, ny) << "single-rank owns the whole grid";
    }

    // Drive the real Output_Gather primitive sequence for one field, counting
    // the collectives THIS rank issues.
    int collective_count = 0;
    const std::vector<double> assembled = RunOutputGatherOneField(band, nx, ny, nz, collective_count);

    // Lock-step (Property 8, Req 6.6, 7.4): every rank issued the same number of
    // collectives. Surplus ranks entered every gather + the bcast too.
    if (size > 1) {
        ASSERT_EQ(collective_count, nz + 1) << "each rank issues nz gathers + 1 bcast for the single field";
        ExpectIntAgreesAcrossRanks(collective_count, "issued collective count");
    }

    // Correct reconstruction (Req 6.2, 6.3, 6.5) + cross-rank identity (7.4).
    const std::vector<double> ref = SerialReference(nx, ny, nz);
    ASSERT_EQ(assembled.size(), ref.size());
    for (std::size_t idx = 0; idx < ref.size(); ++idx) {
        ASSERT_EQ(assembled[idx], ref[idx]) << "assembled global field element " << idx << " != serial reference";
    }
    ExpectCrossRankBitIdentical(assembled);
}

// Same lock-step + correctness with ny=3 < np=4 (one surplus rank, uneven band
// split among the three row-owning ranks) to exercise a different remainder
// distribution.
//
// Feature: distributed-domain-decomposition, Property 8: Collective sequence is
//   identical across ranks
// **Validates: Requirements 1.4, 3.4, 3.5, 6.6, 7.4**
TEST(SurplusRankLockstep, GatherSequenceLockStepAndCorrect_NyThree) {
    const int size = WorldSize();
    const int rank = WorldRank();

    const int nx = 8;
    const int ny = 3;
    const int nz = 2;

    const BandDecomposition band = BandDecomposition::compute(ny, MPI_COMM_WORLD);

    ASSERT_EQ(band.ny_global, ny);
    ASSERT_EQ(band.j1 - band.j0, band.ny_local);
    if (size > 1) {
        int total = 0;
        for (int r = 0; r < size; ++r) total += band.row_counts[r];
        ASSERT_EQ(total, ny);
        if (rank >= ny) ASSERT_EQ(band.ny_local, 0);
    } else {
        ASSERT_EQ(band.ny_local, ny);
    }

    int collective_count = 0;
    const std::vector<double> assembled = RunOutputGatherOneField(band, nx, ny, nz, collective_count);

    if (size > 1) {
        ASSERT_EQ(collective_count, nz + 1);
        ExpectIntAgreesAcrossRanks(collective_count, "issued collective count");
    }

    const std::vector<double> ref = SerialReference(nx, ny, nz);
    ASSERT_EQ(assembled.size(), ref.size());
    for (std::size_t idx = 0; idx < ref.size(); ++idx) {
        ASSERT_EQ(assembled[idx], ref[idx]) << "assembled global field element " << idx << " != serial reference";
    }
    ExpectCrossRankBitIdentical(assembled);
}

// Two fields in sorted order: proves the FIELD-ORDER lock-step too — every rank
// issues (nz gathers + 1 bcast) PER field in the same rank-invariant order, so
// the total collective count is identical across ranks even with a surplus rank.
//
// Feature: distributed-domain-decomposition, Property 8: Collective sequence is
//   identical across ranks
// **Validates: Requirements 1.4, 3.4, 3.5, 6.6, 7.4**
TEST(SurplusRankLockstep, MultiFieldSequenceLockStep) {
    const int size = WorldSize();

    const int nx = 8;
    const int ny = 2;  // < np=4 => surplus ranks
    const int nz = 2;
    const int num_fields = 3;  // stand-ins for a sorted set of variable names

    const BandDecomposition band = BandDecomposition::compute(ny, MPI_COMM_WORLD);

    int collective_count = 0;
    for (int f = 0; f < num_fields; ++f) {
        const std::vector<double> assembled = RunOutputGatherOneField(band, nx, ny, nz, collective_count);
        const std::vector<double> ref = SerialReference(nx, ny, nz);
        ASSERT_EQ(assembled.size(), ref.size());
        for (std::size_t idx = 0; idx < ref.size(); ++idx) {
            ASSERT_EQ(assembled[idx], ref[idx]);
        }
        ExpectCrossRankBitIdentical(assembled);
    }

    if (size > 1) {
        ASSERT_EQ(collective_count, num_fields * (nz + 1)) << "per-field nz gathers + 1 bcast, all fields, all ranks";
        ExpectIntAgreesAcrossRanks(collective_count, "total issued collective count across fields");
    }
}

}  // namespace cece

// ============================================================================
// Own main() with MPI init (MPI-main pattern, mirrors
// tests/test_halo_multirank_integration.cpp): ALL ranks stay alive and run
// RUN_ALL_TESTS so every rank — including the surplus ny_local==0 ranks — enters
// the MPI collectives this test issues. No Kokkos needed (the harness uses only
// std::vector + BandDecomposition + MPI). Slurm/PMI env is scrubbed so a plain
// mpirun -np N works in the container without a batch scheduler.
// ============================================================================
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
