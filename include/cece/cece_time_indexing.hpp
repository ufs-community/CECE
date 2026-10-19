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

#include <array>
#include <cstddef>
#include <cstdint>
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
/// Throws std::invalid_argument for an unknown cadence/taxmode/tintalgo,
/// time_label, or an inverted yearFirst/yearLast range.
void validate_stream_temporal_config(const std::string& cadence, const std::string& taxmode, const std::string& tintalgo, int yearFirst, int yearLast,
                                     int yearAlign, const std::string& where, const std::string& time_label = "auto");

/// Throws std::invalid_argument unless a stream that sets use_local_time uses
/// cadence weekly with tintalgo nearest. @p where identifies the stream.
void validate_stream_local_time(const std::string& cadence, const std::string& tintalgo, const std::string& where);

/// Seconds since 1970-01-01T00:00:00Z for @p dt (proleptic Gregorian, UTC).
std::int64_t sim_datetime_epoch_secs(const SimDateTime& dt);

/// Weekly-profile record (0=Monday .. 6=Sunday) for a local civil-seconds
/// timestamp, i.e. UTC epoch seconds plus the cell's UTC offset.
int weekly_record_from_local_secs(std::int64_t local_secs);

/// Weekly records that any cell can need at @p dt: the UTC date's record and its
/// neighbours, since UTC offsets (-12h..+14h) shift a local date by at most one
/// day. Rank-invariant, so every rank reads the same records.
std::array<int, 3> weekly_record_window(const SimDateTime& dt);

/// Fill @p out ([level][j][i], nlev * nx * ny_local) with each cell's value from the
/// weekly record of its local weekday. records[r] is record r's band buffer in the
/// same layout, or nullptr when it is not loaded; offset_at(i, j) is the cell's UTC
/// offset in seconds. Returns false and sets @p error if a cell needs a missing record.
template <class OffsetAt>
bool compose_local_weekly_band(const std::array<const double*, 7>& records, OffsetAt&& offset_at, std::int64_t utc_epoch_secs, int nx, int ny_local,
                               int nlev, std::vector<double>& out, std::string& error) {
    const std::size_t band_spatial = static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny_local);
    out.assign(static_cast<std::size_t>(nlev) * band_spatial, 0.0);
    for (int j = 0; j < ny_local; ++j) {
        for (int i = 0; i < nx; ++i) {
            const std::int64_t offset = offset_at(i, j);
            const double* record_band = records[static_cast<std::size_t>(weekly_record_from_local_secs(utc_epoch_secs + offset))];
            if (record_band == nullptr) {
                error = "UTC offset " + std::to_string(offset) + " s at band cell (" + std::to_string(i) + ", " + std::to_string(j) +
                        ") needs a weekly record outside the loaded window";
                return false;
            }
            const std::size_t spatial_index = static_cast<std::size_t>(j) * nx + i;
            for (int level = 0; level < nlev; ++level) {
                const std::size_t index = static_cast<std::size_t>(level) * band_spatial + spatial_index;
                out[index] = record_band[index];
            }
        }
    }
    return true;
}

RecordBracket bracket_from_cadence(const std::string& cadence, const std::string& tintalgo, const SimDateTime& dt, int file_nt, int yearFirst = 0,
                                   int yearLast = 0, int yearAlign = 0, const std::string& taxmode = "");

/// @p period_days is the repeat period used by taxmode "cycle". Pass 0 to infer
/// it from the axis, which is exact only for uniformly sampled records.
RecordBracket find_bracket(const std::vector<double>& times, double target, bool linear, const std::string& taxmode = "", double period_days = 0.0);

/// @p bounds is the CF bounds array for @p time_vals (2 values per record, in
/// the same units). When present it states each record's interval directly, so
/// @p time_label "auto" needs no inference. Pass empty when the file has none.
RecordBracket bracket_from_coords(const std::vector<double>& time_vals, const std::string& units, const std::string& calendar, const SimDateTime& dt,
                                  const std::string& tintalgo, int yearAlign = 0, const std::string& taxmode = "",
                                  const std::string& time_label = "auto", const std::vector<double>& bounds = {});

/// @p context names the stream in diagnostics (e.g. "field 'BC_agr' (file ...)").
RecordBracket bracket_from_dataset(amio_dataset_handle dataset, const std::string& time_var, const SimDateTime& dt, int file_nt,
                                   const std::string& tintalgo, int yearAlign = 0, const std::string& taxmode = "",
                                   const std::string& units_override = "", const std::string& calendar_override = "",
                                   const std::string& time_label = "auto", const std::string& context = "");

}  // namespace detail
}  // namespace cece

#endif  // CECE_TIME_INDEXING_HPP
