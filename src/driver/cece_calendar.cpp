#include "cece/cece_calendar.hpp"

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <string>
#include <tick/tick.hpp>

#include "cece/cece_string_utils.hpp"

namespace cece {
namespace detail {

CalKind parse_calendar(const std::string& calendar) {
    const std::string c = to_lower(calendar);
    if (c == "noleap" || c == "no_leap" || c == "365_day" || c == "365day") return CalKind::NoLeap;
    if (c == "360_day" || c == "360day") return CalKind::Cal360;
    // "", "gregorian", "standard", "proleptic_gregorian", "julian" (approx), unknown.
    return CalKind::Gregorian;
}

std::int64_t cal_to_nanos(CalKind kind, const tick::Date_Time& dt) {
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

tick::Date_Time cal_to_dt(CalKind kind, std::int64_t nanos) {
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

std::int64_t cal_add_months(CalKind kind, std::int64_t nanos, int months) {
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

int cal_days_in_month(CalKind kind, int year, int month) {
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

    const std::string lower = to_lower(units);
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

}  // namespace detail
}  // namespace cece
