// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors
//
// Feature: cece-halo-collective-adoption
// Property 4: Fused front-half gate equals the five separate gates (Task 10.3)
//
// **Validates: Requirements 6.1, 6.2, 6.3, 6.4**
//
// The adoption fuses the former front-half readiness gate — one
// collective_all_ready over the readiness flag plus four collective_int_matches
// over (file_nx, file_ny, field_nlev, plan.identity), i.e. ~9 MPI_Allreduce
// reductions — into ONE packed 5-entry integer vector reduced by a single
// halo::allreduce<int>(MIN) and a single halo::allreduce<int>(MAX), whose
// elementwise results (mn[], mx[]) are then mapped to accept/reject +
// failure_detail by the pure static helper
// CeceDriverOrchestrator::FusedGateDecision.
//
// This test proves that fused decision is IDENTICAL to the conjunction of the
// five separate legacy gates, both in the accept/reject boolean and in the
// exact failure_detail string, under the legacy precedence
// (readiness -> file_nx -> file_ny -> field_nlev -> plan.identity).
//
// Two complementary parts:
//
//   1. Pure property test (runs at every rank count, meaningful at np1): drive
//      the REAL production FusedGateDecision with generated (mn, mx) vectors
//      (mn[k] <= mx[k], since they are MIN/MAX reductions of the same per-rank
//      values), including equal and unequal metadata and mn[0] in {0,1}, and
//      assert it equals an INDEPENDENT reimplementation of the five separate
//      gates over the same reductions. >=100 RapidCheck iterations.
//
//   2. End-to-end packing case (np2 / np4): each rank contributes a packed
//      5-entry vector [readiness, file_nx, file_ny, field_nlev, identity], we
//      reduce it through the REAL fused halo::allreduce<int>(MIN)/(MAX) over a
//      halo::Communicator wrapping MPI_COMM_WORLD, then assert FusedGateDecision
//      on the reduced vectors matches the conjunction of the five legacy
//      per-value decisions computed directly from the known per-rank inputs.
//      This exercises the actual collective packing that
//      RegridToDestinationBuffer issues (Req 6.1, 6.2).

#include <gtest/gtest.h>
#include <mpi.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <unistd.h>

#include <halo/collectives.hpp>
#include <halo/communicator.hpp>
#include <halo/environment.hpp>
#include <string>
#include <vector>

#include "cece/cece_driver_facade.hpp"

namespace cece {

// ============================================================================
// Test-only friend shim.
//
// FusedGateDecision is a private static member of CeceDriverOrchestrator. The
// class declares `friend struct CollectiveGateTestAccess;` (see
// include/cece/cece_driver_facade.hpp, Task 6.1) so this struct — declared in
// namespace cece so the friendship resolves — can invoke the REAL production
// decision helper off-MPI without altering its signature, logic, or visibility.
// It does not touch any production code path.
// ============================================================================
struct CollectiveGateTestAccess {
    static bool FusedGate(const std::vector<int>& mn, const std::vector<int>& mx, std::string& failure_detail) {
        return CeceDriverOrchestrator::FusedGateDecision(mn, mx, failure_detail);
    }
};

}  // namespace cece

namespace cece::test {

namespace {

// ---------------------------------------------------------------------------
// INDEPENDENT legacy reference: the five separate gates applied to the same
// elementwise MIN/MAX reductions.
//
//   * Gate 0 (collective_all_ready over readiness, MIN semantics): accept iff
//     mn[0] == 1 (any rank contributing 0 fails the MIN). On failure it sets
//     the not-ready message ONLY if failure_detail is still empty, mirroring the
//     local not-ready branch that pre-populates a detail.
//   * Gates 1..4 (collective_int_matches over file_nx, file_ny, field_nlev,
//     plan.identity): accept iff mn[k] == mx[k]; on the first mismatch it sets
//     the value-specific "... differs across ranks (minimum X, maximum Y)"
//     message.
//
// Overall accept = conjunction; failure_detail = the FIRST failing gate's
// message under the precedence readiness -> nx -> ny -> nlev -> identity. This
// is a from-scratch reimplementation (NOT a call into FusedGateDecision) so the
// test genuinely cross-checks the fused helper against the separate-gate model.
// The message strings are the SAME ones the production gates emit.
// ---------------------------------------------------------------------------
bool LegacyFiveGates(const std::vector<int>& mn, const std::vector<int>& mx, std::string& failure_detail) {
    // Gate 0: readiness (MIN).
    if (mn[0] != 1) {
        if (failure_detail.empty()) failure_detail = "source and regrid metadata readiness failed on one or more ranks";
        return false;
    }
    // Gate 1: file_nx (MIN vs MAX).
    if (mn[1] != mx[1]) {
        failure_detail =
            "source longitude count differs across ranks (minimum " + std::to_string(mn[1]) + ", maximum " + std::to_string(mx[1]) + ")";
        return false;
    }
    // Gate 2: file_ny.
    if (mn[2] != mx[2]) {
        failure_detail =
            "source latitude count differs across ranks (minimum " + std::to_string(mn[2]) + ", maximum " + std::to_string(mx[2]) + ")";
        return false;
    }
    // Gate 3: field_nlev.
    if (mn[3] != mx[3]) {
        failure_detail =
            "source level count differs across ranks (minimum " + std::to_string(mn[3]) + ", maximum " + std::to_string(mx[3]) + ")";
        return false;
    }
    // Gate 4: plan.identity.
    if (mn[4] != mx[4]) {
        failure_detail =
            "regrid-plan identity mode differs across ranks (minimum " + std::to_string(mn[4]) + ", maximum " + std::to_string(mx[4]) + ")";
        return false;
    }
    return true;
}

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

}  // namespace

// ===========================================================================
// Property 4a: fused decision equals the five-separate-gates decision, with a
// possibly pre-populated failure_detail (the local not-ready branch pre-sets a
// detail before the collective, and the readiness gate must preserve it).
// Feature: cece-halo-collective-adoption, Property 4
// **Validates: Requirements 6.1, 6.3, 6.4**
// ===========================================================================
RC_GTEST_PROP(FusedGateEquivalence, Property4_FusedEqualsFiveGates, ()) {
    // Generate the per-value MIN/MAX pair for each of the 5 packed entries with
    // mn[k] <= mx[k] (they are MIN/MAX over the same set of per-rank ints).
    // Index 0 (readiness) is a 0/1 flag with MIN semantics: mn[0] in {0,1} and
    // mx[0] >= mn[0], but only mn[0] governs the readiness decision.
    std::vector<int> mn(5);
    std::vector<int> mx(5);

    // Readiness flag: mn[0] in {0,1}. mx[0] in {mn[0]..1} (still a 0/1 flag).
    mn[0] = *rc::gen::element(0, 1);
    mx[0] = *rc::gen::inRange(mn[0], 2);  // [mn[0], 1]

    // Metadata values 1..4: pick a MIN in a modest range, then a MAX >= MIN so
    // some entries agree (mn==mx) and some differ.
    for (int k = 1; k < 5; ++k) {
        const int lo = *rc::gen::inRange(-8, 200);
        const int hi = *rc::gen::inRange(lo, 200);  // [lo, 199], guarantees hi >= lo
        mn[k] = lo;
        mx[k] = hi;
    }

    // Optionally pre-populate failure_detail to exercise the readiness gate's
    // "only write when empty" preservation of the local not-ready detail.
    const bool prepopulate = *rc::gen::arbitrary<bool>();
    const std::string preset = prepopulate ? "source buffer or regrid metadata changed after AMIO validation" : "";

    std::string fused_detail = preset;
    std::string legacy_detail = preset;

    const bool fused = cece::CollectiveGateTestAccess::FusedGate(mn, mx, fused_detail);
    const bool legacy = LegacyFiveGates(mn, mx, legacy_detail);

    RC_ASSERT(fused == legacy);
    RC_ASSERT(fused_detail == legacy_detail);
}

// ===========================================================================
// Property 4b: acceptance characterization — the fused gate accepts iff
// mn[0]==1 AND mn[k]==mx[k] for k in {1,2,3,4}, and on acceptance leaves any
// pre-set detail untouched.
// Feature: cece-halo-collective-adoption, Property 4
// **Validates: Requirements 6.1, 6.3**
// ===========================================================================
RC_GTEST_PROP(FusedGateEquivalence, Property4_AcceptanceCharacterization, ()) {
    std::vector<int> mn(5);
    std::vector<int> mx(5);
    mn[0] = *rc::gen::element(0, 1);
    mx[0] = *rc::gen::inRange(mn[0], 2);
    for (int k = 1; k < 5; ++k) {
        const int lo = *rc::gen::inRange(-8, 200);
        const int hi = *rc::gen::inRange(lo, 200);
        mn[k] = lo;
        mx[k] = hi;
    }

    const bool expect_accept = (mn[0] == 1) && (mn[1] == mx[1]) && (mn[2] == mx[2]) && (mn[3] == mx[3]) && (mn[4] == mx[4]);

    std::string detail;
    const bool fused = cece::CollectiveGateTestAccess::FusedGate(mn, mx, detail);
    RC_ASSERT(fused == expect_accept);
    if (fused) {
        // Acceptance never sets a failure_detail.
        RC_ASSERT(detail.empty());
    } else {
        RC_ASSERT(!detail.empty());
    }
}

// ===========================================================================
// Property 4c: precedence — when several gates would fail, the reported detail
// is the FIRST failing gate under readiness -> nx -> ny -> nlev -> identity.
// Cross-checked against the independent five-gate reference.
// Feature: cece-halo-collective-adoption, Property 4
// **Validates: Requirements 6.2, 6.4**
// ===========================================================================
RC_GTEST_PROP(FusedGateEquivalence, Property4_PrecedenceMatchesLegacy, ()) {
    // Bias toward multi-failure vectors: force at least one metadata mismatch
    // and randomly a not-ready readiness, so precedence is genuinely exercised.
    std::vector<int> mn(5);
    std::vector<int> mx(5);
    mn[0] = *rc::gen::element(0, 1);
    mx[0] = *rc::gen::inRange(mn[0], 2);
    for (int k = 1; k < 5; ++k) {
        const int lo = *rc::gen::inRange(-8, 50);
        // Force many mismatches: MAX strictly greater than MIN more often.
        const bool differ = *rc::gen::arbitrary<bool>();
        const int hi = differ ? *rc::gen::inRange(lo + 1, 60) : lo;
        mn[k] = lo;
        mx[k] = hi;
    }

    std::string fused_detail;
    std::string legacy_detail;
    const bool fused = cece::CollectiveGateTestAccess::FusedGate(mn, mx, fused_detail);
    const bool legacy = LegacyFiveGates(mn, mx, legacy_detail);
    RC_ASSERT(fused == legacy);
    RC_ASSERT(fused_detail == legacy_detail);
}

// ===========================================================================
// End-to-end packing case (meaningful at -np 2 / -np 4): each rank contributes
// a packed 5-entry vector, reduce it through the REAL fused
// halo::allreduce<int>(MIN)/(MAX) over a halo::Communicator wrapping
// MPI_COMM_WORLD, then assert FusedGateDecision on the reduced vectors matches
// the conjunction of the five legacy per-value decisions computed directly from
// the KNOWN per-rank inputs. At np1 the reduction is the identity, so this still
// exercises the packing + decision structurally.
// Feature: cece-halo-collective-adoption, Property 4
// **Validates: Requirements 6.1, 6.2, 6.3, 6.4**
// ===========================================================================
TEST(FusedGateEquivalenceE2E, PackedReduceMatchesFiveGates) {
    int inited = 0;
    MPI_Initialized(&inited);
    ASSERT_TRUE(inited);

    const int nranks = WorldSize();
    const int rank = WorldRank();

    // Deterministic per-rank input scenarios. Each entry is the packed
    // [readiness, file_nx, file_ny, field_nlev, identity] this rank contributes.
    // The scenarios are chosen so, across the participating ranks, we cover:
    //   - all-agree accept,
    //   - a single not-ready rank (readiness MIN -> reject),
    //   - a metadata divergence on each of nx / ny / nlev / identity.
    struct Scenario {
        const char* name;
        // per_rank[r] = packed vector rank r contributes. Ranks beyond the
        // scenario length reuse the last defined vector so every rank always
        // contributes something identical-shaped.
        std::vector<std::vector<int>> per_rank;
    };

    // A "healthy" agreed baseline every rank can fall back to.
    const std::vector<int> healthy{1, 144, 90, 3, 1};

    const std::vector<Scenario> scenarios = {
        {"all_agree_accept", {{1, 144, 90, 3, 1}, {1, 144, 90, 3, 1}, {1, 144, 90, 3, 1}, {1, 144, 90, 3, 1}}},
        {"rank_not_ready", {{1, 144, 90, 3, 1}, {0, 144, 90, 3, 1}, {1, 144, 90, 3, 1}, {1, 144, 90, 3, 1}}},
        {"file_nx_diverges", {{1, 144, 90, 3, 1}, {1, 288, 90, 3, 1}, {1, 144, 90, 3, 1}, {1, 144, 90, 3, 1}}},
        {"file_ny_diverges", {{1, 144, 90, 3, 1}, {1, 144, 91, 3, 1}, {1, 144, 90, 3, 1}, {1, 144, 45, 3, 1}}},
        {"field_nlev_diverges", {{1, 144, 90, 3, 1}, {1, 144, 90, 5, 1}, {1, 144, 90, 3, 1}, {1, 144, 90, 3, 1}}},
        {"identity_diverges", {{1, 144, 90, 3, 1}, {1, 144, 90, 3, 0}, {1, 144, 90, 3, 1}, {1, 144, 90, 3, 1}}},
        // Multiple simultaneous failures to exercise precedence end-to-end:
        // readiness not-ready on one rank AND nx divergence on another. The
        // readiness gate must win (precedence), matching the local pre-set path.
        {"not_ready_and_nx", {{0, 144, 90, 3, 1}, {1, 288, 90, 3, 1}, {1, 144, 90, 3, 1}, {1, 144, 90, 3, 1}}},
    };

    // Build the halo::Communicator wrapping MPI_COMM_WORLD (predefined => stored
    // directly, never freed), matching the production distributed branch.
    halo::Communicator comm(MPI_COMM_WORLD);

    for (const Scenario& s : scenarios) {
        // Determine this rank's contribution: the scenario's per_rank[rank] if
        // defined, otherwise the healthy baseline (so extra ranks agree).
        std::vector<int> my_vec = (rank < static_cast<int>(s.per_rank.size())) ? s.per_rank[rank] : healthy;

        SCOPED_TRACE(std::string("scenario=") + s.name + " nranks=" + std::to_string(nranks) + " rank=" + std::to_string(rank));

        // REAL fused reduction — one MIN and one MAX over the packed vector,
        // exactly as RegridToDestinationBuffer issues them (Req 6.1, 6.2).
        const std::vector<int> mn = halo::allreduce<int>(comm, my_vec, MPI_MIN);
        const std::vector<int> mx = halo::allreduce<int>(comm, my_vec, MPI_MAX);
        ASSERT_EQ(mn.size(), 5u);
        ASSERT_EQ(mx.size(), 5u);

        // Independently compute the expected elementwise MIN/MAX from the KNOWN
        // per-rank inputs across exactly the participating ranks, and the
        // conjunction of the five legacy per-value decisions from those.
        std::vector<int> expect_mn = (0 < static_cast<int>(s.per_rank.size())) ? s.per_rank[0] : healthy;
        std::vector<int> expect_mx = expect_mn;
        for (int r = 0; r < nranks; ++r) {
            const std::vector<int>& v = (r < static_cast<int>(s.per_rank.size())) ? s.per_rank[r] : healthy;
            for (int k = 0; k < 5; ++k) {
                expect_mn[k] = std::min(expect_mn[k], v[k]);
                expect_mx[k] = std::max(expect_mx[k], v[k]);
            }
        }

        // The real reduction must equal the independently computed reduction.
        EXPECT_EQ(mn, expect_mn);
        EXPECT_EQ(mx, expect_mx);

        // Apply the REAL production decision to the REAL reduction.
        std::string fused_detail;
        const bool fused = cece::CollectiveGateTestAccess::FusedGate(mn, mx, fused_detail);

        // Reference: conjunction of the five legacy per-value gates over the
        // independently computed reduction.
        std::string legacy_detail;
        const bool legacy = LegacyFiveGates(expect_mn, expect_mx, legacy_detail);

        EXPECT_EQ(fused, legacy);
        EXPECT_EQ(fused_detail, legacy_detail);
    }
}

}  // namespace cece::test

// ---------------------------------------------------------------------------
// Own main() with MPI init, mirroring test_mpi_reuse_decision.cpp. ALL ranks
// stay alive and run RUN_ALL_TESTS so they participate in the halo::allreduce
// collectives the end-to-end case issues. Kokkos is not needed (no device
// work), but MPI must be live for the whole run. Slurm/PMI env is scrubbed so a
// plain mpirun -np N works in the container without a batch scheduler.
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
        // HALO collectives assume the environment is initialized; production
        // does this in main.cpp after MPI init. Safe/idempotent here.
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
