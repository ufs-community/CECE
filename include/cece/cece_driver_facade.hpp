#ifndef CECE_DRIVER_FACADE_HPP
#define CECE_DRIVER_FACADE_HPP

#include <amio/amio.h>
#include <mpi.h>

#include <dagr/dagr.hpp>
#include <memory>
#include <string>
#include <tick/tick.hpp>
#include <unordered_map>
#include <vector>

#include "cece/cece_io.hpp"
#include "cece/cece_regridder_utils.hpp"

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

/// Decoded CF "<unit> since <reference>" time-units string.
struct CFTimeUnits {
    double unit_days = 0.0;       ///< length of one axis unit, in days
    tick::Date_Time reference{};  ///< the "since" reference date-time, as written
    double offset_days = 0.0;     ///< UTC offset of @c reference (e.g. -0.25 for "-06:00"); subtract to get UTC
    bool valid = false;           ///< false when the units are missing or not decodable
};

CFTimeUnits parse_cf_units(const std::string& units);

/// Size in bytes of one @p dtype element, or 0 if CECE cannot handle it.
std::size_t amio_dtype_size(amio_dtype_t dtype);

/// Widen @p n elements of an AMIO view payload to double, applying CF packing
/// (`value = stored * scale + offset`). Handles every AMIO numeric type, so an
/// integer-typed coordinate or packed variable decodes correctly rather than
/// being reinterpreted. Returns false for a dtype CECE does not handle.
bool widen_amio_elements(const void* data, amio_dtype_t dtype, std::size_t n, double scale, double offset, std::vector<double>& out);

SimDateTime parse_sim_datetime(const std::string& iso8601);

/// Validate a stream's temporal options and warn about knobs the chosen cadence
/// ignores. @p where is appended to messages to identify the offending stream.
/// Throws std::invalid_argument for an unknown cadence/taxmode/tintalgo or an
/// inverted yearFirst/yearLast range.
void validate_stream_temporal_config(const std::string& cadence, const std::string& taxmode, const std::string& tintalgo, int yearFirst, int yearLast,
                                     int yearAlign, const std::string& where);

RecordBracket bracket_from_cadence(const std::string& cadence, const std::string& tintalgo, const SimDateTime& dt, int file_nt, int yearFirst = 0,
                                   int yearLast = 0, int yearAlign = 0, const std::string& taxmode = "");

RecordBracket find_bracket(const std::vector<double>& times, double target, bool linear, const std::string& taxmode = "");

RecordBracket bracket_from_coords(const std::vector<double>& time_vals, const std::string& units, const std::string& calendar, const SimDateTime& dt,
                                  const std::string& tintalgo, int yearAlign = 0, const std::string& taxmode = "");

RecordBracket bracket_from_dataset(amio_dataset_handle dataset, const std::string& time_var, const SimDateTime& dt, int file_nt,
                                   const std::string& tintalgo, int yearAlign = 0, const std::string& taxmode = "",
                                   const std::string& units_override = "", const std::string& calendar_override = "");

}  // namespace detail

/// Per-variable stream configuration cached at construction time.
/// Eliminates repeated config re-parsing on every timestep.
struct StreamVarConfig {
    std::string input_file_path = "";
    std::string input_var_name = "";
    std::string mapalgo = "consd";
    std::string cadence;  // "" means series (time-aware default)
    int yearFirst = 0;    // 0 = unknown/climatology
    int yearLast = 0;
    int yearAlign = 0;
    std::string taxmode;  // "" defaults to cycle
    std::string tintalgo = "nearest";
    std::string time_var = "time";  // time coordinate variable name
    std::string time_units;         // override for a missing/non-standard "units" attribute
    std::string calendar;           // "" -> file attribute, else gregorian
    std::string data_model = "enhanced";
    bool data_model_explicit = false;
    int amio_threads = 1;
    int amio_staging_buffer_count = 8;
};

class CeceDriverOrchestrator {
   public:
    CeceDriverOrchestrator(const std::string& config_file, int nx, int ny, int nz, const double* lon_coords, int lon_len, const double* lat_coords,
                           int lat_len, MPI_Comm comm_c);
    ~CeceDriverOrchestrator();

    // Prevent copy/move construction and assignment (Rule of Five)
    CeceDriverOrchestrator(const CeceDriverOrchestrator&) = delete;
    CeceDriverOrchestrator& operator=(const CeceDriverOrchestrator&) = delete;
    CeceDriverOrchestrator(CeceDriverOrchestrator&&) = delete;
    CeceDriverOrchestrator& operator=(CeceDriverOrchestrator&&) = delete;

    bool AdvanceTime(const std::string& time_iso8601, void* cece_core_data_ptr);

   private:
    using DeviceView3D = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>;

    bool AssembleReplicatedField(const std::string& var_name, const io::RegridPlan& plan, const std::vector<double>& source, int file_nx, int file_ny,
                                 int field_nlev, DeviceView3D stream_view, void* cece_core_data_ptr, std::vector<double>& ingest_buffer,
                                 std::string& failure_detail);

    std::string config_file_;
    int nx_{0}, ny_{0}, nz_{0};
    std::vector<double> target_lons_;
    std::vector<double> target_lats_;
    int step_index_{0};
    MPI_Comm comm_c_{MPI_COMM_NULL};

    // Cached per-variable stream configuration (parsed once at construction)
    std::unordered_map<std::string, StreamVarConfig> stream_var_configs_;
    // Cached regridding plans keyed by model variable name. The expensive
    // interpolation weights are built once (per rank-local destination band)
    // and reused for every timestep.
    std::unordered_map<std::string, io::RegridPlan> regrid_plans_;
    std::unordered_map<std::string, int> file_nt_cache_;

    // HELM Orchestration and pipeline components
    std::unique_ptr<dagr::GraphOrchestrator> dagr_;
    std::unique_ptr<io::CeceIO> cece_io_;
    std::string gridspec_file_;
};

}  // namespace cece

extern "C" {
void cece_driver_create(const char* yaml_path, int path_len, int nx, int ny, int nz, const double* lon_coords, int lon_len, const double* lat_coords,
                        int lat_len, int mpi_comm_f, void** driver_ptr_out, int* rc);

void cece_driver_advance_time(void* driver_ptr, const char* time_iso8601, int time_len, void* cece_core_data_ptr, int* rc);

void cece_driver_destroy(void* driver_ptr);
}

#endif  // CECE_DRIVER_FACADE_HPP
