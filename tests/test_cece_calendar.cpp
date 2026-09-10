/**
 * @file test_cece_calendar.cpp
 * @brief Tests for CF time-units decoding (cece/cece_calendar.hpp).
 *
 * The runtime calendar dispatchers are covered indirectly by the decode tests
 * in test_cece_time_indexing.cpp, which drive them through bracket_from_coords
 * for gregorian, noleap and 360_day axes.
 */

#include <gtest/gtest.h>

#include <tick/tick.hpp>

#include "cece/cece_calendar.hpp"

namespace cece {

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

TEST(CeceCfUnits, ParseHandlesZeroOffsetSuffixes) {
    using namespace cece::detail;

    // Fractional seconds and the various spellings of "UTC" are all common in
    // real files, and all leave the reference unshifted.
    for (const char* units : {"hours since 1900-01-01 00:00:00.0", "seconds since 1900-01-01 00:00:00 UTC", "days since 1900-01-01T00:00:00Z",
                              "days since 1900-01-01 00:00:00 utc", "days since 1900-01-01 00:00:00 GMT", "days since 1900-01-01 00:00:00+00:00"}) {
        const CFTimeUnits u = parse_cf_units(units);
        EXPECT_TRUE(u.valid) << units;
        EXPECT_EQ(u.reference, (tick::Date_Time{1900, 1, 1, 0, 0, 0, 0})) << units;
        EXPECT_NEAR(u.offset_days, 0.0, 1e-12) << units;
    }
}

TEST(CeceCfUnits, ParseCapturesNonZeroUtcOffsets) {
    using namespace cece::detail;

    // A non-zero offset must not be discarded: the reference is given in that
    // zone, so ignoring it shifts every selected record.
    EXPECT_NEAR(parse_cf_units("hours since 2000-01-01 00:00:00 -06:00").offset_days, -0.25, 1e-12);
    EXPECT_NEAR(parse_cf_units("hours since 2000-01-01 00:00:00 -0600").offset_days, -0.25, 1e-12);
    EXPECT_NEAR(parse_cf_units("hours since 2000-01-01 00:00:00 -06").offset_days, -0.25, 1e-12);
    EXPECT_NEAR(parse_cf_units("hours since 2000-01-01 00:00:00 +05:30").offset_days, 5.5 / 24.0, 1e-12);
    EXPECT_NEAR(parse_cf_units("hours since 2000-01-01T00:00:00.000 -06:00").offset_days, -0.25, 1e-12);

    // The reference itself is reported as written, not pre-shifted.
    const CFTimeUnits cf = parse_cf_units("hours since 2000-01-01 00:00:00 -06:00");
    EXPECT_TRUE(cf.valid);
    EXPECT_EQ(cf.reference, (tick::Date_Time{2000, 1, 1, 0, 0, 0, 0}));

    // A malformed offset makes the units undecodable rather than silently UTC.
    EXPECT_FALSE(parse_cf_units("hours since 2000-01-01 00:00:00 +").valid);
    EXPECT_FALSE(parse_cf_units("hours since 2000-01-01 00:00:00 -6:00").valid);
    EXPECT_FALSE(parse_cf_units("hours since 2000-01-01 00:00:00 +25:00").valid);
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

TEST(CeceCfUnits, ParseKeepsFractionalSeconds) {
    using namespace cece::detail;

    // Discarding the fraction shifts every record by up to a second.
    EXPECT_EQ(parse_cf_units("seconds since 2000-01-01 00:00:00.5").reference.nanosecond, 500000000);
    EXPECT_EQ(parse_cf_units("seconds since 2000-01-01 00:00:00.25").reference.nanosecond, 250000000);
    EXPECT_EQ(parse_cf_units("seconds since 2000-01-01 00:00:00.000000001").reference.nanosecond, 1);

    // Digits below nanosecond resolution are dropped, not rolled over.
    EXPECT_EQ(parse_cf_units("seconds since 2000-01-01 00:00:00.0000000009").reference.nanosecond, 0);

    // The fraction survives alongside a zone suffix.
    const CFTimeUnits offset = parse_cf_units("seconds since 2000-01-01 00:00:00.5 -06:00");
    ASSERT_TRUE(offset.valid);
    EXPECT_EQ(offset.reference.nanosecond, 500000000);
    EXPECT_NEAR(offset.offset_days, -0.25, 1e-12);

    // A bare decimal point is malformed.
    EXPECT_FALSE(parse_cf_units("seconds since 2000-01-01 00:00:00.").valid);
}

TEST(CeceCfUnits, ParseRejectsMalformedReference) {
    using namespace cece::detail;

    // A time component that starts must finish; accepting a partial parse
    // silently decoded this as 12:00.
    EXPECT_FALSE(parse_cf_units("hours since 2000-01-01T12:bogus").valid);
    EXPECT_FALSE(parse_cf_units("hours since 2000-01-01T12:30:bogus").valid);

    // Trailing text is not a zone suffix, so the reference was not understood.
    EXPECT_FALSE(parse_cf_units("days since 2000-01-01 nonsense").valid);
    EXPECT_FALSE(parse_cf_units("days since 2000-01-01 12:30:45 extra").valid);
    EXPECT_FALSE(parse_cf_units("days since 2000-01-01T00:00:00Z junk").valid);

    // Out-of-range time components.
    EXPECT_FALSE(parse_cf_units("hours since 2000-01-01 25:00:00").valid);
    EXPECT_FALSE(parse_cf_units("hours since 2000-01-01 00:70:00").valid);

    // Trailing whitespace is not junk.
    EXPECT_TRUE(parse_cf_units("days since 2000-01-01 12:30:45  ").valid);
}

TEST(CeceCalendarKind, MapsSupportedNamesAndRejectsTheRest) {
    using namespace cece::detail;

    // An absent attribute means CF's default, "standard".
    EXPECT_EQ(parse_calendar(""), CalKind::Gregorian);
    EXPECT_EQ(parse_calendar("gregorian"), CalKind::Gregorian);
    EXPECT_EQ(parse_calendar("standard"), CalKind::Gregorian);
    EXPECT_EQ(parse_calendar("proleptic_gregorian"), CalKind::Gregorian);
    EXPECT_EQ(parse_calendar("Gregorian"), CalKind::Gregorian);

    EXPECT_EQ(parse_calendar("noleap"), CalKind::NoLeap);
    EXPECT_EQ(parse_calendar("no_leap"), CalKind::NoLeap);
    EXPECT_EQ(parse_calendar("365_day"), CalKind::NoLeap);
    EXPECT_EQ(parse_calendar("365day"), CalKind::NoLeap);

    EXPECT_EQ(parse_calendar("360_day"), CalKind::Cal360);
    EXPECT_EQ(parse_calendar("360day"), CalKind::Cal360);

    // Real CF calendars TICK has no engine for. Decoding them as Gregorian
    // would be silently wrong -- julian is 13 days off in the modern era.
    EXPECT_EQ(parse_calendar("julian"), CalKind::Unsupported);
    EXPECT_EQ(parse_calendar("all_leap"), CalKind::Unsupported);
    EXPECT_EQ(parse_calendar("366_day"), CalKind::Unsupported);
    EXPECT_EQ(parse_calendar("not_a_calendar"), CalKind::Unsupported);
}

}  // namespace cece
