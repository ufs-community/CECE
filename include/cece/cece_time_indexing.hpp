#ifndef CECE_TIME_INDEXING_HPP
#define CECE_TIME_INDEXING_HPP

/**
 * @file cece_time_indexing.hpp
 * @brief Resolving a simulation date-time to data-stream record indices.
 *
 * The cadence/taxmode/tintalgo model here follows CDEPS, and is CECE stream
 * policy rather than general chronology: see cece_calendar.hpp for the pieces
 * that are candidates for TICK.
 */

#include <amio/amio.h>

#include <string>
#include <vector>

#include "cece/cece_calendar.hpp"

namespace cece {
namespace detail {

struct SimDateTime {
    int year = 0;
    int month = 0;        ///< 1-12
    int day = 0;          ///< 1-31
    int hour = 0;         ///< 0-23
    int minute = 0;       ///< 0-59
    int second = 0;       ///< 0-59
    int day_of_week = 0;  ///< 1=Monday .. 7=Sunday (ISO 8601)
    int day_of_year = 0;  ///< 1-365/366
    bool valid = false;
};

struct RecordBracket {
    int i0 = 0;
    int i1 = 0;
    double weight = 0.0;
    bool valid = false;
    /// Set when the simulation time was resolvable but fell outside the file's
    /// coverage under taxmode "limit". Distinguishes a deliberate rejection
    /// from an axis that could not be read or decoded, which must not degrade
    /// to the arithmetic fallback.
    bool out_of_range = false;
};

/// Cadence dispatch: how a stream's records are addressed.
///   Series   - absolute time axis (decode), degrading to arithmetic for daily/monthly
///   Profile  - climatological profile indexed by a calendar field (hourly/weekly)
///   Stepwise - opt-in legacy step-index cycling (ignores time)
enum class CadenceKind { Series, Profile, Stepwise };

CadenceKind classify_cadence(const std::string& cadence);

/// Note: day_of_week and day_of_year are Gregorian regardless of the stream's
/// calendar, because TICK exposes them only on Gregorian_Calendar. They feed
/// the weekly and daily profile cadences, which are climatological.
SimDateTime parse_sim_datetime(const std::string& iso8601);

/// Validate a stream's temporal options and warn about knobs the chosen cadence
/// ignores. @p where is appended to messages to identify the offending stream.
/// Throws std::invalid_argument for an unknown cadence/taxmode/tintalgo or an
/// inverted yearFirst/yearLast range.
void validate_stream_temporal_config(const std::string& cadence, const std::string& taxmode, const std::string& tintalgo, int yearFirst, int yearLast,
                                     int yearAlign, const std::string& where);

RecordBracket bracket_from_cadence(const std::string& cadence, const std::string& tintalgo, const SimDateTime& dt, int file_nt, int yearFirst = 0,
                                   int yearLast = 0, int yearAlign = 0, const std::string& taxmode = "");

/// @p period_days is the repeat period used by taxmode "cycle". Pass 0 to infer
/// it from the axis, which is exact only for uniformly sampled records.
RecordBracket find_bracket(const std::vector<double>& times, double target, bool linear, const std::string& taxmode = "", double period_days = 0.0);

RecordBracket bracket_from_coords(const std::vector<double>& time_vals, const std::string& units, const std::string& calendar, const SimDateTime& dt,
                                  const std::string& tintalgo, int yearAlign = 0, const std::string& taxmode = "");

RecordBracket bracket_from_dataset(amio_dataset_handle dataset, const std::string& time_var, const SimDateTime& dt, int file_nt,
                                   const std::string& tintalgo, int yearAlign = 0, const std::string& taxmode = "",
                                   const std::string& units_override = "", const std::string& calendar_override = "");

}  // namespace detail
}  // namespace cece

#endif  // CECE_TIME_INDEXING_HPP
