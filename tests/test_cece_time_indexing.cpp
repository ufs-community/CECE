/**
 * @file test_cece_time_indexing.cpp
 * @brief Tests for resolving a simulation date-time to file record indices.
 *
 * Covers the cece::detail helpers behind data stream time indexing, one suite
 * per function under test:
 *   - CeceCadenceArithmetic: bracket_from_cadence, the arithmetic fallback
 *   - CeceAxisDecode:        bracket_from_coords, the decoded time-axis path
 *   - CeceFindBracket:       find_bracket, the primitive both paths delegate to
 *   - CeceSimDateTime:       parse_sim_datetime
 *   - CeceCycleYearAlignment and CeceStreamConfigValidation cut across these
 *
 * CF units parsing is covered by test_cece_calendar.cpp and view widening by
 * test_cece_amio_utils.cpp; both are used here to build decode-path fixtures.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <tick/tick.hpp>
#include <vector>

#include "cece/cece_amio_utils.hpp"
#include "cece/cece_calendar.hpp"
#include "cece/cece_time_indexing.hpp"

namespace cece {

namespace {

// 48 hourly records covering 2000-01-01T00 .. 2000-01-02T23.
std::vector<double> two_day_hourly_axis() {
    std::vector<double> raw(48);
    for (int k = 0; k < 48; ++k) raw[k] = static_cast<double>(k);
    return raw;
}

constexpr const char* kTwoDayHourlyUnits = "hours since 2000-01-01 00:00:00";

}  // namespace

TEST(CeceCadenceArithmetic, HourlyCadence) {
    using namespace cece::detail;

    // Hourly cadence selects the hour-of-day profile record (0-23).
    SimDateTime dt_05 = parse_sim_datetime("2026-01-01T05:00:00");
    EXPECT_TRUE(dt_05.valid);
    EXPECT_EQ(dt_05.hour, 5);
    RecordBracket br_05 = bracket_from_cadence("hourly", "nearest", dt_05, 24);
    EXPECT_TRUE(br_05.valid);
    EXPECT_EQ(br_05.i0, 5);
    EXPECT_EQ(br_05.i1, 5);
    EXPECT_DOUBLE_EQ(br_05.weight, 0.0);

    // Last hour of the day -> record 23.
    SimDateTime dt_23 = parse_sim_datetime("2026-01-01T23:00:00");
    RecordBracket br_23 = bracket_from_cadence("hourly", "nearest", dt_23, 24);
    EXPECT_TRUE(br_23.valid);
    EXPECT_EQ(br_23.i0, 23);
    EXPECT_EQ(br_23.i1, 23);

    // Hourly cadence ignores tintalgo=linear: still nearest, no interpolation.
    // (Documents that there is no true sub-hourly interpolation path.)
    RecordBracket br_lin = bracket_from_cadence("hourly", "linear", dt_05, 24);
    EXPECT_TRUE(br_lin.valid);
    EXPECT_EQ(br_lin.i0, 5);
    EXPECT_EQ(br_lin.i1, 5);
    EXPECT_DOUBLE_EQ(br_lin.weight, 0.0);

    // Fewer records than 24 -> clamp to the last available record.
    RecordBracket br_clamp = bracket_from_cadence("hourly", "nearest", dt_23, 12);
    EXPECT_TRUE(br_clamp.valid);
    EXPECT_EQ(br_clamp.i0, 11);
    EXPECT_EQ(br_clamp.i1, 11);
}

TEST(CeceCadenceArithmetic, DailyCadenceNearest) {
    using namespace cece::detail;

    // 2026-01-01 -> day_of_year = 1 -> record index 0
    SimDateTime dt_jan1 = parse_sim_datetime("2026-01-01T00:00:00");
    EXPECT_TRUE(dt_jan1.valid);
    EXPECT_EQ(dt_jan1.day_of_year, 1);
    RecordBracket br_jan1 = bracket_from_cadence("daily", "nearest", dt_jan1, 365);
    EXPECT_TRUE(br_jan1.valid);
    EXPECT_EQ(br_jan1.i0, 0);
    EXPECT_EQ(br_jan1.i1, 0);

    // 2026-12-31 -> day_of_year = 365 -> record index 364
    SimDateTime dt_dec31 = parse_sim_datetime("2026-12-31T00:00:00");
    EXPECT_TRUE(dt_dec31.valid);
    EXPECT_EQ(dt_dec31.day_of_year, 365);
    RecordBracket br_dec31 = bracket_from_cadence("daily", "nearest", dt_dec31, 365);
    EXPECT_TRUE(br_dec31.valid);
    EXPECT_EQ(br_dec31.i0, 364);
    EXPECT_EQ(br_dec31.i1, 364);

    // Leap year: 2024-12-31 -> day_of_year = 366 -> record index 365
    SimDateTime dt_leap = parse_sim_datetime("2024-12-31T00:00:00");
    EXPECT_TRUE(dt_leap.valid);
    EXPECT_EQ(dt_leap.day_of_year, 366);
    RecordBracket br_leap = bracket_from_cadence("daily", "nearest", dt_leap, 366);
    EXPECT_TRUE(br_leap.valid);
    EXPECT_EQ(br_leap.i0, 365);
    EXPECT_EQ(br_leap.i1, 365);
}

TEST(CeceCadenceArithmetic, DailyCadenceLinear) {
    using namespace cece::detail;

    // Single-year daily file (365 records). Mid-day convention: hour-of-day
    // weights interpolation between consecutive daily records.
    // 2026-06-15 -> day_of_year = 166 -> abs_day = 165.
    SimDateTime dt_noon = parse_sim_datetime("2026-06-15T12:00:00");
    EXPECT_EQ(dt_noon.day_of_year, 166);
    RecordBracket br_noon = bracket_from_cadence("daily", "linear", dt_noon, 365);
    EXPECT_TRUE(br_noon.valid);
    EXPECT_EQ(br_noon.i0, 165);
    EXPECT_EQ(br_noon.i1, 166);
    EXPECT_NEAR(br_noon.weight, 0.0, 1e-9);

    // Midnight -> frac = 0 -> halfway between the previous day and today.
    SimDateTime dt_mid = parse_sim_datetime("2026-06-15T00:00:00");
    RecordBracket br_mid = bracket_from_cadence("daily", "linear", dt_mid, 365);
    EXPECT_TRUE(br_mid.valid);
    EXPECT_EQ(br_mid.i0, 164);
    EXPECT_EQ(br_mid.i1, 165);
    EXPECT_NEAR(br_mid.weight, 0.5, 1e-9);

    // 18:00 -> frac = 0.75 -> quarter of the way from today to tomorrow.
    SimDateTime dt_eve = parse_sim_datetime("2026-06-15T18:00:00");
    RecordBracket br_eve = bracket_from_cadence("daily", "linear", dt_eve, 365);
    EXPECT_TRUE(br_eve.valid);
    EXPECT_EQ(br_eve.i0, 165);
    EXPECT_EQ(br_eve.i1, 166);
    EXPECT_NEAR(br_eve.weight, 0.25, 1e-9);
}

TEST(CeceCadenceArithmetic, DailyCadenceLeapYearNormalization) {
    using namespace cece::detail;

    // A 365-record daily climatology has no leap-day record, so every date
    // after Feb 28 of a leap simulation year must shift back one to keep
    // pointing at its own calendar day.

    // Feb 28 is unaffected either way (day_of_year 59 -> record 58).
    EXPECT_EQ(bracket_from_cadence("daily", "nearest", parse_sim_datetime("2024-02-28T00:00:00"), 365).i0, 58);
    EXPECT_EQ(bracket_from_cadence("daily", "nearest", parse_sim_datetime("2026-02-28T00:00:00"), 365).i0, 58);

    // Policy: Feb 29 reuses the Feb 28 record rather than consuming Mar 1's.
    EXPECT_EQ(bracket_from_cadence("daily", "nearest", parse_sim_datetime("2024-02-29T00:00:00"), 365).i0, 58);

    // Dates after the leap day line up with the same record as in a non-leap
    // year. Without normalisation the leap year is one record ahead all the way
    // to December.
    for (const char* mmdd : {"03-01", "07-04", "10-15", "12-31"}) {
        const std::string leap = std::string("2024-") + mmdd + "T00:00:00";
        const std::string plain = std::string("2026-") + mmdd + "T00:00:00";
        const RecordBracket br_leap = bracket_from_cadence("daily", "nearest", parse_sim_datetime(leap), 365);
        const RecordBracket br_plain = bracket_from_cadence("daily", "nearest", parse_sim_datetime(plain), 365);
        ASSERT_TRUE(br_leap.valid) << leap;
        ASSERT_TRUE(br_plain.valid) << plain;
        EXPECT_EQ(br_leap.i0, br_plain.i0) << mmdd;
    }

    // Mar 1 is record 59 in both years (Jan 0-30, Feb 31-58, Mar 1 -> 59).
    EXPECT_EQ(bracket_from_cadence("daily", "nearest", parse_sim_datetime("2024-03-01T00:00:00"), 365).i0, 59);

    // The normalisation happens before the nearest/linear split, so the linear
    // path agrees on the record it interpolates from.
    const RecordBracket lin = bracket_from_cadence("daily", "linear", parse_sim_datetime("2024-03-01T12:00:00"), 365);
    ASSERT_TRUE(lin.valid);
    EXPECT_EQ(lin.i0, 59);
    EXPECT_EQ(lin.i1, 60);
    EXPECT_NEAR(lin.weight, 0.0, 1e-9);
}

TEST(CeceCadenceArithmetic, DailyCadenceLeapAwareClimatologyInNonLeapYear) {
    using namespace cece::detail;

    // The mirror case: a 366-record climatology carries a Feb 29 record that a
    // non-leap simulation year has to skip.
    EXPECT_EQ(bracket_from_cadence("daily", "nearest", parse_sim_datetime("2026-02-28T00:00:00"), 366).i0, 58);
    EXPECT_EQ(bracket_from_cadence("daily", "nearest", parse_sim_datetime("2026-03-01T00:00:00"), 366).i0, 60);
    EXPECT_EQ(bracket_from_cadence("daily", "nearest", parse_sim_datetime("2026-12-31T00:00:00"), 366).i0, 365);

    // A leap year uses the same file with no shift at all.
    EXPECT_EQ(bracket_from_cadence("daily", "nearest", parse_sim_datetime("2024-02-29T00:00:00"), 366).i0, 59);
    EXPECT_EQ(bracket_from_cadence("daily", "nearest", parse_sim_datetime("2024-03-01T00:00:00"), 366).i0, 60);
    EXPECT_EQ(bracket_from_cadence("daily", "nearest", parse_sim_datetime("2024-12-31T00:00:00"), 366).i0, 365);
}

TEST(CeceCadenceArithmetic, DailyCadenceLinearLeapYearOverflow) {
    using namespace cece::detail;

    // A 365-record daily climatology used during a leap simulation year.
    // Dec 31 2024 has day_of_year = 366; the leap normalisation brings it back
    // to the final record instead of overflowing past the end of the file.
    SimDateTime dt_noon = parse_sim_datetime("2024-12-31T12:00:00");
    EXPECT_EQ(dt_noon.day_of_year, 366);

    // Nearest resolves to the final record (364) of the 365-record file.
    RecordBracket br_near = bracket_from_cadence("daily", "nearest", dt_noon, 365);
    EXPECT_TRUE(br_near.valid);
    EXPECT_EQ(br_near.i0, 364);
    EXPECT_EQ(br_near.i1, 364);

    // Linear normalises the same way, then applies the mid-day convention. At
    // noon (frac = 0.5) the weight is 0 -> pure record 364, with the cyclic
    // upper bracket wrapping to record 0 (Jan 1).
    RecordBracket br_lin = bracket_from_cadence("daily", "linear", dt_noon, 365);
    EXPECT_TRUE(br_lin.valid);
    EXPECT_EQ(br_lin.i0, 364);
    EXPECT_EQ(br_lin.i1, 0);
    EXPECT_NEAR(br_lin.weight, 0.0, 1e-9);

    // A 366-record (leap-aware) climatology has a record for the leap day, so a
    // leap year needs no shift: abs_day = 365 maps directly to record 365.
    RecordBracket br_366 = bracket_from_cadence("daily", "linear", dt_noon, 366);
    EXPECT_TRUE(br_366.valid);
    EXPECT_EQ(br_366.i0, 365);
    EXPECT_EQ(br_366.i1, 0);  // cyclic wrap to Jan 1
    EXPECT_NEAR(br_366.weight, 0.0, 1e-9);

    // Non-leap years against a 365-record file need no shift either: Dec 31
    // 2026 -> day_of_year 365 -> abs_day 364, cyclic wrap to Jan 1 at noon.
    SimDateTime dt_nonleap = parse_sim_datetime("2026-12-31T12:00:00");
    EXPECT_EQ(dt_nonleap.day_of_year, 365);
    RecordBracket br_nonleap = bracket_from_cadence("daily", "linear", dt_nonleap, 365);
    EXPECT_TRUE(br_nonleap.valid);
    EXPECT_EQ(br_nonleap.i0, 364);
    EXPECT_EQ(br_nonleap.i1, 0);
    EXPECT_NEAR(br_nonleap.weight, 0.0, 1e-9);
}

TEST(CeceCadenceArithmetic, WeeklyCadenceISO8601) {
    using namespace cece::detail;

    // 2026-01-05 is Monday -> ISO 1 -> 0-indexed profile index 0
    SimDateTime dt_mon = parse_sim_datetime("2026-01-05T00:00:00");
    EXPECT_TRUE(dt_mon.valid);
    EXPECT_EQ(dt_mon.day_of_week, 1);
    RecordBracket br_mon = bracket_from_cadence("weekly", "nearest", dt_mon, 7);
    EXPECT_TRUE(br_mon.valid);
    EXPECT_EQ(br_mon.i0, 0);
    EXPECT_EQ(br_mon.i1, 0);

    // 2026-01-04 is Sunday -> ISO 7 -> 0-indexed profile index 6
    SimDateTime dt_sun = parse_sim_datetime("2026-01-04T00:00:00");
    EXPECT_TRUE(dt_sun.valid);
    EXPECT_EQ(dt_sun.day_of_week, 7);
    RecordBracket br_sun = bracket_from_cadence("weekly", "nearest", dt_sun, 7);
    EXPECT_TRUE(br_sun.valid);
    EXPECT_EQ(br_sun.i0, 6);
    EXPECT_EQ(br_sun.i1, 6);

    // 2026-01-01 is Thursday -> ISO 4 -> 0-indexed profile index 3
    SimDateTime dt_thu = parse_sim_datetime("2026-01-01T00:00:00");
    EXPECT_TRUE(dt_thu.valid);
    EXPECT_EQ(dt_thu.day_of_week, 4);
    RecordBracket br_thu = bracket_from_cadence("weekly", "nearest", dt_thu, 7);
    EXPECT_TRUE(br_thu.valid);
    EXPECT_EQ(br_thu.i0, 3);
    EXPECT_EQ(br_thu.i1, 3);
}

TEST(CeceCadenceArithmetic, MonthlyCadenceNearestMultiYear) {
    using namespace cece::detail;

    // Simulation date: 2023-07-01 (July 2023)
    SimDateTime dt = parse_sim_datetime("2023-07-01T00:00:00");
    EXPECT_TRUE(dt.valid);
    EXPECT_EQ(dt.year, 2023);
    EXPECT_EQ(dt.month, 7);

    // Multi-year file: CEDS 2000-2023 (288 records, nearest neighbor interpolation)
    // Formula: (effective_year - yearFirst) * 12 + (month - 1)
    // (2023 - 2000) * 12 + (7 - 1) = 23 * 12 + 6 = 282
    RecordBracket br = bracket_from_cadence("monthly", "nearest", dt, 288, 2000, 2023, 2000, "extend");
    EXPECT_TRUE(br.valid);
    EXPECT_EQ(br.i0, 282);
    EXPECT_EQ(br.i1, 282);

    // Out-of-range simulation date: 2026-08-01 with taxmode="extend".
    // "extend" holds the file's last record (Dec 2023, index 287). Clamping
    // only the year would keep August and give 283, which is mid-file and
    // disagrees with the decoded-axis path.
    SimDateTime dt_future = parse_sim_datetime("2026-08-01T00:00:00");
    RecordBracket br_future = bracket_from_cadence("monthly", "nearest", dt_future, 288, 2000, 2023, 2000, "extend");
    EXPECT_TRUE(br_future.valid);
    EXPECT_EQ(br_future.i0, 287);
    EXPECT_EQ(br_future.i1, 287);

    // First month: 2000-01-01 -> record 0
    SimDateTime dt_first = parse_sim_datetime("2000-01-01T00:00:00");
    RecordBracket br_first = bracket_from_cadence("monthly", "nearest", dt_first, 288, 2000, 2023, 2000, "extend");
    EXPECT_TRUE(br_first.valid);
    EXPECT_EQ(br_first.i0, 0);
    EXPECT_EQ(br_first.i1, 0);
}

TEST(CeceCadenceArithmetic, MonthlyCadenceLinearMultiYear) {
    using namespace cece::detail;

    // Multi-year monthly file (288 records, 2000-2023) with linear interpolation.
    // June 2023 -> abs_month = (2023-2000)*12 + 5 = 281. Mid-month (frac=0.5)
    // gives pure record 281.
    SimDateTime dt = parse_sim_datetime("2023-06-16T00:00:00");
    RecordBracket br = bracket_from_cadence("monthly", "linear", dt, 288, 2000, 2023, 2000, "extend");
    EXPECT_TRUE(br.valid);
    EXPECT_EQ(br.i0, 281);
    EXPECT_EQ(br.i1, 282);
    EXPECT_NEAR(br.weight, 0.0, 1e-9);
}

TEST(CeceCadenceArithmetic, MultiYearLinearHonorsTaxmodeAtFileEdges) {
    using namespace cece::detail;

    // Multi-year monthly file (288 records, 2000-2023). At the file edges the
    // mid-month bracket would otherwise wrap, blending December of the last
    // year into January of the first. Only "cycle" should do that.

    // Late December 2023 (last record, 287). December has 31 days; day 20 ->
    // frac = 19/31 ~= 0.613, so the forward neighbour is off the end.
    const SimDateTime dt_end = parse_sim_datetime("2023-12-20T00:00:00");
    // Early January 2000 (record 0). Jan day 5 -> frac = 4/31 ~= 0.129, so the
    // backward neighbour is off the front.
    const SimDateTime dt_start = parse_sim_datetime("2000-01-05T00:00:00");

    // extend: hold the edge record rather than wrapping to the far end.
    const RecordBracket ext_end = bracket_from_cadence("monthly", "linear", dt_end, 288, 2000, 2023, 2000, "extend");
    ASSERT_TRUE(ext_end.valid);
    EXPECT_EQ(ext_end.i0, 287);
    EXPECT_EQ(ext_end.i1, 287);
    EXPECT_NEAR(ext_end.weight, 0.0, 1e-12);

    const RecordBracket ext_start = bracket_from_cadence("monthly", "linear", dt_start, 288, 2000, 2023, 2000, "extend");
    ASSERT_TRUE(ext_start.valid);
    EXPECT_EQ(ext_start.i0, 0);
    EXPECT_EQ(ext_start.i1, 0);

    // limit: the target lies outside the records' coverage, so it is rejected
    // the same way the decoded-axis path rejects it.
    const RecordBracket lim_end = bracket_from_cadence("monthly", "linear", dt_end, 288, 2000, 2023, 2000, "limit");
    EXPECT_FALSE(lim_end.valid);
    EXPECT_TRUE(lim_end.out_of_range);

    // cycle: wrapping is the whole point, so the far end is the neighbour.
    const RecordBracket cyc_end = bracket_from_cadence("monthly", "linear", dt_end, 288, 2000, 2023, 2000, "cycle");
    ASSERT_TRUE(cyc_end.valid);
    EXPECT_EQ(cyc_end.i0, 287);
    EXPECT_EQ(cyc_end.i1, 0);
    EXPECT_NEAR(cyc_end.weight, 19.0 / 31.0 - 0.5, 1e-9);

    const RecordBracket cyc_start = bracket_from_cadence("monthly", "linear", dt_start, 288, 2000, 2023, 2000, "cycle");
    ASSERT_TRUE(cyc_start.valid);
    EXPECT_EQ(cyc_start.i0, 287);
    EXPECT_EQ(cyc_start.i1, 0);
    EXPECT_NEAR(cyc_start.weight, 4.0 / 31.0 + 0.5, 1e-9);

    // A mid-file month is unaffected by taxmode.
    const SimDateTime dt_mid = parse_sim_datetime("2010-06-20T00:00:00");
    for (const char* tax : {"cycle", "extend", "limit"}) {
        const RecordBracket br = bracket_from_cadence("monthly", "linear", dt_mid, 288, 2000, 2023, 2000, tax);
        ASSERT_TRUE(br.valid) << tax;
        EXPECT_EQ(br.i0, 125) << tax;  // (2010-2000)*12 + 5
        EXPECT_EQ(br.i1, 126) << tax;
    }
}

TEST(CeceCadenceArithmetic, ExtendHoldsTheEndRecordNotTheSameMonth) {
    using namespace cece::detail;

    // A simulation year past the end of a 2000-2023 monthly file. "extend"
    // means hold the last record (Dec 2023, index 287) -- clamping only the
    // year would keep August and land on index 283, disagreeing with the
    // decoded-axis path, which clamps the instant to the final record.
    const SimDateTime aug_2026 = parse_sim_datetime("2026-08-15T00:00:00");
    const RecordBracket late = bracket_from_cadence("monthly", "nearest", aug_2026, 288, 2000, 2023, 2000, "extend");
    ASSERT_TRUE(late.valid);
    EXPECT_EQ(late.i0, 287);
    EXPECT_EQ(late.i1, 287);

    // Symmetrically, a year before the file holds the first record.
    const SimDateTime aug_1990 = parse_sim_datetime("1990-08-15T00:00:00");
    const RecordBracket early = bracket_from_cadence("monthly", "nearest", aug_1990, 288, 2000, 2023, 2000, "extend");
    ASSERT_TRUE(early.valid);
    EXPECT_EQ(early.i0, 0);

    // The daily branch follows the same rule: a 1095-record file over 2021-2023.
    const RecordBracket daily_late =
        bracket_from_cadence("daily", "nearest", parse_sim_datetime("2026-08-15T00:00:00"), 1095, 2021, 2023, 2021, "extend");
    ASSERT_TRUE(daily_late.valid);
    EXPECT_EQ(daily_late.i0, 1094);

    const RecordBracket daily_early =
        bracket_from_cadence("daily", "nearest", parse_sim_datetime("1990-08-15T00:00:00"), 1095, 2021, 2023, 2021, "extend");
    ASSERT_TRUE(daily_early.valid);
    EXPECT_EQ(daily_early.i0, 0);
}

TEST(CeceCadenceArithmetic, RemappedFebruaryUsesTheEffectiveYearMonthLength) {
    using namespace cece::detail;

    // A 36-record monthly file covering non-leap 2021-2023. February 2024 has
    // 29 days but cycles onto 2021, which has 28: dividing by the simulation
    // year's month length would give a weight the decoded path never produces.
    const RecordBracket feb29 = bracket_from_cadence("monthly", "linear", parse_sim_datetime("2024-02-29T00:00:00"), 36, 2021, 2023, 2021, "cycle");
    ASSERT_TRUE(feb29.valid);
    EXPECT_EQ(feb29.i0, 1);  // Feb 2021
    EXPECT_EQ(feb29.i1, 2);
    // Day 29 is clamped onto the 28-day February, so frac = 27/28.
    EXPECT_NEAR(feb29.weight, 27.0 / 28.0 - 0.5, 1e-9);

    // A mid-February date divides by 28, not 29.
    const RecordBracket feb10 = bracket_from_cadence("monthly", "linear", parse_sim_datetime("2024-02-10T00:00:00"), 36, 2021, 2023, 2021, "cycle");
    ASSERT_TRUE(feb10.valid);
    EXPECT_NEAR(feb10.weight, 9.0 / 28.0 + 0.5, 1e-9);
}

TEST(CeceCadenceArithmetic, MonthlyCadenceLinearClimatology) {
    using namespace cece::detail;

    // 12-record climatology (yearFirst=0). Mid-month linear convention:
    // record M represents the midpoint of month M; interpolation runs between
    // the midpoints of consecutive records.

    // June has 30 days, so day 16 (day-1=15) at hour 0 -> frac = 15/30 = 0.5,
    // which is exactly mid-month -> pure June (i0=5), weight 0.
    SimDateTime dt_mid = parse_sim_datetime("2020-06-16T00:00:00");
    RecordBracket br_mid = bracket_from_cadence("monthly", "linear", dt_mid, 12);
    EXPECT_TRUE(br_mid.valid);
    EXPECT_EQ(br_mid.i0, 5);
    EXPECT_EQ(br_mid.i1, 6);
    EXPECT_NEAR(br_mid.weight, 0.0, 1e-9);

    // June 1 -> frac = 0 -> halfway between May (i0=4) and June (i1=5).
    SimDateTime dt_start = parse_sim_datetime("2020-06-01T00:00:00");
    RecordBracket br_start = bracket_from_cadence("monthly", "linear", dt_start, 12);
    EXPECT_TRUE(br_start.valid);
    EXPECT_EQ(br_start.i0, 4);
    EXPECT_EQ(br_start.i1, 5);
    EXPECT_NEAR(br_start.weight, 0.5, 1e-9);

    // Hour-of-day contributes to the sub-month fraction: June 16 12:00 ->
    // frac = (15 + 0.5)/30 = 0.51667 -> just past mid-month.
    SimDateTime dt_hour = parse_sim_datetime("2020-06-16T12:00:00");
    RecordBracket br_hour = bracket_from_cadence("monthly", "linear", dt_hour, 12);
    EXPECT_TRUE(br_hour.valid);
    EXPECT_EQ(br_hour.i0, 5);
    EXPECT_EQ(br_hour.i1, 6);
    EXPECT_NEAR(br_hour.weight, 0.5 / 30.0, 1e-9);

    // January 1 wraps the lower bracket to December for a climatology file.
    SimDateTime dt_jan = parse_sim_datetime("2020-01-01T00:00:00");
    RecordBracket br_jan = bracket_from_cadence("monthly", "linear", dt_jan, 12);
    EXPECT_TRUE(br_jan.valid);
    EXPECT_EQ(br_jan.i0, 11);
    EXPECT_EQ(br_jan.i1, 0);
    EXPECT_NEAR(br_jan.weight, 0.5, 1e-9);
}

TEST(CeceCadenceArithmetic, MonthlyTaxmodeCycleAndLimit) {
    using namespace cece::detail;

    // Multi-year monthly file 2000-2023 (288 records). 2024 is one year past
    // the end of the file range.
    SimDateTime dt_oob = parse_sim_datetime("2024-01-01T00:00:00");

    // Default taxmode ("") == cycle: 2024 wraps back to 2000 -> record 0.
    RecordBracket br_cycle = bracket_from_cadence("monthly", "nearest", dt_oob, 288, 2000, 2023, 2000, "");
    EXPECT_TRUE(br_cycle.valid);
    EXPECT_EQ(br_cycle.i0, 0);
    EXPECT_EQ(br_cycle.i1, 0);

    // taxmode "limit": out-of-range year yields an invalid bracket.
    RecordBracket br_limit = bracket_from_cadence("monthly", "nearest", dt_oob, 288, 2000, 2023, 2000, "limit");
    EXPECT_FALSE(br_limit.valid);

    // taxmode cycle for 2025-07: (2025-2000) % 24 = 1 -> effective year 2001,
    // July -> abs_month = 1*12 + 6 = 18.
    SimDateTime dt_2025 = parse_sim_datetime("2025-07-01T00:00:00");
    RecordBracket br_2025 = bracket_from_cadence("monthly", "nearest", dt_2025, 288, 2000, 2023, 2000, "cycle");
    EXPECT_TRUE(br_2025.valid);
    EXPECT_EQ(br_2025.i0, 18);
    EXPECT_EQ(br_2025.i1, 18);
}

TEST(CeceStreamConfigValidation, RejectsUnknownTemporalValues) {
    using namespace cece::detail;

    // A typo must be reported, not silently normalised: "limti" would otherwise
    // become cycling, and an unknown tintalgo would become nearest.
    EXPECT_THROW(validate_stream_temporal_config("series", "limti", "linear", 0, 0, 0, ""), std::invalid_argument);
    EXPECT_THROW(validate_stream_temporal_config("series", "cycle", "cubic", 0, 0, 0, ""), std::invalid_argument);
    EXPECT_THROW(validate_stream_temporal_config("yearly", "cycle", "linear", 0, 0, 0, ""), std::invalid_argument);

    // The values the driver actually implements are accepted, case-insensitively.
    for (const char* tax : {"", "cycle", "extend", "limit", "LIMIT"}) {
        EXPECT_NO_THROW(validate_stream_temporal_config("series", tax, "linear", 0, 0, 0, "")) << tax;
    }
    for (const char* algo : {"linear", "nearest", "NEAREST"}) {
        EXPECT_NO_THROW(validate_stream_temporal_config("monthly", "cycle", algo, 0, 0, 0, "")) << algo;
    }
    for (const char* cad : {"", "series", "daily", "monthly", "hourly", "weekly", "stepwise", "step", "MONTHLY"}) {
        EXPECT_NO_THROW(validate_stream_temporal_config(cad, "", "nearest", 0, 0, 0, "")) << cad;
    }
}

TEST(CeceStreamConfigValidation, RejectsInvertedYearRange) {
    using namespace cece::detail;

    EXPECT_THROW(validate_stream_temporal_config("monthly", "cycle", "linear", 2000, 1999, 0, ""), std::invalid_argument);

    // A single-year range and an unset range are both fine.
    EXPECT_NO_THROW(validate_stream_temporal_config("monthly", "cycle", "linear", 2000, 2000, 0, ""));
    EXPECT_NO_THROW(validate_stream_temporal_config("monthly", "cycle", "linear", 2000, 0, 0, ""));
    EXPECT_NO_THROW(validate_stream_temporal_config("monthly", "cycle", "linear", 0, 0, 0, ""));
}

TEST(CeceCadenceArithmetic, InvertedYearRangeIsRejected) {
    using namespace cece::detail;

    // yearLast == yearFirst - 1 makes the cycle span zero years. Before the
    // guard the modulo divided by zero as soon as a date fell out of range.
    SimDateTime dt = parse_sim_datetime("2024-07-01T00:00:00");

    for (const char* tax : {"", "cycle", "extend", "limit"}) {
        EXPECT_FALSE(bracket_from_cadence("monthly", "nearest", dt, 288, 2000, 1999, 0, tax).valid) << tax;
        EXPECT_FALSE(bracket_from_cadence("daily", "nearest", dt, 730, 2000, 1999, 0, tax).valid) << tax;
    }

    // A single-year range (yearLast == yearFirst) spans one year and still works.
    const RecordBracket ok = bracket_from_cadence("monthly", "nearest", dt, 288, 2000, 2000, 0, "cycle");
    EXPECT_TRUE(ok.valid);
}

TEST(CeceAxisDecode, DecodeAppliesUtcOffset) {
    using namespace cece::detail;

    // Records 0..47 of "hours since 2000-01-01 00:00:00 -06:00" are 06Z, 07Z, ...
    const std::vector<double> raw = two_day_hourly_axis();
    const char* units_local = "hours since 2000-01-01 00:00:00 -06:00";

    // 06Z is record 0 on the offset axis, but record 6 if the offset is dropped.
    const SimDateTime at_06z = parse_sim_datetime("2000-01-01T06:00:00");
    EXPECT_EQ(bracket_from_coords(raw, units_local, "gregorian", at_06z, "nearest").i0, 0);
    EXPECT_EQ(bracket_from_coords(raw, kTwoDayHourlyUnits, "gregorian", at_06z, "nearest").i0, 6);

    const SimDateTime at_11z = parse_sim_datetime("2000-01-01T11:00:00");
    EXPECT_EQ(bracket_from_coords(raw, units_local, "gregorian", at_11z, "nearest").i0, 5);
}

TEST(CeceAxisDecode, LimitRejectionIsDistinguishedFromUnusableAxis) {
    using namespace cece::detail;

    const std::vector<double> raw = two_day_hourly_axis();
    const SimDateTime day3 = parse_sim_datetime("2000-01-03T05:00:00");

    // Decoded fine, but outside coverage under 'limit'. The caller must not
    // degrade to the arithmetic fallback on this.
    const RecordBracket limited = bracket_from_coords(raw, kTwoDayHourlyUnits, "gregorian", day3, "nearest", 0, "limit");
    EXPECT_FALSE(limited.valid);
    EXPECT_TRUE(limited.out_of_range);

    // Undecodable units: there is no axis to be out of range against.
    const RecordBracket undecodable = bracket_from_coords(raw, "months since 2000-01-01", "gregorian", day3, "nearest", 0, "limit");
    EXPECT_FALSE(undecodable.valid);
    EXPECT_FALSE(undecodable.out_of_range);

    // An in-range time is unaffected.
    const RecordBracket ok =
        bracket_from_coords(raw, kTwoDayHourlyUnits, "gregorian", parse_sim_datetime("2000-01-02T05:00:00"), "nearest", 0, "limit");
    EXPECT_TRUE(ok.valid);
    EXPECT_FALSE(ok.out_of_range);

    // The arithmetic path reports it the same way.
    const RecordBracket arith = bracket_from_cadence("monthly", "nearest", parse_sim_datetime("2024-01-01T00:00:00"), 288, 2000, 2023, 2000, "limit");
    EXPECT_FALSE(arith.valid);
    EXPECT_TRUE(arith.out_of_range);

    // An inverted range is a configuration error, not an out-of-range rejection.
    const RecordBracket inverted = bracket_from_cadence("monthly", "nearest", parse_sim_datetime("2024-01-01T00:00:00"), 288, 2000, 1999, 0, "limit");
    EXPECT_FALSE(inverted.valid);
    EXPECT_FALSE(inverted.out_of_range);
}

TEST(CeceCadenceArithmetic, MultiYearDailyRemapUsesEffectiveYearDayOfYear) {
    using namespace cece::detail;

    // A 1095-record daily file covering 2021-2023 (no leap years). A leap
    // simulation year cycles onto 2021, so the day-of-year has to be recomputed
    // in the effective year -- using the simulation year's puts every date
    // after February one record late.
    const int nt = 1095;
    auto rec = [&](const char* iso) { return bracket_from_cadence("daily", "nearest", parse_sim_datetime(iso), nt, 2021, 2023, 2021, "cycle").i0; };

    // 2024-03-01 cycles to 2021-03-01, which is record 59 (2021 day-of-year 60).
    EXPECT_EQ(rec("2024-03-01T00:00:00"), 59);
    EXPECT_EQ(rec("2021-03-01T00:00:00"), 59);

    // The alignment holds through the rest of the year.
    EXPECT_EQ(rec("2024-07-04T00:00:00"), 184);
    EXPECT_EQ(rec("2021-07-04T00:00:00"), 184);

    // Policy: Feb 29 maps onto Feb 28 of the non-leap effective year.
    EXPECT_EQ(rec("2024-02-29T00:00:00"), 58);
    EXPECT_EQ(rec("2021-02-28T00:00:00"), 58);
}

// ============================================================================
// Cycling year-aligned data. A calendar year is 365 or 366 days, so no fixed
// repeat period exists; wrapping the instant by one drifts a day per leap year
// and accumulates without bound. bracket_from_coords wraps the simulation year
// instead. Sub-annual files keep the (exact) modular behaviour.
// ============================================================================

namespace {

// Mid-month records for the given inclusive year range, as "days since 2000-01-01".
std::vector<double> monthly_axis(int year_first, int year_last) {
    std::vector<double> raw;
    const std::int64_t epoch = tick::Gregorian_Calendar::to_time_point(tick::Date_Time{2000, 1, 1, 0, 0, 0, 0}).nanos();
    for (int y = year_first; y <= year_last; ++y) {
        for (int m = 1; m <= 12; ++m) {
            const std::int64_t t = tick::Gregorian_Calendar::to_time_point(tick::Date_Time{y, m, 15, 0, 0, 0, 0}).nanos();
            raw.push_back(static_cast<double>(t - epoch) / static_cast<double>(tick::nanos_per_day));
        }
    }
    return raw;
}

constexpr const char* kEpochDays = "days since 2000-01-01 00:00:00";

}  // namespace

TEST(CeceCycleYearAlignment, MonthlyCyclingDoesNotDriftAcrossLeapYears) {
    using namespace cece::detail;

    // A Jan-Dec 2023 monthly climatology. Mar 15 of *any* later year must land
    // exactly on the March record. Under a fixed period the weight crept from
    // 0.06 in 2024 to 0.77 by 2042.
    const std::vector<double> raw = monthly_axis(2023, 2023);

    for (int yr : {2024, 2025, 2027, 2030, 2036, 2042}) {
        char iso[32];
        std::snprintf(iso, sizeof(iso), "%04d-03-15T00:00:00", yr);
        const RecordBracket br = bracket_from_coords(raw, kEpochDays, "gregorian", parse_sim_datetime(iso), "linear", 0, "cycle");
        ASSERT_TRUE(br.valid) << iso;
        EXPECT_EQ(br.i0, 2) << iso;
        EXPECT_NEAR(br.weight, 0.0, 1e-9) << iso;
    }
}

TEST(CeceCycleYearAlignment, MultiYearMonthlyCyclesByCalendarYear) {
    using namespace cece::detail;

    // 2020-2023, 48 records. Cycling repeats the four-year block.
    const std::vector<double> raw = monthly_axis(2020, 2023);

    // 2025 -> (2025-2020) % 4 = 1 -> 2021; March is record 1*12 + 2 = 14.
    EXPECT_EQ(bracket_from_coords(raw, kEpochDays, "gregorian", parse_sim_datetime("2025-03-15T00:00:00"), "nearest", 0, "cycle").i0, 14);
    // 2028 -> offset 0 -> 2020; July is record 6.
    EXPECT_EQ(bracket_from_coords(raw, kEpochDays, "gregorian", parse_sim_datetime("2028-07-15T00:00:00"), "nearest", 0, "cycle").i0, 6);
}

TEST(CeceCycleYearAlignment, SeamBetweenCyclesBracketsLastAgainstFirst) {
    using namespace cece::detail;

    // Jan 5 sits in the 31-day gap between the Dec 15 and Jan 15 records, so a
    // future Jan 5 must blend December into January, 21/31 of the way across.
    const std::vector<double> raw = monthly_axis(2023, 2023);
    const RecordBracket br = bracket_from_coords(raw, kEpochDays, "gregorian", parse_sim_datetime("2025-01-05T00:00:00"), "linear", 0, "cycle");
    ASSERT_TRUE(br.valid);
    EXPECT_EQ(br.i0, 11);
    EXPECT_EQ(br.i1, 0);
    EXPECT_NEAR(br.weight, 21.0 / 31.0, 1e-9);
}

TEST(CeceCycleYearAlignment, DailyCyclingSurvivesTheLeapDay) {
    using namespace cece::detail;

    // 365 daily records for 2023. March 1 of leap year 2024 must still be the
    // March 1 record (59), not the March 2 record that a 365-day period gives.
    std::vector<double> raw(365);
    const std::int64_t epoch = tick::Gregorian_Calendar::to_time_point(tick::Date_Time{2000, 1, 1, 0, 0, 0, 0}).nanos();
    const std::int64_t jan1 = tick::Gregorian_Calendar::to_time_point(tick::Date_Time{2023, 1, 1, 0, 0, 0, 0}).nanos();
    const double base = static_cast<double>(jan1 - epoch) / static_cast<double>(tick::nanos_per_day);
    for (int k = 0; k < 365; ++k) raw[k] = base + k;

    EXPECT_EQ(bracket_from_coords(raw, kEpochDays, "gregorian", parse_sim_datetime("2024-03-01T00:00:00"), "nearest", 0, "cycle").i0, 59);
    // The same date in a non-leap year is unchanged.
    EXPECT_EQ(bracket_from_coords(raw, kEpochDays, "gregorian", parse_sim_datetime("2025-03-01T00:00:00"), "nearest", 0, "cycle").i0, 59);
}

TEST(CeceCycleYearAlignment, SubAnnualFilesStillCycleModulo) {
    using namespace cece::detail;

    // A 48-hour file is not year-aligned, so it must keep wrapping on the axis:
    // hour 53 -> record 5. Year wrapping would be meaningless here.
    const std::vector<double> raw = two_day_hourly_axis();
    const RecordBracket br =
        bracket_from_coords(raw, kTwoDayHourlyUnits, "gregorian", parse_sim_datetime("2000-01-03T05:00:00"), "nearest", 0, "cycle");
    ASSERT_TRUE(br.valid);
    EXPECT_EQ(br.i0, 5);
}

TEST(CeceCycleYearAlignment, PartialYearFileIsNotTreatedAsAnnual) {
    using namespace cece::detail;

    // Mar-Aug only: the leftover to the next calendar year dwarfs the record
    // spacing, so this is not an annual cycle and must keep wrapping on the
    // axis. January is outside the covered months, which is where the two
    // regimes diverge -- year-wrapping would put it in the Aug/Mar seam
    // (record 0), modular wrapping lands it on record 4.
    std::vector<double> raw = monthly_axis(2023, 2023);
    raw = std::vector<double>(raw.begin() + 2, raw.begin() + 8);  // Mar..Aug

    const RecordBracket br = bracket_from_coords(raw, kEpochDays, "gregorian", parse_sim_datetime("2025-01-15T00:00:00"), "nearest", 0, "cycle");
    ASSERT_TRUE(br.valid);
    EXPECT_EQ(br.i0, 4);

    // Months the file does cover are unremarkable either way.
    EXPECT_EQ(bracket_from_coords(raw, kEpochDays, "gregorian", parse_sim_datetime("2025-05-15T00:00:00"), "nearest", 0, "cycle").i0, 2);
}

TEST(CeceCycleYearAlignment, PhaseShiftedAnnualFileIsRecognised) {
    using namespace cece::detail;

    // A complete 12-month cycle that does not start in January (July 2023 to
    // June 2024). Counting cycles from the first and last year *labels* called
    // this a two-year partial axis and fell back to a period inferred from the
    // final interval, so month alignment drifted a day per year.
    std::vector<double> raw;
    const std::int64_t epoch = tick::Gregorian_Calendar::to_time_point(tick::Date_Time{2000, 1, 1, 0, 0, 0, 0}).nanos();
    for (int k = 0; k < 12; ++k) {
        const int y = 2023 + (6 + k) / 12;
        const int m = 1 + (6 + k) % 12;
        const std::int64_t t = tick::Gregorian_Calendar::to_time_point(tick::Date_Time{y, m, 15, 0, 0, 0, 0}).nanos();
        raw.push_back(static_cast<double>(t - epoch) / static_cast<double>(tick::nanos_per_day));
    }

    // Record 0 is July, so September is record 2 and January is record 6.
    // Year remapping is not available here (the coverage is not January-
    // aligned), so this leans on the cycle period being one calendar year;
    // an inferred period drifts far enough to pick the wrong month.
    for (int y = 2025; y <= 2035; ++y) {
        const std::string sep = std::to_string(y) + "-09-15T00:00:00";
        EXPECT_EQ(bracket_from_coords(raw, kEpochDays, "gregorian", parse_sim_datetime(sep), "nearest", 0, "cycle").i0, 2) << sep;

        const std::string jan = std::to_string(y) + "-01-15T00:00:00";
        EXPECT_EQ(bracket_from_coords(raw, kEpochDays, "gregorian", parse_sim_datetime(jan), "nearest", 0, "cycle").i0, 6) << jan;
    }

    // Dates the file already covers are unaffected.
    EXPECT_EQ(bracket_from_coords(raw, kEpochDays, "gregorian", parse_sim_datetime("2023-07-15T00:00:00"), "nearest", 0, "cycle").i0, 0);
    EXPECT_EQ(bracket_from_coords(raw, kEpochDays, "gregorian", parse_sim_datetime("2024-06-15T00:00:00"), "nearest", 0, "cycle").i0, 11);
}

TEST(CeceCycleYearAlignment, ExtendAndLimitAreUnaffected) {
    using namespace cece::detail;

    const std::vector<double> raw = monthly_axis(2023, 2023);
    const SimDateTime future = parse_sim_datetime("2030-06-15T00:00:00");

    // extend clamps to the last record rather than wrapping the year.
    const RecordBracket ext = bracket_from_coords(raw, kEpochDays, "gregorian", future, "nearest", 0, "extend");
    ASSERT_TRUE(ext.valid);
    EXPECT_EQ(ext.i0, 11);

    // limit still rejects, and reports it as a deliberate rejection.
    const RecordBracket lim = bracket_from_coords(raw, kEpochDays, "gregorian", future, "nearest", 0, "limit");
    EXPECT_FALSE(lim.valid);
    EXPECT_TRUE(lim.out_of_range);
}

TEST(CeceCycleYearAlignment, InRangeDatesAreUnaffected) {
    using namespace cece::detail;

    // Nothing about the year wrap should touch a date the file already covers.
    const std::vector<double> raw = monthly_axis(2023, 2023);
    EXPECT_EQ(bracket_from_coords(raw, kEpochDays, "gregorian", parse_sim_datetime("2023-07-15T00:00:00"), "nearest", 0, "cycle").i0, 6);
    EXPECT_EQ(bracket_from_coords(raw, kEpochDays, "gregorian", parse_sim_datetime("2023-01-15T00:00:00"), "nearest", 0, "cycle").i0, 0);
    EXPECT_EQ(bracket_from_coords(raw, kEpochDays, "gregorian", parse_sim_datetime("2023-12-15T00:00:00"), "nearest", 0, "cycle").i0, 11);
}

TEST(CeceCycleYearAlignment, Feb29ClampsOntoNonLeapFileYear) {
    using namespace cece::detail;

    // Cycling Feb 29 onto non-leap 2023 must clamp to Feb 28 rather than
    // constructing a date the calendar rejects (which would degrade the whole
    // decode).
    const std::vector<double> raw = monthly_axis(2023, 2023);
    const RecordBracket br = bracket_from_coords(raw, kEpochDays, "gregorian", parse_sim_datetime("2024-02-29T00:00:00"), "nearest", 0, "cycle");
    ASSERT_TRUE(br.valid);
    EXPECT_EQ(br.i0, 1);
}

TEST(CeceAxisDecode, TimeAxisFallbackOnNullDataset) {
    using namespace cece::detail;

    SimDateTime dt = parse_sim_datetime("2023-07-01T00:00:00");
    RecordBracket br = bracket_from_dataset(nullptr, "time", dt, 288, "nearest", 2000, "extend");
    EXPECT_FALSE(br.valid);
}

TEST(CeceCadenceArithmetic, InvalidAndCaseInsensitiveInputs) {
    using namespace cece::detail;

    SimDateTime dt = parse_sim_datetime("2023-07-01T00:00:00");
    EXPECT_TRUE(dt.valid);

    // Empty cadence -> invalid (falls back to legacy step-index cycling).
    RecordBracket br_empty = bracket_from_cadence("", "nearest", dt, 12);
    EXPECT_FALSE(br_empty.valid);

    // Unknown cadence -> invalid.
    RecordBracket br_unknown = bracket_from_cadence("yearly", "nearest", dt, 12);
    EXPECT_FALSE(br_unknown.valid);

    // Invalid (unparsed) datetime -> invalid regardless of cadence.
    SimDateTime bad_dt;  // valid == false
    RecordBracket br_bad = bracket_from_cadence("monthly", "nearest", bad_dt, 12);
    EXPECT_FALSE(br_bad.valid);

    // Cadence matching is case-insensitive: "MONTHLY" == "monthly".
    RecordBracket br_upper = bracket_from_cadence("MONTHLY", "NEAREST", dt, 12);
    EXPECT_TRUE(br_upper.valid);
    EXPECT_EQ(br_upper.i0, 6);  // July -> month-1 for a 12-record climatology
    EXPECT_EQ(br_upper.i1, 6);
}

// ============================================================================
// Tests for the decoded axis path, bracket_from_coords. These feed
// synthetic raw axes plus CF units/calendar strings (no AMIO handle) and check
// the decode -> absolute-time -> find_bracket flow. The reference sim date
// 2000-02-10 is exactly 40 days after a 2000-01-01 epoch.
// ============================================================================

TEST(CeceAxisDecode, DecodeDaysGregorian) {
    using namespace cece::detail;

    // "days since 2000-01-01" start-of-month (2000 is a leap year: Mar 1 = 60).
    const std::vector<double> raw = {0.0, 31.0, 60.0};
    SimDateTime dt = parse_sim_datetime("2000-02-10T00:00:00");

    RecordBracket lin = bracket_from_coords(raw, "days since 2000-01-01", "gregorian", dt, "linear");
    EXPECT_TRUE(lin.valid);
    EXPECT_EQ(lin.i0, 1);
    EXPECT_EQ(lin.i1, 2);
    EXPECT_NEAR(lin.weight, 9.0 / 29.0, 1e-9);

    RecordBracket nr = bracket_from_coords(raw, "days since 2000-01-01", "gregorian", dt, "nearest");
    EXPECT_TRUE(nr.valid);
    EXPECT_EQ(nr.i0, 1);
    EXPECT_EQ(nr.i1, 1);
}

TEST(CeceAxisDecode, DecodeHoursUnitAndBlankCalendar) {
    using namespace cece::detail;

    // Same axis in hours; a blank calendar defaults to gregorian. The 'T' in
    // the reference and the sub-day scaling must both be handled.
    const std::vector<double> raw = {0.0, 744.0, 1440.0};  // 31 d, then +29 d, in hours
    SimDateTime dt = parse_sim_datetime("2000-02-10T00:00:00");

    RecordBracket lin = bracket_from_coords(raw, "hours since 2000-01-01T00:00:00", "", dt, "linear");
    EXPECT_TRUE(lin.valid);
    EXPECT_EQ(lin.i0, 1);
    EXPECT_EQ(lin.i1, 2);
    EXPECT_NEAR(lin.weight, 9.0 / 29.0, 1e-9);
}

TEST(CeceAxisDecode, UnsupportedCalendarsDoNotDecodeAsGregorian) {
    using namespace cece::detail;

    const std::vector<double> raw = {0.0, 31.0, 60.0};
    const SimDateTime dt = parse_sim_datetime("2000-02-10T00:00:00");

    // julian and all_leap are real CF calendars TICK cannot honour. Silently
    // treating them as Gregorian gives a plausible but wrong record (julian is
    // 13 days from Gregorian today), so the axis is reported as undecodable.
    for (const char* cal : {"julian", "all_leap", "366_day", "not_a_calendar"}) {
        EXPECT_FALSE(bracket_from_coords(raw, "days since 2000-01-01", cal, dt, "linear").valid) << cal;
    }

    // The supported spellings still decode, including CF's default for an
    // absent attribute.
    for (const char* cal : {"", "standard", "gregorian", "proleptic_gregorian", "noleap", "360_day"}) {
        EXPECT_TRUE(bracket_from_coords(raw, "days since 2000-01-01", cal, dt, "linear").valid) << cal;
    }
}

TEST(CeceAxisDecode, SubSecondAxesAreUsable) {
    using namespace cece::detail;

    // Ten records 0.05 s apart. The old guard rejected any multi-record axis
    // spanning under a second, which threw away legitimate high-frequency data.
    std::vector<double> raw(10);
    for (int k = 0; k < 10; ++k) raw[k] = 0.05 * k;

    const SimDateTime dt = parse_sim_datetime("2000-01-01T00:00:00");
    const RecordBracket br = bracket_from_coords(raw, "seconds since 2000-01-01 00:00:00", "gregorian", dt, "nearest");
    ASSERT_TRUE(br.valid);
    EXPECT_EQ(br.i0, 0);

    // A span below TICK's nanosecond resolution still cannot be a time axis.
    const std::vector<double> collapsed = {0.0, 1e-20, 2e-20};
    EXPECT_FALSE(bracket_from_coords(collapsed, "seconds since 2000-01-01 00:00:00", "gregorian", dt, "nearest").valid);
}

TEST(CeceAxisDecode, DecodeNoLeapCalendar) {
    using namespace cece::detail;

    // noleap calendar: February always has 28 days, so Mar 1 = day 59.
    const std::vector<double> raw = {0.0, 31.0, 59.0};
    SimDateTime dt = parse_sim_datetime("2000-02-10T00:00:00");

    RecordBracket lin = bracket_from_coords(raw, "days since 2000-01-01", "noleap", dt, "linear");
    EXPECT_TRUE(lin.valid);
    EXPECT_EQ(lin.i0, 1);
    EXPECT_EQ(lin.i1, 2);
    EXPECT_NEAR(lin.weight, 9.0 / 28.0, 1e-9);  // (40 - 31) / (59 - 31)
}

TEST(CeceAxisDecode, DecodeNonDecodableUnitsDegrade) {
    using namespace cece::detail;

    const std::vector<double> raw = {0.0, 1.0, 2.0};
    SimDateTime dt = parse_sim_datetime("2000-02-10T00:00:00");

    // Non-fixed unit (months/years) -> invalid so the caller degrades.
    EXPECT_FALSE(bracket_from_coords(raw, "months since 2000-01-01", "", dt, "linear").valid);
    EXPECT_FALSE(bracket_from_coords(raw, "years since 2000-01-01", "", dt, "linear").valid);
    // Missing / garbled units -> invalid.
    EXPECT_FALSE(bracket_from_coords(raw, "", "", dt, "linear").valid);
    EXPECT_FALSE(bracket_from_coords(raw, "time_counter", "", dt, "linear").valid);
    // Empty axis / invalid datetime -> invalid.
    EXPECT_FALSE(bracket_from_coords({}, "days since 2000-01-01", "", dt, "linear").valid);
    SimDateTime bad_dt;
    EXPECT_FALSE(bracket_from_coords(raw, "days since 2000-01-01", "", bad_dt, "linear").valid);
}

TEST(CeceAxisDecode, DecodeYearAlignRemap) {
    using namespace cece::detail;

    // File covers year 2000 (record 0). yearAlign=2020 means sim year 2020
    // aligns to the file's first year -> 2020-02-10 maps to 2000-02-10 (day 40).
    const std::vector<double> raw = {0.0, 31.0, 60.0};
    SimDateTime dt = parse_sim_datetime("2020-02-10T00:00:00");

    RecordBracket br = bracket_from_coords(raw, "days since 2000-01-01", "gregorian", dt, "linear", 2020);
    EXPECT_TRUE(br.valid);
    EXPECT_EQ(br.i0, 1);
    EXPECT_EQ(br.i1, 2);
    EXPECT_NEAR(br.weight, 9.0 / 29.0, 1e-9);
}

TEST(CeceAxisDecode, DecodeTaxmode) {
    using namespace cece::detail;

    // Jan-Mar 2000 file. Sim 2000-06-15 is 166 days in -> beyond day 60.
    const std::vector<double> raw = {0.0, 31.0, 60.0};
    SimDateTime dt = parse_sim_datetime("2000-06-15T00:00:00");

    EXPECT_FALSE(bracket_from_coords(raw, "days since 2000-01-01", "", dt, "nearest", 0, "limit").valid);

    RecordBracket ext = bracket_from_coords(raw, "days since 2000-01-01", "", dt, "nearest", 0, "extend");
    EXPECT_TRUE(ext.valid);
    EXPECT_EQ(ext.i0, 2);
    EXPECT_EQ(ext.i1, 2);

    // cycle: the file repeats with period 89 d (60 d span + the trailing 29 d
    // interval), so day 166 wraps to day 77 -- past the last record, in the
    // trailing interval that brackets Mar 1 against Jan 1 of the next cycle.
    RecordBracket cyc = bracket_from_coords(raw, "days since 2000-01-01", "", dt, "linear", 0, "cycle");
    EXPECT_TRUE(cyc.valid);
    EXPECT_EQ(cyc.i0, 2);
    EXPECT_EQ(cyc.i1, 0);
    EXPECT_NEAR(cyc.weight, 17.0 / 29.0, 1e-9);
}

// Direct tests for the shared generic bracketer that both the arithmetic and
// axis paths now delegate to. Times are already in a common unit (days here).
TEST(CeceFindBracket, BracketTimesDirect) {
    using namespace cece::detail;

    const std::vector<double> times = {0.0, 31.0, 60.0};

    // Linear: 40 falls between rec 1 (31) and rec 2 (60).
    RecordBracket lin = find_bracket(times, 40.0, /*linear=*/true, "extend");
    EXPECT_TRUE(lin.valid);
    EXPECT_EQ(lin.i0, 1);
    EXPECT_EQ(lin.i1, 2);
    EXPECT_NEAR(lin.weight, 9.0 / 29.0, 1e-9);

    // Nearest: 40 is closer to rec 1 (31) than rec 2 (60).
    RecordBracket nr = find_bracket(times, 40.0, /*linear=*/false, "extend");
    EXPECT_TRUE(nr.valid);
    EXPECT_EQ(nr.i0, 1);
    EXPECT_EQ(nr.i1, 1);

    // taxmode limit: out-of-range target -> invalid.
    EXPECT_FALSE(find_bracket(times, 166.0, true, "limit").valid);

    // taxmode cycle: the repeat period is 89 (the 60 span plus the trailing 29
    // interval), so 166 wraps to 77 -- inside that trailing interval, which
    // brackets the last record against the first of the next cycle.
    RecordBracket cyc = find_bracket(times, 166.0, true, "cycle");
    EXPECT_TRUE(cyc.valid);
    EXPECT_EQ(cyc.i0, 2);
    EXPECT_EQ(cyc.i1, 0);
    EXPECT_NEAR(cyc.weight, 17.0 / 29.0, 1e-9);

    // A target inside the recorded range is unaffected by the period.
    RecordBracket in_range = find_bracket(times, 46.0, true, "cycle");
    EXPECT_TRUE(in_range.valid);
    EXPECT_EQ(in_range.i0, 1);
    EXPECT_EQ(in_range.i1, 2);
    EXPECT_NEAR(in_range.weight, 15.0 / 29.0, 1e-9);

    // Single record and empty edge cases.
    RecordBracket single = find_bracket({5.0}, 99.0, true, "extend");
    EXPECT_TRUE(single.valid);
    EXPECT_EQ(single.i0, 0);
    EXPECT_EQ(single.i1, 0);
    EXPECT_FALSE(find_bracket({}, 0.0, true, "extend").valid);
}

TEST(CeceFindBracket, RejectsUnorderedAxis) {
    using namespace cece::detail;

    // find_bracket binary-searches, so an out-of-order axis has no meaningful
    // answer; returning invalid lets the caller degrade instead of silently
    // resolving to the wrong record.
    const std::vector<double> unsorted = {0.0, 60.0, 31.0};
    EXPECT_FALSE(find_bracket(unsorted, 40.0, true, "extend").valid);
    EXPECT_FALSE(find_bracket(unsorted, 40.0, false, "cycle").valid);

    const SimDateTime dt = parse_sim_datetime("2000-02-10T00:00:00");
    EXPECT_FALSE(bracket_from_coords(unsorted, "days since 2000-01-01", "gregorian", dt, "linear").valid);

    // Repeated stamps are still ordered, so they stay usable.
    const std::vector<double> repeated = {0.0, 31.0, 31.0, 60.0};
    const RecordBracket br = find_bracket(repeated, 40.0, true, "extend");
    EXPECT_TRUE(br.valid);
    EXPECT_EQ(br.i0, 2);
    EXPECT_EQ(br.i1, 3);
    EXPECT_NEAR(br.weight, 9.0 / 29.0, 1e-9);
}

TEST(CeceAxisDecode, DecodeRejectsDegenerateAxisSpan) {
    using namespace cece::detail;

    const SimDateTime dt = parse_sim_datetime("2000-02-10T00:00:00");

    // Several records at the same instant carry no time information.
    EXPECT_FALSE(bracket_from_coords({5.0, 5.0, 5.0}, "days since 2000-01-01", "gregorian", dt, "nearest").valid);

    // A single-record file is still fine -- there is nothing to bracket.
    EXPECT_TRUE(bracket_from_coords({5.0}, "days since 2000-01-01", "gregorian", dt, "nearest").valid);
}

// ============================================================================
// Dated hourly meteorology: a file holding 48 hourly records (two full days)
// driving a two-day run must resolve each simulation hour to the record for
// that absolute date-hour, so day 2 reads records 24-47 rather than replaying
// day 1. This is the "series" cadence, not "hourly" which is for a single profile.
//
// Sim times are whole hours and the records are hourly, so every target lands
// exactly on a record and nearest-neighbour is the meaningful assertion.
// ============================================================================

TEST(CeceAxisDecode, SeriesHourlyTwoDayFileWalksAllRecords) {
    using namespace cece::detail;

    const std::vector<double> raw = two_day_hourly_axis();

    for (int k = 0; k < 48; ++k) {
        char iso[32];
        std::snprintf(iso, sizeof(iso), "2000-01-%02dT%02d:00:00", 1 + k / 24, k % 24);

        const SimDateTime dt = parse_sim_datetime(iso);
        ASSERT_TRUE(dt.valid) << iso;

        const RecordBracket br = bracket_from_coords(raw, kTwoDayHourlyUnits, "gregorian", dt, "nearest");
        ASSERT_TRUE(br.valid) << iso;
        EXPECT_EQ(br.i0, k) << iso;
        EXPECT_EQ(br.i1, k) << iso;
    }
}

TEST(CeceAxisDecode, SeriesHourlyDiffersFromHourlyProfile) {
    using namespace cece::detail;

    const std::vector<double> raw = two_day_hourly_axis();
    const SimDateTime day1 = parse_sim_datetime("2000-01-01T05:00:00");
    const SimDateTime day2 = parse_sim_datetime("2000-01-02T05:00:00");

    // series: 05Z on the second day advances to that day's own record.
    EXPECT_EQ(bracket_from_coords(raw, kTwoDayHourlyUnits, "gregorian", day1, "nearest").i0, 5);
    EXPECT_EQ(bracket_from_coords(raw, kTwoDayHourlyUnits, "gregorian", day2, "nearest").i0, 29);

    // hourly profile: preserved repeating hour-of-day, so both days give record 5.
    EXPECT_EQ(bracket_from_cadence("hourly", "nearest", day1, 48).i0, 5);
    EXPECT_EQ(bracket_from_cadence("hourly", "nearest", day2, 48).i0, 5);
}

TEST(CeceAxisDecode, SeriesHourlyRunOutlastsFile) {
    using namespace cece::detail;

    const std::vector<double> raw = two_day_hourly_axis();
    // A third day is past the final record (53 h > 47 h).
    const SimDateTime day3 = parse_sim_datetime("2000-01-03T05:00:00");

    // limit: a run that outlasts the file is reported as unresolvable.
    EXPECT_FALSE(bracket_from_coords(raw, kTwoDayHourlyUnits, "gregorian", day3, "nearest", 0, "limit").valid);

    // extend: hold the last record.
    const RecordBracket ext = bracket_from_coords(raw, kTwoDayHourlyUnits, "gregorian", day3, "nearest", 0, "extend");
    ASSERT_TRUE(ext.valid);
    EXPECT_EQ(ext.i0, 47);

    // cycle (default): a 48-record hourly file repeats with a 48-hour period,
    // so hour 53 of the run resolves to record 53 % 48 == 5.
    const RecordBracket cyc = bracket_from_coords(raw, kTwoDayHourlyUnits, "gregorian", day3, "nearest", 0, "cycle");
    ASSERT_TRUE(cyc.valid);
    EXPECT_EQ(cyc.i0, 5);

    // The whole third day replays the first day record-for-record, with no
    // drift at the seam that a period of 47 h would introduce.
    for (int h = 0; h < 24; ++h) {
        char iso[32];
        std::snprintf(iso, sizeof(iso), "2000-01-03T%02d:00:00", h);
        const SimDateTime dt = parse_sim_datetime(iso);
        ASSERT_TRUE(dt.valid) << iso;
        const RecordBracket br = bracket_from_coords(raw, kTwoDayHourlyUnits, "gregorian", dt, "nearest", 0, "cycle");
        ASSERT_TRUE(br.valid) << iso;
        EXPECT_EQ(br.i0, h) << iso;
    }
}

// ============================================================================
// Sub-hourly simulation times. SimDateTime must carry minutes and seconds all
// the way through to the record lookup; truncating to the hour silently reads
// the wrong record (or the wrong interpolation weight) on sub-hourly axes.
// ============================================================================

TEST(CeceSimDateTime, ParseKeepsMinutesAndSeconds) {
    using namespace cece::detail;

    const SimDateTime dt = parse_sim_datetime("2000-01-01T00:40:30");
    ASSERT_TRUE(dt.valid);
    EXPECT_EQ(dt.hour, 0);
    EXPECT_EQ(dt.minute, 40);
    EXPECT_EQ(dt.second, 30);
}

TEST(CeceAxisDecode, DecodeSubHourlySimTimeOnHourlyAxis) {
    using namespace cece::detail;

    const std::vector<double> raw = two_day_hourly_axis();

    // nearest: 00:40 is closer to the 01Z record than to 00Z. Truncating to the
    // hour would keep record 0.
    const SimDateTime late = parse_sim_datetime("2000-01-01T00:40:00");
    const RecordBracket nr = bracket_from_coords(raw, kTwoDayHourlyUnits, "gregorian", late, "nearest");
    ASSERT_TRUE(nr.valid);
    EXPECT_EQ(nr.i0, 1);

    // linear: half past the hour blends the bracketing records evenly. Under
    // hour truncation the weight would collapse to 0.
    const SimDateTime half = parse_sim_datetime("2000-01-01T00:30:00");
    const RecordBracket lin = bracket_from_coords(raw, kTwoDayHourlyUnits, "gregorian", half, "linear");
    ASSERT_TRUE(lin.valid);
    EXPECT_EQ(lin.i0, 0);
    EXPECT_EQ(lin.i1, 1);
    EXPECT_NEAR(lin.weight, 0.5, 1e-9);
}

TEST(CeceAxisDecode, DecodeQuarterHourlyAxis) {
    using namespace cece::detail;

    // Six 15-minute records covering 00:00 .. 01:15.
    std::vector<double> raw(6);
    for (int k = 0; k < 6; ++k) raw[k] = 15.0 * k;
    const char* units = "minutes since 2000-01-01 00:00:00";

    // Every quarter-hour stamp lands exactly on its own record; hour truncation
    // would collapse the first four onto record 0.
    const char* stamps[] = {"2000-01-01T00:00:00", "2000-01-01T00:15:00", "2000-01-01T00:30:00",
                            "2000-01-01T00:45:00", "2000-01-01T01:00:00", "2000-01-01T01:15:00"};
    for (int k = 0; k < 6; ++k) {
        const SimDateTime dt = parse_sim_datetime(stamps[k]);
        ASSERT_TRUE(dt.valid) << stamps[k];
        const RecordBracket br = bracket_from_coords(raw, units, "gregorian", dt, "nearest");
        ASSERT_TRUE(br.valid) << stamps[k];
        EXPECT_EQ(br.i0, k) << stamps[k];
    }

    // Seconds count as well: 00:37:30 sits midway between records 2 and 3.
    const SimDateTime dt = parse_sim_datetime("2000-01-01T00:37:30");
    const RecordBracket lin = bracket_from_coords(raw, units, "gregorian", dt, "linear");
    ASSERT_TRUE(lin.valid);
    EXPECT_EQ(lin.i0, 2);
    EXPECT_EQ(lin.i1, 3);
    EXPECT_NEAR(lin.weight, 0.5, 1e-9);
}

TEST(CeceCadenceArithmetic, SubHourlyRefinesArithmeticFraction) {
    using namespace cece::detail;

    // Daily mid-day convention: 06:00 and 06:30 must not produce the same weight.
    const RecordBracket at_0600 = bracket_from_cadence("daily", "linear", parse_sim_datetime("2026-06-15T06:00:00"), 365);
    const RecordBracket at_0630 = bracket_from_cadence("daily", "linear", parse_sim_datetime("2026-06-15T06:30:00"), 365);
    ASSERT_TRUE(at_0600.valid);
    ASSERT_TRUE(at_0630.valid);
    EXPECT_EQ(at_0600.i0, 164);
    EXPECT_EQ(at_0600.i1, 165);
    EXPECT_NEAR(at_0600.weight, 6.0 / 24.0 + 0.5, 1e-9);
    EXPECT_NEAR(at_0630.weight, 6.5 / 24.0 + 0.5, 1e-9);

    // Monthly mid-month convention: June 16 00:36 is just past the mid-month
    // midpoint, which hour truncation would report as exactly on it.
    const RecordBracket mon = bracket_from_cadence("monthly", "linear", parse_sim_datetime("2020-06-16T00:36:00"), 12);
    ASSERT_TRUE(mon.valid);
    EXPECT_EQ(mon.i0, 5);
    EXPECT_EQ(mon.i1, 6);
    EXPECT_NEAR(mon.weight, (0.6 / 24.0) / 30.0, 1e-12);
}

TEST(CeceAxisDecode, DecodeRejectsIntegerAxisMisreadAsFloat) {
    using namespace cece::detail;

    // CF time coordinates are commonly stored as integers. Reading an int32
    // "hours since ..." axis as float32 is the failure this guards: the bit
    // patterns of ascending non-negative integers are themselves ascending
    // floats, so the axis still looks ordered, but every value collapses to a
    // denormal and the whole file decodes to a single instant.
    std::vector<double> misread(48);
    for (int k = 0; k < 48; ++k) {
        const std::int32_t stored = k;
        float reinterpreted = 0.0f;
        std::memcpy(&reinterpreted, &stored, sizeof(reinterpreted));
        misread[k] = static_cast<double>(reinterpreted);
    }
    // Ordering alone does not reveal the misread -- the span guard is what does.
    ASSERT_TRUE(std::is_sorted(misread.begin(), misread.end()));

    const SimDateTime dt = parse_sim_datetime("2000-01-02T05:00:00");
    EXPECT_FALSE(bracket_from_coords(misread, kTwoDayHourlyUnits, "gregorian", dt, "nearest").valid);

    // The same axis with the correct numeric type resolves normally.
    const RecordBracket ok = bracket_from_coords(two_day_hourly_axis(), kTwoDayHourlyUnits, "gregorian", dt, "nearest");
    ASSERT_TRUE(ok.valid);
    EXPECT_EQ(ok.i0, 29);
}

TEST(CeceAxisDecode, DecodeIntegerTimeAxis) {
    using namespace cece::detail;

    // int32 "hours since ..." is a common CF encoding. Widening through the
    // view's dtype gives the same answer a float64 axis would.
    std::vector<std::int32_t> stored(48);
    for (int k = 0; k < 48; ++k) stored[k] = k;

    std::vector<double> decoded;
    ASSERT_TRUE(widen_amio_elements(stored.data(), AMIO_DTYPE_I32, stored.size(), 1.0, 0.0, decoded));
    EXPECT_EQ(decoded, two_day_hourly_axis());

    const SimDateTime dt = parse_sim_datetime("2000-01-02T05:00:00");
    const RecordBracket br = bracket_from_coords(decoded, kTwoDayHourlyUnits, "gregorian", dt, "nearest");
    ASSERT_TRUE(br.valid);
    EXPECT_EQ(br.i0, 29);
}

TEST(CeceAxisDecode, DecodePackedTimeAxis) {
    using namespace cece::detail;

    // Stored as int16 half-hour counts with scale_factor 0.5, so record k is
    // hour k on a "hours since" axis.
    std::vector<std::int16_t> stored(48);
    for (int k = 0; k < 48; ++k) stored[k] = static_cast<std::int16_t>(2 * k);

    std::vector<double> decoded;
    ASSERT_TRUE(widen_amio_elements(stored.data(), AMIO_DTYPE_I16, stored.size(), 0.5, 0.0, decoded));
    EXPECT_NEAR(decoded[29], 29.0, 1e-12);

    const SimDateTime dt = parse_sim_datetime("2000-01-02T05:00:00");
    const RecordBracket br = bracket_from_coords(decoded, kTwoDayHourlyUnits, "gregorian", dt, "nearest");
    ASSERT_TRUE(br.valid);
    EXPECT_EQ(br.i0, 29);

    // Ignoring the packing would put every record at twice its true hour, so
    // the same stamp would land on record 14 instead.
    std::vector<double> unscaled;
    ASSERT_TRUE(widen_amio_elements(stored.data(), AMIO_DTYPE_I16, stored.size(), 1.0, 0.0, unscaled));
    const RecordBracket wrong = bracket_from_coords(unscaled, kTwoDayHourlyUnits, "gregorian", dt, "nearest");
    ASSERT_TRUE(wrong.valid);
    EXPECT_EQ(wrong.i0, 14);
}

}  // namespace cece
