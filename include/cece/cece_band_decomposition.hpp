// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors

#ifndef CECE_BAND_DECOMPOSITION_HPP
#define CECE_BAND_DECOMPOSITION_HPP

#include <mpi.h>

#include <vector>

namespace cece {

/// Describes how the destination latitude rows [0, ny_global) are partitioned
/// into contiguous per-rank bands via the existing block decomposition.
///
/// This is the single source of truth for the band geometry that the ingest,
/// regrid, core-import, compute, and output stages all agree on, replacing the
/// two inline `band_start` computations that previously lived in
/// `CeceDriverOrchestrator::RegridToDestinationBuffer` and the regrid-plan
/// build. It is a pure, deterministic function of `(ny_global, comm size)`, so
/// every rank computes an identical decomposition for a given communicator.
///
/// The block decomposition matches the exact formula the regrid path uses:
///   band_base    = ny_global / size
///   band_rem     = ny_global % size
///   band_start(r)= r * band_base + min(r, band_rem)
/// so `j0`/`j1` are identical to the current single-rank / multi-rank paths.
struct BandDecomposition {
    int ny_global = 0;  ///< full destination latitude count (== ny_)
    int j0 = 0;         ///< first global row this rank owns
    int j1 = 0;         ///< one-past-last global row this rank owns
    int ny_local = 0;   ///< j1 - j0 (0 for surplus ranks when size > ny_global)

    /// Global row counts/offsets for every rank, for the Output_Gather and any
    /// band-aware collective. Both have size == comm size.
    ///   row_counts[r] = band_start(r+1) - band_start(r)  (rows owned by rank r)
    ///   row_displs[r] = band_start(r)                    (global offset of rank r)
    std::vector<int> row_counts;
    std::vector<int> row_displs;

    /// Compute the decomposition for `comm`. Pure and deterministic: identical
    /// on every rank for a given `(ny_global, comm size)`.
    ///
    /// Short-circuits to `whole_grid(ny_global)` — issuing NO collective — when
    /// MPI is uninitialized, `comm == MPI_COMM_NULL`, or the communicator size
    /// is <= 1, matching the existing single-rank path (j0=0, j1=ny_global,
    /// ny_local=ny_global). The uninitialized/NULL-comm cases are supported
    /// entry points, not errors: CECE's serial path (standalone runs, unit
    /// tests, tools linking without MPI_Init) reaches `compute` outside an MPI
    /// environment, and every such caller must land on the same whole-grid
    /// geometry the size-1 path produces.
    ///
    /// A negative `ny_global` is a caller/configuration bug (grid dimensions
    /// are unsigned everywhere upstream). It is clamped to 0 and logged at
    /// ERROR level — never fed into the band arithmetic, where it would yield
    /// negative row_counts and UB in the MPI_Allgatherv consumers.
    static BandDecomposition compute(int ny_global, MPI_Comm comm);

    /// Convenience for the single-rank / no-MPI short-circuit: the whole grid
    /// on one rank (j0=0, j1=ny_global, ny_local=ny_global). Negative
    /// `ny_global` is clamped to 0 with an ERROR log, as for `compute`.
    static BandDecomposition whole_grid(int ny_global);
};

}  // namespace cece

#endif  // CECE_BAND_DECOMPOSITION_HPP
