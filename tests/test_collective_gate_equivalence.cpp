// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors
//
// Feature: cece-halo-collective-adoption
// Property 2: collective_all_ready preserves the legacy decision and message
// Property 3: collective_int_matches preserves the legacy decision and message
//
// The adoption re-expresses the two anonymous-namespace collective helpers in
// src/driver/cece_driver_facade.cpp on top of halo::allreduce<int>:
//
//   * collective_all_ready reduces a single-element 0/1 readiness flag with
//     MPI_MIN and returns false if ANY rank is not ready; short-circuits
//     (returning the LOCAL readiness value, with the context-based
//     failure_detail discipline) when MPI is uninitialized, the communicator is
//     MPI_COMM_NULL, or mpi_size <= 1 (Req 4.1-4.4).
//   * collective_int_matches reduces local_value with MPI_MIN and MPI_MAX and
//     returns false (naming the value and both minimum and maximum) when the two
//     differ; short-circuits (returning true) under the same three conditions
//     (Req 5.1-5.4).
//
// This test drives the REAL production helpers — reached through the
// CollectiveGateTestAccess friend shim's CallCollectiveAllReady /
// CallCollectiveIntMatches forwarders, which wrap the passed MPI_Comm exactly as
// cece_helm_graph.cpp does and delegate to the internal-linkage helpers — and
// compares each helper's (bool result, failure_detail) against an INDEPENDENT
// legacy reference computed from the FULL, known per-rank input vector:
//
//   * all_ready reference: result = MIN over ranks of the 0/1 readiness flag
//     (true iff every rank is ready). When not ready and failure_detail is
//     empty, the message is "<context> failed on one or more ranks".
//   * int_matches reference: result = (min == max) across ranks. On mismatch the
//     message is "<name> differs across ranks (minimum X, maximum Y)".
//
// Because RapidCheck seeds its generators independently per rank, the per-rank
// inputs would NOT agree across ranks on their own. To build a well-defined
// collective scenario, rank 0 generates the full per-rank vector and broadcasts
// it (MPI_Bcast) so every rank shares the SAME scenario; each rank then feeds
// its OWN slot's value into the helper. The legacy reference is computed from
// the shared full vector on every rank, so the comparison is exact and
// rank-consistent. At np1 the vector has one entry and the helpers exercise the
// size<=1 short-circuit (returning the local value / true), which the reference
// reproduces.
//
// The short-circuit cases (MPI_COMM_NULL and a size-1 communicator via
// MPI_COMM_SELF; "MPI uninitialized" is not reachable mid-run once MPI_Init has
// run, so it is covered structurally by the NULL/size1 discipline) are pinned by
// deterministic unit tests below.
//
// The test runs correctly both as a single process (ctest default) and under
// mpirun -np 2: the property degrades gracefully at np1 and exercises the real
// two-plus-rank collective at np2.
//
// Validates: Requirements 4.1, 4.2, 4.3, 4.4, 5.1, 5.2, 5.3, 5.4

#include <gtest/gtest.h>
#include <mpi.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "cece/cece_driver_facade.hpp"

namespace cece {

// ============================================================================
// Test-only shim: reach the private static forwarders that delegate to the REAL
// production HALO-backed collective helpers, declared as
// `friend struct CollectiveGateTestAccess;` on CeceDriverOrchestrator (Task 6.1
// / 10.2). Declared in namespace cece so the friendship resolves. It changes no
// production signature or visibility and validates the REAL production helpers,
// not a copy.
// ============================================================================
struct CollectiveGateTestAccess {
    static bool AllReady(MPI_Comm comm, bool local_ready, const std::string& context, std::string& failure_detail) {
        return CeceDriverOrchestrator::CallCollectiveAllReady(comm, local_ready, context, failure_detail);
    }
    static bool IntMatches(MPI_Comm comm, int local_value, const std::string& name, std::string& failure_detail) {
        return CeceDriverOrchestrator::CallCollectiveIntMatches(comm, local_value, name, failure_detail);
    }
};

}  // namespace cece

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

// ---------------------------------------------------------------------------
// Legacy reference for collective_all_ready over a KNOWN full per-rank vector:
// the group is ready iff every rank's flag is 1 (MIN semantics). Returns the
// reference (bool, failure_detail) so the test can compare both against the
// production helper. `preset_detail` models the caller having already set a
// detail (e.g. the local not-ready branch); the message is only written when
// the incoming detail is empty, matching the helper's discipline.
// ---------------------------------------------------------------------------
struct GateResult {
    bool ok = false;
    std::string detail;
};

GateResult LegacyAllReady(const std::vector<int>& flags, const std::string& context, const std::string& preset_detail) {
    GateResult r;
    r.detail = preset_detail;
    const int nranks = static_cast<int>(flags.size());
    // Reproduce the legacy short-circuit discipline exactly: when the group has
    // <= 1 participant (the size<=1 / NULL / uninit short-circuit), the helper
    // returns the LOCAL readiness value and, when not ready with an empty detail,
    // sets failure_detail = context VERBATIM (no " failed on one or more ranks"
    // suffix — that suffix belongs only to the size>1 collective path). This
    // mirrors the anonymous-namespace collective_all_ready in
    // src/driver/cece_driver_facade.cpp (Req 4.2, 4.3).
    if (nranks <= 1) {
        const bool local_ready = nranks == 1 && flags.front() == 1;
        r.ok = local_ready;
        if (!local_ready && r.detail.empty()) r.detail = context;
        return r;
    }
    int minv = 1;
    for (int f : flags) minv = std::min(minv, f);
    if (minv != 1) {
        if (r.detail.empty()) r.detail = context + " failed on one or more ranks";
        r.ok = false;
    } else {
        r.ok = true;
    }
    return r;
}

// Legacy reference for collective_int_matches over a KNOWN full per-rank vector:
// values agree iff min == max across ranks; on mismatch the message names the
// value and both minimum and maximum.
GateResult LegacyIntMatches(const std::vector<int>& values, const std::string& name) {
    GateResult r;
    int minv = values.empty() ? 0 : values.front();
    int maxv = values.empty() ? 0 : values.front();
    for (int v : values) {
        minv = std::min(minv, v);
        maxv = std::max(maxv, v);
    }
    if (minv != maxv) {
        r.ok = false;
        r.detail = name + " differs across ranks (minimum " + std::to_string(minv) + ", maximum " + std::to_string(maxv) + ")";
    } else {
        r.ok = true;
    }
    return r;
}

// Broadcast a rank-0-generated vector of `n` ints so every rank shares the same
// per-rank scenario. Returns the shared vector on every rank.
std::vector<int> BroadcastVector(std::vector<int> from_rank0, int n) {
    std::vector<int> v(static_cast<std::size_t>(n), 0);
    if (WorldRank() == 0) {
        for (int i = 0; i < n && i < static_cast<int>(from_rank0.size()); ++i) v[i] = from_rank0[i];
    }
    int inited = 0;
    MPI_Initialized(&inited);
    if (inited && WorldSize() > 1) {
        MPI_Bcast(v.data(), n, MPI_INT, 0, MPI_COMM_WORLD);
    }
    return v;
}

}  // namespace

// ---------------------------------------------------------------------------
// Property 2 (RapidCheck, >=100 iterations): for ANY per-rank readiness vector,
// the production collective_all_ready returns the SAME (bool, failure_detail)
// as the legacy MIN-over-readiness reference. Rank 0 generates the full vector
// and broadcasts it; each rank feeds its own slot and compares against the
// reference computed from the shared vector.
// Validates: Requirements 4.1, 4.2, 4.3, 4.4
// ---------------------------------------------------------------------------
RC_GTEST_PROP(CollectiveGateEquivalence, AllReadyMatchesLegacy, ()) {
    const int nranks = WorldSize();
    const int rank = WorldRank();

    // Rank 0 draws one 0/1 flag per rank; broadcast so all ranks share it.
    std::vector<int> gen;
    if (rank == 0) {
        gen.reserve(nranks);
        for (int i = 0; i < nranks; ++i) gen.push_back(*rc::gen::element(0, 1));
    }
    const std::vector<int> flags = BroadcastVector(std::move(gen), nranks);

    const bool local_ready = flags[static_cast<std::size_t>(rank)] == 1;
    const std::string context = "source and regrid metadata readiness";

    // Model a caller with no preset detail (the common case).
    std::string prod_detail;
    const bool prod_ok = cece::CollectiveGateTestAccess::AllReady(MPI_COMM_WORLD, local_ready, context, prod_detail);

    const GateResult ref = LegacyAllReady(flags, context, /*preset_detail=*/"");
    RC_ASSERT(prod_ok == ref.ok);
    RC_ASSERT(prod_detail == ref.detail);
}

// ---------------------------------------------------------------------------
// Property 2 companion: the "caller already set a detail" discipline. When the
// local rank is not ready and a detail is preset, the helper must NOT overwrite
// it (it only writes when the detail is empty). We only assert the message-
// preservation invariant on the not-ready path; the boolean still equals the
// MIN reference.
// Validates: Requirements 4.2, 4.3
// ---------------------------------------------------------------------------
RC_GTEST_PROP(CollectiveGateEquivalence, AllReadyPreservesPresetDetail, ()) {
    const int nranks = WorldSize();
    const int rank = WorldRank();

    std::vector<int> gen;
    if (rank == 0) {
        gen.reserve(nranks);
        for (int i = 0; i < nranks; ++i) gen.push_back(*rc::gen::element(0, 1));
    }
    const std::vector<int> flags = BroadcastVector(std::move(gen), nranks);

    const bool local_ready = flags[static_cast<std::size_t>(rank)] == 1;
    const std::string context = "source and regrid metadata readiness";
    const std::string preset = "source buffer or regrid metadata changed after AMIO validation";

    std::string prod_detail = preset;
    const bool prod_ok = cece::CollectiveGateTestAccess::AllReady(MPI_COMM_WORLD, local_ready, context, prod_detail);

    const GateResult ref = LegacyAllReady(flags, context, preset);
    RC_ASSERT(prod_ok == ref.ok);
    RC_ASSERT(prod_detail == ref.detail);
}

// ---------------------------------------------------------------------------
// Property 3 (RapidCheck, >=100 iterations): for ANY per-rank integer vector,
// the production collective_int_matches returns the SAME (bool, failure_detail)
// as the legacy min==max reference (with the verbatim mismatch message).
// Validates: Requirements 5.1, 5.2, 5.3, 5.4
// ---------------------------------------------------------------------------
RC_GTEST_PROP(CollectiveGateEquivalence, IntMatchesMatchesLegacy, ()) {
    const int nranks = WorldSize();
    const int rank = WorldRank();

    // A small integer range that still produces both agreement and disagreement
    // across ranks. Rank 0 draws one value per rank; broadcast to share.
    std::vector<int> gen;
    if (rank == 0) {
        gen.reserve(nranks);
        for (int i = 0; i < nranks; ++i) gen.push_back(*rc::gen::inRange(-4, 5));
    }
    const std::vector<int> values = BroadcastVector(std::move(gen), nranks);

    const int local_value = values[static_cast<std::size_t>(rank)];
    const std::string name = "source longitude count";

    std::string prod_detail;
    const bool prod_ok = cece::CollectiveGateTestAccess::IntMatches(MPI_COMM_WORLD, local_value, name, prod_detail);

    const GateResult ref = LegacyIntMatches(values, name);
    RC_ASSERT(prod_ok == ref.ok);
    RC_ASSERT(prod_detail == ref.detail);
}

// ---------------------------------------------------------------------------
// Property 3 companion: force frequent AGREEMENT so the true/empty-detail branch
// is well exercised (a plain uniform draw disagrees most of the time at np>=2).
// Half the iterations use a single shared value on every rank (guaranteed
// match); the rest use the uniform draw.
// Validates: Requirements 5.1, 5.2, 5.4
// ---------------------------------------------------------------------------
RC_GTEST_PROP(CollectiveGateEquivalence, IntMatchesAgreementBranch, ()) {
    const int nranks = WorldSize();
    const int rank = WorldRank();

    std::vector<int> gen;
    if (rank == 0) {
        const bool force_match = *rc::gen::arbitrary<bool>();
        const int shared = *rc::gen::inRange(-100, 100);
        gen.reserve(nranks);
        for (int i = 0; i < nranks; ++i) {
            gen.push_back(force_match ? shared : *rc::gen::inRange(-4, 5));
        }
    }
    const std::vector<int> values = BroadcastVector(std::move(gen), nranks);

    const int local_value = values[static_cast<std::size_t>(rank)];
    const std::string name = "AMIO record count for 'var'";

    std::string prod_detail;
    const bool prod_ok = cece::CollectiveGateTestAccess::IntMatches(MPI_COMM_WORLD, local_value, name, prod_detail);

    const GateResult ref = LegacyIntMatches(values, name);
    RC_ASSERT(prod_ok == ref.ok);
    RC_ASSERT(prod_detail == ref.detail);
}

// ---------------------------------------------------------------------------
// Short-circuit case: MPI_COMM_NULL. all_ready returns the local readiness value
// and, when not ready with an empty detail, sets failure_detail = context. No
// collective is issued. (Req 4.2)
// ---------------------------------------------------------------------------
TEST(CollectiveGateShortCircuit, AllReadyNullCommReturnsLocal) {
    {
        std::string detail;
        EXPECT_TRUE(cece::CollectiveGateTestAccess::AllReady(MPI_COMM_NULL, /*local_ready=*/true, "ctx", detail));
        EXPECT_TRUE(detail.empty());
    }
    {
        std::string detail;
        EXPECT_FALSE(cece::CollectiveGateTestAccess::AllReady(MPI_COMM_NULL, /*local_ready=*/false, "ctx", detail));
        EXPECT_EQ(detail, "ctx");  // context set verbatim when previously empty
    }
    {
        std::string detail = "preset";
        EXPECT_FALSE(cece::CollectiveGateTestAccess::AllReady(MPI_COMM_NULL, /*local_ready=*/false, "ctx", detail));
        EXPECT_EQ(detail, "preset");  // not overwritten when already set
    }
}

// Short-circuit case: size-1 communicator (MPI_COMM_SELF). Same discipline as
// NULL: returns local readiness without any collective. (Req 4.2)
TEST(CollectiveGateShortCircuit, AllReadySizeOneReturnsLocal) {
    int inited = 0;
    MPI_Initialized(&inited);
    if (!inited) GTEST_SKIP() << "MPI not initialized";
    std::string detail;
    EXPECT_TRUE(cece::CollectiveGateTestAccess::AllReady(MPI_COMM_SELF, /*local_ready=*/true, "ctx", detail));
    EXPECT_TRUE(detail.empty());

    std::string detail2;
    EXPECT_FALSE(cece::CollectiveGateTestAccess::AllReady(MPI_COMM_SELF, /*local_ready=*/false, "ctx", detail2));
    EXPECT_EQ(detail2, "ctx");
}

// Short-circuit case: MPI_COMM_NULL and size-1 for int_matches — always true,
// no collective, no detail written. (Req 5.2)
TEST(CollectiveGateShortCircuit, IntMatchesNullAndSizeOneReturnTrue) {
    {
        std::string detail;
        EXPECT_TRUE(cece::CollectiveGateTestAccess::IntMatches(MPI_COMM_NULL, /*value=*/7, "name", detail));
        EXPECT_TRUE(detail.empty());
    }
    int inited = 0;
    MPI_Initialized(&inited);
    if (inited) {
        std::string detail;
        EXPECT_TRUE(cece::CollectiveGateTestAccess::IntMatches(MPI_COMM_SELF, /*value=*/42, "name", detail));
        EXPECT_TRUE(detail.empty());
    }
}

}  // namespace cece::test

// ---------------------------------------------------------------------------
// Own main() with MPI init, mirroring tests/test_mpi_reuse_decision.cpp. ALL
// ranks stay alive and run RUN_ALL_TESTS so they participate in the MPI
// collectives the property issues. halo::Environment::initialize() is invoked
// after MPI_Init so the HALO-backed helpers have a live environment. Slurm/PMI
// env is scrubbed so a plain mpirun -np N works in the container without a batch
// scheduler.
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
