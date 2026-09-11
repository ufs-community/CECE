#ifndef CECE_CALENDAR_HPP
#define CECE_CALENDAR_HPP

/**
 * @file cece_calendar.hpp
 * @brief Runtime calendar selection over TICK, and CF time-units decoding.
 *
 * TICK models calendars as a compile-time concept (@c tick::Calendar), which is
 * the right default for hot-path arithmetic but leaves no way to choose a
 * calendar from a string. Anything driven by a file's CF @c calendar attribute
 * or a config key needs runtime dispatch, so this is that dispatcher.
 *
 * Both halves of this file are candidates for TICK itself: the dispatcher as a
 * type-erased @c tick::Any_Calendar, and the units parser under a @c tick::cf
 * namespace, so that every downstream code does not rewrite them. Two things
 * block the move today:
 *
 *   1. TICK exposes day_of_year() and day_of_week() only on
 *      Gregorian_Calendar, not on NoLeap_Calendar or Cal360_Calendar, so a
 *      runtime facade cannot offer them uniformly. That gap also means
 *      SimDateTime's day_of_year/day_of_week are Gregorian regardless of the
 *      stream's calendar -- correct while the model clock is Gregorian, but it
 *      would need closing before a general facade could be complete.
 *   2. Whether CF, a NetCDF metadata convention, belongs in a chronology
 *      kernel at all is a call for HELM to make.
 */

#include <cstdint>
#include <string>
#include <tick/tick.hpp>

namespace cece {
namespace detail {

/// Calendar selected by the CF "calendar" attribute value.
/// @c Unsupported covers names CF defines but TICK has no engine for, such as
/// @c julian (13 days from Gregorian today) and @c all_leap. Decoding those as
/// Gregorian would silently produce wrong dates, so callers must treat the axis
/// as undecodable instead.
enum class CalKind { Gregorian, NoLeap, Cal360, Unsupported };

/// An empty attribute means CF's default, @c standard. A name we cannot honour
/// yields @c Unsupported rather than a wrong-but-plausible Gregorian date.
CalKind parse_calendar(const std::string& calendar);

std::int64_t cal_to_nanos(CalKind kind, const tick::Date_Time& dt);

tick::Date_Time cal_to_dt(CalKind kind, std::int64_t nanos);

std::int64_t cal_add_months(CalKind kind, std::int64_t nanos, int months);

int cal_days_in_month(CalKind kind, int year, int month);

/// Decoded CF "<unit> since <reference>" time-units string.
struct CFTimeUnits {
    double unit_days = 0.0;       ///< length of one axis unit, in days
    tick::Date_Time reference{};  ///< the "since" reference date-time, as written
    double offset_days = 0.0;     ///< UTC offset of @c reference (e.g. -0.25 for "-06:00"); subtract to get UTC
    bool valid = false;           ///< false when the units are missing or not decodable
};

CFTimeUnits parse_cf_units(const std::string& units);

}  // namespace detail
}  // namespace cece

#endif  // CECE_CALENDAR_HPP
