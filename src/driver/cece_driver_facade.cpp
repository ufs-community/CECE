#include "cece/cece_driver_facade.hpp"

#include <amio/amio.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <axis/axis.hpp>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <conf/conf.hpp>
#include <cstdint>
#include <cstdlib>
#include <dagr/logging.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <tick/tick.hpp>
#include <vector>

#include "cece/cece_fatal.hpp"
#include "cece/cece_helm_graph.hpp"
#include "cece/cece_internal.hpp"
#include "cece/cece_logger.hpp"
#include "cece/cece_regridder_utils.hpp"
#include "cece/cece_standalone_writer.hpp"

namespace fs = std::filesystem;

extern "C" {
void cece_ingestor_set_field(void* data_ptr, const char* field_name, int name_len, const double* field_data, int n_lev, int n_elem, int* rc);
void amio_set_parent_communicator(MPI_Fint comm);
}

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

std::size_t amio_dtype_size(amio_dtype_t dtype) {
    switch (dtype) {
        case AMIO_DTYPE_I8:
        case AMIO_DTYPE_U8:
            return 1;
        case AMIO_DTYPE_I16:
        case AMIO_DTYPE_U16:
            return 2;
        case AMIO_DTYPE_F32:
        case AMIO_DTYPE_I32:
        case AMIO_DTYPE_U32:
            return 4;
        case AMIO_DTYPE_F64:
        case AMIO_DTYPE_I64:
        case AMIO_DTYPE_U64:
            return 8;
        default:
            return 0;
    }
}

bool widen_amio_elements(const void* data, amio_dtype_t dtype, std::size_t n, double scale, double offset, std::vector<double>& out) {
    if (data == nullptr) return false;
    out.resize(n);

    auto convert = [&](auto tag) {
        using T = decltype(tag);
        const T* p = static_cast<const T*>(data);
        for (std::size_t k = 0; k < n; ++k) {
            out[k] = static_cast<double>(p[k]) * scale + offset;
        }
    };

    switch (dtype) {
        case AMIO_DTYPE_F32:
            convert(float{});
            return true;
        case AMIO_DTYPE_F64:
            convert(double{});
            return true;
        case AMIO_DTYPE_I8:
            convert(std::int8_t{});
            return true;
        case AMIO_DTYPE_I16:
            convert(std::int16_t{});
            return true;
        case AMIO_DTYPE_I32:
            convert(std::int32_t{});
            return true;
        case AMIO_DTYPE_I64:
            convert(std::int64_t{});
            return true;
        case AMIO_DTYPE_U8:
            convert(std::uint8_t{});
            return true;
        case AMIO_DTYPE_U16:
            convert(std::uint16_t{});
            return true;
        case AMIO_DTYPE_U32:
            convert(std::uint32_t{});
            return true;
        case AMIO_DTYPE_U64:
            convert(std::uint64_t{});
            return true;
        default:
            out.clear();
            return false;
    }
}

/// CF packing attributes; absent attributes leave the identity transform.
static void read_cf_packing(amio_dataset_handle dataset, const std::string& var, double& scale, double& offset) {
    scale = 1.0;
    offset = 0.0;
    double value = 0.0;
    if (amio_get_var_attribute_double(dataset, var.c_str(), "scale_factor", &value) == AMIO_OK) scale = value;
    if (amio_get_var_attribute_double(dataset, var.c_str(), "add_offset", &value) == AMIO_OK) offset = value;
}

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return s;
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

// Calendar selection for the CF "calendar" attribute value.
enum class CalKind { Gregorian, NoLeap, Cal360 };

static CalKind parse_calendar(const std::string& calendar) {
    std::string c = calendar;
    std::transform(c.begin(), c.end(), c.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (c == "noleap" || c == "no_leap" || c == "365_day" || c == "365day") return CalKind::NoLeap;
    if (c == "360_day" || c == "360day") return CalKind::Cal360;
    // "", "gregorian", "standard", "proleptic_gregorian", "julian" (approx), unknown.
    return CalKind::Gregorian;
}

static std::int64_t cal_to_nanos(CalKind kind, const tick::Date_Time& dt) {
    switch (kind) {
        case CalKind::NoLeap:
            return tick::NoLeap_Calendar::to_time_point(dt).nanos();
        case CalKind::Cal360:
            return tick::Cal360_Calendar::to_time_point(dt).nanos();
        case CalKind::Gregorian:
        default:
            return tick::Gregorian_Calendar::to_time_point(dt).nanos();
    }
}

static tick::Date_Time cal_to_dt(CalKind kind, std::int64_t nanos) {
    const tick::Time_Point tp{nanos};
    switch (kind) {
        case CalKind::NoLeap:
            return tick::NoLeap_Calendar::to_date_time(tp);
        case CalKind::Cal360:
            return tick::Cal360_Calendar::to_date_time(tp);
        case CalKind::Gregorian:
        default:
            return tick::Gregorian_Calendar::to_date_time(tp);
    }
}

static std::int64_t cal_add_months(CalKind kind, std::int64_t nanos, int months) {
    const tick::Time_Point tp{nanos};
    switch (kind) {
        case CalKind::NoLeap:
            return tick::NoLeap_Calendar::add_months(tp, months).nanos();
        case CalKind::Cal360:
            return tick::Cal360_Calendar::add_months(tp, months).nanos();
        case CalKind::Gregorian:
        default:
            return tick::Gregorian_Calendar::add_months(tp, months).nanos();
    }
}

static int cal_days_in_month(CalKind kind, int year, int month) {
    switch (kind) {
        case CalKind::NoLeap:
            return tick::NoLeap_Calendar::days_in_month(year, month);
        case CalKind::Cal360:
            return tick::Cal360_Calendar::days_in_month(year, month);
        case CalKind::Gregorian:
        default:
            return tick::Gregorian_Calendar::days_in_month(year, month);
    }
}

/**
 * @brief Parse a CF time-units string "<unit> since <reference>".
 *
 * Only fixed-length units (seconds/minutes/hours/days) are decodable; months
 * and years are calendar-ambiguous and yield an invalid result. The reference
 * parser is lenient: "YYYY-M-D", optional " [T]h[:m[:s]]", optional fractional
 * seconds, and an optional UTC offset ("Z", "UTC", "+HH:MM", "-HHMM", "+HH").
 * The offset is reported as @c offset_days rather than folded into
 * @c reference because normalising to UTC can cross a day boundary, and the
 * file's calendar is not known here. Callers subtract it once they have one.
 * A malformed offset makes the units undecodable rather than silently decoding
 * the wrong epoch.
 */
CFTimeUnits parse_cf_units(const std::string& units) {
    auto trim = [](std::string& s) {
        const auto b = s.find_first_not_of(" \t");
        const auto e = s.find_last_not_of(" \t");
        s = (b == std::string::npos) ? std::string() : s.substr(b, e - b + 1);
    };

    std::string lower = units;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    const std::string key = " since ";
    const auto pos = lower.find(key);
    if (pos == std::string::npos) return {};

    std::string unit = lower.substr(0, pos);
    trim(unit);
    double unit_days = 0.0;
    if (unit == "s" || unit == "sec" || unit == "secs" || unit == "second" || unit == "seconds") {
        unit_days = 1.0 / 86400.0;
    } else if (unit == "min" || unit == "mins" || unit == "minute" || unit == "minutes") {
        unit_days = 1.0 / 1440.0;
    } else if (unit == "h" || unit == "hr" || unit == "hrs" || unit == "hour" || unit == "hours") {
        unit_days = 1.0 / 24.0;
    } else if (unit == "d" || unit == "day" || unit == "days") {
        unit_days = 1.0;
    } else {
        return {};  // months / years / unknown -> not decodable
    }

    std::string ref = units.substr(pos + key.size());
    trim(ref);
    auto parse_int = [](const char*& p, int& value) -> bool {
        errno = 0;
        char* end = nullptr;
        const long v = std::strtol(p, &end, 10);
        if (end == p || errno == ERANGE || v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max()) return false;
        value = static_cast<int>(v);
        p = end;
        return true;
    };
    auto consume = [](const char*& p, char expected) -> bool {
        if (*p != expected) return false;
        p += 1;
        return true;
    };
    auto skip_ws = [](const char*& p) {
        while (*p == ' ' || *p == '\t') ++p;
    };
    auto is_digit = [](char ch) { return ch >= '0' && ch <= '9'; };
    auto read_digits = [&](const char*& p, int count, int& value) -> bool {
        value = 0;
        for (int i = 0; i < count; ++i) {
            if (!is_digit(*p)) return false;
            value = value * 10 + (*p - '0');
            ++p;
        }
        return true;
    };

    int y = 0, mo = 1, d = 1, h = 0, mi = 0, s = 0;
    const char* p = ref.c_str();
    if (!parse_int(p, y) || !consume(p, '-')) return {};
    if (!parse_int(p, mo) || !consume(p, '-')) return {};
    if (!parse_int(p, d)) return {};

    // Optional time part, separated by 'T' or whitespace. Consume the separator
    // explicitly rather than rewriting it, so a trailing "UTC" stays intact.
    skip_ws(p);
    if (*p == 'T' || *p == 't') ++p;
    skip_ws(p);
    if (is_digit(*p) && parse_int(p, h) && consume(p, ':')) {
        if (parse_int(p, mi) && consume(p, ':')) {
            parse_int(p, s);
        }
    }
    if (*p == '.') {  // fractional seconds
        ++p;
        while (is_digit(*p)) ++p;
    }

    // Optional UTC offset. CF gives the reference in that zone, so a record at
    // offset 0 is `offset` later in UTC.
    double offset_days = 0.0;
    skip_ws(p);
    if (*p == 'Z' || *p == 'z') {
        ++p;
    } else if (*p == '+' || *p == '-') {
        const int sign = (*p == '-') ? -1 : 1;
        ++p;
        int oh = 0;
        int om = 0;
        if (!read_digits(p, 2, oh)) return {};
        if (*p == ':') ++p;
        if (is_digit(*p) && !read_digits(p, 2, om)) return {};
        if (oh > 23 || om > 59) return {};
        offset_days = sign * (oh * 60 + om) / 1440.0;
    }

    if (mo < 1 || mo > 12 || d < 1 || d > 31) return {};
    return CFTimeUnits{unit_days, tick::Date_Time{y, mo, d, h, mi, s, 0}, offset_days, true};
}

// Cadence dispatch: how a stream's records are addressed.
//   Series   - absolute time axis (decode), degrading to arithmetic for daily/monthly
//   Profile  - climatological profile indexed by a calendar field (hourly/weekly)
//   Stepwise - opt-in legacy step-index cycling (ignores time)
enum class CadenceKind { Series, Profile, Stepwise };

static CadenceKind classify_cadence(const std::string& cadence) {
    std::string c = cadence;
    std::transform(c.begin(), c.end(), c.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (c == "hourly" || c == "weekly") return CadenceKind::Profile;
    if (c == "stepwise" || c == "step") return CadenceKind::Stepwise;
    // "", "series", "daily", "monthly", and unknown -> Series (time-aware default).
    return CadenceKind::Series;
}

// Mid-point cyclic bracket shared by the daily (mid-day) and monthly
// (mid-month) linear paths. `frac` is the fractional position through record
// `idx`'s interval; records wrap modulo `nrec` (climatology cycle).
static RecordBracket midpoint_bracket(int idx, double frac, int nrec) {
    RecordBracket br;
    if (frac >= 0.5) {
        br.i0 = idx % nrec;
        br.i1 = (idx + 1) % nrec;
        br.weight = frac - 0.5;
    } else {
        br.i0 = (idx - 1 + nrec) % nrec;
        br.i1 = idx % nrec;
        br.weight = frac + 0.5;
    }
    br.valid = true;
    return br;
}

// Map `eff_year` into [yearFirst, yLast] per taxmode. Returns false when no
// year can be resolved: an out-of-range year under "limit" (which also sets
// @p out_of_range), or an inverted range (which would otherwise make the cycle
// span zero years).
static bool apply_year_taxmode(int& eff_year, int yearFirst, int yLast, const std::string& tax, bool& out_of_range) {
    if (eff_year >= yearFirst && eff_year <= yLast) return true;

    const int year_span = yLast - yearFirst + 1;
    if (year_span <= 0) return false;
    if (tax == "limit") {
        out_of_range = true;
        return false;
    }
    if (tax == "extend") {
        eff_year = std::max(yearFirst, std::min(eff_year, yLast));
        return true;
    }
    int offset = (eff_year - yearFirst) % year_span;  // default: cycle
    if (offset < 0) offset += year_span;
    eff_year = yearFirst + offset;
    return true;
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
 *                   "extend": clamp to file boundary.
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

    std::string c = cadence;
    std::transform(c.begin(), c.end(), c.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    std::string algo = tintalgo;
    std::transform(algo.begin(), algo.end(), algo.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    std::string tax = taxmode;
    std::transform(tax.begin(), tax.end(), tax.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    const bool linear = (algo == "linear");

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

            if (!apply_year_taxmode(eff_year, yearFirst, yLast, tax, br.out_of_range)) return br;
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

        // Caveat: for multi-year files the modulo wrap makes the first/last
        // records interpolate against the opposite file end; the axis-based
        // resolver is the robust path, this arithmetic path is the fallback.
        br = midpoint_bracket(abs_day, frac, nrec);
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

            if (!apply_year_taxmode(eff_year, yearFirst, yLast, tax, br.out_of_range)) return br;
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

        // Mid-month linear interpolation convention.
        const int dim = tick::Gregorian_Calendar::days_in_month(dt.year, dt.month);
        const double frac = (static_cast<double>(dt.day - 1) + day_fraction(dt)) / static_cast<double>(dim);
        const int nrec = (file_nt > 0) ? file_nt : 12;

        // Caveat: for multi-year files the modulo wrap makes the first/last
        // records interpolate against the opposite file end (Dec of the last
        // year <-> Jan of the first); the axis-based resolver avoids this.
        br = midpoint_bracket(abs_month, frac, nrec);
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

    std::string tax = taxmode;
    std::transform(tax.begin(), tax.end(), tax.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

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

        // A multi-record axis covering under a second is not a real time axis.
        // An integer time variable read as floating point looks exactly like
        // this: the reinterpreted bit patterns stay ordered but collapse to
        // denormals, so every record decodes to the same instant.
        if (rec_days.size() > 1 && (rec_days.back() - rec_days.front()) < 1.0 / 86400.0) {
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
        // year is 365 or 366 days. Detect it by checking that the leftover
        // between the last record and the same calendar point one cycle on is
        // no larger than the axis's own coarsest record spacing.
        const int last_year = cal_to_dt(cal, abs_nanos(rec_days.back())).year;
        const int span_years = last_year - first_year + 1;
        const double span_days = rec_days.back() - rec_days.front();
        double max_interval = 0.0;
        for (size_t k = 1; k < rec_days.size(); ++k) {
            max_interval = std::max(max_interval, rec_days[k] - rec_days[k - 1]);
        }
        const double annual_days =
            static_cast<double>(cal_add_months(cal, first_nanos, span_years * 12) - first_nanos) / static_cast<double>(tick::nanos_per_day);
        const bool annual_cycle = (rec_days.size() > 1 && annual_days > span_days && (annual_days - span_days) <= 1.5 * max_interval);

        // Wrap the simulation *year* into the file's coverage rather than
        // wrapping the instant: wrapping the instant by a fixed period drifts
        // a day per leap year and never stops accumulating.
        const std::string tax = to_lower(taxmode);
        if (annual_cycle && (tax.empty() || tax == "cycle")) {
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

    std::vector<double> time_vals;
    const bool widened = widen_amio_elements(view_data, dtype, n_vals, time_scale, time_offset, time_vals);
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

using namespace detail;

CeceDriverOrchestrator::CeceDriverOrchestrator(const std::string& config_file, int nx, int ny, int nz, const double* lon_coords, int lon_len,
                                               const double* lat_coords, int lat_len, MPI_Comm comm_c)
    : config_file_(config_file),
      nx_(nx),
      ny_(ny),
      nz_(nz),
      target_lons_(lon_coords, lon_coords + lon_len),
      target_lats_(lat_coords, lat_coords + lat_len),
      comm_c_(comm_c) {
    // Configure the CECE logger with this driver's communicator so rank
    // filtering is correct when running under a non-COMM_WORLD split.
    {
        int mpi_init = 0;
        MPI_Initialized(&mpi_init);
        if (mpi_init && comm_c_ != MPI_COMM_NULL) {
            cece::CeceLogger::GetInstance().ConfigureCommunicator(comm_c_);
        }
    }

    // Parse config once at construction using HELM CONF
    try {
        conf::Config cfg = conf::Config::from_file(config_file_);
        gridspec_file_ = cfg.get_or<std::string>("driver.gridspec_file", "");

        int amio_threads = cfg.get_or("driver.amio_worker_threads", 1);
        if (amio_threads < 1) {
            throw std::invalid_argument("driver.amio_worker_threads must be >= 1; got " + std::to_string(amio_threads) + ".");
        }
        int amio_staging_buffer_count = cfg.get_or("driver.amio_staging_buffer_count", 8);
        if (amio_staging_buffer_count < 1) {
            throw std::invalid_argument("driver.amio_staging_buffer_count must be >= 1; got " + std::to_string(amio_staging_buffer_count) + ".");
        }

        // Cache per-variable stream configuration
        if (cfg.has("cece_data.streams")) {
            conf::Value streams = cfg.at("cece_data.streams");
            for (std::size_t si = 0; si < streams.size(); ++si) {
                conf::Value stream = streams[si];
                std::string stream_file = stream["file"].string_or("");
                std::string stream_mapalgo = stream["mapalgo"].string_or("consd");
                std::string stream_cadence = stream["cadence"].string_or("");
                int stream_year_first = stream["yearFirst"].int_or(0);
                int stream_year_last = stream["yearLast"].int_or(0);
                int stream_year_align = stream["yearAlign"].int_or(0);
                std::string stream_taxmode = stream["taxmode"].string_or("");
                std::string stream_tintalgo = stream["tintalgo"].string_or("nearest");
                std::string stream_time_var = stream["time_var"].string_or("time");
                std::string stream_time_units = stream["time_units"].string_or("");
                std::string stream_calendar = stream["calendar"].string_or("");

                // Validate the cadence and warn on knobs that the chosen cadence ignores.
                validate_stream_temporal_config(stream_cadence, stream_taxmode, stream_tintalgo, stream_year_first, stream_year_last,
                                                stream_year_align, " (stream file '" + stream_file + "')");

                // Parse data_model
                std::string data_model = "enhanced";
                bool data_model_explicit = false;
                conf::Value dm_val = stream["data_model"];
                if (dm_val.is_defined()) {
                    std::string requested_model = dm_val.as_string();
                    std::transform(requested_model.begin(), requested_model.end(), requested_model.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (requested_model == "classic" || requested_model == "enhanced") {
                        data_model = requested_model;
                        data_model_explicit = true;
                    } else if (requested_model != "auto") {
                        CECE_LOG_WARNING("[DRIVER] Invalid stream data_model='" + requested_model +
                                         "'; using default auto behavior (enhanced then classic fallback).");
                    }
                }

                // Map each variable in this stream to its config
                conf::Value variables = stream["variables"];
                for (std::size_t vi = 0; vi < variables.size(); ++vi) {
                    conf::Value var = variables[vi];
                    std::string model_name = var["model"].string_or("");
                    if (model_name.empty()) continue;

                    StreamVarConfig svc;
                    svc.input_file_path = stream_file;
                    svc.input_var_name = var["file"].string_or(model_name);
                    svc.mapalgo = stream_mapalgo;
                    svc.cadence = stream_cadence;
                    svc.yearFirst = stream_year_first;
                    svc.yearLast = stream_year_last;
                    svc.yearAlign = stream_year_align;
                    svc.taxmode = stream_taxmode;
                    svc.tintalgo = stream_tintalgo;
                    svc.time_var = stream_time_var;
                    svc.time_units = stream_time_units;
                    svc.calendar = stream_calendar;
                    svc.data_model = data_model;
                    svc.data_model_explicit = data_model_explicit;
                    svc.amio_threads = amio_threads;
                    svc.amio_staging_buffer_count = amio_staging_buffer_count;
                    stream_var_configs_[model_name] = svc;
                }
            }
        }
    } catch (const conf::Conf_Error& e) {
        CECE_LOG_ERROR("[DRIVER] Failed to parse config file '" + config_file_ + "': " + e.what());
        throw;
    }

    cece_io_ = std::make_unique<io::CeceIO>();
    cece_io_->Initialize(config_file_, nx_, ny_, nz_);
    CompileHelmGraph(config_file_, dagr_, *cece_io_, comm_c_);

    // Route DAGR's diagnostics through its shared LOGS logger with the same
    // MPI communicator CECE uses, and quiet non-root ranks (they still emit
    // FATAL). Without this, DAGR's logger is unconfigured and every rank prints
    // identical "GraphOrchestrator: shutdown initiated" lines with a [RANK:----]
    // sentinel stamp.
    {
        int mpi_initialized = 0;
        MPI_Initialized(&mpi_initialized);
        int rank = 0;
        if (mpi_initialized && comm_c_ != MPI_COMM_NULL) {
            MPI_Comm_rank(comm_c_, &rank);
        }
        dagr::configure_logging(comm_c_ != MPI_COMM_NULL ? comm_c_ : MPI_COMM_WORLD, rank == 0 ? dagr::Log_Level::info : dagr::Log_Level::error);
    }
}

CeceDriverOrchestrator::~CeceDriverOrchestrator() {
    // Cleanly drain any in-flight pipeline tasks and release hijacked ranks
    // before destroying the graph. Without this, tearing down the DAGR
    // GraphOrchestrator while a task is still in flight races with the
    // Event_Loop worker(s) and can segfault at teardown. shutdown() is
    // idempotent and safe to call here.
    if (dagr_) {
        dagr_->shutdown();
    }
    dagr_.reset();
    cece_io_.reset();
}

bool CeceDriverOrchestrator::AdvanceTime(const std::string& time_iso8601, void* cece_core_data_ptr) {
    if (!cece_core_data_ptr) return false;

    // A. Advance the pipeline step
    dagr_->advance_step();
    Kokkos::fence();
    // Parse the current simulation datetime once. Every cadence except
    // 'stepwise' uses these calendar fields to select the correct file record.
    const SimDateTime sim_dt = parse_sim_datetime(time_iso8601);

    // B. Push CeceIO's newly computed emission views into CECE's data ingestor
    for (const auto& var_name : cece_io_->GetOutputVarNames()) {
        auto stream_view = cece_io_->GetFieldView(var_name);

        // Use cached stream configuration (parsed once at construction)
        std::string input_file_path = "";
        std::string input_var_name = "";
        std::string mapalgo = "consd";
        std::string stream_data_model = "enhanced";
        std::string cadence;
        int yearFirst = 0;
        int yearLast = 0;
        int yearAlign = 0;
        std::string taxmode;
        std::string tintalgo = "nearest";
        std::string time_var = "time";
        std::string time_units;
        std::string calendar;
        bool stream_data_model_explicit = false;
        int amio_threads = 1;
        int amio_staging_buffer_count = 8;

        auto cfg_it = stream_var_configs_.find(var_name);
        if (cfg_it != stream_var_configs_.end()) {
            const StreamVarConfig& svc = cfg_it->second;
            input_file_path = svc.input_file_path;
            input_var_name = svc.input_var_name;
            mapalgo = svc.mapalgo;
            cadence = svc.cadence;
            yearFirst = svc.yearFirst;
            yearLast = svc.yearLast;
            yearAlign = svc.yearAlign;
            taxmode = svc.taxmode;
            tintalgo = svc.tintalgo;
            time_var = svc.time_var;
            time_units = svc.time_units;
            calendar = svc.calendar;
            stream_data_model = svc.data_model;
            stream_data_model_explicit = svc.data_model_explicit;
            amio_threads = svc.amio_threads;
            amio_staging_buffer_count = svc.amio_staging_buffer_count;
        }

        if (input_file_path.empty()) {
            LogFatal("[DRIVER FATAL] Input file path not specified for stream variable '" + var_name + "' in configuration!");
            return false;
        }
        if (input_var_name.empty()) {
            input_var_name = var_name;
        }

        // Verify if the input file path exists and is accessible from this compute/login node
        std::error_code fs_ec;
        if (!fs::exists(input_file_path, fs_ec)) {
            LogFatal("[DRIVER FATAL] File '" + input_file_path + "' does not exist or is unreadable on this node! (System error: " + fs_ec.message() +
                     ")");
        } else {
            CECE_LOG_DEBUG("[DRIVER] Input file '" + input_file_path + "' successfully verified on local filesystem.");
        }

        bool read_success = false;
        // Human-readable reason for the most recent read failure, propagated to
        // the fatal error message so the underlying AMIO status reaches CECE.
        std::string failure_detail;

        // Dynamically open and read using AMIO API
        std::string read_manifest_path = "amio_read_manifest_facade_" + var_name + ".yaml";

        int rank = 0;
        int mpi_initialized = 0;
        MPI_Initialized(&mpi_initialized);
        if (mpi_initialized && comm_c_ != MPI_COMM_NULL) {
            MPI_Comm_rank(comm_c_, &rank);
        }

        amio_core_handle read_core = nullptr;
        amio_dataset_handle read_dataset = nullptr;

        std::vector<std::string> data_models_to_try;
        if (stream_data_model_explicit) {
            data_models_to_try.push_back(stream_data_model);
        } else {
            data_models_to_try.push_back("enhanced");
            data_models_to_try.push_back("classic");
        }

        amio_status_t amio_rc = AMIO_ERR_BACKEND_FAILURE;
        std::string active_data_model = data_models_to_try.front();
        for (const auto& candidate_model : data_models_to_try) {
            active_data_model = candidate_model;

            if (rank == 0) {
                // Write input manifest YAML (Rank 0 only to prevent parallel write conflicts)
                std::ofstream m_file(read_manifest_path);
                if (!m_file) {
                    LogFatal("[DRIVER FATAL] Failed to create AMIO manifest YAML file '" + read_manifest_path + "'");
                    return false;
                }
                m_file << "backend: netcdf4\n"
                       << "path: " << input_file_path << "\n"
                       << "data_model: " << candidate_model << "\n"
                       << "staging_pool:\n"
                       << "  buffer_count: " << amio_staging_buffer_count << "\n"
                       << "  buffer_capacity_bytes: 268435456\n"
                       << "worker_pool:\n"
                       << "  threads: " << amio_threads << "\n"
                       << "prefetch:\n"
                       << "  depth: 2\n"
                       << "  read_timeout_s: 120\n"
                       << "staging_timeout_ms: 30000\n";
                m_file.close();
            }

            // Wait for Rank 0 to finish writing the manifest before other ranks load it.
            if (mpi_initialized && comm_c_ != MPI_COMM_NULL) {
                int barrier_rc = MPI_Barrier(comm_c_);
                if (barrier_rc != MPI_SUCCESS) {
                    CECE_LOG_WARNING("[DRIVER] MPI_Barrier failed with error code " + std::to_string(barrier_rc));
                }
            }

            // Force serial I/O fallback for reading offline datasets to prevent MPI multithreading deadlocks.
            if (mpi_initialized) {
                amio_set_parent_communicator(MPI_Comm_c2f(MPI_COMM_SELF));
            }

            amio_rc = amio_init(read_manifest_path.c_str(), &read_core);
            if (amio_rc != AMIO_OK) {
                failure_detail = std::string("amio_init failed for manifest '") + read_manifest_path + "': rc=" + std::to_string(amio_rc) + " (" +
                                 amio_strerror(amio_rc) + ")";
            } else {
                amio_rc = amio_open_dataset(read_core, read_manifest_path.c_str(), AMIO_MODE_READ, &read_dataset);
                if (amio_rc != AMIO_OK) {
                    failure_detail = std::string("amio_open_dataset failed for '") + input_file_path + "': rc=" + std::to_string(amio_rc) + " (" +
                                     amio_strerror(amio_rc) + ")";
                }
            }

            // Restore parent communicator for downstream operations.
            if (mpi_initialized && comm_c_ != MPI_COMM_NULL) {
                amio_set_parent_communicator(MPI_Comm_c2f(comm_c_));
            }

            if (amio_rc == AMIO_OK) {
                break;
            }

            CECE_LOG_DEBUG("[DRIVER] AMIO open attempt failed (data_model='" + candidate_model + "') with rc = " + std::to_string(amio_rc) + " (" +
                           amio_strerror(amio_rc) + ")");

            if (read_dataset) {
                amio_close(read_dataset);
                read_dataset = nullptr;
            }
            if (read_core) {
                amio_finalize(read_core);
                read_core = nullptr;
            }
        }

        if (amio_rc != AMIO_OK) {
            CECE_LOG_DEBUG("[DRIVER] amio_open_dataset failed for " + input_file_path + " with rc = " + std::to_string(amio_rc) + " (" +
                           amio_strerror(amio_rc) + ") after trying data_model='" + active_data_model + "'");
        } else {
            if (!stream_data_model_explicit && active_data_model != "enhanced") {
                CECE_LOG_INFO("[DRIVER] AMIO read manifest auto-fell back to data_model='" + active_data_model + "' for " + input_file_path);
            }

            // Determine this rank's contiguous destination latitude band [j0, j1)
            // via a simple block decomposition of the ny_ destination rows.
            int mpi_size = 1;
            int mpi_rank = 0;
            if (mpi_initialized && comm_c_ != MPI_COMM_NULL) {
                MPI_Comm_size(comm_c_, &mpi_size);
                MPI_Comm_rank(comm_c_, &mpi_rank);
            }
            const int band_base = ny_ / mpi_size;
            const int band_rem = ny_ % mpi_size;
            auto band_start = [&](int r) { return r * band_base + std::min(r, band_rem); };
            const int j0 = band_start(mpi_rank);
            const int j1 = band_start(mpi_rank + 1);

            // 1. Determine total timesteps from the input variable.
            //    Since AMIO doesn't expose a public function to query total timesteps,
            //    we use a binary search with amio_read on the input variable to identify
            //    the actual record limit (since reads beyond the record limit return AMIO_ERR_INVALID_INPUT).
            //    We cache the result in file_nt_cache_ to avoid binary search overhead on subsequent steps.
            int file_nt = 1;
            auto nt_it = file_nt_cache_.find(var_name);
            if (nt_it != file_nt_cache_.end()) {
                file_nt = nt_it->second;
            } else {
                if (!input_var_name.empty()) {
                    int low = 1;
                    int high = 1000000;
                    int found_nt = 1;
                    while (low <= high) {
                        int mid = low + (high - low) / 2;
                        amio_view_handle v = nullptr;
                        amio_status_t rc = amio_read(read_dataset, input_var_name.c_str(), mid, nullptr, &v);
                        if (rc == AMIO_OK) {
                            amio_release_view(v);
                            found_nt = mid + 1;
                            low = mid + 1;
                        } else {
                            high = mid - 1;
                        }
                    }
                    file_nt = found_nt;
                }
                file_nt_cache_[var_name] = file_nt;
            }

            // 2. Build (or reuse cached) interpolation weights for this rank's band.
            //    Weights depend only on the grids, so they are generated once and
            //    reused for every timestep.
            auto plan_it = regrid_plans_.find(var_name);
            if (plan_it == regrid_plans_.end() || !plan_it->second.built) {
                cece::io::RegridPlan plan;
                if (!cece::io::build_regrid_plan(read_dataset, nx_, ny_, target_lons_, target_lats_, mapalgo, j0, j1, gridspec_file_, plan)) {
                    CECE_LOG_DEBUG("[DRIVER] build_regrid_plan failed for '" + var_name + "'");
                    failure_detail = "regrid plan construction failed (could not read source grid coordinates)";
                } else {
                    plan_it = regrid_plans_.emplace(var_name, std::move(plan)).first;
                }
            }

            // 3. Read the bracketing record(s) for this timestep, blend in time on
            //    the SOURCE grid, then regrid ONCE. Because regridding is a linear
            //    operator, interpolating in time before space is mathematically
            //    identical to the reverse, but it costs a single regrid apply (not
            //    two) and keeps fill-value handling on the native grid.
            //
            //    The record bracket comes from the stream's cadence kind:
            //      - series (default) -> decode the file's time axis; degrade to
            //        arithmetic for daily/monthly when it can't be decoded
            //      - hourly / weekly  -> nearest discrete profile record
            //      - stepwise         -> opt-in step-index cycling (ignores time)
            if (plan_it != regrid_plans_.end() && plan_it->second.built) {
                const cece::io::RegridPlan& plan = plan_it->second;

                RecordBracket bracket;

                // Dispatch on cadence kind: Series decodes the file's time axis
                // (degrading to arithmetic only for daily/monthly), Profile indexes a
                // calendar field, Stepwise walks the record index (ignores time).
                const CadenceKind kind = classify_cadence(cadence);
                std::string c_lower = cadence;
                std::transform(c_lower.begin(), c_lower.end(), c_lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                std::string bracket_note;

                if (kind == CadenceKind::Stepwise) {
                    const int t_idx = (file_nt > 0) ? (step_index_ % file_nt) : 0;
                    bracket.i0 = bracket.i1 = t_idx;
                    bracket.weight = 0.0;
                    bracket.valid = true;
                    bracket_note = "stepwise (time ignored)";
                } else if (kind == CadenceKind::Profile) {
                    bracket = bracket_from_cadence(cadence, tintalgo, sim_dt, file_nt, yearFirst, yearLast, yearAlign, taxmode);
                    bracket_note = "profile:" + c_lower;
                } else {  // Series
                    if (file_nt > 1) {
                        bracket = bracket_from_dataset(read_dataset, time_var, sim_dt, file_nt, tintalgo, yearAlign, taxmode, time_units, calendar);
                    }
                    if (bracket.valid) {
                        bracket_note = "decoded axis";
                    } else if (bracket.out_of_range) {
                        // The axis decoded; taxmode 'limit' rejected the time. Degrading
                        // here would quietly hand back a climatology record instead.
                        bracket_note = "decoded axis, out of range";
                    } else if (c_lower == "daily" || c_lower == "monthly") {
                        // Undecodable axis but the cadence carries a granularity: degrade.
                        bracket = bracket_from_cadence(cadence, tintalgo, sim_dt, file_nt, yearFirst, yearLast, yearAlign, taxmode);
                        bracket_note = "degraded arithmetic:" + c_lower;
                    } else if (file_nt == 1) {
                        bracket.i0 = bracket.i1 = 0;
                        bracket.weight = 0.0;
                        bracket.valid = true;
                        bracket_note = "single record";
                    }
                }

                if (!bracket.valid) {
                    amio_close(read_dataset);
                    amio_finalize(read_core);
                    const std::string cadence_note = (cadence.empty() ? std::string("series (default)") : cadence);
                    if (bracket.out_of_range) {
                        LogFatal("[DRIVER FATAL] Simulation time " + time_iso8601 + " is outside the coverage of '" + input_file_path +
                                 "' for field '" + var_name + "' (cadence='" + cadence_note +
                                 "', taxmode='limit'). Use taxmode 'extend' to hold the nearest end or 'cycle' to repeat the file.");
                    } else {
                        LogFatal("[DRIVER FATAL] Could not resolve a time record for field '" + var_name + "' in '" + input_file_path +
                                 "' (cadence='" + cadence_note +
                                 "'): the time axis is not usable (missing/non-fixed units, records out of ascending order, or a degenerate span) "
                                 "and there is no cadence granularity to fall back on. "
                                 "Set 'cadence: stepwise' to ignore time, use 'cadence: daily'/'monthly', or provide 'time_units'.");
                    }
                    return false;
                }

                // Diagnostic: report which time slice(s) are being read and via which path.
                if (bracket.i0 == bracket.i1 || bracket.weight == 0.0) {
                    CECE_LOG_INFO("[DRIVER] Reading time slice " + std::to_string(bracket.i0 + 1) + "/" + std::to_string(file_nt) + " from '" +
                                  input_file_path + "' for field '" + var_name + "' (" + bracket_note + ", time=" + time_iso8601 + ")");
                } else {
                    CECE_LOG_INFO("[DRIVER] Interpolating time slices " + std::to_string(bracket.i0 + 1) + " & " + std::to_string(bracket.i1 + 1) +
                                  "/" + std::to_string(file_nt) + " (w=" + std::to_string(bracket.weight) + ") from '" + input_file_path +
                                  "' for field '" + var_name + "' (" + bracket_note + ", tintalgo=" + tintalgo + ", time=" + time_iso8601 + ")");
                }

                int file_nx = 0;
                int file_ny = 0;

                // CF packing for this variable, read once per step.
                double var_scale = 1.0;
                double var_offset = 0.0;
                read_cf_packing(read_dataset, input_var_name, var_scale, var_offset);

                // Read a single record into a double buffer on the source grid. The
                // AMIO netCDF backend detects the CF time dimension and returns a
                // single [lat, lon] slab, so each read stays at ny*nx elements even
                // for long, high-resolution sub-daily datasets (e.g. CAMS-TEMPO).
                auto read_slab = [&](int t_idx, std::vector<double>& out) -> bool {
                    amio_view_handle slab_view = nullptr;
                    amio_status_t rc = amio_read(read_dataset, input_var_name.c_str(), t_idx, nullptr, &slab_view);
                    if (rc != AMIO_OK) {
                        amio_rc = rc;
                        CECE_LOG_DEBUG("[DRIVER] amio_read('" + input_var_name + "', t=" + std::to_string(t_idx) +
                                       ") failed with rc = " + std::to_string(rc));
                        failure_detail =
                            std::string("amio_read('") + input_var_name + "') failed: rc=" + std::to_string(rc) + " (" + amio_strerror(rc) + ")";
                        return false;
                    }
                    const void* view_data = nullptr;
                    size_t view_size = 0;
                    rc = amio_view_data(slab_view, &view_data, &view_size);
                    if (rc != AMIO_OK) {
                        amio_rc = rc;
                        failure_detail = std::string("amio_view_data failed: rc=") + std::to_string(rc) + " (" + amio_strerror(rc) + ")";
                        amio_release_view(slab_view);
                        return false;
                    }
                    amio_shape_t read_shape{};
                    if (amio_view_shape(slab_view, &read_shape) != AMIO_OK) {
                        failure_detail = "amio_view_shape failed";
                        amio_release_view(slab_view);
                        return false;
                    }
                    amio_dtype_t slab_dtype = AMIO_DTYPE_F32;
                    if (amio_view_dtype(slab_view, &slab_dtype) != AMIO_OK) {
                        failure_detail = "amio_view_dtype failed";
                        amio_release_view(slab_view);
                        return false;
                    }
                    const std::size_t elem_size = amio_dtype_size(slab_dtype);
                    if (elem_size == 0) {
                        failure_detail = "unsupported element type on variable '" + input_var_name + "'";
                        amio_release_view(slab_view);
                        return false;
                    }
                    const int fny = static_cast<int>(read_shape.extents[read_shape.rank - 2]);
                    const int fnx = static_cast<int>(read_shape.extents[read_shape.rank - 1]);
                    size_t total_elements = 1;
                    for (int d = 0; d < read_shape.rank; ++d) {
                        total_elements *= read_shape.extents[d];
                    }
                    const size_t spatial = static_cast<size_t>(fny) * fnx;
                    // Normally the view holds a single slab (offset 0). Stay robust to
                    // a backend that returns the whole variable.
                    const size_t slices_in_view = (spatial > 0) ? (total_elements / spatial) : 1;
                    const size_t off = (slices_in_view > 1) ? static_cast<size_t>(t_idx) * spatial : 0;
                    if (view_size < (off + spatial) * elem_size) {
                        failure_detail = "view payload smaller than the requested slab";
                        amio_release_view(slab_view);
                        return false;
                    }
                    const void* slab_start = static_cast<const char*>(view_data) + off * elem_size;
                    if (!widen_amio_elements(slab_start, slab_dtype, spatial, var_scale, var_offset, out)) {
                        failure_detail = "could not widen element type of variable '" + input_var_name + "'";
                        amio_release_view(slab_view);
                        return false;
                    }
                    file_nx = fnx;
                    file_ny = fny;
                    amio_release_view(slab_view);
                    CECE_LOG_DEBUG("[DRIVER] Read slab t=" + std::to_string(t_idx) + " for '" + input_var_name + "': " + std::to_string(fny) + "x" +
                                   std::to_string(fnx) + " (" + std::to_string(spatial) + " elements, " + std::to_string(elem_size) + "-byte dtype " +
                                   std::to_string(static_cast<int>(slab_dtype)) + ")");
                    return true;
                };

                // Read the lower record and, when interpolating, the upper record;
                // blend on the source grid with the bracket weight.
                std::vector<double> src;
                bool have_data = read_slab(bracket.i0, src);
                if (have_data && bracket.i1 != bracket.i0 && bracket.weight > 0.0) {
                    std::vector<double> src1;
                    if (read_slab(bracket.i1, src1) && src1.size() == src.size()) {
                        const double w = bracket.weight;
                        for (size_t k = 0; k < src.size(); ++k) {
                            src[k] = (1.0 - w) * src[k] + w * src1[k];
                        }
                    } else {
                        have_data = false;
                    }
                }

                if (have_data) {
                    std::vector<double> local_dst;
                    if (cece::io::apply_regrid_plan(plan, /*time_offset=*/0, /*is_float=*/false, src.data(), file_nx, file_ny, nx_, local_dst)) {
                        // Gather each rank's destination band into the full [nx_*ny_] field.
                        std::vector<double> full_dst(static_cast<size_t>(nx_) * ny_, 0.0);
                        if (mpi_initialized && mpi_size > 1 && comm_c_ != MPI_COMM_NULL) {
                            std::vector<int> counts(mpi_size), displs(mpi_size);
                            for (int r = 0; r < mpi_size; ++r) {
                                counts[r] = (band_start(r + 1) - band_start(r)) * nx_;
                                displs[r] = band_start(r) * nx_;
                            }
                            MPI_Allgatherv(local_dst.data(), counts[mpi_rank], MPI_DOUBLE, full_dst.data(), counts.data(), displs.data(), MPI_DOUBLE,
                                           comm_c_);
                        } else {
                            std::copy(local_dst.begin(), local_dst.end(), full_dst.begin() + static_cast<size_t>(j0) * nx_);
                        }

                        // Populate the CECE field view (i, j, 0) from the full field.
                        auto h_view = Kokkos::create_mirror_view(stream_view);
                        for (int j = 0; j < ny_; ++j) {
                            for (int i = 0; i < nx_; ++i) {
                                h_view(i, j, 0) = full_dst[static_cast<size_t>(j) * nx_ + i];
                            }
                        }
                        Kokkos::deep_copy(stream_view, h_view);

                        // Also directly populate the C++ Core's import state fields to guarantee
                        // parallel-safe and synchronized import states across the driver facade and compute core!
                        auto* d = static_cast<cece::CeceInternalData*>(cece_core_data_ptr);
                        auto it_core = d->import_state.fields.find(var_name);
                        if (it_core == d->import_state.fields.end()) {
                            // Dynamically allocate the import field DualView inside the core
                            cece::DualView3D dv(var_name, nx_, ny_, nz_);
                            d->import_state.fields[var_name] = dv;
                            it_core = d->import_state.fields.find(var_name);
                        }

                        if (it_core != d->import_state.fields.end()) {
                            auto& core_field = it_core->second;
                            auto h_view_core = Kokkos::create_mirror_view(core_field.view_device());
                            for (int j = 0; j < ny_; ++j) {
                                for (int i = 0; i < nx_; ++i) {
                                    h_view_core(i, j, 0) = full_dst[static_cast<size_t>(j) * nx_ + i];
                                }
                            }
                            Kokkos::deep_copy(core_field.view_device(), h_view_core);
                            core_field.modify_device();
                            core_field.sync_host();
                        }

                        read_success = true;
                    } else {
                        CECE_LOG_DEBUG("[DRIVER] apply_regrid_plan returned false!");
                        failure_detail = "regrid weight application failed";
                    }
                }
            }
            amio_close(read_dataset);
        }
        amio_finalize(read_core);

        // Wait for all ranks to finalize their AMIO sessions before deleting the manifest file
        if (mpi_initialized && comm_c_ != MPI_COMM_NULL) {
            int barrier_rc = MPI_Barrier(comm_c_);
            if (barrier_rc != MPI_SUCCESS) {
                CECE_LOG_WARNING("[DRIVER] MPI_Barrier failed with error code " + std::to_string(barrier_rc));
            }
        }
        if (rank == 0) {
            std::error_code rm_ec;
            fs::remove(read_manifest_path, rm_ec);
            if (rm_ec) {
                CECE_LOG_WARNING("[DRIVER] Failed to remove manifest file '" + read_manifest_path + "': " + rm_ec.message());
            }
        }

        // Throw a fatal error on AMIO read failures
        if (!read_success) {
            std::string detail =
                failure_detail.empty() ? ("open/init failed: rc=" + std::to_string(amio_rc) + " (" + amio_strerror(amio_rc) + ")") : failure_detail;
            LogFatal("[FATAL ERROR] AMIO read failed for field '" + var_name + "' in file '" + input_file_path + "'. Reason: " + detail +
                     ". Idealized fallback is disabled!");
            return false;
        } else {
            CECE_LOG_INFO("[DRIVER] AMIO read succeeded for field '" + var_name + "' - loaded real data from " + input_file_path);
        }

        // Ingest raw data pointer of stream view into CECE's ingestor cache
        int bridge_rc = 0;
        cece_ingestor_set_field(cece_core_data_ptr, var_name.c_str(), static_cast<int>(var_name.length()), stream_view.data(),
                                nz_,        // n_lev
                                nx_ * ny_,  // n_elem
                                &bridge_rc);
        if (bridge_rc != 0) {
            LogFatal("[DRIVER FATAL] cece_ingestor_set_field failed for variable '" + var_name + "' with rc=" + std::to_string(bridge_rc));
            return false;
        }
    }

    step_index_++;
    return true;
}

}  // namespace cece

extern "C" {
void amio_set_parent_communicator(MPI_Fint comm);

void cece_driver_create(const char* yaml_path, int path_len, int nx, int ny, int nz, const double* lon_coords, int lon_len, const double* lat_coords,
                        int lat_len, int mpi_comm_f, void** driver_ptr_out, int* rc) {
    if (rc) *rc = 0;
    try {
        std::string path(yaml_path, path_len);

        // 1. Pass custom parent communicator to AMIO
        amio_set_parent_communicator(static_cast<MPI_Fint>(mpi_comm_f));

        // 2. Convert Fortran MPI handle to C MPI_Comm
        MPI_Comm comm_c = MPI_Comm_f2c(static_cast<MPI_Fint>(mpi_comm_f));

        // 3. Create orchestrator using the custom communicator
        auto* driver = new cece::CeceDriverOrchestrator(path, nx, ny, nz, lon_coords, lon_len, lat_coords, lat_len, comm_c);
        *driver_ptr_out = static_cast<void*>(driver);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: cece_driver_create: " << e.what() << std::endl;
        if (rc) *rc = -1;
    }
}

void cece_driver_advance_time(void* driver_ptr, const char* time_iso8601, int time_len, void* cece_core_data_ptr, int* rc) {
    if (rc) *rc = 0;
    try {
        auto* driver = static_cast<cece::CeceDriverOrchestrator*>(driver_ptr);
        std::string t_iso(time_iso8601, time_len);
        bool ok = driver->AdvanceTime(t_iso, cece_core_data_ptr);
        if (!ok && rc) *rc = -1;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: cece_driver_advance_time: " << e.what() << std::endl;
        if (rc) *rc = -1;
    }
}

extern std::unique_ptr<cece::CeceStandaloneWriter> g_standalone_writer;

void cece_driver_destroy(void* driver_ptr) {
    if (driver_ptr) {
        delete static_cast<cece::CeceDriverOrchestrator*>(driver_ptr);
    }
    g_standalone_writer.reset();
}

}  // extern "C"
