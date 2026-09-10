#include "cece/cece_time_indexing.hpp"

#include <amio/amio.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <tick/tick.hpp>
#include <vector>

#include "cece/cece_amio_utils.hpp"
#include "cece/cece_calendar.hpp"
#include "cece/cece_logger.hpp"
#include "cece/cece_string_utils.hpp"

namespace cece {
namespace detail {

/**
 * @brief Parse an ISO-8601 timestamp ("YYYY-MM-DDThh:mm:ss") into calendar fields.
 *
 * Parsing and calendar arithmetic use the HELM TICK library (tick::parse_iso8601
 * and tick::Gregorian_Calendar). The day-of-week uses ISO 8601 numbering
 * (1=Monday ... 7=Sunday).
 */
SimDateTime parse_sim_datetime(const std::string& iso8601) {
    SimDateTime dt;
    try {
        const tick::Date_Time tdt = tick::parse_iso8601(iso8601);
        dt.year = tdt.year;
        dt.month = tdt.month;
        dt.day = tdt.day;
        dt.hour = tdt.hour;
        dt.minute = tdt.minute;
        dt.second = tdt.second;
        dt.day_of_week = tick::Gregorian_Calendar::day_of_week(tdt);
        dt.day_of_year = tick::Gregorian_Calendar::day_of_year(tdt);
        dt.valid = true;
    } catch (const std::exception&) {
        // Malformed timestamp: use explicit default values so callers report an
        // invalid bracket rather than silently picking a record.
        dt = SimDateTime{};
    }
    return dt;
}

/// Position within the calendar day, in [0, 1), at sub-hour resolution.
static double day_fraction(const SimDateTime& dt) {
    return (dt.hour + dt.minute / 60.0 + dt.second / 3600.0) / 24.0;
}

void validate_stream_temporal_config(const std::string& cadence, const std::string& taxmode, const std::string& tintalgo, int yearFirst, int yearLast,
                                     int yearAlign, const std::string& where) {
    const std::string cl = to_lower(cadence);
    const bool is_profile = (cl == "hourly" || cl == "weekly");
    const bool is_stepwise = (cl == "stepwise" || cl == "step");
    const bool is_series = (cl.empty() || cl == "series" || cl == "daily" || cl == "monthly");
    if (!is_profile && !is_stepwise && !is_series) {
        throw std::invalid_argument("Unknown stream cadence '" + cadence + "' (expected series|daily|monthly|hourly|weekly|stepwise)" + where + ".");
    }

    const std::string xl = to_lower(taxmode);
    if (!xl.empty() && xl != "cycle" && xl != "extend" && xl != "limit") {
        throw std::invalid_argument("Unknown stream taxmode '" + taxmode + "' (expected cycle|extend|limit)" + where + ".");
    }

    const std::string tl = to_lower(tintalgo);
    if (!tl.empty() && tl != "linear" && tl != "nearest") {
        throw std::invalid_argument("Unknown stream tintalgo '" + tintalgo + "' (expected linear|nearest)" + where + ".");
    }

    // An inverted range would make the cycle span zero years.
    if (yearFirst != 0 && yearLast != 0 && yearLast < yearFirst) {
        throw std::invalid_argument("Stream yearLast (" + std::to_string(yearLast) + ") is before yearFirst (" + std::to_string(yearFirst) + ")" +
                                    where + ".");
    }

    if (is_profile && (yearAlign != 0 || yearFirst != 0 || yearLast != 0 || !taxmode.empty() || tl == "linear")) {
        CECE_LOG_WARNING("[DRIVER] taxmode/yearAlign/yearFirst/yearLast/tintalgo are ignored for profile cadence '" + cl + "'" + where + ".");
    }
    if (is_stepwise && (yearAlign != 0 || !taxmode.empty() || tl == "linear")) {
        CECE_LOG_WARNING("[DRIVER] taxmode/yearAlign/tintalgo are ignored for stepwise cadence" + where + " (the time axis is not consulted).");
    }
    if (is_series && (yearFirst != 0 || yearLast != 0)) {
        CECE_LOG_WARNING("[DRIVER] yearFirst/yearLast are only consulted if the time axis cannot be decoded" + where + ".");
    }
}

CadenceKind classify_cadence(const std::string& cadence) {
    const std::string c = to_lower(cadence);
    if (c == "hourly" || c == "weekly") return CadenceKind::Profile;
    if (c == "stepwise" || c == "step") return CadenceKind::Stepwise;
    // "", "series", "daily", "monthly", and unknown -> Series (time-aware default).
    return CadenceKind::Series;
}

// What a mid-point bracket does when a neighbour falls off either end of the
// file. A climatology always wraps; a multi-year file follows taxmode, so that
// the arithmetic fallback agrees with the decoded-axis path.
enum class EdgePolicy { Wrap, Hold, Reject };

// Mid-point cyclic bracket shared by the daily (mid-day) and monthly
// (mid-month) linear paths. `frac` is the fractional position through record
// `idx`'s interval.
static RecordBracket midpoint_bracket(int idx, double frac, int nrec, EdgePolicy edge) {
    RecordBracket br;
    int lo, hi;
    double weight;
    if (frac >= 0.5) {
        lo = idx;
        hi = idx + 1;
        weight = frac - 0.5;
    } else {
        lo = idx - 1;
        hi = idx;
        weight = frac + 0.5;
    }

    if (lo < 0 || hi >= nrec) {
        if (edge == EdgePolicy::Hold) {
            br.i0 = br.i1 = (lo < 0) ? 0 : nrec - 1;
            br.weight = 0.0;
            br.valid = true;
            return br;
        }
        if (edge == EdgePolicy::Reject) {
            br.out_of_range = true;
            return br;
        }
    }

    br.i0 = ((lo % nrec) + nrec) % nrec;
    br.i1 = ((hi % nrec) + nrec) % nrec;
    br.weight = weight;
    br.valid = true;
    return br;
}

// How a simulation year outside [yearFirst, yLast] was resolved.
enum class YearMapping { Resolved, ClampFirst, ClampLast, Reject };

// Map `eff_year` into [yearFirst, yLast] per taxmode. Reject covers an
// out-of-range year under "limit" (which also sets @p out_of_range) and an
// inverted range (which would otherwise make the cycle span zero years).
static YearMapping apply_year_taxmode(int& eff_year, int yearFirst, int yLast, const std::string& tax, bool& out_of_range) {
    if (eff_year >= yearFirst && eff_year <= yLast) return YearMapping::Resolved;

    const int year_span = yLast - yearFirst + 1;
    if (year_span <= 0) return YearMapping::Reject;
    if (tax == "limit") {
        out_of_range = true;
        return YearMapping::Reject;
    }
    if (tax == "extend") {
        // "extend" holds the first/last record, as the decoded-axis path does.
        // Clamping only the year would keep the month and land mid-file: August
        // 2026 against a 2000-2023 file would give August 2023, not December.
        return (eff_year < yearFirst) ? YearMapping::ClampFirst : YearMapping::ClampLast;
    }
    int offset = (eff_year - yearFirst) % year_span;  // default: cycle
    if (offset < 0) offset += year_span;
    eff_year = yearFirst + offset;
    return YearMapping::Resolved;
}

/**
 * @brief Map a simulation datetime onto a record bracket for a given cadence.
 *
 * @param cadence    One of "hourly", "daily", "weekly", "monthly" (case-insensitive).
 * @param tintalgo   Time-interpolation algorithm. "linear" interpolates between
 *                   the two bracketing records for monthly (mid-month
 *                   convention) and daily (mid-day convention) cadences;
 *                   any other @c tintalgo value selects the nearest record.
 *                   Hourly and weekly cadences ignore @c tintalgo
 *                   and always use nearest-neighbour
 *                   (there is no true sub-hourly interpolation).
 * @param dt         Parsed simulation datetime.
 * @param file_nt    Number of records available in the file (for clamping).
 * @param yearFirst  First year covered by the file (0 = unknown/climatology).
 * @param yearLast   Last year covered by the file (0 = unknown/climatology).
 * @param yearAlign  Year the simulation time aligns to within the file range.
 *                   When yearAlign != 0, the effective sim year is remapped:
 *                   effective_year = yearFirst + (sim_year - yearAlign) mapped
 *                   into [yearFirst, yearLast] per taxmode.
 * @param taxmode    "cycle" (default): wrap sim year into file range.
 *                   "extend": hold the file's first/last record.
 *                   "limit": return invalid bracket if outside range.
 *
 * For monthly cadence with multi-year files (file_nt > 12), the record index
 * is computed as: (effective_year - yearFirst) * 12 + (month - 1).
 * For daily cadence with multi-year files (file_nt > 366), the record index
 * is computed from cumulative day offsets across years plus (day_of_year - 1).
 * For single-year daily files the day-of-year is normalised against the file's
 * record count so a record keeps meaning the same calendar day in both leap and
 * non-leap simulation years: a 365-record file maps Feb 29 onto the Feb 28
 * record and shifts subsequent dates back one, and a 366-record file skips its
 * Feb 29 record in non-leap years.
 *
 * Hourly and weekly cadences select discrete profile records (hour-of-day,
 * day-of-week) and always use nearest-neighbour. Monthly and daily cadences
 * honor @c tintalgo for linear temporal interpolation.
 */
RecordBracket bracket_from_cadence(const std::string& cadence, const std::string& tintalgo, const SimDateTime& dt, int file_nt, int yearFirst,
                                   int yearLast, int yearAlign, const std::string& taxmode) {
    RecordBracket br;
    if (cadence.empty() || !dt.valid) return br;

    const std::string c = to_lower(cadence);
    const std::string tax = to_lower(taxmode);
    const bool linear = (to_lower(tintalgo) == "linear");

    auto clamp_idx = [&](int idx) {
        if (file_nt > 0 && idx >= file_nt) idx = file_nt - 1;
        if (idx < 0) idx = 0;
        return idx;
    };

    if (c == "hourly") {
        br.i0 = br.i1 = clamp_idx(dt.hour);
        br.valid = true;
    } else if (c == "daily") {
        const bool multi_year = (yearFirst > 0 && file_nt > 366);
        int eff_year = dt.year;

        if (multi_year) {
            if (yearAlign > 0) {
                eff_year = yearFirst + (dt.year - yearAlign);
            }

            int yLast = yearLast;
            if (yLast <= 0) {
                yLast = yearFirst + std::max(1, file_nt / 365) - 1;
            }

            const YearMapping mapping = apply_year_taxmode(eff_year, yearFirst, yLast, tax, br.out_of_range);
            if (mapping == YearMapping::Reject) return br;
            if (mapping != YearMapping::Resolved) {
                br.i0 = br.i1 = (mapping == YearMapping::ClampFirst) ? 0 : std::max(0, file_nt - 1);
                br.valid = true;
                return br;
            }
        }

        int abs_day;
        if (multi_year) {
            int days_offset = 0;
            for (int y = yearFirst; y < eff_year; ++y) {
                days_offset += tick::Gregorian_Calendar::days_in_year(y);
            }
            // Day-of-year must come from the effective year: remapping a leap
            // simulation year onto a non-leap file year (or vice versa) shifts
            // every date after February otherwise. Feb 29 maps onto Feb 28.
            int eff_day = dt.day;
            if (dt.month == 2 && dt.day == 29 && !tick::Gregorian_Calendar::is_leap_year(eff_year)) {
                eff_day = 28;
            }
            const int eff_doy = tick::Gregorian_Calendar::day_of_year(tick::Date_Time{eff_year, dt.month, eff_day, 0, 0, 0, 0});
            abs_day = days_offset + (eff_doy - 1);
        } else {
            abs_day = dt.day_of_year - 1;  // 0-364 or 0-365 for single-year / climatology files
        }

        const int nrec = (file_nt > 0) ? file_nt : 365;

        // Reconcile the simulation calendar with a fixed-length climatology so
        // that a record keeps meaning the same calendar day either side of the
        // leap day. Day-of-year 60 is Feb 29 in a leap year and Mar 1 otherwise.
        //   365-record file, leap sim year  -> Feb 29 reuses the Feb 28 record
        //                                      and later dates shift back one.
        //   366-record file, non-leap year  -> later dates skip the Feb 29 record.
        if (!multi_year && dt.day_of_year >= 60) {
            const bool leap = tick::Gregorian_Calendar::is_leap_year(dt.year);
            if (nrec == 365 && leap) {
                abs_day -= 1;
            } else if (nrec == 366 && !leap) {
                abs_day += 1;
            }
        }

        if (!linear) {
            br.i0 = br.i1 = clamp_idx(abs_day);
            br.valid = true;
            return br;
        }

        const double frac = day_fraction(dt);

        const EdgePolicy edge = !multi_year         ? EdgePolicy::Wrap
                                : (tax == "extend") ? EdgePolicy::Hold
                                : (tax == "limit")  ? EdgePolicy::Reject
                                                    : EdgePolicy::Wrap;
        br = midpoint_bracket(abs_day, frac, nrec, edge);
    } else if (c == "weekly") {
        // dt.day_of_week is ISO 8601 (1=Monday ... 7=Sunday).
        // Weekly profile records are 0-indexed (0=Monday ... 6=Sunday).
        br.i0 = br.i1 = clamp_idx(dt.day_of_week - 1);
        br.valid = true;
    } else if (c == "monthly") {
        // Determine effective year for multi-year files.
        // If yearFirst is set and file has more than 12 records, compute
        // the absolute month index within the file.
        const bool multi_year = (yearFirst > 0 && file_nt > 12);
        int eff_year = dt.year;

        if (multi_year) {
            // If yearAlign is specified, remap simulation year into file range.
            // yearAlign means: simulation year `yearAlign` corresponds to file
            // year `yearFirst`. So offset = sim_year - yearAlign + yearFirst.
            if (yearAlign > 0) {
                eff_year = yearFirst + (dt.year - yearAlign);
            }

            // Determine yearLast from file if not explicitly provided.
            int yLast = yearLast;
            if (yLast <= 0) {
                yLast = yearFirst + (file_nt / 12) - 1;
            }

            const YearMapping mapping = apply_year_taxmode(eff_year, yearFirst, yLast, tax, br.out_of_range);
            if (mapping == YearMapping::Reject) return br;
            if (mapping != YearMapping::Resolved) {
                br.i0 = br.i1 = (mapping == YearMapping::ClampFirst) ? 0 : std::max(0, file_nt - 1);
                br.valid = true;
                return br;
            }
        }

        // Compute absolute month index within the file.
        int abs_month;
        if (multi_year) {
            abs_month = (eff_year - yearFirst) * 12 + (dt.month - 1);
        } else {
            abs_month = dt.month - 1;  // 0-11 for climatology files
        }

        if (!linear) {
            br.i0 = br.i1 = clamp_idx(abs_month);
            br.valid = true;
            return br;
        }

        // Mid-month linear interpolation convention. The month length comes
        // from the effective year: remapping February 2024 onto non-leap 2021
        // must divide by 28, not 29.
        const int dim = tick::Gregorian_Calendar::days_in_month(eff_year, dt.month);
        const int eff_day = std::min(dt.day, dim);
        const double frac = (static_cast<double>(eff_day - 1) + day_fraction(dt)) / static_cast<double>(dim);
        const int nrec = (file_nt > 0) ? file_nt : 12;

        const EdgePolicy edge = !multi_year         ? EdgePolicy::Wrap
                                : (tax == "extend") ? EdgePolicy::Hold
                                : (tax == "limit")  ? EdgePolicy::Reject
                                                    : EdgePolicy::Wrap;
        br = midpoint_bracket(abs_month, frac, nrec, edge);
    }
    return br;
}

/**
 * @brief Bracket a target time within a sorted array of record times.
 *
 * Shared generic core: binary-searches @p times for @p target, applies
 * @p taxmode ("cycle"/"extend"/"limit") to out-of-range targets, and returns
 * the nearest record or (when @p linear) the two bracketing records with a
 * blend weight. @p times and @p target must share the same units.
 *
 * "cycle" repeats the file with period @p period_days. Pass 0 to infer it as
 * span + the final record interval, which is exact for a uniformly sampled
 * axis: a 48-record hourly file then has period 48 h, so hour 53 resolves to
 * record 5. No fixed period is exact for year-aligned data, where a cycle is
 * 365 or 366 days -- bracket_from_coords() handles that by wrapping the
 * simulation year instead, and passes the resulting cycle length here so the
 * seam is right. A target landing in the trailing interval brackets the last
 * record against the first record of the next cycle.
 *
 * Returns an invalid bracket for an out-of-order @p times, which the binary
 * search cannot answer meaningfully.
 */
RecordBracket find_bracket(const std::vector<double>& times, double target, bool linear, const std::string& taxmode, double period_days) {
    RecordBracket br;
    const size_t n = times.size();
    if (n == 0) return br;
    if (n == 1) {
        br.i0 = br.i1 = 0;
        br.weight = 0.0;
        br.valid = true;
        return br;
    }

    // Nothing upstream guarantees the file's records are in ascending order.
    for (size_t k = 1; k < n; ++k) {
        if (times[k] < times[k - 1]) return br;
    }

    const std::string tax = to_lower(taxmode);

    const double file_start = times[0];
    const double file_end = times[n - 1];
    const double file_span = file_end - file_start;
    double period = period_days;
    if (period <= 0.0) {
        // The last interval stands in for the (unrecorded) step from the final
        // record back to the start of the next cycle.
        const double last_interval = times[n - 1] - times[n - 2];
        period = (last_interval > 0.0) ? file_span + last_interval : file_span;
    }
    bool in_wrap_gap = false;

    if (target < file_start || target > file_end) {
        if (tax == "limit") {
            br.out_of_range = true;  // decodable axis, deliberate rejection
            return br;
        } else if (tax == "extend") {
            target = std::max(file_start, std::min(target, file_end));
        } else {
            // cycle
            if (period > 0.0) {
                double offset_from_start = std::fmod(target - file_start, period);
                if (offset_from_start < 0.0) offset_from_start += period;
                target = file_start + offset_from_start;
                in_wrap_gap = (target > file_end);
            }
        }
    }

    if (in_wrap_gap) {
        const double gap = period - file_span;
        double w = (gap > 0.0) ? (target - file_end) / gap : 0.0;
        w = std::max(0.0, std::min(1.0, w));
        if (linear) {
            br.i0 = static_cast<int>(n) - 1;
            br.i1 = 0;
            br.weight = w;
        } else {
            br.i0 = br.i1 = (w < 0.5) ? static_cast<int>(n) - 1 : 0;
            br.weight = 0.0;
        }
        br.valid = true;
        return br;
    }

    int lo = 0, hi = static_cast<int>(n) - 1;
    while (lo < hi - 1) {
        const int mid = (lo + hi) / 2;
        if (times[mid] <= target) {
            lo = mid;
        } else {
            hi = mid;
        }
    }

    if (linear && lo != hi) {
        const double span = times[hi] - times[lo];
        double w = (span > 0.0) ? (target - times[lo]) / span : 0.0;
        w = std::max(0.0, std::min(1.0, w));
        br.i0 = lo;
        br.i1 = hi;
        br.weight = w;
    } else {
        if (std::abs(times[hi] - target) < std::abs(times[lo] - target)) {
            br.i0 = br.i1 = hi;
        } else {
            br.i0 = br.i1 = lo;
        }
        br.weight = 0.0;
    }
    br.valid = true;
    return br;
}

/**
 * @brief Decode a CF time axis and bracket the simulation time against it.
 *
 * Parses @p units ("<fixed-unit> since <ref>") and @p calendar, converts each
 * raw record value to an absolute time (as days since the file's reference),
 * maps the simulation datetime into the same frame (with an optional
 * @p yearAlign remap onto the file's first record year), and delegates to
 * find_bracket(). Returns an invalid bracket when the axis is not decodable
 * (non-fixed unit, missing/garbled units, an out-of-range calendar date, or a
 * multi-record axis that spans less than a second) so the caller falls back to
 * the arithmetic bracket_from_cadence().
 *
 * For taxmode "cycle" on an axis covering a whole number of calendar years,
 * the simulation *year* is wrapped into the file's coverage. Wrapping the
 * instant by a fixed period instead drifts a day per leap year and keeps
 * accumulating. Coverage is inferred from the decoded records; the CF-exact
 * source would be the time variable's `bounds` (time_bnds), whose first and
 * last cell edges delimit the cycle directly, but AMIO does not surface bounds
 * variables yet.
 */
RecordBracket bracket_from_coords(const std::vector<double>& time_vals, const std::string& units, const std::string& calendar, const SimDateTime& dt,
                                  const std::string& tintalgo, int yearAlign, const std::string& taxmode) {
    RecordBracket br;
    if (!dt.valid || time_vals.empty()) return br;

    try {
        const CFTimeUnits cf = parse_cf_units(units);
        if (!cf.valid) return br;  // not decodable -> degrade

        const CalKind cal = parse_calendar(calendar);
        if (cal == CalKind::Unsupported) {
            CECE_LOG_WARNING("[DRIVER] Unsupported stream calendar '" + calendar + "'; the time axis cannot be decoded.");
            return br;
        }
        // Normalise the reference to UTC. Shifting in the time-point domain is
        // calendar-agnostic, whereas adding hours to the reference date could
        // land on a date the file's calendar does not have (e.g. Jan 31 under
        // 360_day).
        const std::int64_t ref_nanos =
            cal_to_nanos(cal, cf.reference) - static_cast<std::int64_t>(std::llround(cf.offset_days * static_cast<double>(tick::nanos_per_day)));

        // Record times as days since the file's reference epoch.
        std::vector<double> rec_days(time_vals.size());
        for (size_t k = 0; k < time_vals.size(); ++k) {
            rec_days[k] = time_vals[k] * cf.unit_days;
        }

        // Records must be distinguishable at TICK's nanosecond resolution. An
        // integer time variable read as floating point fails this: the
        // reinterpreted bit patterns stay ordered but collapse to denormals, so
        // the whole axis spans ~1e-29 ns. Anything coarser than a nanosecond is
        // a real axis, including legitimately sub-second ones.
        const double span_days = rec_days.back() - rec_days.front();
        if (rec_days.size() > 1 && span_days * static_cast<double>(tick::nanos_per_day) < 1.0) {
            return br;
        }

        auto abs_nanos = [&](double d) { return ref_nanos + static_cast<std::int64_t>(std::llround(d * static_cast<double>(tick::nanos_per_day))); };
        const std::int64_t first_nanos = abs_nanos(rec_days.front());
        const int first_year = cal_to_dt(cal, first_nanos).year;

        // yearAlign is the simulation year that aligns to the file's first
        // record year. yearAlign == 0 keeps the sim year.
        int sim_year = dt.year;
        if (yearAlign > 0) {
            sim_year = dt.year + (first_year - yearAlign);
        }

        // Does the axis cover a whole number of calendar years? If so, cycling
        // repeats calendar years, which no fixed period can express because a
        // year is 365 or 366 days. Count the cycles from the span itself rather
        // than from the first and last year labels, which would call a
        // July-to-June file two years and never detect it. Then check that the
        // leftover between the last record and the same calendar point one
        // cycle on is no larger than the axis's own coarsest record spacing.
        double max_interval = 0.0;
        for (size_t k = 1; k < rec_days.size(); ++k) {
            max_interval = std::max(max_interval, rec_days[k] - rec_days[k - 1]);
        }
        const int span_years = std::max(1, static_cast<int>(std::llround(span_days / 365.25)));
        const double annual_days =
            static_cast<double>(cal_add_months(cal, first_nanos, span_years * 12) - first_nanos) / static_cast<double>(tick::nanos_per_day);
        const bool annual_cycle = (rec_days.size() > 1 && annual_days > span_days && (annual_days - span_days) <= 1.5 * max_interval);

        // Year remapping assumes the coverage starts on a January boundary. A
        // phase-shifted annual file (July-June) is cycled by find_bracket
        // instead, using the exact annual_days period computed above.
        const bool year_aligned = (cal_to_dt(cal, first_nanos).month == 1);

        // Wrap the simulation *year* into the file's coverage rather than
        // wrapping the instant: wrapping the instant by a fixed period drifts
        // a day per leap year and never stops accumulating.
        const std::string tax = to_lower(taxmode);
        if (annual_cycle && year_aligned && (tax.empty() || tax == "cycle")) {
            int offset = (sim_year - first_year) % span_years;
            if (offset < 0) offset += span_years;
            sim_year = first_year + offset;
        }

        // Feb 29 has no counterpart in a non-leap target year.
        const int sim_day = std::min(dt.day, cal_days_in_month(cal, sim_year, dt.month));
        const tick::Date_Time sim_dt{sim_year, dt.month, sim_day, dt.hour, dt.minute, dt.second, 0};
        const std::int64_t sim_nanos = cal_to_nanos(cal, sim_dt);
        const double target_days = static_cast<double>(sim_nanos - ref_nanos) / static_cast<double>(tick::nanos_per_day);

        const std::string talgo = to_lower(tintalgo);
        return find_bracket(rec_days, target_days, talgo == "linear", taxmode, annual_cycle ? annual_days : 0.0);
    } catch (const std::exception&) {
        return br;  // any calendar/parse error -> degrade
    }
}

/**
 * @brief Read the file's time axis via AMIO and resolve the record bracket.
 *
 * Thin I/O wrapper around bracket_from_coords(): reads the time
 * coordinate variable (trying common alternate names), then delegates the
 * numeric computation. Returns an invalid bracket (so the caller falls back to
 * bracket_from_cadence) on any read failure or when the view is sliced to
 * fewer than @c file_nt values.
 */
RecordBracket bracket_from_dataset(amio_dataset_handle dataset, const std::string& time_var, const SimDateTime& dt, int file_nt,
                                   const std::string& tintalgo, int yearAlign, const std::string& taxmode, const std::string& units_override,
                                   const std::string& calendar_override) {
    RecordBracket br;
    if (!dataset || !dt.valid || file_nt < 1) return br;

    // Read the time coordinate variable. It's typically a 1D array of
    // doubles representing offsets from a reference date.
    std::string tvar = time_var.empty() ? "time" : time_var;

    // Read the whole axis in one call: AMIO describes a variable whose only
    // dimension is the record dimension as that axis's coordinate variable
    // rather than a field sampled along it, so this returns all nt values.
    amio_view_handle view = nullptr;
    amio_status_t rc = amio_read(dataset, tvar.c_str(), 0, nullptr, &view);
    if (rc != AMIO_OK) {
        // Try alternate time variable names.
        const char* alt_names[] = {"Time", "t", "valid_time", nullptr};
        for (int i = 0; alt_names[i] != nullptr; ++i) {
            rc = amio_read(dataset, alt_names[i], 0, nullptr, &view);
            if (rc == AMIO_OK) {
                tvar = alt_names[i];
                break;
            }
        }
        if (rc != AMIO_OK) return br;
    }

    const void* view_data = nullptr;
    size_t view_size = 0;
    if (amio_view_data(view, &view_data, &view_size) != AMIO_OK) {
        amio_release_view(view);
        return br;
    }

    amio_shape_t shape{};
    if (amio_view_shape(view, &shape) != AMIO_OK) {
        amio_release_view(view);
        return br;
    }

    // Determine number of time values returned.
    size_t n_vals = 1;
    for (int d = 0; d < shape.rank; ++d) {
        n_vals *= static_cast<size_t>(shape.extents[d]);
    }

    if (static_cast<int>(n_vals) < file_nt) {
        // The view didn't return all time values — perhaps AMIO sliced it.
        // Fall back to arithmetic approach.
        amio_release_view(view);
        return br;
    }

    // Convert to double using the view's own element type. CF time coordinates
    // are commonly stored as integers, so the payload size alone cannot say how
    // to read them.
    amio_dtype_t dtype = AMIO_DTYPE_F64;
    if (amio_view_dtype(view, &dtype) != AMIO_OK) {
        amio_release_view(view);
        return br;
    }
    const std::size_t elem_size = amio_dtype_size(dtype);
    if (elem_size == 0 || view_size < n_vals * elem_size) {
        amio_release_view(view);
        return br;
    }

    // A packed time axis is unusual but legal.
    double time_scale = 1.0;
    double time_offset = 0.0;
    read_cf_packing(dataset, tvar, time_scale, time_offset);

    // file_nt comes from the data variable, not the time variable. A partially
    // written file can leave the axis longer than the records that exist, and
    // bracketing against the surplus would hand back indices that cannot be
    // read. Only the records the data variable actually has are usable.
    const std::size_t n_axis = std::min(n_vals, static_cast<std::size_t>(file_nt));

    std::vector<double> time_vals;
    const bool widened = widen_amio_elements(view_data, dtype, n_axis, time_scale, time_offset, time_vals);
    amio_release_view(view);
    if (!widened) return br;

    // Decode the axis using the file's own CF metadata. Missing/undecodable
    // units make bracket_from_coords return an invalid bracket, so
    // the caller degrades to the arithmetic bracket_from_cadence.
    auto read_text_attr = [&](const char* name) -> std::string {
        size_t len = 0;
        if (amio_get_var_attribute_text(dataset, tvar.c_str(), name, nullptr, 0, &len) != AMIO_OK) return std::string();
        std::string buf(len + 1, '\0');
        size_t got = 0;
        if (amio_get_var_attribute_text(dataset, tvar.c_str(), name, buf.data(), buf.size(), &got) != AMIO_OK) return std::string();
        buf.resize(got);
        return buf;
    };
    // Config overrides take precedence over the file's own attributes (for
    // files with missing or non-standard units/calendar).
    const std::string units = units_override.empty() ? read_text_attr("units") : units_override;
    const std::string calendar = calendar_override.empty() ? read_text_attr("calendar") : calendar_override;

    return bracket_from_coords(time_vals, units, calendar, dt, tintalgo, yearAlign, taxmode);
}

}  // namespace detail
}  // namespace cece
