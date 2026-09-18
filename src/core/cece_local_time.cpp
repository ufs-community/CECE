/**
 * @file cece_local_time.cpp
 * @brief Local-time service: offset provider seam + band-local lookup utility.
 *
 * Implements the contracts in specs/001-local-time-support/contracts/
 * {offset-provider,local-time-service}.md. All arithmetic is integer-only and
 * shared with device kernels via the KOKKOS_INLINE_FUNCTION helpers in the
 * header (research D4); this translation unit owns the host-side construction
 * path (decode-once mapping, device mirror, UTC fallback).
 */

#include "cece/cece_local_time.hpp"

#include <stdexcept>
#include <utility>

#include "cece/utc_grid.hpp"

namespace cece {

// ---------------------------------------------------------------------------
// StaticGridOffsetProvider
// ---------------------------------------------------------------------------

StaticGridOffsetProvider::StaticGridOffsetProvider(UtcGrid grid) : grid_(std::move(grid)) {
    if (grid_.cells.empty() || grid_.nrows <= 0) {
        throw std::invalid_argument("StaticGridOffsetProvider: decoded grid is empty");
    }
}

std::int32_t StaticGridOffsetProvider::offsetSecondsAt(double lat, double lon, std::int64_t /*utc_epoch_secs*/) const {
    // Static snapshot: the instant is accepted for the seam (a DST-aware
    // provider will read it) but unused here (spec Assumptions).
    const int row = UtcGridRowForLat(lat);
    const int col = UtcGridColForLon(lon, static_cast<int>(grid_.ncol[static_cast<std::size_t>(row)]));
    return static_cast<std::int32_t>(grid_.cells[grid_.index(row, col)]) * kUtcOffsetQuarterHoursPerSecond;
}

// ---------------------------------------------------------------------------
// LocalTimeService
// ---------------------------------------------------------------------------

std::unique_ptr<LocalTimeService> LocalTimeService::Create(std::unique_ptr<IUtcOffsetProvider> provider, const std::vector<double>& native_lons,
                                                           const std::vector<double>& native_lats, int nx, int j0, int ny_local,
                                                           std::int64_t utc_start_epoch_secs) {
    auto service = std::unique_ptr<LocalTimeService>(new LocalTimeService());
    service->start_epoch_secs_ = utc_start_epoch_secs;

    if (!provider) {
        // UTC fallback: zero offsets, no provider consulted.
        service->utc_fallback_ = true;
        service->offsets_ = OffsetView("utc_offsets_fallback", nx, ny_local);
        Kokkos::deep_copy(service->offsets_, static_cast<std::int32_t>(0));
        return service;
    }

    // Map the provider onto the native grid ONCE at the run start instant
    // (v1 static-snapshot semantics, research D7). For the static-grid
    // provider this is identical to MapToNativeOffsetsSec; routing through the
    // provider interface keeps the seam honest — a future time-varying
    // provider can make the mapping instant-dependent here.
    std::vector<std::int32_t> host_offsets(static_cast<std::size_t>(nx) * ny_local, 0);
    for (int j = 0; j < ny_local; ++j) {
        const double lat = native_lats[static_cast<std::size_t>(j0) + static_cast<std::size_t>(j)];
        for (int i = 0; i < nx; ++i) {
            const double lon = native_lons[static_cast<std::size_t>(i)];
            host_offsets[static_cast<std::size_t>(j) * nx + i] = provider->offsetSecondsAt(lat, lon, utc_start_epoch_secs);
        }
    }

    // Build the device view and release the host buffer immediately (SC-005:
    // nothing but the band-local device array outlives init).
    service->offsets_ = OffsetView("utc_offsets", nx, ny_local);
    auto host_mirror = Kokkos::create_mirror_view(service->offsets_);
    for (int j = 0; j < ny_local; ++j) {
        for (int i = 0; i < nx; ++i) {
            host_mirror(i, j) = host_offsets[static_cast<std::size_t>(j) * nx + i];
        }
    }
    Kokkos::deep_copy(service->offsets_, host_mirror);
    service->provider_ = std::move(provider);
    return service;
}

std::unique_ptr<LocalTimeService> LocalTimeService::CreateUtcFallback(int nx, int ny_local) {
    return Create(nullptr, {}, {}, nx, 0, ny_local, 0);
}

LocalTimeParts LocalTimeService::Resolve(double lat, double lon, std::int64_t utc_epoch_secs) const {
    const std::int32_t offset = provider_ ? provider_->offsetSecondsAt(lat, lon, utc_epoch_secs) : 0;
    const std::int64_t local_secs = utc_epoch_secs + offset;
    LocalTimeParts p;
    p.hour = LocalHourFromSecs(local_secs);
    p.day_of_week = LocalDayOfWeekFromSecs(local_secs);
    p.month = LocalMonthFromSecs(local_secs);
    p.day_of_year = LocalDayOfYearFromSecs(local_secs);
    return p;
}

}  // namespace cece
