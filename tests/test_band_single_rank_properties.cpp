// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors
//
// Feature: distributed-domain-decomposition
// Property 2: Single-rank band is the whole grid
//
// For any destination latitude count `ny >= 0`, resolving the band
// decomposition on a single participant — whether via the pure
// `whole_grid(ny)` factory, via `compute(ny, comm)` on a size-1 communicator
// (MPI_COMM_SELF), via `compute(ny, MPI_COMM_NULL)`, or via `compute(ny, comm)`
// while MPI is uninitialized — SHALL yield the whole grid on that rank:
//
//     j0 == 0, j1 == ny, ny_local == ny.
//
// This is the single-rank / degenerate-MPI short-circuit that keeps a
// single-rank run owning the entire Global_Grid and lets the system degrade to
// the existing single-rank behavior when MPI is unavailable/degenerate.
//
// The property is exercised over generated `ny` with RapidCheck (>= 100 iters)
// plus explicit edge-case unit tests (ny == 0, MPI_COMM_NULL). The
// "MPI uninitialized" short-circuit is validated by a deterministic test that
// runs BEFORE MPI_Init when the binary is launched as a single process (the
// ctest default); once MPI_Init has run it is not re-reachable, so under a
// launcher that case is covered structurally by the NULL / size-1 discipline.
//
// The test file is intentionally self-contained (its own translation unit,
// its own CMake target, its own main()) so it never touches the Property 1
// test file (task 2.1) or its target.
//
// Validates: Requirements 1.3, 8.5

#include <gtest/gtest.h>
#include <mpi.h>

#include <cstdlib>
#include <string>

#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "cece/cece_band_decomposition.hpp"

namespace cece {
namespace {

// Assert that `band` describes the whole grid on a single participant:
// j0 == 0, j1 == ny, ny_local == ny.
void ExpectWholeGrid(const BandDecomposition& band, int ny) {
    RC_ASSERT(band.ny_global == ny);
    RC_ASSERT(band.j0 == 0);
    RC_ASSERT(band.j1 == ny);
    RC_ASSERT(band.ny_local == ny);
}

// Generate a non-negative destination latitude count. Spans the trivial and
// small-grid regime up through a modest global grid so the property covers the
// realistic `ny` space without allocating large vectors.
rc::Gen<int> genNy() { return rc::gen::inRange(0, 5000); }

}  // namespace

// ============================================================================
// Property 2a: whole_grid(ny) is the whole grid
// Feature: distributed-domain-decomposition, Property 2: Single-rank band is
// the whole grid
// **Validates: Requirements 1.3, 8.5**
//
// The pure `whole_grid(ny)` factory always yields j0 == 0, j1 == ny,
// ny_local == ny, with a single row-count/displacement slot covering [0, ny).
// ============================================================================
RC_GTEST_PROP(BandSingleRankProperty, Property2_WholeGridIsWholeGrid, ()) {
    const int ny = *genNy();
    const BandDecomposition band = BandDecomposition::whole_grid(ny);
    ExpectWholeGrid(band, ny);

    // The single participant owns every row in one contiguous slot.
    RC_ASSERT(band.row_counts.size() == 1u);
    RC_ASSERT(band.row_displs.size() == 1u);
    RC_ASSERT(band.row_counts[0] == ny);
    RC_ASSERT(band.row_displs[0] == 0);
}

// ============================================================================
// Property 2b: compute(ny, MPI_COMM_SELF) is the whole grid (size-1 comm)
// Feature: distributed-domain-decomposition, Property 2: Single-rank band is
// the whole grid
// **Validates: Requirements 1.3, 8.5**
//
// A size-1 communicator short-circuits to the whole grid with NO collective,
// matching the single-rank path: j0 == 0, j1 == ny, ny_local == ny.
// ============================================================================
RC_GTEST_PROP(BandSingleRankProperty, Property2_ComputeSelfIsWholeGrid, ()) {
    int mpi_initialized = 0;
    MPI_Initialized(&mpi_initialized);
    RC_PRE(mpi_initialized != 0);  // MPI_COMM_SELF requires an initialized MPI

    const int ny = *genNy();
    const BandDecomposition band = BandDecomposition::compute(ny, MPI_COMM_SELF);
    ExpectWholeGrid(band, ny);
}

// ============================================================================
// Property 2c: compute(ny, MPI_COMM_NULL) is the whole grid
// Feature: distributed-domain-decomposition, Property 2: Single-rank band is
// the whole grid
// **Validates: Requirements 1.3, 8.5**
//
// A null communicator degrades to the single-rank behavior with no collective.
// ============================================================================
RC_GTEST_PROP(BandSingleRankProperty, Property2_ComputeNullCommIsWholeGrid, ()) {
    const int ny = *genNy();
    const BandDecomposition band = BandDecomposition::compute(ny, MPI_COMM_NULL);
    ExpectWholeGrid(band, ny);
}

// ============================================================================
// Edge case: ny == 0 (empty grid) via every single-rank entry point.
// Feature: distributed-domain-decomposition, Property 2: Single-rank band is
// the whole grid
// **Validates: Requirements 1.3, 8.5**
// ============================================================================
TEST(BandSingleRankEdge, EmptyGridIsWholeGrid) {
    const int ny = 0;

    const BandDecomposition whole = BandDecomposition::whole_grid(ny);
    EXPECT_EQ(whole.ny_global, 0);
    EXPECT_EQ(whole.j0, 0);
    EXPECT_EQ(whole.j1, 0);
    EXPECT_EQ(whole.ny_local, 0);
    EXPECT_EQ(whole.row_counts.size(), 1u);
    EXPECT_EQ(whole.row_displs.size(), 1u);
    EXPECT_EQ(whole.row_counts[0], 0);
    EXPECT_EQ(whole.row_displs[0], 0);

    const BandDecomposition null_band = BandDecomposition::compute(ny, MPI_COMM_NULL);
    EXPECT_EQ(null_band.j0, 0);
    EXPECT_EQ(null_band.j1, 0);
    EXPECT_EQ(null_band.ny_local, 0);

    int mpi_initialized = 0;
    MPI_Initialized(&mpi_initialized);
    if (mpi_initialized) {
        const BandDecomposition self_band = BandDecomposition::compute(ny, MPI_COMM_SELF);
        EXPECT_EQ(self_band.j0, 0);
        EXPECT_EQ(self_band.j1, 0);
        EXPECT_EQ(self_band.ny_local, 0);
    }
}

// ============================================================================
// Edge case: MPI_COMM_NULL yields the whole grid for a representative ny.
// Feature: distributed-domain-decomposition, Property 2: Single-rank band is
// the whole grid
// **Validates: Requirements 1.3, 8.5**
// ============================================================================
TEST(BandSingleRankEdge, NullCommIsWholeGrid) {
    const int ny = 1440;
    const BandDecomposition band = BandDecomposition::compute(ny, MPI_COMM_NULL);
    EXPECT_EQ(band.ny_global, ny);
    EXPECT_EQ(band.j0, 0);
    EXPECT_EQ(band.j1, ny);
    EXPECT_EQ(band.ny_local, ny);
}

}  // namespace cece

// ---------------------------------------------------------------------------
// Own main() with MPI init, mirroring tests/test_collective_gate_equivalence.cpp.
//
// The "MPI uninitialized" short-circuit (Req 8.5) is validated deterministically
// BEFORE MPI_Init, but only when the binary runs as a single process (the ctest
// default). Under a launcher (--gtest_list_tests discovery or a real mpirun),
// we skip that pre-init check: probing/aborting it there is neither meaningful
// nor safe. After the (optional) pre-init check, MPI is initialized so the
// MPI_COMM_SELF property (Property 2b) can run. All ranks run RUN_ALL_TESTS.
//
// Slurm/PMI env is scrubbed so a plain `mpirun -np N` works in the container
// without a batch scheduler.
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    bool is_discovery = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--gtest_list_tests") {
            is_discovery = true;
            break;
        }
    }

    // Validate the MPI-uninitialized short-circuit (Req 8.5) while MPI is
    // genuinely uninitialized. Reachable only in the single-process discovery-
    // free path; harmless to skip otherwise.
    if (!is_discovery) {
        int already = 0;
        MPI_Initialized(&already);
        if (!already) {
            const cece::BandDecomposition band = cece::BandDecomposition::compute(720, MPI_COMM_WORLD);
            if (!(band.j0 == 0 && band.j1 == 720 && band.ny_local == 720)) {
                std::fprintf(stderr,
                             "FATAL: compute() before MPI_Init did not short-circuit to whole_grid "
                             "(j0=%d j1=%d ny_local=%d)\n",
                             band.j0, band.j1, band.ny_local);
                return 1;
            }
        }

        unsetenv("SLURM_JOB_ID");
        unsetenv("SLURM_STEP_ID");
        unsetenv("PMI_RANK");
        unsetenv("PMI_SIZE");

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
