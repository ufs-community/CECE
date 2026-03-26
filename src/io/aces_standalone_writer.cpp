#include "aces/aces_standalone_writer.hpp"
#include "aces/aces_logger.hpp"

#include <netcdf.h>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <vector>
#include <cstring>

namespace fs = std::filesystem;

namespace aces {

namespace {
void check_nc(int status, const std::string& msg = "") {
    if (status != NC_NOERR) {
        std::string err = "NetCDF Error: " + std::string(nc_strerror(status));
        if (!msg.empty()) err += " (" + msg + ")";
        ACES_LOG_ERROR(err);
        throw std::runtime_error(err);
    }
}

std::tm ParseISO8601(const std::string& iso_time) {
    std::tm tm = {};
    std::istringstream ss(iso_time);
    char delimiter;
    // Expected format: YYYY-MM-DDTHH:MM:SS
    if (ss >> tm.tm_year) {
        tm.tm_year -= 1900; // tm_year is years since 1900
        if (ss >> delimiter && delimiter == '-') {
            if (ss >> tm.tm_mon) {
                tm.tm_mon -= 1; // tm_mon is 0-11
                if (ss >> delimiter && delimiter == '-') {
                    ss >> tm.tm_mday;
                    if (ss >> delimiter && (delimiter == 'T' || delimiter == ' ')) {
                        if (ss >> tm.tm_hour) {
                            if (ss >> delimiter && delimiter == ':') {
                                if (ss >> tm.tm_min) {
                                    if (ss >> delimiter && delimiter == ':') {
                                        ss >> tm.tm_sec;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    tm.tm_isdst = -1; // Let mktime determine DST, though usually UTC is preferred
    return tm;
}

std::string FormatTime(const std::tm& tm, const std::string& pattern) {
    std::string result = pattern;
    char buffer[64];

    // YYYY
    std::strftime(buffer, sizeof(buffer), "%Y", &tm);
    size_t pos;
    while ((pos = result.find("{YYYY}")) != std::string::npos) result.replace(pos, 6, buffer);

    // MM
    std::strftime(buffer, sizeof(buffer), "%m", &tm);
    while ((pos = result.find("{MM}")) != std::string::npos) result.replace(pos, 4, buffer);

    // DD
    std::strftime(buffer, sizeof(buffer), "%d", &tm);
    while ((pos = result.find("{DD}")) != std::string::npos) result.replace(pos, 4, buffer);

    // HH
    std::strftime(buffer, sizeof(buffer), "%H", &tm);
    while ((pos = result.find("{HH}")) != std::string::npos) result.replace(pos, 4, buffer);

    // mm
    std::strftime(buffer, sizeof(buffer), "%M", &tm);
    while ((pos = result.find("{mm}")) != std::string::npos) result.replace(pos, 4, buffer);

    // ss
    std::strftime(buffer, sizeof(buffer), "%S", &tm);
    while ((pos = result.find("{ss}")) != std::string::npos) result.replace(pos, 4, buffer);

    return result;
}

} // namespace

AcesStandaloneWriter::AcesStandaloneWriter(const AcesOutputConfig& config)
    : config_(config) {}

AcesStandaloneWriter::~AcesStandaloneWriter() {
    Finalize();
}

int AcesStandaloneWriter::Initialize(const std::string& start_time_iso8601, int nx, int ny, int nz) {
    if (!config_.enabled) return 0;

    start_time_iso8601_ = start_time_iso8601;
    nx_ = nx;
    ny_ = ny;
    nz_ = nz;

    ACES_LOG_INFO("[ACES] Initializing standalone writer with start time: " + start_time_iso8601);

    // Create output directory if it doesn't exist
    if (!fs::exists(config_.directory)) {
        try {
            fs::create_directories(config_.directory);
            ACES_LOG_INFO("[ACES] Created output directory: " + config_.directory);
        } catch (const std::exception& e) {
            ACES_LOG_ERROR("[ACES] Failed to create output directory: " + std::string(e.what()));
            return -1;
        }
    }

    initialized_ = true;
    return 0;
}

std::string AcesStandaloneWriter::ResolveFilename(double time_seconds_since_start) const {
    std::tm tm = ParseISO8601(start_time_iso8601_);
    std::time_t time = std::mktime(&tm);
    time += static_cast<std::time_t>(time_seconds_since_start);
    std::tm* new_tm = std::localtime(&time); // Use localtime or pure calculation? Simple addition is safer for pure UTC if available.
    // Assuming start_time is UTC, and we just add seconds.
    // However, mktime assumes local time. This is a bit tricky with time zones.
    // For simplicity in this standalone writer, we assume inputs are UTC and machine is UTC or we ignore TZ.

    fs::path p = fs::path(config_.directory) / FormatTime(*new_tm, config_.filename_pattern);
    return p.string();
}

int AcesStandaloneWriter::WriteTimeStep(const std::unordered_map<std::string, DualView3D>& fields,
                                        double time_seconds, int step) {
    if (!initialized_ || !config_.enabled) return 0;

    // Check frequency
    if (step % config_.frequency_steps != 0) return 0;

    ACES_LOG_INFO("[ACES] Writing time step " + std::to_string(step) + " (t=" + std::to_string(time_seconds) + ")");

    std::string filename = ResolveFilename(time_seconds);
    ACES_LOG_INFO("[ACES] Output file: " + filename);

    int ncid;
    try {
        // Create file (clobber existing)
        check_nc(nc_create(filename.c_str(), NC_CLOBBER | NC_NETCDF4, &ncid), "create");

        // Define dimensions
        int dim_x, dim_y, dim_z, dim_time;
        check_nc(nc_def_dim(ncid, "time", NC_UNLIMITED, &dim_time), "def_dim time");
        check_nc(nc_def_dim(ncid, "x", nx_, &dim_x), "def_dim x");
        check_nc(nc_def_dim(ncid, "y", ny_, &dim_y), "def_dim y");
        check_nc(nc_def_dim(ncid, "z", nz_, &dim_z), "def_dim z");
        // Using z as level

        // Define time variable
        int var_time;
        check_nc(nc_def_var(ncid, "time", NC_DOUBLE, 1, &dim_time, &var_time), "def_var time");
        check_nc(nc_put_att_text(ncid, var_time, "units",
                 14 + start_time_iso8601_.length(), ("seconds since " + start_time_iso8601_).c_str()));

        // Define field variables
        // If config_.fields is empty, write all fields. Else filter.
        std::vector<int> field_var_ids;
        std::vector<std::string> field_names;

        // Collect fields to write
        for (const auto& [name, view] : fields) {
            bool should_write = false;
            if (config_.fields.empty()) {
                should_write = true;
            } else {
                for (const auto& f : config_.fields) {
                    if (f == name) {
                        should_write = true;
                        break;
                    }
                }
            }

            if (should_write) {
                int var_id;
                int dims[4] = {dim_time, dim_z, dim_y, dim_x}; // time, z, y, x (ESMF usually z,y,x or x,y,z? usually [time, lev, lat, lon])

                check_nc(nc_def_var(ncid, name.c_str(), NC_DOUBLE, 4, dims, &var_id), "def_var " + name);
                field_var_ids.push_back(var_id);
                field_names.push_back(name);
            }
        }

        check_nc(nc_enddef(ncid), "enddef");

        // Write time
        size_t start[1] = {0};
        check_nc(nc_put_var1_double(ncid, var_time, start, &time_seconds), "put_var time");

        // Write fields
        for (size_t i = 0; i < field_names.size(); ++i) {
            const auto& name = field_names[i];
            const auto& view = fields.at(name);
            int var_id = field_var_ids[i];

            // Sync to host (cast away const to allow sync)
            auto& view_rw = const_cast<DualView3D&>(view);
            view_rw.sync<Kokkos::HostSpace>();
            auto h_view = view_rw.h_view;

            // Prepare buffer.
            // ACES View is (nx, ny, nz) with LayoutLeft (Fortran).
            // Memory layout: x varies fastest (stride 1).
            // NetCDF Variable defined as (time, z, y, x).
            // NetCDF expects buffer to be C-contiguous for (time, z, y, x), meaning x varies fastest.
            // So LayoutLeft (nx, ny, nz) is compatible with NetCDF (time, z, y, x).
            // We can write directly from h_view.data().

            size_t start_field[4] = {0, 0, 0, 0};
            size_t count_field[4] = {1, (size_t)nz_, (size_t)ny_, (size_t)nx_};

            check_nc(nc_put_vara_double(ncid, var_id, start_field, count_field, h_view.data()), "put_vara " + name);
        }

        check_nc(nc_close(ncid), "close");
        ACES_LOG_INFO("[ACES] Successfully wrote " + filename);

    } catch (const std::exception& e) {
        ACES_LOG_ERROR("[ACES] Failed to write NetCDF file: " + std::string(e.what()));
        if (ncid >= 0) nc_close(ncid); // Try to close
        return -1;
    }

    return 0;
}

void AcesStandaloneWriter::Finalize() {
    // Cleanup if needed
}

} // namespace aces
