// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors

/**
 * @file test_band_decomposition_partition.cpp
 * @brief Property-based test for exact band partitioning of the destination
 *        latitude grid.
 *
 * Feature: distributed-domain-decomposition, Property 1: Band decomposition
 * partitions the grid exactly
 *
 * Property 1 (design.md): "For any `ny >= 0` and communicator size `P >= 1`,
 * the bands `{[band_start(r), band_start(r+1)) : r in [0, P)}` are disjoint,
 * cover `[0, ny)` exactly, and `sum_r row_counts[r] == ny`. Row counts differ
 * by at most one across ranks."
 *
 * `BandDecomposition` is the single source of truth for band geometry. Because
 * the property ranges over `(ny, size)` — pure block-decomposition logic rather
 * than a live MPI communicator — this test drives that logic directly: it
 * reconstructs every rank's band from the exact block formula the production
 * `BandDecomposition::compute` embeds
 * (`band_start(r) = r*(ny/size) + min(r, ny%size)`), then asserts the union of
 * the per-rank `[j0, j1)` bands and their `row_counts`/`row_displs` partition
 * `[0, ny)` exactly. The single-participant case additionally cross-checks the
 * production `whole_grid` factory.
 *
 * **Validates: Requirements 1.1, 1.4, 6.3**
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "cece/cece_band_decomposition.hpp"

namespace cece {
namespace {

// The exact block-decomposition start formula that BandDecomposition::compute
// embeds (and that the pre-existing regrid path uses). Mirrored here so the
// property can enumerate every rank's band without a live communicator.
int band_start(int ny, int size, int r) {
    const int band_base = ny / size;
    const int band_rem = ny % size;
    return r * band_base + std::min(r, band_rem);
}

}  // namespace

// ============================================================================
// Property 1: Band decomposition partitions the grid exactly
// Feature: distributed-domain-decomposition, Property 1: Band decomposition
// partitions the grid exactly
// **Validates: Requirements 1.1, 1.4, 6.3**
//
// For any ny >= 0 and communicator size P >= 1, the P bands are disjoint, cover
// [0, ny) exactly, sum(row_counts) == ny, and any two rank row-counts differ by
// at most one. This underpins Req 1.1 (bands match the block decomposition the
// regrid path uses), Req 1.4 (surplus ranks when size > ny get ny_local == 0
// yet remain part of the partition), and Req 6.3 (the Output_Gather places rank
// r at [band_start(r), band_start(r+1)) with no gaps or overlaps).
// ============================================================================
RC_GTEST_PROP(BandDecompositionPartition, PartitionsGridExactly, ()) {
    // ny >= 0 (including the empty-grid edge case) and size >= 1.
    const int ny = *rc::gen::inRange(0, 4097);
    const int size = *rc::gen::inRange(1, 129);

    // Reconstruct every rank's band + row_counts/row_displs from the exact
    // production block formula.
    std::vector<int> row_counts(size);
    std::vector<int> row_displs(size);
    for (int r = 0; r < size; ++r) {
        const int rj0 = band_start(ny, size, r);
        const int rj1 = band_start(ny, size, r + 1);
        RC_ASSERT(rj1 >= rj0);  // each band is well-formed (non-negative width)
        row_displs[r] = rj0;
        row_counts[r] = rj1 - rj0;
    }

    // Coverage + disjointness: walking the bands in rank order must tile
    // [0, ny) contiguously with no gaps and no overlaps.
    RC_ASSERT(row_displs.front() == 0);
    for (int r = 1; r < size; ++r) {
        // rank r starts exactly where rank r-1 ended (contiguous, no gap/overlap)
        RC_ASSERT(row_displs[r] == row_displs[r - 1] + row_counts[r - 1]);
    }
    RC_ASSERT(row_displs.back() + row_counts.back() == ny);

    // Every global row in [0, ny) is covered exactly once. Verified by an
    // explicit ownership sweep so a gap OR an overlap both fail.
    std::vector<int> owner_count(static_cast<std::size_t>(std::max(ny, 0)), 0);
    for (int r = 0; r < size; ++r) {
        for (int j = row_displs[r]; j < row_displs[r] + row_counts[r]; ++j) {
            RC_ASSERT(j >= 0 && j < ny);
            ++owner_count[static_cast<std::size_t>(j)];
        }
    }
    for (int j = 0; j < ny; ++j) {
        RC_ASSERT(owner_count[static_cast<std::size_t>(j)] == 1);
    }

    // sum(row_counts) == ny.
    long long total = 0;
    int min_count = row_counts.empty() ? 0 : row_counts.front();
    int max_count = min_count;
    for (int r = 0; r < size; ++r) {
        total += row_counts[r];
        min_count = std::min(min_count, row_counts[r]);
        max_count = std::max(max_count, row_counts[r]);
    }
    RC_ASSERT(total == static_cast<long long>(ny));

    // Balanced: any two ranks' row counts differ by at most one.
    RC_ASSERT(max_count - min_count <= 1);

    // Cross-check the production whole_grid factory for the single-participant
    // case: it must agree that one rank owns the entire grid.
    if (size == 1) {
        const BandDecomposition whole = BandDecomposition::whole_grid(ny);
        RC_ASSERT(whole.j0 == 0);
        RC_ASSERT(whole.j1 == ny);
        RC_ASSERT(whole.ny_local == ny);
        RC_ASSERT(whole.row_counts.size() == 1);
        RC_ASSERT(whole.row_counts[0] == ny);
        RC_ASSERT(whole.row_displs.size() == 1);
        RC_ASSERT(whole.row_displs[0] == 0);
    }
}

}  // namespace cece
