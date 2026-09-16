// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors
//
// Feature: cece-halo-collective-adoption
// Task 11.1: Multi-rank deadlock-freedom and np-invariance integration test
//
// This is a focused integration test for the regrid+gather path in
// RegridToDestinationBuffer. It exercises the REAL HALO primitives the driver
// uses — the pre-gather local_ok readiness allreduce (halo::allreduce<int>
// MIN) and the fused front-half gate (one packed 5-entry halo::allreduce MIN
// plus one MAX) — together with the production-style per-level MPI_Allgatherv
// assembly (a contiguous [level][band] send buffer gathered level by level into
// the replicated [level][j][i] destination), in a faithful harness rather than
// standing up a full CeceDriverOrchestrator (which needs DAGR/CeceIO/config).
// This mirrors the sibling focused-harness test (tests/test_halo_gather_
// equivalence.cpp) for determinism.
//
// It asserts three things under mpirun --oversubscribe -np {1,2,4}:
//
//   1. DEADLOCK FREEDOM / no-skip (Req 8.3, 8.4, 9.1): a not-ready failure-
//      injection case. One rank reports local_ok=false (or a front-gate not-
//      ready/mismatch). ALL ranks still enter the same collectives (the fused
//      gate's MIN/MAX and the pre-gather readiness allreduce) and reach the SAME
//      reject decision, so no rank returns early skipping a collective a peer
//      enters. The test completing (not hanging) IS the deadlock-freedom
//      evidence; we additionally assert every rank observes the same
//      reject/accept outcome. Covered with the not-ready rank being rank 0 and a
//      non-zero rank.
//
//   2. np-invariance (Req 9.2): at np2/np4 the assembled Replicated_Field is
//      bit-identical across all ranks (allreduce the bit pattern with MIN and
//      MAX, require equal — same technique as test_halo_gather_equivalence.cpp's
//      ExpectCrossRankBitIdentical).
//
//   3. np1-equivalence (Req 9.3): the np2/np4 assembled field equals the np1
//      result for the same inputs within 1e-10. Since a single test process
//      can't run at two rank counts simultaneously, each rank computes the full
//      serial reference field the same way a single rank would (the whole-grid
//      regrid of the rank-invariant source), then compares the gathered
//      [level][j][i] field against that serial reference — the design's index-
//      math proof guarantees they are equal.
//
// Dims are kept modest (nx<=32, ny<=32, nlev<=4) for the ~7 GB container and np4
// oversubscription.
//
// _Requirements: 8.3, 8.4, 9.1, 9.2, 9.3_

#include <gtest/gtest.h>
#include <mpi.h>
#include <unistd.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <halo/collectives.hpp>
#include <halo/communicator.hpp>
#include <halo/environment.hpp>
#include <optional>
#include <string>
#include <vector>

#include "cece/cece_regridder_utils.hpp"

namespace cece::io {
namespace {

// Matrix regrids accumulate the SpMV in a different summation order than a naive
// serial reference; identity regrids are a pure copy. The single-gather path
// uses the SAME per-band regrid output as the serial reference, so identity is
// asserted bit-for-bit and the tolerance branch matches test_numerical_
// equivalence's 1e-10.
constexpr double kRegridTolerance = 1.0e-10;

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

// Production band decomposition (src/driver/cece_driver_facade.cpp): rank r owns
// destination rows [band_start(r), band_start(r+1)) with a block split of the ny
// rows, distributing the remainder to the low ranks.
int BandStart(int rank, int ny, int size) {
    const int base = ny / size;
    const int rem = ny % size;
    return rank * base + std::min(rank, rem);
}

// Fixed identity (passthrough) plan over the nx * ny source grid owning rows
// [j0, j1). Matches tests/test_shared_plan_equivalence.cpp.
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

// Build the long-lived halo::Communicator wrapper over MPI_COMM_WORLD, mirroring
// the driver's RefreshHaloCommunicator: absent (nullopt) when MPI is
// uninitialized or size <= 1; wraps the predefined WORLD handle directly
// otherwise (Communicator never frees WORLD/SELF/NULL).
std::optional<halo::Communicator> MakeHaloComm() {
    int inited = 0;
    MPI_Initialized(&inited);
    if (!inited || WorldSize() <= 1) return std::nullopt;
    return std::optional<halo::Communicator>(std::in_place, MPI_COMM_WORLD);
}

// Broadcast a single int from rank 0 so every rank uses rank-invariant dims
// (production feeds these from rank-invariant config).
int BcastInt(int value) {
    if (WorldSize() > 1) {
        MPI_Bcast(&value, 1, MPI_INT, 0, MPI_COMM_WORLD);
    }
    return value;
}

// Broadcast a double buffer of a known length from rank 0.
void BcastDoubles(std::vector<double>& buf) {
    if (WorldSize() > 1 && !buf.empty()) {
        MPI_Bcast(buf.data(), static_cast<int>(buf.size()), MPI_DOUBLE, 0, MPI_COMM_WORLD);
    }
}

// The pre-gather readiness allreduce the driver runs over local_ok: a single
// halo::allreduce<int>(MIN). EVERY rank enters this regardless of its own
// local_ok, so a failing rank never strands a peer. Returns true only if all
// ranks are ready. At size<=1 there is no collective (short-circuit), returning
// the local flag directly.
bool ReadinessAllreduce(bool local_ok, const std::optional<halo::Communicator>& halo_comm) {
    if (!halo_comm.has_value()) return local_ok;
    const std::vector<int> reduced = halo::allreduce<int>(*halo_comm, {local_ok ? 1 : 0}, MPI_MIN);
    return reduced.at(0) == 1;
}

// The fused front-half gate the driver runs: pack [readiness, file_nx, file_ny,
// field_nlev, plan.identity] into one integer vector, reduce with ONE MIN and
// ONE MAX allreduce, then apply the pure decision. EVERY rank enters both
// reduces. Returns the accept decision; on size<=1 there is no collective and
// the local decision is returned directly.
//
// Decision (mirrors the production static helper): accept iff readiness==1 and
// each of the four match entries agree across ranks (mn[k] == mx[k]).
bool FusedFrontGate(const std::array<int, 5>& local, const std::optional<halo::Communicator>& halo_comm) {
    std::array<int, 5> mn = local;
    std::array<int, 5> mx = local;
    if (halo_comm.has_value()) {
        const std::vector<int> packed(local.begin(), local.end());
        const std::vector<int> rmin = halo::allreduce<int>(*halo_comm, packed, MPI_MIN);
        const std::vector<int> rmax = halo::allreduce<int>(*halo_comm, packed, MPI_MAX);
        for (std::size_t k = 0; k < 5; ++k) {
            mn[k] = rmin.at(k);
            mx[k] = rmax.at(k);
        }
    }
    if (mn[0] != 1) return false;  // readiness (any rank not-ready => reject)
    for (std::size_t k = 1; k < 5; ++k) {
        if (mn[k] != mx[k]) return false;  // shape/identity mismatch across ranks
    }
    return true;
}

// Assemble the replicated [level][j][i] field via the PRODUCTION-STYLE per-level
// MPI_Allgatherv loop: build rank-invariant band counts/displacements, then for
// each level issue ONE plain-MPI_DOUBLE MPI_Allgatherv that places this rank's
// contiguous band into full[level*nx*ny + j*nx + i]. This is exactly
// RegridToDestinationBuffer's assembly. At size 1 uses the Decision A std::copy
// fast path (no collective). The halo_comm argument is unused on this path (the
// gather runs on MPI_COMM_WORLD directly, as the driver runs on comm_c_); it is
// retained for signature parity with the readiness/gate helpers.
std::vector<double> AssembleSingleGather(const std::vector<double>& send_buf, int nx, int ny, int nlev, int size, int rank,
                                         const std::optional<halo::Communicator>& halo_comm) {
    (void)halo_comm;
    std::vector<double> full(static_cast<std::size_t>(nlev) * nx * ny, 0.0);

    std::vector<int> counts(static_cast<std::size_t>(size));
    std::vector<int> displs(static_cast<std::size_t>(size));
    for (int r = 0; r < size; ++r) {
        const int rj0 = BandStart(r, ny, size);
        const int rj1 = BandStart(r + 1, ny, size);
        counts[static_cast<std::size_t>(r)] = (rj1 - rj0) * nx;
        displs[static_cast<std::size_t>(r)] = rj0 * nx;
    }
    const int band_elems = counts[static_cast<std::size_t>(rank)];

    for (int level = 0; level < nlev; ++level) {
        const double* level_band = send_buf.data() + static_cast<std::size_t>(level) * band_elems;
        double* layer = full.data() + static_cast<std::size_t>(level) * nx * ny;
        if (size <= 1) {
            // Decision A single-rank fast path: copy the band directly.
            std::copy(level_band, level_band + band_elems, layer + displs[static_cast<std::size_t>(rank)]);
        } else {
            MPI_Allgatherv(level_band, band_elems, MPI_DOUBLE, layer, counts.data(), displs.data(), MPI_DOUBLE, MPI_COMM_WORLD);
        }
    }
    return full;
}

// The FULL-GRID serial reference field a single rank would produce: the whole
// [ny][nx] source regridded via the identity plan into every level, laid out
// [level][j][i]. Each rank can compute this independently from the rank-
// invariant source, so it stands in for the np1 result the design's index-math
// proof says the gathered field must equal.
std::vector<double> SerialReferenceField(const std::vector<double>& source, int nx, int ny, int nlev) {
    const RegridPlan whole = MakeIdentityPlan(nx, ny, /*j0=*/0, /*j1=*/ny);
    std::vector<double> full(static_cast<std::size_t>(nlev) * nx * ny, 0.0);
    for (int level = 0; level < nlev; ++level) {
        std::vector<double> band;
        const bool ok = apply_regrid_plan(whole, /*time_offset=*/0, /*is_float=*/false, source.data(), nx, ny, nx, band);
        EXPECT_TRUE(ok);
        EXPECT_EQ(static_cast<int>(band.size()), nx * ny);
        std::copy(band.begin(), band.end(), full.begin() + static_cast<std::size_t>(level) * nx * ny);
    }
    return full;
}

// Build this rank's contiguous [level][band] send buffer from the rank-invariant
// source via the REAL apply_regrid_plan (identity) over the rank's band.
std::vector<double> BuildSendBuffer(const std::vector<double>& source, int nx, int ny, int nlev, int j0, int j1) {
    const int band_elems = (j1 - j0) * nx;
    const RegridPlan plan = MakeIdentityPlan(nx, ny, j0, j1);
    std::vector<double> send_buf(static_cast<std::size_t>(nlev) * band_elems, 0.0);
    for (int level = 0; level < nlev; ++level) {
        std::vector<double> band;
        const bool ok = apply_regrid_plan(plan, /*time_offset=*/0, /*is_float=*/false, source.data(), nx, ny, nx, band);
        EXPECT_TRUE(ok);
        EXPECT_EQ(static_cast<int>(band.size()), band_elems);
        std::copy(band.begin(), band.end(), send_buf.begin() + static_cast<std::size_t>(level) * band_elems);
    }
    return send_buf;
}

// Assert `field` is bit-identical on every rank by allreducing its bit pattern
// with MIN and MAX and requiring them equal. At np=1 this is a trivial no-op.
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
        ASSERT_EQ(mn[k], mx[k]) << "field element " << k << " differs across ranks";
    }
}

// Assert every rank agrees on a single boolean decision (used to prove the
// deadlock-free gate reaches the SAME reject/accept outcome on all ranks). At
// np=1 trivially true.
void ExpectDecisionAgreesAcrossRanks(bool decision) {
    if (WorldSize() <= 1) return;
    const int local = decision ? 1 : 0;
    int mn = local;
    int mx = local;
    MPI_Allreduce(&local, &mn, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
    MPI_Allreduce(&local, &mx, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
    ASSERT_EQ(mn, mx) << "ranks disagree on the gate decision (potential no-skip violation)";
}

// Rank-invariant modest dims + source, shared by the tests. Drawn on rank 0 and
// broadcast so every rank operates on the identical inputs.
struct Scenario {
    int nx = 0;
    int ny = 0;
    int nlev = 0;
    std::vector<double> source;  // [ny][nx], rank-invariant
};

Scenario MakeScenario(int nx, int ny, int nlev) {
    Scenario s;
    s.nx = BcastInt(nx);
    s.ny = BcastInt(ny);
    s.nlev = BcastInt(nlev);
    s.source.assign(static_cast<std::size_t>(s.nx) * s.ny, 0.0);
    if (WorldRank() == 0) {
        for (std::size_t k = 0; k < s.source.size(); ++k) {
            // Deterministic, spread-out values so the bit-identity/tolerance
            // checks are meaningful (not all zeros).
            s.source[k] = static_cast<double>((k * 37 + 11) % 1000) - 500.0 + 0.25 * static_cast<double>(k % 7);
        }
    }
    BcastDoubles(s.source);
    return s;
}

}  // namespace

// ============================================================================
// np-invariance + np1-equivalence: the single-gather assembly of the rank-
// invariant source produces a Replicated_Field that is (a) bit-identical across
// all ranks and (b) equal to the full-grid serial reference within 1e-10.
//
// _Requirements: 9.2, 9.3_
// ============================================================================
TEST(HaloMultirankIntegration, NpInvarianceAndNp1Equivalence) {
    const int size = WorldSize();
    const int rank = WorldRank();

    // Keep dims modest for the ~7 GB container. Ensure ny >= size so every rank
    // owns >= 1 band row.
    const Scenario s = MakeScenario(/*nx=*/16, /*ny=*/std::max(12, size), /*nlev=*/3);

    const int j0 = BandStart(rank, s.ny, size);
    const int j1 = BandStart(rank + 1, s.ny, size);

    // Everyone is ready in the happy path; the gate and readiness allreduce must
    // both accept (all ranks enter, agree).
    const std::optional<halo::Communicator> halo_comm = MakeHaloComm();
    const std::array<int, 5> local_gate = {1, s.nx, s.ny, s.nlev, /*identity=*/1};
    const bool gate_accept = FusedFrontGate(local_gate, halo_comm);
    ExpectDecisionAgreesAcrossRanks(gate_accept);
    ASSERT_TRUE(gate_accept) << "happy-path fused gate must accept";

    const std::vector<double> send_buf = BuildSendBuffer(s.source, s.nx, s.ny, s.nlev, j0, j1);

    const bool ready = ReadinessAllreduce(/*local_ok=*/true, halo_comm);
    ExpectDecisionAgreesAcrossRanks(ready);
    ASSERT_TRUE(ready) << "happy-path readiness allreduce must accept";

    const std::vector<double> gathered = AssembleSingleGather(send_buf, s.nx, s.ny, s.nlev, size, rank, halo_comm);

    // np-invariance (Req 9.2): identical replicated field on every rank.
    ExpectCrossRankBitIdentical(gathered);

    // np1-equivalence (Req 9.3): equal to the full-grid serial reference within
    // 1e-10 (identity regrid => actually bit-for-bit).
    const std::vector<double> reference = SerialReferenceField(s.source, s.nx, s.ny, s.nlev);
    ASSERT_EQ(gathered.size(), reference.size());
    for (std::size_t k = 0; k < gathered.size(); ++k) {
        const double diff = std::abs(gathered[k] - reference[k]);
        const double scale = std::max({1.0, std::abs(gathered[k]), std::abs(reference[k])});
        ASSERT_LE(diff, kRegridTolerance * scale) << "gathered field element " << k << " diverges from np1 serial reference";
    }
}

// ============================================================================
// Deadlock freedom / no-skip, not-ready rank == RANK 0: rank 0 reports
// local_ok=false. Every rank must still enter the fused gate's MIN/MAX and the
// pre-gather readiness allreduce, and reach the SAME reject decision. The test
// completing (not hanging) at np>1 is the deadlock-freedom evidence; we also
// assert the reject outcome is unanimous.
//
// _Requirements: 8.3, 8.4, 9.1_
// ============================================================================
TEST(HaloMultirankIntegration, DeadlockFreedomNotReadyRankZero) {
    const int size = WorldSize();
    const int rank = WorldRank();
    const Scenario s = MakeScenario(/*nx=*/8, /*ny=*/std::max(8, size), /*nlev=*/2);
    const std::optional<halo::Communicator> halo_comm = MakeHaloComm();

    // Inject not-ready on rank 0 via the readiness flag of the fused gate.
    const int readiness = (rank == 0) ? 0 : 1;
    const std::array<int, 5> local_gate = {readiness, s.nx, s.ny, s.nlev, /*identity=*/1};

    // EVERY rank enters both reduces of the fused gate.
    const bool gate_accept = FusedFrontGate(local_gate, halo_comm);
    ExpectDecisionAgreesAcrossRanks(gate_accept);

    // EVERY rank also enters the pre-gather readiness allreduce (even the failing
    // rank), simulating the driver carrying local_ok past a per-rank failure
    // without an early return.
    const bool local_ok = (rank != 0);
    const bool ready = ReadinessAllreduce(local_ok, halo_comm);
    ExpectDecisionAgreesAcrossRanks(ready);

    if (size > 1) {
        // A not-ready rank 0 forces a unanimous reject at both gates.
        ASSERT_FALSE(gate_accept) << "fused gate must reject when rank 0 is not ready";
        ASSERT_FALSE(ready) << "readiness allreduce must reject when rank 0 is not ready";
    } else {
        // np1: the local not-ready decision is returned directly (no collective).
        ASSERT_FALSE(gate_accept);
        ASSERT_FALSE(ready);
    }
}

// ============================================================================
// Deadlock freedom / no-skip, not-ready rank == a NON-ZERO rank (the last rank).
// Same collectives, same unanimous reject. At np1 the "last rank" is rank 0, so
// this degrades to the local not-ready case.
//
// _Requirements: 8.3, 8.4, 9.1_
// ============================================================================
TEST(HaloMultirankIntegration, DeadlockFreedomNotReadyNonZeroRank) {
    const int size = WorldSize();
    const int rank = WorldRank();
    const Scenario s = MakeScenario(/*nx=*/8, /*ny=*/std::max(8, size), /*nlev=*/2);
    const std::optional<halo::Communicator> halo_comm = MakeHaloComm();

    const int not_ready_rank = size - 1;  // a non-zero rank when size > 1
    const int readiness = (rank == not_ready_rank) ? 0 : 1;
    const std::array<int, 5> local_gate = {readiness, s.nx, s.ny, s.nlev, /*identity=*/1};

    const bool gate_accept = FusedFrontGate(local_gate, halo_comm);
    ExpectDecisionAgreesAcrossRanks(gate_accept);

    const bool local_ok = (rank != not_ready_rank);
    const bool ready = ReadinessAllreduce(local_ok, halo_comm);
    ExpectDecisionAgreesAcrossRanks(ready);

    ASSERT_FALSE(gate_accept) << "fused gate must reject when a non-zero rank is not ready";
    ASSERT_FALSE(ready) << "readiness allreduce must reject when a non-zero rank is not ready";
}

// ============================================================================
// Deadlock freedom under a SHAPE MISMATCH (front-gate mismatch, not readiness):
// one rank reports a divergent field_nlev in its gate vector. The gate's MIN !=
// MAX on that entry must produce a unanimous reject with every rank entering the
// same two reduces. This proves the no-skip guarantee also holds for the match-
// value path, not just the readiness flag.
//
// _Requirements: 8.3, 8.4, 9.1_
// ============================================================================
TEST(HaloMultirankIntegration, DeadlockFreedomShapeMismatch) {
    const int size = WorldSize();
    const int rank = WorldRank();
    const Scenario s = MakeScenario(/*nx=*/8, /*ny=*/std::max(8, size), /*nlev=*/2);
    const std::optional<halo::Communicator> halo_comm = MakeHaloComm();

    // Rank 0 claims a different field_nlev so mn[3] != mx[3] across ranks.
    const int nlev_claim = (rank == 0) ? (s.nlev + 1) : s.nlev;
    const std::array<int, 5> local_gate = {1, s.nx, s.ny, nlev_claim, /*identity=*/1};

    const bool gate_accept = FusedFrontGate(local_gate, halo_comm);
    ExpectDecisionAgreesAcrossRanks(gate_accept);

    if (size > 1) {
        ASSERT_FALSE(gate_accept) << "fused gate must reject on a cross-rank shape mismatch";
    } else {
        // np1: no collective, the single rank's own consistent vector accepts.
        ASSERT_TRUE(gate_accept);
    }
}

}  // namespace cece::io

// ============================================================================
// Own main() with MPI init (MPI-main pattern, mirrors
// tests/test_halo_gather_equivalence.cpp): ALL ranks stay alive and run
// RUN_ALL_TESTS so every rank enters the MPI collectives this test issues.
// Kokkos must be initialized because apply_regrid_plan uses HostSpace views. Slurm/PMI env is scrubbed so a plain mpirun -np N works in
// the container without a batch scheduler.
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
        // HALO wrappers assume Environment::initialize() ran after MPI init
        // (as main.cpp does in production).
        halo::Environment::initialize();
    }

    if (!Kokkos::is_initialized()) {
        Kokkos::initialize(argc, argv);
    }

    ::testing::InitGoogleTest(&argc, argv);
    const int rc = RUN_ALL_TESTS();

    if (Kokkos::is_initialized()) {
        Kokkos::finalize();
    }
    int mpi_initialized = 0;
    MPI_Initialized(&mpi_initialized);
    if (mpi_initialized) {
        int finalized = 0;
        MPI_Finalized(&finalized);
        if (!finalized) MPI_Finalize();
    }
    return rc;
}
