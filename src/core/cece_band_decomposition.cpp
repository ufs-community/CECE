// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors

#include "cece/cece_band_decomposition.hpp"

#include <algorithm>

namespace cece {

BandDecomposition BandDecomposition::whole_grid(int ny_global) {
    if (ny_global < 0) ny_global = 0;
    BandDecomposition band;
    band.ny_global = ny_global;
    band.j0 = 0;
    band.j1 = ny_global;
    band.ny_local = ny_global;
    // Single participant owns every row.
    band.row_counts.assign(1, ny_global);
    band.row_displs.assign(1, 0);
    return band;
}

BandDecomposition BandDecomposition::compute(int ny_global, MPI_Comm comm) {
    if (ny_global < 0) ny_global = 0;

    // Short-circuit to the whole grid when MPI is unavailable/degenerate,
    // issuing NO collective — mirrors the existing single-rank guards
    // (MPI_Initialized / MPI_COMM_NULL / size <= 1).
    int mpi_initialized = 0;
    MPI_Initialized(&mpi_initialized);
    if (!mpi_initialized || comm == MPI_COMM_NULL) {
        return whole_grid(ny_global);
    }

    int size = 1;
    int rank = 0;
    MPI_Comm_size(comm, &size);
    MPI_Comm_rank(comm, &rank);
    if (size <= 1) {
        return whole_grid(ny_global);
    }

    // Exact existing block decomposition:
    //   band_base    = ny / size
    //   band_rem     = ny % size
    //   band_start(r)= r * band_base + min(r, band_rem)
    const int band_base = ny_global / size;
    const int band_rem = ny_global % size;
    auto band_start = [&](int r) { return r * band_base + std::min(r, band_rem); };

    BandDecomposition band;
    band.ny_global = ny_global;
    band.j0 = band_start(rank);
    band.j1 = band_start(rank + 1);
    band.ny_local = band.j1 - band.j0;

    band.row_counts.resize(size);
    band.row_displs.resize(size);
    for (int r = 0; r < size; ++r) {
        const int rj0 = band_start(r);
        const int rj1 = band_start(r + 1);
        band.row_counts[r] = rj1 - rj0;
        band.row_displs[r] = rj0;
    }

    return band;
}

}  // namespace cece
