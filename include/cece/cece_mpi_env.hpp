// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors

#ifndef CECE_MPI_ENV_HPP
#define CECE_MPI_ENV_HPP

#include <mpi.h>

namespace cece {

// ---------------------------------------------------------------
// MPI environment queries (consolidated).
//
// CECE's collective paths are entered from contexts where MPI may
// legitimately not be initialized yet: the standalone driver runs the
// same code paths before/without MPI_Init in unit tests (see
// tests/test_band_single_rank_properties.cpp, which asserts
// BandDecomposition::compute() short-circuits when MPI is absent), and
// the library is linked into tools that never call MPI_Init at all.
// Every distributed gate therefore has to ask "is MPI live?" before
// touching a communicator, so the query is centralized here rather than
// hand-rolled as `int x = 0; MPI_Initialized(&x);` at each site.
// ---------------------------------------------------------------

/// @brief True once MPI_Init/MPI_Init_thread has completed (and MPI has not
///        been finalized). Cheap; safe to call before initialization.
inline bool mpi_environment_ready() {
    int initialized = 0;
    MPI_Initialized(&initialized);
    return initialized != 0;
}

/// @brief True when @p comm is a live, multi-rank communicator — the single
///        predicate shared by every distributed collective gate. Returns false
///        (i.e. "run the serial path") when MPI is uninitialized, @p comm is
///        MPI_COMM_NULL, or the communicator holds one rank.
///
/// When @p out_size is non-null and the environment is live, the communicator
/// size is written there (defaulting to 1 on the serial path) so callers that
/// also need the count do not issue a second query.
inline bool comm_is_distributed(MPI_Comm comm, int* out_size = nullptr) {
    if (!mpi_environment_ready() || comm == MPI_COMM_NULL) {
        if (out_size) *out_size = 1;
        return false;
    }
    int size = 1;
    MPI_Comm_size(comm, &size);
    if (out_size) *out_size = size;
    return size > 1;
}

}  // namespace cece

#endif  // CECE_MPI_ENV_HPP
