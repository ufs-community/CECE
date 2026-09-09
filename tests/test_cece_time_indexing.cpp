/**
 * @file test_cece_time_indexing.cpp
 * @brief Tests for resolving a simulation date-time to file record indices.
 *
 * Covers the cece::detail helpers behind data stream time indexing:
 *   - parse_cf_units: decoding CF "<unit> since <reference>" strings
 *   - bracket_from_cadence: the arithmetic cadence fallback (hourly/daily/weekly/monthly)
 *   - bracket_from_coords: the decoded time-axis path
 *   - find_bracket: the shared bracketing primitive both paths delegate to
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <tick/tick.hpp>
#include <vector>

#include "cece/cece_driver_facade.hpp"

namespace cece {

TEST(CeceCadenceIndexing, HourlyCadence) {
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

TEST(CeceCadenceIndexing, DailyCadenceNearest) {
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

TEST(CeceCadenceIndexing, DailyCadenceLinear) {
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

TEST(CeceCadenceIndexing, DailyCadenceLeapYearNormalization) {
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

TEST(CeceCadenceIndexing, DailyCadenceLeapAwareClimatologyInNonLeapYear) {
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

TEST(CeceCadenceIndexing, DailyCadenceLinearLeapYearOverflow) {
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

TEST(CeceCadenceIndexing, WeeklyCadenceISO8601) {
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

TEST(CeceCadenceIndexing, MonthlyCadenceNearestMultiYear) {
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

    // Out-of-range simulation date: 2026-08-01 with taxmode="extend"
    // Clamps effective year to 2023 -> August 2023 -> (2023-2000)*12 + 7 = 283
    SimDateTime dt_future = parse_sim_datetime("2026-08-01T00:00:00");
    RecordBracket br_future = bracket_from_cadence("monthly", "nearest", dt_future, 288, 2000, 2023, 2000, "extend");
    EXPECT_TRUE(br_future.valid);
    EXPECT_EQ(br_future.i0, 283);
    EXPECT_EQ(br_future.i1, 283);

    // First month: 2000-01-01 -> record 0
    SimDateTime dt_first = parse_sim_datetime("2000-01-01T00:00:00");
    RecordBracket br_first = bracket_from_cadence("monthly", "nearest", dt_first, 288, 2000, 2023, 2000, "extend");
    EXPECT_TRUE(br_first.valid);
    EXPECT_EQ(br_first.i0, 0);
    EXPECT_EQ(br_first.i1, 0);
}

TEST(CeceCadenceIndexing, MonthlyCadenceLinearMultiYear) {
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

TEST(CeceCadenceIndexing, MonthlyCadenceLinearMultiYearBoundaryWrapCaveat) {
    using namespace cece::detail;

    // CAVEAT TEST (documents current fallback behaviour, not ideal semantics):
    // For a multi-year monthly file (288 records, 2000-2023) the arithmetic
    // linear path wraps modulo file_nt, so the first/last records interpolate
    // against the opposite end of the file instead of clamping. The robust
    // axis-based resolver (bracket_from_dataset) avoids this;
    // bracket_from_cadence is only the fallback used when the axis read fails.

    // Late December 2023 (last month, record 287). December has 31 days;
    // day 20 -> frac = 19/31 ~= 0.613 -> forward interpolation wraps i1 to 0.
    SimDateTime dt_end = parse_sim_datetime("2023-12-20T00:00:00");
    RecordBracket br_end = bracket_from_cadence("monthly", "linear", dt_end, 288, 2000, 2023, 2000, "extend");
    EXPECT_TRUE(br_end.valid);
    EXPECT_EQ(br_end.i0, 287);
    EXPECT_EQ(br_end.i1, 0);  // wraps to the first record (Jan 2000) -- caveat
    EXPECT_NEAR(br_end.weight, 19.0 / 31.0 - 0.5, 1e-9);

    // Early January 2000 (first month, record 0). Jan day 5 -> frac = 4/31 ~= 0.129
    // -> backward interpolation wraps i0 to the last record (Dec 2023).
    SimDateTime dt_start = parse_sim_datetime("2000-01-05T00:00:00");
    RecordBracket br_start = bracket_from_cadence("monthly", "linear", dt_start, 288, 2000, 2023, 2000, "extend");
    EXPECT_TRUE(br_start.valid);
    EXPECT_EQ(br_start.i0, 287);  // wraps to the last record (Dec 2023) -- caveat
    EXPECT_EQ(br_start.i1, 0);
    EXPECT_NEAR(br_start.weight, 4.0 / 31.0 + 0.5, 1e-9);
}

TEST(CeceCadenceIndexing, MonthlyCadenceLinearClimatology) {
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

TEST(CeceCadenceIndexing, MonthlyTaxmodeCycleAndLimit) {
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

TEST(CeceCadenceIndexing, TimeAxisFallbackOnNullDataset) {
    using namespace cece::detail;

    SimDateTime dt = parse_sim_datetime("2023-07-01T00:00:00");
    RecordBracket br = bracket_from_dataset(nullptr, "time", dt, 288, "nearest", 2000, "extend");
    EXPECT_FALSE(br.valid);
}

TEST(CeceCadenceIndexing, InvalidAndCaseInsensitiveInputs) {
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

TEST(CeceCfUnits, ParseFixedLengthUnits) {
    using namespace cece::detail;

    EXPECT_NEAR(parse_cf_units("seconds since 2000-01-01").unit_days, 1.0 / 86400.0, 1e-15);
    EXPECT_NEAR(parse_cf_units("minutes since 2000-01-01").unit_days, 1.0 / 1440.0, 1e-15);
    EXPECT_NEAR(parse_cf_units("hours since 2000-01-01").unit_days, 1.0 / 24.0, 1e-15);
    EXPECT_NEAR(parse_cf_units("days since 2000-01-01").unit_days, 1.0, 1e-15);

    // Abbreviations and case are both tolerated.
    EXPECT_NEAR(parse_cf_units("Hrs SINCE 2000-01-01").unit_days, 1.0 / 24.0, 1e-15);
    EXPECT_NEAR(parse_cf_units("  d  since 2000-01-01").unit_days, 1.0, 1e-15);
}

TEST(CeceCfUnits, ParseReferenceDateTime) {
    using namespace cece::detail;

    const CFTimeUnits full = parse_cf_units("hours since 1999-03-04T05:06:07");
    ASSERT_TRUE(full.valid);
    EXPECT_EQ(full.reference, (tick::Date_Time{1999, 3, 4, 5, 6, 7, 0}));

    // A bare date leaves the time at midnight; a partial time fills only what is given.
    const CFTimeUnits bare = parse_cf_units("days since 1850-1-2");
    ASSERT_TRUE(bare.valid);
    EXPECT_EQ(bare.reference, (tick::Date_Time{1850, 1, 2, 0, 0, 0, 0}));

    const CFTimeUnits partial = parse_cf_units("days since 1850-01-02 12:30");
    ASSERT_TRUE(partial.valid);
    EXPECT_EQ(partial.reference, (tick::Date_Time{1850, 1, 2, 12, 30, 0, 0}));
}

TEST(CeceCfUnits, ParseIgnoresTrailingReferenceText) {
    using namespace cece::detail;

    // Fractional seconds, a zone suffix and a UTC offset are all common in real files.
    for (const char* units : {"hours since 1900-01-01 00:00:00.0", "seconds since 1900-01-01 00:00:00 UTC", "days since 1900-01-01T00:00:00Z",
                              "days since 1900-01-01 00:00:00+00:00"}) {
        const CFTimeUnits u = parse_cf_units(units);
        EXPECT_TRUE(u.valid) << units;
        EXPECT_EQ(u.reference, (tick::Date_Time{1900, 1, 1, 0, 0, 0, 0})) << units;
    }
}

TEST(CeceCfUnits, ParseRejectsUndecodableUnits) {
    using namespace cece::detail;

    // Calendar-ambiguous units.
    EXPECT_FALSE(parse_cf_units("months since 2000-01-01").valid);
    EXPECT_FALSE(parse_cf_units("years since 2000-01-01").valid);
    EXPECT_FALSE(parse_cf_units("furlongs since 2000-01-01").valid);
    // Missing or garbled units strings.
    EXPECT_FALSE(parse_cf_units("").valid);
    EXPECT_FALSE(parse_cf_units("time_counter").valid);
    EXPECT_FALSE(parse_cf_units("days 2000-01-01").valid);
    // Unparsable or out-of-range reference dates.
    EXPECT_FALSE(parse_cf_units("days since not-a-date").valid);
    EXPECT_FALSE(parse_cf_units("days since 2000").valid);
    EXPECT_FALSE(parse_cf_units("days since 2000-01").valid);
    EXPECT_FALSE(parse_cf_units("days since 2000-13-01").valid);
    EXPECT_FALSE(parse_cf_units("days since 2000-01-32").valid);
}

TEST(CeceCadenceIndexing, DecodeDaysGregorian) {
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

TEST(CeceCadenceIndexing, DecodeHoursUnitAndBlankCalendar) {
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

TEST(CeceCadenceIndexing, DecodeNoLeapCalendar) {
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

TEST(CeceCadenceIndexing, DecodeNonDecodableUnitsDegrade) {
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

TEST(CeceCadenceIndexing, DecodeYearAlignRemap) {
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

TEST(CeceCadenceIndexing, DecodeTaxmode) {
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
TEST(CeceCadenceIndexing, BracketTimesDirect) {
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

TEST(CeceCadenceIndexing, RejectsUnorderedAxis) {
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

TEST(CeceCadenceIndexing, DecodeRejectsDegenerateAxisSpan) {
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

namespace {

// 48 hourly records covering 2000-01-01T00 .. 2000-01-02T23.
std::vector<double> two_day_hourly_axis() {
    std::vector<double> raw(48);
    for (int k = 0; k < 48; ++k) raw[k] = static_cast<double>(k);
    return raw;
}

constexpr const char* kTwoDayHourlyUnits = "hours since 2000-01-01 00:00:00";

}  // namespace

TEST(CeceCadenceIndexing, SeriesHourlyTwoDayFileWalksAllRecords) {
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

TEST(CeceCadenceIndexing, SeriesHourlyDiffersFromHourlyProfile) {
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

TEST(CeceCadenceIndexing, SeriesHourlyRunOutlastsFile) {
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

TEST(CeceCadenceIndexing, ParseKeepsMinutesAndSeconds) {
    using namespace cece::detail;

    const SimDateTime dt = parse_sim_datetime("2000-01-01T00:40:30");
    ASSERT_TRUE(dt.valid);
    EXPECT_EQ(dt.hour, 0);
    EXPECT_EQ(dt.minute, 40);
    EXPECT_EQ(dt.second, 30);
}

TEST(CeceCadenceIndexing, DecodeSubHourlySimTimeOnHourlyAxis) {
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

TEST(CeceCadenceIndexing, DecodeQuarterHourlyAxis) {
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

TEST(CeceCadenceIndexing, SubHourlyRefinesArithmeticFraction) {
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

TEST(CeceCadenceIndexing, DecodeRejectsIntegerAxisMisreadAsFloat) {
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

}  // namespace cece
