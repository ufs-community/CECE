// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors

#ifndef CECE_LOCAL_TIME_HPP
#define CECE_LOCAL_TIME_HPP

#include <Kokkos_Core.hpp>
#include <cstdint>
#include <memory>
#include <vector>

#include "cece/utc_grid.hpp"

namespace cece {

/**
 * @brief Seconds in a day (UTC arithmetic constant).
 */
inline constexpr std::int64_t kSecsPerDay = 86400;

/**
 * @brief Floor division (mathematical division rounding toward -inf).
 *
 * C++ integer `/` truncates toward zero; local-time arithmetic near midnight
 * requires floor semantics for negative-shifted values.
 */
KOKKOS_INLINE_FUNCTION std::int64_t FloorDiv(std::int64_t a, std::int64_t b) {
    std::int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) {
        --q;
    }
    return q;
}

/**
 * @brief Floor modulo: result in [0, b) for b > 0.
 */
KOKKOS_INLINE_FUNCTION std::int64_t FloorMod(std::int64_t a, std::int64_t b) {
    std::int64_t r = a % b;
    if (r < 0) {
        r += b;
    }
    return r;
}

/**
 * @brief Civil date (proleptic Gregorian) from Howard Hinnant's algorithm.
 */
struct CivilDate {
    int year;   ///< Astronomical year (can be <= 0).
    int month;  ///< [1, 12].
    int day;    ///< [1, 31].
};

/**
 * @brief Convert days since 1970-01-01 to a civil date (Hinnant, integer-only).
 * @param z Days since the Unix epoch (may be negative).
 */
KOKKOS_INLINE_FUNCTION CivilDate CivilFromDays(std::int64_t z) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const std::int64_t doe = z - era * 146097;                                       // [0, 146096]
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;  // [0, 399]
    const std::int64_t y = yoe + era * 400;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);  // [0, 365], since Mar 1
    const std::int64_t mp = (5 * doy + 2) / 153;                       // [0, 11]
    const std::int64_t d = doy - (153 * mp + 2) / 5 + 1;               // [1, 31]
    const std::int64_t m = mp + (mp < 10 ? 3 : -9);                    // [1, 12]
    CivilDate out;
    out.year = static_cast<int>(y + (m <= 2));
    out.month = static_cast<int>(m);
    out.day = static_cast<int>(d);
    return out;
}

/**
 * @brief Days since 1970-01-01 for a civil date (Hinnant, integer-only).
 *
 * Exact inverse of CivilFromDays; used for day-of-year arithmetic.
 */
KOKKOS_INLINE_FUNCTION std::int64_t DaysFromCivil(int year, int month, int day) {
    year -= month <= 2;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const std::int64_t yoe = year - era * 400;                                          // [0, 399]
    const std::int64_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;  // [0, 365]
    const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                     // [0, 146096]
    return era * 146097 + doe - 719468;
}

/**
 * @brief Local hour-of-day [0, 23] from local civil seconds.
 */
KOKKOS_INLINE_FUNCTION int LocalHourFromSecs(std::int64_t local_secs) {
    return static_cast<int>(FloorMod(local_secs, kSecsPerDay) / 3600);
}

/**
 * @brief Local day-of-week [0, 6] (Sunday = 0) from local civil seconds.
 *
 * Unix epoch day 0 (1970-01-01) was a Thursday, hence the +4 alignment.
 * Agrees with CeceClock's gmtime tm_wday at offset 0.
 */
KOKKOS_INLINE_FUNCTION int LocalDayOfWeekFromSecs(std::int64_t local_secs) {
    return static_cast<int>(FloorMod(FloorDiv(local_secs, kSecsPerDay) + 4, 7));
}

/**
 * @brief Local month [0, 11] from local civil seconds.
 */
KOKKOS_INLINE_FUNCTION int LocalMonthFromSecs(std::int64_t local_secs) {
    return CivilFromDays(FloorDiv(local_secs, kSecsPerDay)).month - 1;
}

/**
 * @brief Local day-of-year [0, 365] from local civil seconds.
 */
KOKKOS_INLINE_FUNCTION int LocalDayOfYearFromSecs(std::int64_t local_secs) {
    const std::int64_t days = FloorDiv(local_secs, kSecsPerDay);
    const CivilDate c = CivilFromDays(days);
    return static_cast<int>(days - DaysFromCivil(c.year, 1, 1));
}

/**
 * @brief Local civil components for a UTC instant + offset (spec FR-005).
 */
struct LocalTimeParts {
    int hour = 0;         ///< [0, 23]
    int day_of_week = 0;  ///< [0, 6], Sunday = 0
    int month = 0;        ///< [0, 11]
    int day_of_year = 0;  ///< [0, 365]
};

/**
 * @brief Pluggable UTC-offset source — the seam that isolates consumers from the
 *        concrete offset data (spec FR-010).
 *
 * v1: StaticGridOffsetProvider (static 2026-01-15 snapshot grid). A future
 * DST-aware / time-varying source implements the same method and uses the
 * utc_epoch_secs argument; no consumer changes are required to swap it in.
 */
class IUtcOffsetProvider {
   public:
    virtual ~IUtcOffsetProvider() = default;

    /**
     * @brief Seconds east of UTC at a geographic point and UTC instant.
     * @param lat Latitude in degrees [-90, 90] (clamped).
     * @param lon Longitude in degrees (any convention; wrapped).
     * @param utc_epoch_secs Seconds since 1970-01-01T00:00:00Z.
     * @return Offset in seconds; always an exact multiple of 900 (quarter-hour).
     */
    virtual std::int32_t offsetSecondsAt(double lat, double lon, std::int64_t utc_epoch_secs) const = 0;
};

/**
 * @brief v1 provider: nearest-cell lookup into the decoded static reduced grid.
 *
 * Owns the dense cosine-reduced int8 quarter-hour offset field (decoded exactly
 * once by DecodeUtcGridRle). The utc_epoch_secs argument is accepted but unused
 * — the snapshot is static (DST is a future provider).
 */
class StaticGridOffsetProvider final : public IUtcOffsetProvider {
   public:
    /// Takes ownership of the decoded grid; throws std::invalid_argument on an empty grid.
    explicit StaticGridOffsetProvider(UtcGrid grid);

    std::int32_t offsetSecondsAt(double lat, double lon, std::int64_t utc_epoch_secs) const override;

   private:
    UtcGrid grid_;
};

/**
 * @brief Central local-time utility (spec FR-001).
 *
 * Holds a band-local (nx x ny_local) read-only device array of per-cell UTC
 * offsets in seconds, built exactly once at initialization by mapping the
 * provider onto the native grid at the run start instant. All derivations are
 * integer-only and identical on host and device; the offset is applied at
 * quarter-hour precision before extracting components so date boundaries roll
 * over correctly (FR-005).
 *
 * The service is an internal computation input only — no output path (NetCDF,
 * provenance, diagnostics, logs) ever consults it (FR-009).
 */
class LocalTimeService {
   public:
    using OffsetView = Kokkos::View<std::int32_t**, Kokkos::LayoutLeft>;

    /**
     * @brief Build the service by mapping the provider onto the native grid once.
     *
     * @param provider                Offset source (nullptr => UTC fallback).
     * @param native_lons             1-D longitude coordinates (size nx), degrees.
     * @param native_lats             1-D latitude coordinates (size ny GLOBAL), degrees.
     * @param nx                      Native grid x extent.
     * @param j0                      Global latitude index of this rank's first row.
     * @param ny_local                Rows owned by this rank (0 for surplus ranks).
     * @param utc_start_epoch_secs    Run start instant (Unix epoch seconds).
     */
    static std::unique_ptr<LocalTimeService> Create(std::unique_ptr<IUtcOffsetProvider> provider, const std::vector<double>& native_lons,
                                                    const std::vector<double>& native_lats, int nx, int j0, int ny_local,
                                                    std::int64_t utc_start_epoch_secs);

    /**
     * @brief UTC-fallback service: zero offsets everywhere (grid load failed at
     *        init; FR-008). UtcFallback() == true lets consumers take the exact
     *        scalar UTC path so output remains bit-identical to a disabled run.
     */
    static std::unique_ptr<LocalTimeService> CreateUtcFallback(int nx, int ny_local);

    /// @brief Band-local per-cell UTC offset in seconds (device-callable).
    KOKKOS_FUNCTION std::int32_t OffsetSec(int i, int j) const {
        return offsets_(i, j);
    }

    /// @brief Local hour-of-day for band cell (i, j) at a UTC instant (device-callable).
    KOKKOS_FUNCTION int LocalHourAt(int i, int j, std::int64_t utc_epoch_secs) const {
        return LocalHourFromSecs(utc_epoch_secs + offsets_(i, j));
    }

    /// @brief Local day-of-week (Sunday = 0) for band cell (i, j) (device-callable).
    KOKKOS_FUNCTION int LocalDayOfWeekAt(int i, int j, std::int64_t utc_epoch_secs) const {
        return LocalDayOfWeekFromSecs(utc_epoch_secs + offsets_(i, j));
    }

    /// @brief Local month [0, 11] for band cell (i, j) (device-callable).
    KOKKOS_FUNCTION int LocalMonthAt(int i, int j, std::int64_t utc_epoch_secs) const {
        return LocalMonthFromSecs(utc_epoch_secs + offsets_(i, j));
    }

    /// @brief Local day-of-year [0, 365] for band cell (i, j) (device-callable).
    KOKKOS_FUNCTION int LocalDayOfYearAt(int i, int j, std::int64_t utc_epoch_secs) const {
        return LocalDayOfYearFromSecs(utc_epoch_secs + offsets_(i, j));
    }

    /// @brief All local civil components for band cell (i, j) (device-callable).
    KOKKOS_FUNCTION LocalTimeParts LocalPartsAt(int i, int j, std::int64_t utc_epoch_secs) const {
        const std::int64_t local_secs = utc_epoch_secs + offsets_(i, j);
        LocalTimeParts p;
        p.hour = LocalHourFromSecs(local_secs);
        p.day_of_week = LocalDayOfWeekFromSecs(local_secs);
        p.month = LocalMonthFromSecs(local_secs);
        p.day_of_year = LocalDayOfYearFromSecs(local_secs);
        return p;
    }

    /**
     * @brief Point query via the provider (host; tests + future consumers).
     *        In UTC-fallback mode the provider is absent and parts are UTC.
     */
    LocalTimeParts Resolve(double lat, double lon, std::int64_t utc_epoch_secs) const;

    /// @brief Read-only device view for capture into Kokkos kernels (FR-011).
    const OffsetView& Offsets() const {
        return offsets_;
    }

    /// @brief True when the grid failed to load and every lookup yields UTC.
    bool UtcFallback() const {
        return utc_fallback_;
    }

    /// @brief Absolute UTC epoch seconds for a step's elapsed time.
    std::int64_t UtcEpochSecs(std::int64_t elapsed_secs) const {
        return start_epoch_secs_ + elapsed_secs;
    }

    /// @brief The underlying provider (read-only; nullptr in fallback mode).
    const IUtcOffsetProvider* Provider() const {
        return provider_.get();
    }

   private:
    OffsetView offsets_;
    std::unique_ptr<IUtcOffsetProvider> provider_;
    std::int64_t start_epoch_secs_ = 0;
    bool utc_fallback_ = false;
};

}  // namespace cece

#endif  // CECE_LOCAL_TIME_HPP
