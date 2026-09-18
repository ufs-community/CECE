// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Feature 001 (local-time support) — T014 [US1] + T019/T020 [US2]: local-time
// service tests (hour / day-of-week / month / day-of-year with rollover).
//
// Uses a ConstantOffsetProvider stub (an arbitrary IUtcOffsetProvider, also the
// US4 seam proof) plus the real StaticGridOffsetProvider, and cross-checks the
// offset-0 case against CeceClock's independent gmtime decomposition.

#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <cstdint>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "cece/cece_clock.hpp"
#include "cece/cece_local_time.hpp"
#include "cece/utc_grid.hpp"

#ifndef CECE_SOURCE_DIR
#define CECE_SOURCE_DIR "."
#endif

namespace cece {
namespace {

constexpr std::int64_t kHour = 3600;

/// Stub provider: a fixed offset for every point/instant. Doubles as the US4
/// proof that any IUtcOffsetProvider implementation plugs in unchanged.
class ConstantOffsetProvider final : public IUtcOffsetProvider {
   public:
    explicit ConstantOffsetProvider(std::int32_t offset_secs) : offset_secs_(offset_secs) {}
    std::int32_t offsetSecondsAt(double, double, std::int64_t) const override {
        return offset_secs_;
    }

   private:
    std::int32_t offset_secs_;
};

class LocalTimeServiceTest : public ::testing::Test {
   protected:
    void SetUp() override {
        if (!Kokkos::is_initialized()) Kokkos::initialize();
    }

    // 1x1-cell band so the stub offset is the only variable.
    static std::unique_ptr<LocalTimeService> MakeSingle(std::int32_t offset_secs, std::int64_t start_epoch = 0) {
        return LocalTimeService::Create(std::make_unique<ConstantOffsetProvider>(offset_secs), {0.0}, {0.0}, 1, 0, 1, start_epoch);
    }
};

// ---------------------------------------------------------------------------
// Hour-of-day with rollover (US1 / FR-005)
// ---------------------------------------------------------------------------

TEST_F(LocalTimeServiceTest, LocalHourMatchesUtcWhenOffsetZero) {
    auto svc = MakeSingle(0);
    // 2026-01-15T06:00:00Z
    const std::int64_t epoch = 1768456800;
    EXPECT_EQ(svc->LocalHourAt(0, 0, epoch), 6);
    EXPECT_FALSE(svc->UtcFallback());
}

TEST_F(LocalTimeServiceTest, LocalHourRollsBackwardAcrossMidnight) {
    auto svc = MakeSingle(-5 * kHour);                   // UTC-5
    const std::int64_t utc_02 = 1768456800 - 4 * kHour;  // 02:00Z same day
    // 02:00 UTC - 5h => 21:00 local the previous day.
    EXPECT_EQ(svc->LocalHourAt(0, 0, utc_02), 21);
}

TEST_F(LocalTimeServiceTest, LocalHourRollsForwardAcrossMidnight) {
    auto svc = MakeSingle(12 * kHour);                    // UTC+12
    const std::int64_t utc_18 = 1768456800 + 12 * kHour;  // 18:00Z
    // 18:00 UTC + 12h => 06:00 local next day.
    EXPECT_EQ(svc->LocalHourAt(0, 0, utc_18), 6);
}

TEST_F(LocalTimeServiceTest, HalfHourOffsetGivesExactHour) {
    auto svc = MakeSingle(5 * kHour + 1800);  // UTC+5:30 (Delhi)
    const std::int64_t utc_06 = 1768456800;   // 06:00Z
    // 06:00 + 5:30 => 11:30 local => hour 11.
    EXPECT_EQ(svc->LocalHourAt(0, 0, utc_06), 11);
}

// ---------------------------------------------------------------------------
// Day-of-week / month / day-of-year rollover (US2 / FR-006)
// ---------------------------------------------------------------------------

TEST_F(LocalTimeServiceTest, DayOfWeekRollsOverForward) {
    // 2026-01-15 is a Thursday (dow=4, Sunday=0). At 18:00Z + 12h => 06:00
    // Friday local: dow must advance to 5.
    auto svc = MakeSingle(12 * kHour);
    const std::int64_t thu_18 = 1768456800 + 12 * kHour;
    EXPECT_EQ(svc->LocalDayOfWeekAt(0, 0, thu_18), 5);
}

TEST_F(LocalTimeServiceTest, DayOfWeekRollsOverBackward) {
    // 2026-01-18 is a Sunday (dow=0). At 02:00Z - 5h => 21:00 Saturday local:
    // dow must wrap to 6.
    auto svc = MakeSingle(-5 * kHour);
    const std::int64_t sun_02 = 1768701600;  // 2026-01-18T02:00:00Z
    EXPECT_EQ(svc->LocalDayOfWeekAt(0, 0, sun_02), 6);
}

TEST_F(LocalTimeServiceTest, MonthRollsBackAcrossYearBoundary) {
    // 2026-03-01T02:00Z at UTC-4 => 22:00 Feb 28 local: month index 1.
    auto svc = MakeSingle(-4 * kHour);
    const std::int64_t mar01 = 1772330400;  // 2026-03-01T02:00:00Z
    const auto parts = svc->LocalPartsAt(0, 0, mar01);
    EXPECT_EQ(parts.month, 1);
    EXPECT_EQ(parts.hour, 22);
}

TEST_F(LocalTimeServiceTest, MonthRollsForwardIntoNextYear) {
    // 2026-12-31T23:30Z at UTC+2 => 01:30 Jan 1 2027 local: month 0, doy 0.
    auto svc = MakeSingle(2 * kHour);
    const std::int64_t dec31 = 1798759800;  // 2026-12-31T23:30:00Z
    const auto parts = svc->LocalPartsAt(0, 0, dec31);
    EXPECT_EQ(parts.month, 0);
    EXPECT_EQ(parts.hour, 1);
    EXPECT_EQ(parts.day_of_year, 0);
}

TEST_F(LocalTimeServiceTest, DayOfYearMatchesCivilCalendar) {
    // 2026-01-15 is day-of-year 14 (0-based). Offset 0 keeps it UTC-aligned.
    auto svc = MakeSingle(0);
    const std::int64_t epoch = 1768456800;  // 2026-01-15T06:00Z
    EXPECT_EQ(svc->LocalDayOfYearAt(0, 0, epoch), 14);
    // 2028 is a leap year: 2028-03-01 is doy 60 (0-based).
    auto svc2 = MakeSingle(0);
    const std::int64_t leap = 1835481600;  // 2028-03-01T00:00:00Z
    EXPECT_EQ(svc2->LocalDayOfYearAt(0, 0, leap), 60);
}

// ---------------------------------------------------------------------------
// Agreement with CeceClock (independent gmtime path) at offset 0
// ---------------------------------------------------------------------------

TEST_F(LocalTimeServiceTest, AgreesWithCeceClockAtOffsetZero) {
    const std::string start = "2026-01-15T06:00:00";
    const std::string end = "2026-01-16T06:00:00";
    std::vector<ClockComponent> comps = {{ComponentType::kStackingEngine, "stacking", 3600}};
    CeceClock clock(start, end, 3600, comps);

    auto svc = MakeSingle(0, clock.StartEpochSeconds());
    for (int step = 0; step < 24; ++step) {
        StepResult r = clock.Advance();
        const std::int64_t epoch = svc->UtcEpochSecs(r.elapsed_seconds);
        EXPECT_EQ(svc->LocalHourAt(0, 0, epoch), r.hour_of_day) << "step " << step;
        EXPECT_EQ(svc->LocalDayOfWeekAt(0, 0, epoch), r.day_of_week) << "step " << step;
        EXPECT_EQ(svc->LocalMonthAt(0, 0, epoch), r.month) << "step " << step;
    }
}

// ---------------------------------------------------------------------------
// Point query + provider-backed static grid
// ---------------------------------------------------------------------------

TEST_F(LocalTimeServiceTest, ResolvePointQueryUsesProvider) {
    auto svc = LocalTimeService::Create(std::make_unique<ConstantOffsetProvider>(-5 * kHour), {0.0}, {0.0}, 1, 0, 1, 0);
    const auto p = svc->Resolve(40.0, -74.0, 1768456800);  // 06:00Z
    EXPECT_EQ(p.hour, 1);                                  // 01:00 local
    EXPECT_EQ(p.day_of_week, 4);
}

TEST_F(LocalTimeServiceTest, StaticGridProviderMatchesKnownCities) {
    auto dense = DecodeUtcGridRle(std::string(CECE_SOURCE_DIR) + "/data/utc_grid_f720r.rle");
    StaticGridOffsetProvider provider(std::move(dense));
    EXPECT_EQ(provider.offsetSecondsAt(40.0, -74.0, 0), -20 * kUtcOffsetQuarterHoursPerSecond);
    EXPECT_EQ(provider.offsetSecondsAt(35.68, 139.7, 0), 36 * kUtcOffsetQuarterHoursPerSecond);
    EXPECT_EQ(provider.offsetSecondsAt(28.6, 77.2, 0), 22 * kUtcOffsetQuarterHoursPerSecond);
}

TEST_F(LocalTimeServiceTest, CreateFromStaticGridBandsAreCorrect) {
    auto dense = DecodeUtcGridRle(std::string(CECE_SOURCE_DIR) + "/data/utc_grid_f720r.rle");
    auto provider = std::make_unique<StaticGridOffsetProvider>(std::move(dense));
    const std::vector<double> lons = {-74.0, 0.0, 139.7};
    const std::vector<double> lats = {40.0, 51.5};
    auto svc = LocalTimeService::Create(std::move(provider), lons, lats, 3, 0, 2, 0);
    auto host = Kokkos::create_mirror_view_and_copy(Kokkos::HostSpace(), svc->Offsets());
    EXPECT_EQ(host(0, 0), -20 * 900);  // NY
    EXPECT_EQ(host(1, 0), 4 * 900);    // 40N, 0E is Spain: UTC+1
    EXPECT_EQ(host(2, 0), 36 * 900);   // Tokyo
    EXPECT_EQ(host(1, 1), 0);          // London
}

// ---------------------------------------------------------------------------
// UTC fallback (FR-008) and elapsed-epoch helper
// ---------------------------------------------------------------------------

TEST_F(LocalTimeServiceTest, UtcFallbackYieldsZeroOffsetsAndUtcParts) {
    auto svc = LocalTimeService::CreateUtcFallback(4, 2);
    EXPECT_TRUE(svc->UtcFallback());
    auto host = Kokkos::create_mirror_view_and_copy(Kokkos::HostSpace(), svc->Offsets());
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 4; ++i) {
            EXPECT_EQ(host(i, j), 0);
        }
    }
    const std::int64_t epoch = 1768456800;  // 06:00Z
    EXPECT_EQ(svc->LocalHourAt(1, 1, epoch), 6);
    // Resolve() in fallback mode has no provider: parts are UTC.
    const auto p = svc->Resolve(40.0, -74.0, epoch);
    EXPECT_EQ(p.hour, 6);
}

TEST_F(LocalTimeServiceTest, UtcEpochSecsAddsStartInstant) {
    auto svc = MakeSingle(0, /*start_epoch=*/1768456800);
    EXPECT_EQ(svc->UtcEpochSecs(0), 1768456800);
    EXPECT_EQ(svc->UtcEpochSecs(7200), 1768456800 + 7200);
}

// ---------------------------------------------------------------------------
// Device-side availability (FR-011): the offset array is captured by kernels.
// ---------------------------------------------------------------------------

TEST_F(LocalTimeServiceTest, OffsetsViewUsableInKernel) {
    auto svc = MakeSingle(3 * kHour);
    const std::int64_t epoch = 1768456800;  // 06:00Z -> 09:00 local
    auto offsets = svc->Offsets();
    Kokkos::View<int*> out("out", 1);
    Kokkos::parallel_for("check", 1, KOKKOS_LAMBDA(int) { out(0) = LocalHourFromSecs(epoch + offsets(0, 0)); });
    auto host = Kokkos::create_mirror_view_and_copy(Kokkos::HostSpace(), out);
    EXPECT_EQ(host(0), 9);
}

// ---------------------------------------------------------------------------
// T026 [US4] — the IUtcOffsetProvider seam is ready for a DST-aware source.
// ---------------------------------------------------------------------------

/// A deliberately time-varying provider (DST-style): UTC+1 in winter, UTC+2 in
/// summer (June-August). v1's StaticGridOffsetProvider ignores the instant; a
/// future provider uses it — and NO consumer changes are required, because the
/// engine only ever sees LocalTimeService/IUtcOffsetProvider.
class SeasonalDstProvider final : public IUtcOffsetProvider {
   public:
    std::int32_t offsetSecondsAt(double, double, std::int64_t utc_epoch_secs) const override {
        const int month = LocalMonthFromSecs(utc_epoch_secs);  // 0-11
        return (month >= 5 && month <= 7) ? 2 * kHour : 1 * kHour;
    }
};

TEST_F(LocalTimeServiceTest, TimeVaryingProviderDrivesServiceWithNoConsumerChanges) {
    // 06:00Z on a winter day: local hour 7 (UTC+1).
    const std::int64_t winter = 1768456800;  // 2026-01-15T06:00Z
    auto svc_winter = LocalTimeService::Create(std::make_unique<SeasonalDstProvider>(), {0.0}, {50.0}, 1, 0, 1, winter);
    EXPECT_EQ(svc_winter->LocalHourAt(0, 0, svc_winter->UtcEpochSecs(0)), 7);

    // 06:00Z on a summer day: local hour 8 (UTC+2) — the same service type,
    // built identically, resolves a different offset because the provider read
    // the instant. This is the DST wiring point (FR-010).
    const std::int64_t summer = 1781503200;  // 2026-06-15T06:00Z
    auto svc_summer = LocalTimeService::Create(std::make_unique<SeasonalDstProvider>(), {0.0}, {50.0}, 1, 0, 1, summer);
    EXPECT_EQ(svc_summer->LocalHourAt(0, 0, svc_summer->UtcEpochSecs(0)), 8);
}

TEST_F(LocalTimeServiceTest, SeamIsolationNoConcreteProviderReferencesInConsumers) {
    // Inspection guard (T026): the stacking engine, config parser, and Python
    // bindings must depend only on the LocalTimeService / IUtcOffsetProvider
    // seam — never on the concrete StaticGridOffsetProvider (or the decoder),
    // so a future DST provider can be swapped in at the init entry alone.
    const std::vector<std::string> consumers = {std::string(CECE_SOURCE_DIR) + "/src/core/cece_stacking_engine.cpp",
                                                std::string(CECE_SOURCE_DIR) + "/src/core/cece_config_parser.cpp",
                                                std::string(CECE_SOURCE_DIR) + "/src/python/cece_pybind.cpp"};
    for (const auto& path : consumers) {
        std::ifstream in(path);
        ASSERT_TRUE(static_cast<bool>(in)) << "missing consumer source: " << path;
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();
        EXPECT_EQ(text.find("StaticGridOffsetProvider"), std::string::npos) << path << " must not name the concrete provider";
        EXPECT_EQ(text.find("DecodeUtcGridRle"), std::string::npos) << path << " must not decode the grid directly";
    }
    // And the engine does use the seam (positive control).
    std::ifstream eng(std::string(CECE_SOURCE_DIR) + "/src/core/cece_stacking_engine.cpp");
    std::stringstream ss;
    ss << eng.rdbuf();
    EXPECT_NE(ss.str().find("LocalTimeService"), std::string::npos);
}

}  // namespace
}  // namespace cece
