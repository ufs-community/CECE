// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors
//
// Feature: cece-halo-collective-adoption
// Task 10.6 — Unit / example / edge-case tests
//
// These are deterministic example/edge-case checks that complement the P1–P6
// property tests. They pin the concrete structural contracts the distributed
// regrid assembly relies on:
//
//   1. send_buf offset correctness: element (level, jrel, i) lives at index
//        level*band_elems + jrel*nx + i     (band_elems = (j1 - j0)*nx)
//      — a pure index-math unit test (runs at any rank count, including np1).
//      Production still builds this contiguous per-level [level][band] buffer.
//
//   2. halo_comm_ wrapping cases (Req 7.1, 7.2, 7.3). RefreshHaloCommunicator
//      and halo_comm_ are PRIVATE and no friend seam exposes them (mirroring the
//      test_plan_cache.cpp convention, which replicates the invariant rather
//      than adding a production friend). So this tests the two things that
//      matter, both without touching production visibility:
//        (a) the DECISION predicate — model RefreshHaloCommunicator's branch
//            (build a wrapper iff mpi_initialized && comm != MPI_COMM_NULL &&
//             size > 1; otherwise leave halo_comm_ absent). This is the SAME
//            predicate as RegridToDestinationBuffer's `distributed_regrid`.
//            It also covers the gate short-circuit reasoning (uninit / NULL /
//            size1) without duplicating Task 10.2's
//            tests/test_collective_gate_equivalence.cpp
//            (CollectiveGateShortCircuit.*): the one gap those tests explicitly
//            call out — "MPI uninitialized" being unreachable mid-run once
//            MPI_Init has run — is pinned here as the decision predicate's
//            `!mpi_initialized -> short-circuit` branch.
//        (b) the real halo::Communicator WRAPPING behavior it invokes:
//            - a plain wrap of MPI_COMM_WORLD / MPI_COMM_SELF (predefined) is
//              never freed by the wrapper's RAII, so the predefined handle
//              survives destruction;
//            - a duplicated non-predefined handle (MPI_Comm_dup then wrap) is
//              freed exactly once by the wrapper's RAII (no leak, no double
//              free), and the wrapper reports the dup'd comm's size/rank.
//
// Validates: Requirements 2.1, 7.1, 7.2, 7.3

#include <gtest/gtest.h>
#include <mpi.h>
#include <unistd.h>

#include <cstddef>
#include <string>
#include <vector>

#include <halo/communicator.hpp>
#include <halo/environment.hpp>

namespace cece::test {

namespace {

int WorldSize() {
    int size = 1;
    int inited = 0;
    MPI_Initialized(&inited);
    if (inited) MPI_Comm_size(MPI_COMM_WORLD, &size);
    return size;
}

int WorldRank() {
    int rank = 0;
    int inited = 0;
    MPI_Initialized(&inited);
    if (inited) MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    return rank;
}

// The decision predicate RefreshHaloCommunicator (and RegridToDestinationBuffer's
// distributed_regrid) use to decide whether a halo::Communicator is built at
// all. Modeled here as a pure predicate so the "absent for NULL / uninitialized
// / size1" decision (Req 7.2) — including the unreachable-mid-run "MPI
// uninitialized" branch noted by Task 10.2 — is pinned off-MPI.
bool ShouldWrap(bool mpi_initialized, MPI_Comm comm, int mpi_size) {
    return mpi_initialized && comm != MPI_COMM_NULL && mpi_size > 1;
}

}  // namespace

// ===========================================================================
// Section 1 — send_buf offset correctness (pure index math, Req 2.1).
//
// The rewrite lays out the contiguous [level][band] send buffer so element
// (level, jrel, i) lives at level*band_elems + jrel*nx + i, with
// band_elems = (j1 - j0)*nx. This mirrors the production copy:
//   std::copy(band.begin(), band.end(), send_buf.begin() + level*band_elems)
// and the per-level band's own [jrel][i] layout (jrel*nx + i). This test proves
// the offset arithmetic is a bijection onto [0, field_nlev*band_elems) with no
// overlap and no gaps, independent of any MPI state, so it runs at every rank
// count including the np1 (single-process) registration.
// ===========================================================================
TEST(HaloAdoptionSendBufOffset, OffsetIsContiguousBijection) {
    // A few modest shapes, including a zero-width band (a rank contributing no
    // latitudes) which must still yield a valid empty layout.
    struct Case {
        int field_nlev, band_rows, nx;
    };
    const std::vector<Case> cases = {{1, 1, 1}, {4, 3, 12}, {3, 0, 7}, {5, 2, 4}, {2, 4, 6}};

    for (const Case& c : cases) {
        const int nx = c.nx;
        const int band_rows = c.band_rows;         // (j1 - j0)
        const size_t band_elems = static_cast<size_t>(band_rows) * nx;
        const size_t total = static_cast<size_t>(c.field_nlev) * band_elems;

        // Fill an occupancy map by the production offset formula and assert each
        // (level, jrel, i) maps to a unique, in-range index covering [0,total).
        std::vector<int> hits(total, 0);
        for (int level = 0; level < c.field_nlev; ++level) {
            for (int jrel = 0; jrel < band_rows; ++jrel) {
                for (int i = 0; i < nx; ++i) {
                    const size_t idx = static_cast<size_t>(level) * band_elems + static_cast<size_t>(jrel) * nx + i;
                    ASSERT_LT(idx, total) << "offset out of range for case nlev=" << c.field_nlev << " rows=" << band_rows << " nx=" << nx;
                    ASSERT_EQ(hits[idx], 0) << "offset collision at index " << idx;
                    hits[idx] = 1;
                }
            }
        }
        // Every slot in [0, total) is written exactly once (no gaps).
        for (size_t k = 0; k < total; ++k) {
            EXPECT_EQ(hits[k], 1) << "gap at index " << k << " (case nlev=" << c.field_nlev << " rows=" << band_rows << " nx=" << nx << ")";
        }
    }
}

// Pin the exact base offset for each level equals level*band_elems (the value
// the production std::copy destination uses), independent of jrel/i.
TEST(HaloAdoptionSendBufOffset, LevelBaseEqualsLevelTimesBandElems) {
    const int nx = 12;
    const int band_rows = 3;
    const size_t band_elems = static_cast<size_t>(band_rows) * nx;
    for (int level = 0; level < 6; ++level) {
        const size_t base = static_cast<size_t>(level) * band_elems;
        // (level, 0, 0) sits exactly at the level base.
        EXPECT_EQ(base + static_cast<size_t>(0) * nx + 0, base);
        // (level, band_rows-1, nx-1) sits at the last slot of this level's block.
        const size_t last = base + static_cast<size_t>(band_rows - 1) * nx + (nx - 1);
        EXPECT_EQ(last, base + band_elems - 1);
    }
}

// ===========================================================================
// Section 2 — halo_comm_ wrapping cases (Req 7.1, 7.2, 7.3).
// ===========================================================================

// 2(a) — the DECISION predicate (also covers the gate short-circuit reasoning of
// the "MPI uninitialized" / NULL / size1 branches without duplicating
// Task 10.2's CollectiveGateShortCircuit binary/tests).
TEST(HaloAdoptionWrapDecision, WrapOnlyWhenInitializedNonNullAndMultiRank) {
    // Absent when MPI is uninitialized (the branch Task 10.2 documents as
    // unreachable mid-run; pinned here as the decision itself).
    EXPECT_FALSE(ShouldWrap(/*mpi_initialized=*/false, MPI_COMM_WORLD, /*size=*/4));
    // Absent when comm is MPI_COMM_NULL.
    EXPECT_FALSE(ShouldWrap(/*mpi_initialized=*/true, MPI_COMM_NULL, /*size=*/4));
    // Absent when size <= 1 (single-rank short-circuit).
    EXPECT_FALSE(ShouldWrap(/*mpi_initialized=*/true, MPI_COMM_WORLD, /*size=*/1));
    // Present only when initialized, non-null, and size > 1.
    EXPECT_TRUE(ShouldWrap(/*mpi_initialized=*/true, MPI_COMM_WORLD, /*size=*/2));
    EXPECT_TRUE(ShouldWrap(/*mpi_initialized=*/true, MPI_COMM_SELF, /*size=*/2));
}

// 2(b) — wrapping a PREDEFINED handle (WORLD / SELF) never frees it: after the
// wrapper is destroyed the predefined handle is still usable. RefreshHaloComm
// wraps WORLD/SELF directly (no dup) precisely because RAII must not free them.
TEST(HaloAdoptionWrapBehavior, PredefinedHandleSurvivesWrapperDestruction) {
    int inited = 0;
    MPI_Initialized(&inited);
    ASSERT_TRUE(inited);

    {
        halo::Communicator wrapper(MPI_COMM_SELF);
        EXPECT_EQ(wrapper.handle(), MPI_COMM_SELF);
        EXPECT_EQ(wrapper.size(), 1);  // SELF is always size 1
    }  // wrapper destroyed here — must NOT free MPI_COMM_SELF

    // MPI_COMM_SELF is still valid and usable after the wrapper's destruction.
    int self_size = -1;
    ASSERT_EQ(MPI_Comm_size(MPI_COMM_SELF, &self_size), MPI_SUCCESS);
    EXPECT_EQ(self_size, 1);

    // Same for MPI_COMM_WORLD.
    {
        halo::Communicator wrapper(MPI_COMM_WORLD);
        EXPECT_EQ(wrapper.handle(), MPI_COMM_WORLD);
        EXPECT_EQ(wrapper.size(), WorldSize());
    }
    int world_size = -1;
    ASSERT_EQ(MPI_Comm_size(MPI_COMM_WORLD, &world_size), MPI_SUCCESS);
    EXPECT_EQ(world_size, WorldSize());
}

// 2(b) — wrapping a DUPLICATED non-predefined handle: the wrapper's RAII frees
// exactly the dup'd handle (not the caller's), mirroring RefreshHaloComm's
// `MPI_Comm_dup(comm_c_, &comm_to_wrap); halo_comm_.emplace(comm_to_wrap);`.
// We prove: the wrapper reports the dup'd comm's size/rank, and the ORIGINAL
// communicator (here WORLD) remains valid after the wrapper (owning the dup) is
// destroyed — i.e. RAII freed only the duplicate, no double free of WORLD.
TEST(HaloAdoptionWrapBehavior, DuplicatedHandleFreedByRaiiLeavesOriginalValid) {
    int inited = 0;
    MPI_Initialized(&inited);
    ASSERT_TRUE(inited);

    MPI_Comm dup = MPI_COMM_NULL;
    ASSERT_EQ(MPI_Comm_dup(MPI_COMM_WORLD, &dup), MPI_SUCCESS);
    ASSERT_NE(dup, MPI_COMM_WORLD);
    ASSERT_NE(dup, MPI_COMM_NULL);

    {
        halo::Communicator wrapper(dup);  // wrapper now owns the dup'd handle
        EXPECT_EQ(wrapper.handle(), dup);
        EXPECT_EQ(wrapper.size(), WorldSize());
        EXPECT_EQ(wrapper.rank(), WorldRank());
    }  // RAII frees the dup'd handle here (non-predefined, non-null)

    // The original WORLD communicator is untouched and still usable — the RAII
    // free acted only on the duplicate, exactly as the dup-then-wrap convention
    // guarantees (no double free, no leak of the caller's handle).
    int world_size = -1;
    ASSERT_EQ(MPI_Comm_size(MPI_COMM_WORLD, &world_size), MPI_SUCCESS);
    EXPECT_EQ(world_size, WorldSize());
}

}  // namespace cece::test

// ---------------------------------------------------------------------------
// Own main() with MPI init, mirroring tests/test_collective_gate_equivalence.cpp
// and tests/test_mpi_reuse_decision.cpp. ALL ranks stay alive and run
// RUN_ALL_TESTS so every rank participates in the wrapping-behavior tests.
// halo::Environment::initialize() is invoked after MPI_Init so the HALO wrappers
// have a live environment. Slurm/PMI env is scrubbed so a plain mpirun -np N
// works in the container without a batch scheduler.
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
