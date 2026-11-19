#pragma once
#ifndef CECE_ENDPOINT_COMMON_HPP
#define CECE_ENDPOINT_COMMON_HPP

#include "cece/cece_driver_facade.hpp"

namespace cece {

// ============================================================================
// Test-only friend shim for the private static bracket_equal helper. Declared a
// friend inside CeceDriverOrchestrator (see
// include/cece/cece_driver_facade.hpp, Task 4.3). Exercises the production
// comparison directly, not a copy.
// ============================================================================
struct EndpointCacheTestAccess {
    static constexpr bool Equal(const RecordBracket& a, const RecordBracket& b) {
        return CeceDriverOrchestrator::bracket_equal(a, b);
    }
};

namespace {

// Fixed rank-invariant destination-grid shape for the reproduced
// ladder. Kept tiny so the blended buffers stay small across the
// >=100 RapidCheck iterations (well within the ~7 GB cece-dev
// container budget). Distinct nx/ny/nlev so a mis-sized buffer would
// be caught.
constexpr int kFieldNlev = 2;
constexpr int kNx = 4;
constexpr int kNy = 3;
constexpr std::size_t kBufferSize = static_cast<std::size_t>(kFieldNlev) * static_cast<std::size_t>(kNx) * static_cast<std::size_t>(kNy);

// Counters standing in for the pieces of per-step work the Tier ladder gates:
//   read_slab   -> one disk read per source record (amio_read)
//   regrid_front-> one RegridToDestinationBuffer (per-record apply_regrid_plan +
//                  Allgatherv) invocation
// On a Tier 1/Tier 2 step both stay 0; a Tier 3 interpolation step adds 2 reads
// + 2 regrids (record i0 and record i1); a Tier 3 single-record step adds 1 + 1.
struct WorkSpy {
    int read_count = 0;
    int regrid_front_count = 0;
};

// A faithful reproduction of the production Tier ladder for ONE step and ONE
// variable, mutating the caller's slice_cache / endpoint_cache exactly as the
// production MISS/refresh paths do, and incrementing the WorkSpy for each
// read_slab / RegridToDestinationBuffer the production path would perform. Uses
// the REAL bracket_equal for the Tier 1 exact-match gate.
bool RunTierLadderStep(const RecordBracket& bracket, bool bracket_ready, SliceCacheEntry& slice_cache, EndpointCacheEntry& endpoint_cache,
                       WorkSpy& spy) {
    const bool needs_upper_record = (bracket.i1 != bracket.i0 && bracket.weight > 0.0);

    // ---- Tier 1: exact slice-cache hit (indices AND weight) — no work ----
    if (bracket_ready && slice_cache.valid && EndpointCacheTestAccess::Equal(bracket, slice_cache.last_bracket)) {
        // Reuse slice_cache.ingest_buffer: no read, no regrid, no blend recompute.
        return true;
    }

    // ---- Tier 2: endpoint-cache hit (same indices, different weight) — blend only ----
    if (bracket_ready && needs_upper_record && endpoint_cache.valid && endpoint_cache.cached_i0 == bracket.i0 &&
        endpoint_cache.cached_i1 == bracket.i1 && endpoint_cache.built_nx == kNx && endpoint_cache.built_ny == kNy &&
        endpoint_cache.built_field_nlev == kFieldNlev) {
        // NO read_slab, NO apply_regrid_plan on this step: blend cached endpoints.
        std::vector<double> blended(kBufferSize);
        const double w = bracket.weight;
        for (std::size_t k = 0; k < kBufferSize; ++k) {
            blended[k] = (1.0 - w) * endpoint_cache.endpoint_i0[k] + w * endpoint_cache.endpoint_i1[k];
        }
        // Refresh slice cache so an immediate exact repeat re-hits Tier 1.
        slice_cache.last_bracket = bracket;
        slice_cache.ingest_buffer = blended;
        slice_cache.ingest_size = blended.size();
        slice_cache.valid = true;
        return true;
    }

    // ---- Tier 3: interpolation miss / rollover — rebuild both endpoints ----
    if (bracket_ready && needs_upper_record) {
        // read_slab(i0) -> srcA ; read_slab(i1) -> srcB
        spy.read_count += 2;
        // RegridToDestinationBuffer(i0) -> epA ; RegridToDestinationBuffer(i1) -> epB
        spy.regrid_front_count += 2;

        // Endpoint buffers are deterministic functions of the record index so a
        // Tier 2 re-hit of the same indices reproduces byte-identical endpoints
        // (mirrors regrid(record) being a pure function of the record).
        std::vector<double> epA(kBufferSize);
        std::vector<double> epB(kBufferSize);
        for (std::size_t k = 0; k < kBufferSize; ++k) {
            epA[k] = static_cast<double>(bracket.i0) + static_cast<double>(k);
            epB[k] = static_cast<double>(bracket.i1) - static_cast<double>(k);
        }
        endpoint_cache.cached_i0 = bracket.i0;
        endpoint_cache.cached_i1 = bracket.i1;
        endpoint_cache.valid = true;
        endpoint_cache.endpoint_i0 = epA;
        endpoint_cache.endpoint_i1 = epB;
        endpoint_cache.built_field_nlev = kFieldNlev;
        endpoint_cache.built_nx = kNx;
        endpoint_cache.built_ny = kNy;

        std::vector<double> blended(kBufferSize);
        const double w = bracket.weight;
        for (std::size_t k = 0; k < kBufferSize; ++k) {
            blended[k] = (1.0 - w) * epA[k] + w * epB[k];
        }
        slice_cache.last_bracket = bracket;
        slice_cache.ingest_buffer = blended;
        slice_cache.ingest_size = blended.size();
        slice_cache.valid = true;
        return true;
    }

    // ---- Tier 3: single record — today's single-record path ----
    if (bracket_ready) {
        // read_slab(i0) -> src ; AssembleReplicatedField (one regrid-front).
        spy.read_count += 1;
        spy.regrid_front_count += 1;  // AssembleReplicatedField (one regrid-front)

        std::vector<double> ingest(kBufferSize);
        for (std::size_t k = 0; k < kBufferSize; ++k) {
            ingest[k] = static_cast<double>(bracket.i0) + static_cast<double>(k);
        }
        slice_cache.last_bracket = bracket;
        slice_cache.ingest_buffer = ingest;
        slice_cache.ingest_size = ingest.size();
        slice_cache.valid = true;
        // NOTE: no endpoint_cache mutation — the single-record path does not
        // build an endpoint entry (Req 2.5).
        return true;
    }

    // Not bracket_ready: production skips this step entirely (no read/regrid,
    // no cache mutation). Nothing to do.
    return false;
}

}  // namespace
}  // namespace cece
#endif
