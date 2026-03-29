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

int AcesStandaloneWriter::InitializeWithCoords(const std::string& start_time_iso8601, int nx, int ny, int nz,
                                             const std::vector<double>& lon_coords, const std::vector<double>& lat_coords) {
    if (!config_.enabled) return 0;

    start_time_iso8601_ = start_time_iso8601;
    nx_ = nx;
    ny_ = ny;
    nz_ = nz;
    lon_coords_ = lon_coords;
    lat_coords_ = lat_coords;
    use_custom_coords_ = true;

    ACES_LOG_INFO("[ACES] Initializing standalone writer with coordinates: " + start_time_iso8601);

    // Create output directory if it doesn't exist
    if (!fs::exists(config_.directory)) {
        try {
            fs::create_directories(config_.directory);
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
    if (!initialized_) return -1;
    if (!config_.enabled) return 0;

    // Check frequency
    if (step % config_.frequency_steps != 0) return 0;

    ACES_LOG_INFO("[ACES] Writing time step " + std::to_string(step) + " (t=" + std::to_string(time_seconds) + ")");

    std::string filename = ResolveFilename(time_seconds);
    ACES_LOG_INFO("[ACES] Output file: " + filename);

    int ncid;
    try {
        bool file_exists = fs::exists(filename);
        int dim_lon, dim_lat, dim_lev, dim_time;
        int var_time;
        std::vector<int> field_var_ids;
        std::vector<std::string> field_names;
        size_t start_time_idx = 0;

        if (file_exists) {
            check_nc(nc_open(filename.c_str(), NC_WRITE, &ncid), "open existing");

            // Get existing time dimension length to know where to append
            check_nc(nc_inq_dimid(ncid, "time", &dim_time), "inq time dim");
            check_nc(nc_inq_dimlen(ncid, dim_time, &start_time_idx), "inq time len");

            check_nc(nc_inq_varid(ncid, "time", &var_time), "inq var time");

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
                    check_nc(nc_inq_varid(ncid, name.c_str(), &var_id), "inq var " + name);
                    field_var_ids.push_back(var_id);
                    field_names.push_back(name);
                }
            }
        } else {
            // Create file (clobber existing)
            check_nc(nc_create(filename.c_str(), NC_CLOBBER | NC_NETCDF4, &ncid), "create");

            // Define dimensions using CF-compliant names
            check_nc(nc_def_dim(ncid, "time", NC_UNLIMITED, &dim_time), "def_dim time");
            check_nc(nc_def_dim(ncid, "lon", nx_, &dim_lon), "def_dim lon");
            check_nc(nc_def_dim(ncid, "lat", ny_, &dim_lat), "def_dim lat");
            check_nc(nc_def_dim(ncid, "lev", nz_, &dim_lev), "def_dim lev");

            // Define time variable
            check_nc(nc_def_var(ncid, "time", NC_DOUBLE, 1, &dim_time, &var_time), "def_var time");
            std::string time_units = "seconds since " + start_time_iso8601_;
            check_nc(nc_put_att_text(ncid, var_time, "units", time_units.size(), time_units.c_str()), "time units");
            check_nc(nc_put_att_text(ncid, var_time, "long_name", 4, "time"), "time long_name");
            check_nc(nc_put_att_text(ncid, var_time, "calendar", 8, "standard"), "time calendar");

            // Define coordinate variables
            int var_lon, var_lat, var_lev;
            check_nc(nc_def_var(ncid, "lon", NC_DOUBLE, 1, &dim_lon, &var_lon), "def_var lon");
            check_nc(nc_put_att_text(ncid, var_lon, "units", 12, "degrees_east"), "lon units");
            check_nc(nc_put_att_text(ncid, var_lon, "long_name", 9, "longitude"), "lon long_name");
            check_nc(nc_put_att_text(ncid, var_lon, "standard_name", 9, "longitude"), "lon standard_name");

            check_nc(nc_def_var(ncid, "lat", NC_DOUBLE, 1, &dim_lat, &var_lat), "def_var lat");
            check_nc(nc_put_att_text(ncid, var_lat, "units", 13, "degrees_north"), "lat units");
            check_nc(nc_put_att_text(ncid, var_lat, "long_name", 8, "latitude"), "lat long_name");
            check_nc(nc_put_att_text(ncid, var_lat, "standard_name", 8, "latitude"), "lat standard_name");

            check_nc(nc_def_var(ncid, "lev", NC_DOUBLE, 1, &dim_lev, &var_lev), "def_var lev");
            check_nc(nc_put_att_text(ncid, var_lev, "units", 1, "1"), "lev units");
            check_nc(nc_put_att_text(ncid, var_lev, "long_name", 20, "model_level_number"), "lev long_name");

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
                    int dims[4] = {dim_time, dim_lev, dim_lat, dim_lon}; // time, lev, lat, lon (CF convention order)

                    check_nc(nc_def_var(ncid, name.c_str(), NC_DOUBLE, 4, dims, &var_id), "def_var " + name);

                    // Add coordinates attribute for proper CF compliance and ncview recognition
                    check_nc(nc_put_att_text(ncid, var_id, "coordinates", 16, "lon lat lev time"), "coordinates " + name);

                    // Add units and missing_value
                    check_nc(nc_put_att_text(ncid, var_id, "units", 7, "kg/m2/s"), "units " + name);
                    double fill_value = 9.96921e+36;
                    check_nc(nc_put_att_double(ncid, var_id, "_FillValue", NC_DOUBLE, 1, &fill_value), "fill " + name);

                    field_var_ids.push_back(var_id);
                    field_names.push_back(name);
                }
            }

            // Add global CF convention attributes for better tool support
            check_nc(nc_put_att_text(ncid, NC_GLOBAL, "Conventions", 6, "CF-1.8"), "global Conventions");
            check_nc(nc_put_att_text(ncid, NC_GLOBAL, "title", 28, "ACES Atmospheric Emissions"), "global title");
            check_nc(nc_put_att_text(ncid, NC_GLOBAL, "institution", 4, "ACES"), "global institution");
            check_nc(nc_put_att_text(ncid, NC_GLOBAL, "source", 44, "ACES - Atmospheric Chemistry Emission System"), "global source");

            check_nc(nc_enddef(ncid), "enddef");

            // Write coordinate arrays
            if (use_custom_coords_ && !lon_coords_.empty() && !lat_coords_.empty()) {
                // Use coordinates provided from ESMF grid
                check_nc(nc_put_var_double(ncid, var_lon, lon_coords_.data()), "put_var lon");
                check_nc(nc_put_var_double(ncid, var_lat, lat_coords_.data()), "put_var lat");
            } else {
                // Calculate coordinates (legacy approach)
                std::vector<double> lon_values(nx_);
                for (int i = 0; i < nx_; i++) {
                    lon_values[i] = -180.0 + (360.0 * (i + 0.5)) / nx_;
                }
                check_nc(nc_put_var_double(ncid, var_lon, lon_values.data()), "put_var lon");

                std::vector<double> lat_values(ny_);
                for (int j = 0; j < ny_; j++) {
                    lat_values[j] = -90.0 + (180.0 * (j + 0.5)) / ny_;
                }
                check_nc(nc_put_var_double(ncid, var_lat, lat_values.data()), "put_var lat");
            }

            // Level: 1 to nz
            std::vector<double> lev_values(nz_);
            for (int k = 0; k < nz_; k++) {
                lev_values[k] = k + 1.0;
            }
            check_nc(nc_put_var_double(ncid, var_lev, lev_values.data()), "put_var lev");
        } // end file_exists = false

        // Write time
        size_t start_time[1] = {start_time_idx};
        check_nc(nc_put_var1_double(ncid, var_time, start_time, &time_seconds), "put_var time");

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
            // NetCDF Variable defined as (time, lev, lat, lon).
            // Need to reorder data from (nx, ny, nz) to (nz, ny, nx) for NetCDF (lev, lat, lon).

            // Create properly ordered buffer for NetCDF
            size_t total_elements = static_cast<size_t>(nx_) * ny_ * nz_;
            std::vector<double> netcdf_buffer(total_elements);

            // Verify host view size matches expected size
            if (h_view.size() != total_elements) {
                ACES_LOG_ERROR("Size mismatch in field '" + name + "': h_view.size()=" + std::to_string(h_view.size()) +
                              " expected=" + std::to_string(total_elements) + " (nx=" + std::to_string(nx_) +
                              " ny=" + std::to_string(ny_) + " nz=" + std::to_string(nz_) + ")");
                return -1;
            }

            // Reorder from Kokkos (nx, ny, nz) to NetCDF (lev, lat, lon)
            // CRITICAL FIX: ACES stores data as (latitude, longitude, level) internally
            // This matches Fortran/ESMF conventions where latitude is first dimension
            for (int k = 0; k < nz_; k++) {          // level
                for (int j = 0; j < ny_; j++) {      // latitude index
                    for (int i = 0; i < nx_; i++) {  // longitude index
                        // Corrected indexing: swap i,j to match ACES internal storage
                        size_t kokkos_idx = j + i * ny_ + k * static_cast<size_t>(nx_) * ny_;  // (lat, lon, lev) order
                        size_t netcdf_idx = k * static_cast<size_t>(ny_) * nx_ + j * nx_ + i;  // (lev, lat, lon) order

                        // Bounds check for safety
                        if (kokkos_idx >= total_elements || netcdf_idx >= total_elements) {
                            ACES_LOG_ERROR("Index out of bounds in field '" + name + "': kokkos_idx=" +
                                          std::to_string(kokkos_idx) + " netcdf_idx=" + std::to_string(netcdf_idx) +
                                          " total=" + std::to_string(total_elements));
                            return -1;
                        }

                        netcdf_buffer[netcdf_idx] = h_view.data()[kokkos_idx];
                    }
                }
            }

            size_t start_field[4] = {start_time_idx, 0, 0, 0};
            size_t count_field[4] = {1, (size_t)nz_, (size_t)ny_, (size_t)nx_}; // time, lev, lat, lon

            check_nc(nc_put_vara_double(ncid, var_id, start_field, count_field, netcdf_buffer.data()), "put_vara " + name);
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
    if (!initialized_) return; // Already finalized or never initialized

    // Add logging to track finalization
    ACES_LOG_INFO("[ACES] AcesStandaloneWriter finalizing...");

    // Clear coordinate arrays to release memory
    lon_coords_.clear();
    lat_coords_.clear();

    initialized_ = false;
    ACES_LOG_INFO("[ACES] AcesStandaloneWriter finalized successfully");
}

} // namespace aces
