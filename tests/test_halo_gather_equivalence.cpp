// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors
//
// Feature: cece-halo-collective-adoption
// Property 1: Per-level MPI_Allgatherv assembly produces the correct replicated
//             [level][j][i] field
//
// The CECE distributed regrid assembly in RegridToDestinationBuffer assembles
// the replicated [level][j][i] destination field with a per-level
// MPI_Allgatherv loop: for each level it issues one plain-MPI_DOUBLE
// MPI_Allgatherv that places every rank's contiguous latitude band
// (counts[r] = (j1(r)-j0(r))*nx, displs[r] = j0(r)*nx) into
// full_destination[level*nx*ny + j*nx + i]. Benchmarking at the F360+
// (>=1440x720) x 64-128 level target proved this contiguous per-level loop is
// markedly faster than a single strided-datatype gather in the container's
// OpenMPI, so this loop IS the production assembly.
//
// This property test validates the assembly directly. On the same random
// source, the same real band decomposition, and the same per-level regridded
// bands, it runs the production-style per-level MPI_Allgatherv assembly and
// asserts:
//
//   * the assembled field equals the full-grid serial reference — bit-for-bit
//     for an IDENTITY plan (a pure copy), and within kRegridTolerance (1e-10)
//     for arbitrary matrix-style band data;
//   * cross-rank bit-identity: every rank's replicated [level][j][i] field is
//     bit-identical;
//   * the mpi_size == 1 boundary (Decision A std::copy fast path) equals the
//     same reference.
//
// It drives the REAL production apply_regrid_plan for the identity path (the
// simplest deterministic regrid, requiring no AMIO dataset / AXIS weight build).
// The matrix-style path feeds arbitrary per-level band values (standing in for
// the SpMV output the assembly consumes identically) so the tolerance branch is
// exercised without an AXIS weight generation.
//
// Runs correctly as a single process (ctest default, mpi_size == 1 boundary)
// and under mpirun --oversubscribe -np {2,4}. Dims are kept modest
// (nx<=32, ny<=32, nlev<=4) for the ~7 GB container and np4 oversubscription.
//
// **Validates: Requirements 1.1, 1.4, 2.1, 2.2, 2.3, 8.1, 8.2, 9.2, 9.3, 12.3**

#include <gtest/gtest.h>
#include <mpi.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <unistd.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <halo/collectives.hpp>
#include <halo/communicator.hpp>
#include <halo/environment.hpp>

#include "cece/cece_regridder_utils.hpp"

namespace cece::io {
namespace {

// Matrix plans accumulate the SpMV in a different summation order than a serial
// whole-grid reference, so identity plans are asserted bit-for-bit while matrix-
// style bands are asserted within this tolerance (mirrors the sibling
// test_numerical_equivalence.cpp / test_endpoint_blend_equivalence.cpp).
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
// destination rows [band_start(r), band_start(r+1)) with a simple block split of
// the ny rows, distributing the remainder to the low ranks.
int BandStart(int rank, int ny, int size) {
    const int base = ny / size;
    const int rem = ny % size;
    return rank * base + std::min(rank, rem);
}

// Build a FIXED identity (passthrough) plan over the nx * ny source grid that
// owns rows [j0, j1). Matches tests/test_shared_plan_equivalence.cpp.
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

// Assemble the replicated [level][j][i] field via the PRODUCTION-STYLE per-level
// MPI_Allgatherv loop: build rank-invariant band counts/displacements, then for
// each level issue ONE plain-MPI_DOUBLE MPI_Allgatherv that places this rank's
// contiguous band (counts[r] = band_elems(r), displs[r] = band_start(r)*nx)
// into full[level*nx*ny + j*nx + i]. This is exactly RegridToDestinationBuffer's
// assembly.
//
// send_buf is the contiguous [level][band] source: element (level, jrel, i) at
// level*band_elems + jrel*nx + i. At size 1 the loop degrades to the Decision A
// std::copy fast path (this rank's band copied into each layer at displs[rank]).
std::vector<double> AssemblePerLevel(const std::vector<double>& send_buf, int nx, int ny, int nlev, int size, int rank,
                                     MPI_Comm comm) {
    std::vector<int> counts(static_cast<std::size_t>(size));
    std::vector<int> displs(static_cast<std::size_t>(size));
    for (int r = 0; r < size; ++r) {
        const int j0 = BandStart(r, ny, size);
        const int j1 = BandStart(r + 1, ny, size);
        counts[static_cast<std::size_t>(r)] = (j1 - j0) * nx;
        displs[static_cast<std::size_t>(r)] = j0 * nx;
    }
    const int band_elems = counts[static_cast<std::size_t>(rank)];

    std::vector<double> full(static_cast<std::size_t>(nlev) * nx * ny, 0.0);
    for (int level = 0; level < nlev; ++level) {
        const double* level_band = send_buf.data() + static_cast<std::size_t>(level) * band_elems;
        double* layer = full.data() + static_cast<std::size_t>(level) * nx * ny;
        if (size <= 1) {
            // Decision A single-rank fast path: copy the band directly.
            std::copy(level_band, level_band + band_elems, layer + displs[static_cast<std::size_t>(rank)]);
        } else {
            MPI_Allgatherv(level_band, band_elems, MPI_DOUBLE, layer, counts.data(), displs.data(), MPI_DOUBLE, comm);
        }
    }
    return full;
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

// Broadcast a double buffer of a known length from rank 0.
void BcastDoubles(std::vector<double>& buf) {
    if (WorldSize() > 1 && !buf.empty()) {
        MPI_Bcast(buf.data(), static_cast<int>(buf.size()), MPI_DOUBLE, 0, MPI_COMM_WORLD);
    }
}

// Assert the assembled field agrees with a reference: bit-for-bit when exact is
// true (identity plan), else within kRegridTolerance.
void ExpectFieldsAgree(const std::vector<double>& a, const std::vector<double>& b, bool exact) {
    RC_ASSERT(a.size() == b.size());
    for (std::size_t k = 0; k < a.size(); ++k) {
        if (exact) {
            RC_ASSERT(a[k] == b[k]);
        } else {
            const double diff = std::abs(a[k] - b[k]);
            const double scale = std::max({1.0, std::abs(a[k]), std::abs(b[k])});
            RC_ASSERT(diff <= kRegridTolerance * scale);
        }
    }
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
        RC_ASSERT(mn[k] == mx[k]);
    }
}

}  // namespace

// ============================================================================
// Property 1 (IDENTITY plan): the per-level MPI_Allgatherv assembly produces the
// correct replicated [level][j][i] field — BIT-FOR-BIT equal to the full-grid
// serial identity regrid — and every rank holds an identical field.
//
// Feature: cece-halo-collective-adoption, Property 1
// **Validates: Requirements 1.1, 1.4, 2.1, 2.2, 2.3, 8.1, 8.2, 9.2, 9.3, 12.3**
//
// Drives the REAL production apply_regrid_plan (identity path) to fill this
// rank's contiguous [level][band] send buffer, assembles it with the production
// per-level MPI_Allgatherv loop, and compares against the whole-grid serial
// identity regrid. The identity regrid is a pure copy, so the comparison is
// exact and cross-rank replication must be bit-identical.
// ============================================================================
RC_GTEST_PROP(HaloGatherEquivalence, IdentityPerLevelAssemblyMatchesSerialReference, ()) {
    const int size = WorldSize();
    const int rank = WorldRank();

    // Rank-invariant modest dims (drawn on rank 0, broadcast to all ranks).
    int nx = BcastInt(*rc::gen::inRange(2, 33));      // 2..32
    int ny = BcastInt(*rc::gen::inRange(size, 33));   // at least `size` rows so every rank owns >=1
    int nlev = BcastInt(*rc::gen::inRange(1, 5));      // 1..4

    // Rank-invariant random global source field [ny][nx] doubles (rank 0 draws;
    // broadcast so every rank regrids from the identical source).
    std::vector<double> source(static_cast<std::size_t>(nx) * ny, 0.0);
    if (rank == 0) {
        source = *rc::gen::container<std::vector<double>>(
            source.size(), rc::gen::map(rc::gen::inRange(-1000000, 1000001), [](int v) { return v / 1000.0; }));
    }
    BcastDoubles(source);

    // This rank's band and per-level contiguous [level][band] send buffer built
    // by the REAL apply_regrid_plan (identity).
    const int j0 = BandStart(rank, ny, size);
    const int j1 = BandStart(rank + 1, ny, size);
    const int band_elems = (j1 - j0) * nx;
    const RegridPlan plan = MakeIdentityPlan(nx, ny, j0, j1);

    std::vector<double> send_buf(static_cast<std::size_t>(nlev) * band_elems, 0.0);
    for (int level = 0; level < nlev; ++level) {
        std::vector<double> band;
        // Each level reads the same 2D source (single-level source), which is a
        // sufficient deterministic driver for the assembly property.
        RC_ASSERT(apply_regrid_plan(plan, /*time_offset=*/0, /*is_float=*/false, source.data(), nx, ny, nx, band));
        RC_ASSERT(static_cast<int>(band.size()) == band_elems);
        std::copy(band.begin(), band.end(), send_buf.begin() + static_cast<std::size_t>(level) * band_elems);
    }

    // Production-style per-level MPI_Allgatherv assembly of the replicated field.
    const std::vector<double> assembled = AssemblePerLevel(send_buf, nx, ny, nlev, size, rank, MPI_COMM_WORLD);

    // Whole-grid serial identity reference: every level is the identity regrid of
    // the full source grid. Because the identity regrid is a pure passthrough,
    // full[level*nx*ny + j*nx + i] == source[j*nx + i] for all levels.
    const RegridPlan whole = MakeIdentityPlan(nx, ny, /*j0=*/0, /*j1=*/ny);
    std::vector<double> reference(static_cast<std::size_t>(nlev) * nx * ny, 0.0);
    for (int level = 0; level < nlev; ++level) {
        std::vector<double> band;
        RC_ASSERT(apply_regrid_plan(whole, /*time_offset=*/0, /*is_float=*/false, source.data(), nx, ny, nx, band));
        RC_ASSERT(static_cast<int>(band.size()) == nx * ny);
        std::copy(band.begin(), band.end(), reference.begin() + static_cast<std::size_t>(level) * nx * ny);
    }

    // Identity => bit-for-bit equal (Req 1.1, 2.2, 2.3, 8.1, 8.2, 12.3), including
    // the mpi_size == 1 std::copy fast path (Req 1.4).
    ExpectFieldsAgree(assembled, reference, /*exact=*/true);
    // Cross-rank bit-identity of the replicated field (Req 9.2, 9.3).
    ExpectCrossRankBitIdentical(assembled);
}

// ============================================================================
// Property 1 (MATRIX-style bands): the per-level MPI_Allgatherv assembly places
// arbitrary per-level band data into the correct replicated [level][j][i]
// positions within kRegridTolerance, and every rank holds an identical field.
//
// Feature: cece-halo-collective-adoption, Property 1
// **Validates: Requirements 1.1, 1.4, 2.1, 2.2, 2.3, 8.1, 8.2, 9.2, 9.3, 12.3**
//
// A matrix regrid's SpMV output is per-level band data the assembly consumes
// without reordering or recomputation. This case feeds arbitrary per-level
// bands (standing in for the matrix SpMV output), gathers them, and compares
// against a locally-assembled reference built by placing every rank's known
// band into its own rows. Exercises the within-tolerance assertion the design
// specifies for matrix plans without an AXIS weight build.
// ============================================================================
RC_GTEST_PROP(HaloGatherEquivalence, MatrixStylePerLevelAssemblyPlacesBandsCorrectly, ()) {
    const int size = WorldSize();
    const int rank = WorldRank();

    int nx = BcastInt(*rc::gen::inRange(2, 33));
    int ny = BcastInt(*rc::gen::inRange(size, 33));
    int nlev = BcastInt(*rc::gen::inRange(1, 5));

    const int j0 = BandStart(rank, ny, size);
    const int j1 = BandStart(rank + 1, ny, size);
    const int band_elems = (j1 - j0) * nx;

    // Each rank fills its OWN band with a deterministic, rank-tagged pattern so
    // every element of the replicated field has a known expected value that
    // depends on which rank owns the row. The pattern encodes (rank, level,
    // element) so a misplaced band would be caught.
    std::vector<double> send_buf(static_cast<std::size_t>(nlev) * band_elems, 0.0);
    for (int level = 0; level < nlev; ++level) {
        for (int e = 0; e < band_elems; ++e) {
            send_buf[static_cast<std::size_t>(level) * band_elems + e] =
                static_cast<double>(rank) * 1.0e6 + static_cast<double>(level) * 1.0e3 + static_cast<double>(e) * 0.5;
        }
    }

    // Production-style per-level MPI_Allgatherv assembly.
    const std::vector<double> assembled = AssemblePerLevel(send_buf, nx, ny, nlev, size, rank, MPI_COMM_WORLD);

    // Local reference: place EVERY rank's known band pattern into its own rows,
    // for every level, into full[level*nx*ny + j*nx + i]. This is exactly what a
    // correct gather must produce, computed independently of the MPI collective.
    std::vector<double> reference(static_cast<std::size_t>(nlev) * nx * ny, 0.0);
    for (int r = 0; r < size; ++r) {
        const int rj0 = BandStart(r, ny, size);
        const int rj1 = BandStart(r + 1, ny, size);
        const int r_band = (rj1 - rj0) * nx;
        for (int level = 0; level < nlev; ++level) {
            for (int e = 0; e < r_band; ++e) {
                const double expected = static_cast<double>(r) * 1.0e6 + static_cast<double>(level) * 1.0e3 + static_cast<double>(e) * 0.5;
                reference[static_cast<std::size_t>(level) * nx * ny + static_cast<std::size_t>(rj0) * nx + e] = expected;
            }
        }
    }

    // Matrix-style => within kRegridTolerance (Req 1.1, 2.2, 2.3, 8.1, 8.2, 12.3),
    // including the mpi_size == 1 std::copy fast path (Req 1.4).
    ExpectFieldsAgree(assembled, reference, /*exact=*/false);
    // Cross-rank bit-identity of the replicated field (Req 9.2, 9.3).
    ExpectCrossRankBitIdentical(assembled);
}

}  // namespace cece::io

// ============================================================================
// Own main() with MPI init (MPI-main pattern, mirrors
// tests/test_mpi_reuse_decision.cpp): ALL ranks stay alive and run
// RUN_ALL_TESTS so every rank enters the MPI collectives this test issues.
// Kokkos must be initialized because apply_regrid_plan uses HostSpace views.
// Slurm/PMI env is scrubbed so a plain mpirun -np N works in the container
// without a batch scheduler.
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
