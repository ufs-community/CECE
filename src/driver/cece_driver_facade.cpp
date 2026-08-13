#include "cece/cece_driver_facade.hpp"

#include <amio/amio.h>
#include <yaml-cpp/yaml.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <axis/axis.hpp>
#include <cmath>
#include <dagr/logging.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
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
        dt.day_of_week = tick::Gregorian_Calendar::day_of_week(tdt);
        dt.day_of_year = tick::Gregorian_Calendar::day_of_year(tdt);
        dt.valid = true;
    } catch (const std::exception&) {
        // Malformed timestamp: use explicit default values so callers fall back
        // to legacy step-index cycling.
        dt = SimDateTime{};
    }
    return dt;
}

/**
 * @brief Map a simulation datetime onto a record bracket for a given cadence.
 *
 * @param cadence    One of "hourly", "daily", "weekly", "monthly" (case-insensitive).
 * @param tintalgo   Time-interpolation algorithm: "linear" enables mid-month
 *                   or intra-day interpolation; anything else -> nearest.
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
 *
 * Hourly and weekly cadences select discrete profile records (hour-of-day,
 * day-of-week) and always use nearest-neighbour. Monthly and daily cadences
 * honour @c tintalgo for linear temporal interpolation.
 */
RecordBracket cadence_record_bracket(const std::string& cadence, const std::string& tintalgo, const SimDateTime& dt, int file_nt, int yearFirst,
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

            if (eff_year < yearFirst || eff_year > yLast) {
                const int year_span = yLast - yearFirst + 1;
                if (tax == "limit") {
                    return br;
                } else if (tax == "extend") {
                    eff_year = std::max(yearFirst, std::min(eff_year, yLast));
                } else {
                    int offset = (eff_year - yearFirst) % year_span;
                    if (offset < 0) offset += year_span;
                    eff_year = yearFirst + offset;
                }
            }
        }

        int abs_day;
        if (multi_year) {
            int days_offset = 0;
            for (int y = yearFirst; y < eff_year; ++y) {
                days_offset += tick::Gregorian_Calendar::days_in_year(y);
            }
            abs_day = days_offset + (dt.day_of_year - 1);
        } else {
            abs_day = dt.day_of_year - 1;  // 0-364 or 0-365 for single-year / climatology files
        }

        if (!linear) {
            br.i0 = br.i1 = clamp_idx(abs_day);
            br.valid = true;
            return br;
        }

        const double frac = dt.hour / 24.0;
        const int nrec = (file_nt > 0) ? file_nt : 365;

        if (frac >= 0.5) {
            br.i0 = abs_day % nrec;
            br.i1 = (abs_day + 1) % nrec;
            br.weight = frac - 0.5;
        } else {
            br.i0 = (abs_day - 1 + nrec) % nrec;
            br.i1 = abs_day % nrec;
            br.weight = frac + 0.5;
        }
        br.valid = true;
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

            // Apply taxmode to handle out-of-range years.
            if (eff_year < yearFirst || eff_year > yLast) {
                const int year_span = yLast - yearFirst + 1;
                if (tax == "limit") {
                    // Out of range: return invalid bracket.
                    return br;
                } else if (tax == "extend") {
                    // Clamp to file boundaries.
                    eff_year = std::max(yearFirst, std::min(eff_year, yLast));
                } else {
                    // Default: "cycle" — wrap into the file's year range.
                    int offset = (eff_year - yearFirst) % year_span;
                    if (offset < 0) offset += year_span;
                    eff_year = yearFirst + offset;
                }
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

        // Mid-month linear interpolation convention.
        const int dim = tick::Gregorian_Calendar::days_in_month(dt.year, dt.month);
        const double frac = (static_cast<double>(dt.day - 1) + dt.hour / 24.0) / static_cast<double>(dim);
        const int nrec = (file_nt > 0) ? file_nt : 12;

        if (frac >= 0.5) {
            br.i0 = abs_month % nrec;
            br.i1 = (abs_month + 1) % nrec;
            br.weight = frac - 0.5;
        } else {
            br.i0 = (abs_month - 1 + nrec) % nrec;
            br.i1 = abs_month % nrec;
            br.weight = frac + 0.5;
        }
        br.valid = true;
    }
    return br;
}

/**
 * @brief Resolve the time bracket by reading actual time coordinate values
 *        from the netCDF file via AMIO and finding the nearest/bracketing
 *        record for the given simulation datetime.
 *
 * This is the robust path: it reads the file's time variable (e.g., "time"),
 * parses CF-convention units ("days since YYYY-MM-DD", "hours since ...", etc.)
 * to convert each record's time value into an absolute date, then finds the
 * record(s) that bracket the simulation time.
 *
 * @param dataset    Open AMIO dataset handle (read mode).
 * @param time_var   Name of the time coordinate variable (default: "time").
 * @param dt         Current simulation datetime.
 * @param file_nt    Number of time records in the file.
 * @param tintalgo   "linear" for interpolation, otherwise nearest.
 * @param yearFirst  First year in file (for taxmode cycling fallback).
 * @param yearLast   Last year in file.
 * @param yearAlign  Alignment year.
 * @param taxmode    Cycling mode.
 * @return           A valid RecordBracket if successful, invalid otherwise.
 *
 * When this function returns an invalid bracket, the caller should fall back
 * to the arithmetic cadence_record_bracket() above.
 */
RecordBracket resolve_time_bracket_from_axis(amio_dataset_handle dataset, const std::string& time_var, const SimDateTime& dt, int file_nt,
                                             const std::string& tintalgo, int yearFirst, int yearLast, int yearAlign, const std::string& taxmode) {
    RecordBracket br;
    if (!dataset || !dt.valid || file_nt < 1) return br;

    // Read the time coordinate variable. It's typically a 1D array of
    // doubles representing offsets from a reference date.
    std::string tvar = time_var.empty() ? "time" : time_var;

    // Read all time values by reading timestep 0 of the time variable itself.
    // The time coordinate variable is 1D [nt], so reading it at timestep 0
    // should return the full array (since AMIO's describe_variable treats
    // non-time-varying 1D variables as single-record).
    amio_view_handle view = nullptr;
    amio_status_t rc = amio_read(dataset, tvar.c_str(), 0, nullptr, &view);
    if (rc != AMIO_OK) {
        // Try alternate time variable names.
        const char* alt_names[] = {"Time", "t", "valid_time", nullptr};
        for (int i = 0; alt_names[i] != nullptr; ++i) {
            rc = amio_read(dataset, alt_names[i], 0, nullptr, &view);
            if (rc == AMIO_OK) break;
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

    // Convert to double array (time values in file units).
    std::vector<double> time_vals(n_vals);
    const bool is_float = (view_size == n_vals * 4);
    if (is_float) {
        const float* p = static_cast<const float*>(view_data);
        for (size_t k = 0; k < n_vals; ++k) time_vals[k] = static_cast<double>(p[k]);
    } else {
        const double* p = static_cast<const double*>(view_data);
        for (size_t k = 0; k < n_vals; ++k) time_vals[k] = p[k];
    }
    amio_release_view(view);

    // Convert the simulation datetime to a "months since yearFirst-01"
    // representation for comparison with the time values. For monthly data,
    // the time values are typically "days since YYYY-01-01" or similar.
    // We use a simpler approach: convert each time value to an absolute
    // month index by looking at spacing, then find where the sim datetime
    // falls.
    //
    // Strategy: Determine if the time axis represents monthly data by
    // checking if the spacing between consecutive records is roughly 28-31
    // days (if in days) or ~720-744 hours (if in hours). Then compute
    // the simulation time in the same units as the file and do a binary
    // search.

    // Compute the simulation time as "days since yearFirst-01-01 00:00:00"
    // which is a common CF reference. We'll compare against the file's
    // time values after normalizing.
    //
    // More robustly: infer the file's time unit scale from the spacing
    // of its values, then compute the sim time in those units relative
    // to the same epoch the file uses.
    //
    // The most common CF units for monthly data are:
    //   "days since YYYY-01-01"
    //   "days since YYYY-1-1 00:00:00"
    //
    // Since we can't easily read the 'units' attribute via AMIO's current
    // API, we infer the epoch from yearFirst and the scale from the data:
    //   - If time_vals[0] ~ 0 and spacing ~ 30, units are "days since yearFirst"
    //   - If time_vals[0] ~ large and spacing ~ 30, the epoch predates yearFirst

    // Approach: Use yearFirst to define the reference epoch. Compute sim
    // time as fractional days since yearFirst-01-01 and compare to file vals.
    // If the file's first value doesn't start near 0, shift by the difference.

    // Compute sim_days: days from yearFirst-01-01 00:00:00 to sim datetime.
    const tick::Date_Time ref_dt{yearFirst > 0 ? yearFirst : 2000, 1, 1, 0, 0, 0, 0};
    const tick::Date_Time sim_tdt{dt.year, dt.month, dt.day, dt.hour, 0, 0, 0};
    const tick::Time_Point ref_tp = tick::Gregorian_Calendar::to_time_point(ref_dt);
    const tick::Time_Point sim_tp = tick::Gregorian_Calendar::to_time_point(sim_tdt);
    const double sim_days = static_cast<double>((sim_tp - ref_tp).nanos()) / static_cast<double>(tick::nanos_per_day);

    // Estimate the scale/epoch of the file's time values.
    // If file starts at yearFirst (time_vals[0] ~ 0..31), assume "days since yearFirst-01-01".
    // If file starts at a larger value, compute the offset.
    //
    // For a 288-record monthly file starting at Jan 2000:
    //   time_vals[0] should be ~15 (mid-Jan) or 0 (start-Jan) in "days since 2000-01-01"
    //   time_vals[1] should be ~45 or 31, etc.
    //
    // We compute what mid-January yearFirst would be in "days since yearFirst-01-01" = ~15.
    // If time_vals[0] is close to that, we're aligned. Otherwise, compute the shift.

    // Simple heuristic: first value represents record 0's time.
    // Compute expected first-record time as days since the reference epoch
    // for the first month midpoint (Jan 15 of yearFirst).
    // If the file value differs, that tells us the file's actual epoch.
    //
    // Actually, the most reliable approach is: compute what day each month
    // midpoint would be (in "days since yearFirst-01-01") and find the
    // closest match to time_vals[0] to determine the file epoch offset.

    // More direct: assume the file's time values are in "days since" some
    // epoch. The offset between our reference (yearFirst-01-01) and the
    // file's epoch can be estimated as:
    //   file_epoch_offset = time_vals[0] - expected_first_record_days
    //
    // For monthly data, the first record is typically at day 15 (midpoint)
    // or day 0 (start of month). Check both.

    double file_offset = 0.0;
    if (n_vals >= 2) {
        // Average spacing between records in file units.
        double avg_spacing = (time_vals[n_vals - 1] - time_vals[0]) / static_cast<double>(n_vals - 1);

        // Determine scale: if spacing ~ 28-31, units are likely days.
        // If spacing ~ 1, units might be months. If spacing ~ 720, hours.
        double scale_to_days = 1.0;  // default: assume days
        if (avg_spacing > 600.0 && avg_spacing < 800.0) {
            // Likely hours
            scale_to_days = 1.0 / 24.0;
        } else if (avg_spacing > 0.5 && avg_spacing < 1.5) {
            // Likely months (each record = 1 month unit)
            scale_to_days = 30.4375;  // average days per month
        }
        // else: assume days (spacing ~ 28-31 for monthly)

        // Convert time values to days for comparison.
        std::vector<double> time_days(n_vals);
        for (size_t k = 0; k < n_vals; ++k) {
            time_days[k] = time_vals[k] * scale_to_days;
        }

        // Compute the epoch offset: file_time_days[0] should correspond
        // to some date. If yearFirst is known, record 0 is January of
        // yearFirst. Mid-month would be ~15 days. Start-of-month = 0 days.
        // The file_offset is what we subtract from time_days to get
        // "days since yearFirst-01-01".
        //
        // We just need: sim_days == time_days[target_index] - file_offset
        // => file_offset = time_days[0] - 0 (if file starts at yearFirst Jan 1)
        //    or file_offset = time_days[0] - 15 (mid-month convention)
        //
        // Most robust: directly search for where sim_days falls in the
        // time_days array by computing file_offset = time_days[0] (assuming
        // record 0 = start of yearFirst) and adjusting.

        // Best approach: the file's first time value represents the first
        // record's date offset from the file's internal epoch. If yearFirst
        // is set, record 0 = Jan yearFirst. So the epoch of the file is:
        //   file_epoch_date = yearFirst-01-01 shifted back by time_days[0]
        //
        // Therefore, sim time in file units = sim_days + time_days[0]
        // (since sim_days is days since yearFirst-01-01, and time_days[0]
        // is the file's value for yearFirst-01-01 or mid-Jan).
        //
        // Actually simpler: just compute sim time in the same frame as the
        // file by finding: target = sim_days + time_days[0]
        // No — that's only correct if time_days[0] is at day 0 of yearFirst.
        //
        // Let's just do a direct search using the assumption that the
        // time values are monotonically increasing and we need to find
        // where our target falls. The target is sim_days expressed in
        // the file's coordinate system.
        //
        // target_in_file_units = time_days[0] + sim_days
        //   ... but only if record 0 corresponds to yearFirst-01-01.
        //
        // For CEDS files: time is "days since 1850-01-01" or similar,
        // and yearFirst=2000 means record 0 corresponds to Jan 2000.
        // So time_days[0] already encodes the offset from 1850 to Jan 2000.
        // Our sim_days is relative to yearFirst (2000-01-01).
        // Target in file coords = time_days[0] + sim_days.

        // Estimate the offset of record 0 relative to yearFirst-01-01 00:00:00.
        // For monthly cadence, time_days[0] represents the first record's timestamp.
        // If time_days[0] represents a mid-month point (~15 days into month 1),
        // rec0_days is ~15.2 days (0.5 * avg_spacing). If time_days[0] represents start-of-month,
        // rec0_days is 0.
        double rec0_days = 0.0;
        if (avg_spacing >= 25.0 && avg_spacing <= 32.0) {
            double month_phase = std::fmod(time_days[0], avg_spacing);
            if (month_phase < 0) month_phase += avg_spacing;
            if (month_phase >= 5.0) {
                rec0_days = 0.5 * avg_spacing;
            }
        }

        double base_time = time_days[0] - rec0_days;
        double target = base_time + sim_days;

        // Apply yearAlign adjustment.
        if (yearAlign > 0 && yearFirst > 0 && yearAlign != yearFirst) {
            // sim_days is computed relative to yearFirst, but the simulation
            // year may not map directly. Recompute sim_days relative to
            // yearAlign then add to base_time.
            const tick::Date_Time align_ref{yearAlign, 1, 1, 0, 0, 0, 0};
            const tick::Time_Point align_tp = tick::Gregorian_Calendar::to_time_point(align_ref);
            double sim_days_from_align = static_cast<double>((sim_tp - align_tp).nanos()) / static_cast<double>(tick::nanos_per_day);
            target = base_time + sim_days_from_align;
        }

        // Handle taxmode for out-of-range targets.
        std::string tax = taxmode;
        std::transform(tax.begin(), tax.end(), tax.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

        double file_start = time_days[0];
        double file_end = time_days[n_vals - 1];

        if (target < file_start || target > file_end) {
            double file_span = file_end - file_start;
            if (tax == "limit") {
                return br;  // invalid
            } else if (tax == "extend") {
                target = std::max(file_start, std::min(target, file_end));
            } else {
                // cycle
                if (file_span > 0.0) {
                    double offset_from_start = std::fmod(target - file_start, file_span);
                    if (offset_from_start < 0.0) offset_from_start += file_span;
                    target = file_start + offset_from_start;
                }
            }
        }

        // Binary search for the bracketing records.
        int lo = 0, hi = static_cast<int>(n_vals) - 1;
        while (lo < hi - 1) {
            int mid = (lo + hi) / 2;
            if (time_days[mid] <= target) {
                lo = mid;
            } else {
                hi = mid;
            }
        }

        std::string talgo = tintalgo;
        std::transform(talgo.begin(), talgo.end(), talgo.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

        if (talgo == "linear" && lo != hi) {
            double span = time_days[hi] - time_days[lo];
            double w = (span > 0.0) ? (target - time_days[lo]) / span : 0.0;
            w = std::max(0.0, std::min(1.0, w));
            br.i0 = lo;
            br.i1 = hi;
            br.weight = w;
        } else {
            // Nearest: pick whichever record is closer.
            if (hi < static_cast<int>(n_vals) && std::abs(time_days[hi] - target) < std::abs(time_days[lo] - target)) {
                br.i0 = br.i1 = hi;
            } else {
                br.i0 = br.i1 = lo;
            }
            br.weight = 0.0;
        }
        br.valid = true;
    } else {
        // Single time record.
        br.i0 = br.i1 = 0;
        br.weight = 0.0;
        br.valid = true;
    }

    return br;
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
    try {
        YAML::Node config = YAML::LoadFile(config_file_);
        if (config["driver"] && config["driver"]["gridspec_file"]) {
            gridspec_file_ = config["driver"]["gridspec_file"].as<std::string>();
        }
    } catch (const YAML::Exception& e) {
        gridspec_file_ = "";
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

    // Load full config to parse streams
    YAML::Node config = YAML::LoadFile(config_file_);

    // Parse the current simulation datetime once. Streams that declare a
    // temporal cadence (hourly/weekly/monthly) use these calendar fields to
    // select the correct file record; streams without a cadence keep the
    // legacy step-index cycling behaviour and ignore this.
    const SimDateTime sim_dt = parse_sim_datetime(time_iso8601);

    // B. Push CeceIO's newly computed emission views into CECE's data ingestor
    for (const auto& var_name : cece_io_->GetOutputVarNames()) {
        auto stream_view = cece_io_->GetFieldView(var_name);

        // Parse input file path and variable name dynamically from YAML config cece_data block
        std::string input_file_path = "../scripts/data/MACCity_4x5.nc";  // default fallback
        std::string input_var_name = "MACCity";                          // default fallback
        std::string mapalgo = "consd";                                   // default fallback
        std::string stream_data_model = "enhanced";                      // default AMIO data model
        std::string cadence;                                             // temporal cadence: hourly|weekly|monthly ("" -> legacy cycling)
        std::string tintalgo = "nearest";                                // time-interp algorithm: linear|nearest
        int yearFirst = 0;                                               // first year covered by the file
        int yearLast = 0;                                                // last year covered by the file
        int yearAlign = 0;                                               // sim year that aligns to yearFirst
        std::string taxmode;                                             // "cycle", "extend", or "limit"
        std::string time_var;                                            // name of time coordinate variable
        bool stream_data_model_explicit = false;
        if (config["cece_data"] && config["cece_data"]["streams"]) {
            for (const auto& stream : config["cece_data"]["streams"]) {
                bool found_var = false;
                for (const auto& var : stream["variables"]) {
                    if (var["model"] && var["model"].as<std::string>() == var_name) {
                        if (stream["file"]) {
                            input_file_path = stream["file"].as<std::string>();
                        }
                        if (var["file"]) {
                            input_var_name = var["file"].as<std::string>();
                        }
                        if (stream["mapalgo"]) {
                            mapalgo = stream["mapalgo"].as<std::string>();
                        }
                        if (stream["cadence"]) {
                            cadence = stream["cadence"].as<std::string>();
                        }
                        if (stream["tintalgo"]) {
                            tintalgo = stream["tintalgo"].as<std::string>();
                        }
                        if (stream["yearFirst"]) {
                            yearFirst = stream["yearFirst"].as<int>();
                        }
                        if (stream["yearLast"]) {
                            yearLast = stream["yearLast"].as<int>();
                        }
                        if (stream["yearAlign"]) {
                            yearAlign = stream["yearAlign"].as<int>();
                        }
                        if (stream["taxmode"]) {
                            taxmode = stream["taxmode"].as<std::string>();
                        }
                        if (stream["time_var"]) {
                            time_var = stream["time_var"].as<std::string>();
                        }
                        if (stream["data_model"]) {
                            std::string requested_model = stream["data_model"].as<std::string>();
                            std::transform(requested_model.begin(), requested_model.end(), requested_model.begin(),
                                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                            if (requested_model == "classic" || requested_model == "enhanced") {
                                stream_data_model = requested_model;
                                stream_data_model_explicit = true;
                            } else if (requested_model == "auto") {
                                stream_data_model = "enhanced";
                                stream_data_model_explicit = false;
                            } else {
                                CECE_LOG_WARNING("[DRIVER] Invalid stream data_model='" + requested_model + "' for stream variable '" + var_name +
                                                 "'; using default auto behavior (enhanced then classic fallback).");
                            }
                        }
                        found_var = true;
                        break;
                    }
                }
                if (found_var) break;
            }
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

        int amio_threads = 1;
        if (config["driver"] && config["driver"]["amio_worker_threads"]) {
            amio_threads = config["driver"]["amio_worker_threads"].as<int>();
            if (amio_threads < 1) {
                amio_threads = 1;
            }
        }

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
                       << "  buffer_count: 8\n"
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
            //    The record bracket comes from the stream's temporal cadence:
            //      - no cadence declared  -> legacy step-index cycling (single read)
            //      - hourly / weekly      -> nearest discrete profile record
            //      - monthly + tintalgo=linear -> mid-month linear interpolation
            //        between the two bracketing climatological records.
            if (plan_it != regrid_plans_.end() && plan_it->second.built) {
                const cece::io::RegridPlan& plan = plan_it->second;

                // Resolve the time bracket using a two-tier approach:
                // 1. Primary: read the actual time coordinate values from the file
                //    and find the bracketing records by matching the simulation time.
                // 2. Fallback: arithmetic cadence-based index computation using
                //    yearFirst/yearAlign/taxmode for multi-year files.
                RecordBracket bracket;

                // Try the robust time-axis reader first (for monthly or daily cadence with
                // multi-year files where yearFirst is specified).
                std::string c_lower = cadence;
                std::transform(c_lower.begin(), c_lower.end(), c_lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                if ((c_lower == "monthly" || c_lower == "daily") && yearFirst > 0 && file_nt > 12) {
                    bracket =
                        resolve_time_bracket_from_axis(read_dataset, time_var, sim_dt, file_nt, tintalgo, yearFirst, yearLast, yearAlign, taxmode);
                }

                // Fallback to arithmetic cadence computation if the time-axis
                // reader didn't produce a valid bracket.
                if (!bracket.valid) {
                    bracket = cadence_record_bracket(cadence, tintalgo, sim_dt, file_nt, yearFirst, yearLast, yearAlign, taxmode);
                }

                if (!bracket.valid) {
                    const int t_idx = (file_nt > 0) ? (step_index_ % file_nt) : 0;
                    bracket.i0 = bracket.i1 = t_idx;
                    bracket.weight = 0.0;
                }

                // Diagnostic: report which time slice(s) are being read from the file.
                if (bracket.i0 == bracket.i1 || bracket.weight == 0.0) {
                    CECE_LOG_INFO("[DRIVER] Reading time slice " + std::to_string(bracket.i0 + 1) + "/" + std::to_string(file_nt) + " from '" +
                                  input_file_path + "' for field '" + var_name + "'" +
                                  (cadence.empty() ? " (cycling, step=" + std::to_string(step_index_) + ")"
                                                   : " (cadence=" + cadence + ", time=" + time_iso8601 + ")"));
                } else {
                    CECE_LOG_INFO("[DRIVER] Interpolating time slices " + std::to_string(bracket.i0 + 1) + " & " + std::to_string(bracket.i1 + 1) + "/" +
                                  std::to_string(file_nt) + " (w=" + std::to_string(bracket.weight) + ") from '" + input_file_path +
                                  "' for field '" + var_name + "' (cadence=" + cadence + ", tintalgo=" + tintalgo + ", time=" + time_iso8601 + ")");
                }

                int file_nx = 0;
                int file_ny = 0;

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
                    const int fny = static_cast<int>(read_shape.extents[read_shape.rank - 2]);
                    const int fnx = static_cast<int>(read_shape.extents[read_shape.rank - 1]);
                    size_t total_elements = 1;
                    for (int d = 0; d < read_shape.rank; ++d) {
                        total_elements *= read_shape.extents[d];
                    }
                    const bool is_float = (view_size == total_elements * 4);
                    const size_t spatial = static_cast<size_t>(fny) * fnx;
                    // Normally the view holds a single slab (offset 0). Stay robust to
                    // a backend that returns the whole variable.
                    const size_t slices_in_view = (spatial > 0) ? (total_elements / spatial) : 1;
                    const size_t off = (slices_in_view > 1) ? static_cast<size_t>(t_idx) * spatial : 0;
                    out.resize(spatial);
                    if (is_float) {
                        const float* p = static_cast<const float*>(view_data) + off;
                        for (size_t k = 0; k < spatial; ++k) out[k] = static_cast<double>(p[k]);
                    } else {
                        const double* p = static_cast<const double*>(view_data) + off;
                        for (size_t k = 0; k < spatial; ++k) out[k] = p[k];
                    }
                    file_nx = fnx;
                    file_ny = fny;
                    amio_release_view(slab_view);
                    CECE_LOG_DEBUG("[DRIVER] Read slab t=" + std::to_string(t_idx) + " for '" + input_var_name + "': " + std::to_string(fny) + "x" +
                                   std::to_string(fnx) + " (" + std::to_string(spatial) + " elements, " + (is_float ? "float32" : "float64") + ")");
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
