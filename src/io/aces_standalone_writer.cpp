#include "aces/aces_standalone_writer.hpp"

#include <netcdf.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <vector>

#include "ESMC.h"
#include "aces/aces_utils.hpp"

namespace aces {

AcesStandaloneWriter::AcesStandaloneWriter(const AcesOutputConfig& config) : config_(config) {}

AcesStandaloneWriter::~AcesStandaloneWriter() {
    Finalize();
}

int AcesStandaloneWriter::Initialize(const std::string& start_time_iso8601, int nx, int ny,
                                     int nz) {
    if (initialized_) {
        return 0;
    }

    nx_ = nx;
    ny_ = ny;
    nz_ = nz;
    start_time_iso8601_ = start_time_iso8601;

    struct stat st;
    // If stat fails, try to create directory
    if (stat(config_.directory.c_str(), &st) != 0) {
        if (mkdir(config_.directory.c_str(), 0777) != 0) {
            // If mkdir fails, check if it was created concurrently
            if (stat(config_.directory.c_str(), &st) != 0) {
                std::cerr << "ERROR: Failed to create output directory: " << config_.directory
                          << std::endl;
                return 1;
            }
        }
    }

    initialized_ = true;
    return 0;
}

int AcesStandaloneWriter::WriteTimeStep(ESMC_State export_state, double time_seconds_since_start,
                                        int step_index) {
    if (!initialized_) {
        std::cerr << "ERROR: Writer not initialized" << std::endl;
        return 1;
    }

    if (step_index % config_.frequency_steps != 0) {
        return 0;
    }

    std::string filename = ResolveFilename(time_seconds_since_start);
    std::string filepath = config_.directory + "/" + filename;

    // Use a local list of fields. If config fields is empty, we can't do anything
    // because we can't iterate state in C API easily without names.
    if (config_.fields.empty()) {
        std::cerr << "WARNING: No fields specified in output config and ESMC_State cannot be "
                     "iterated reliably in C API. Skipping output."
                  << std::endl;
        return 0;
    }

    bool first_field = true;
    for (const auto& name : config_.fields) {
        ESMC_Field field;
        int rc = ESMC_StateGetField(export_state, name.c_str(), &field);
        if (rc == ESMF_SUCCESS) {
            // Use REPLACE for the first field to create/overwrite the file.
            // Use OLD for subsequent fields to append variables to the existing file.
            int status = first_field ? ESMC_FILESTATUS_REPLACE : ESMC_FILESTATUS_OLD;

            rc = ESMC_FieldWrite(field, filepath.c_str(), name.c_str(), 0,
                                 (enum ESMC_FileStatus_Flag)status, 1, ESMF_IOFMT_NETCDF);
            if (rc != ESMF_SUCCESS) {
                std::cerr << "ERROR: ESMC_FieldWrite failed for " << name << " to " << filepath
                          << " rc=" << rc << std::endl;
            } else {
                first_field = false;
            }
        } else {
            // Field might not exist in this state (e.g. configured but not present)
            // standard behavior is to skip
        }
    }

    record_count_++;
    return 0;
}

int AcesStandaloneWriter::WriteTimeStep(
    const std::unordered_map<std::string, DualView3D>& export_fields,
    double time_seconds_since_start, int step_index) {
    if (!initialized_) {
        std::cerr << "ERROR: Writer not initialized" << std::endl;
        return 1;
    }

    if (step_index % config_.frequency_steps != 0) {
        return 0;
    }

    // Resolve filepath: use per-timestep filename so callers can use timestamp
    // patterns to create separate files per output step.
    std::string filename = ResolveFilename(time_seconds_since_start);
    std::string filepath = config_.directory + "/" + filename;

    // Determine which fields to write
    std::vector<std::string> fields_to_write;
    if (!config_.fields.empty()) {
        fields_to_write = config_.fields;
    } else {
        for (const auto& [name, view] : export_fields) {
            fields_to_write.push_back(name);
        }
    }

    if (fields_to_write.empty()) {
        return 0;
    }

    // Open or create NetCDF file
    int ncid = -1;
    int rc;
    bool file_exists = (access(filepath.c_str(), F_OK) == 0);

    if (file_exists && record_count_ > 0) {
        // Append to existing file
        rc = nc_open(filepath.c_str(), NC_WRITE, &ncid);
        if (rc != NC_NOERR) {
            std::cerr << "ERROR: nc_open failed: " << nc_strerror(rc) << std::endl;
            return 1;
        }
    } else {
        // Create new file (clobber)
        rc = nc_create(filepath.c_str(), NC_CLOBBER | NC_NETCDF4, &ncid);
        if (rc != NC_NOERR) {
            std::cerr << "ERROR: nc_create failed for " << filepath << ": " << nc_strerror(rc)
                      << std::endl;
            return 1;
        }

        // Write CF global attributes
        const char* conventions = "CF-1.8";
        nc_put_att_text(ncid, NC_GLOBAL, "Conventions", strlen(conventions), conventions);
        const char* source = "ACES";
        nc_put_att_text(ncid, NC_GLOBAL, "source", strlen(source), source);

        // Define dimensions
        int x_dimid, y_dimid, z_dimid, time_dimid;
        nc_def_dim(ncid, "x", nx_, &x_dimid);
        nc_def_dim(ncid, "y", ny_, &y_dimid);
        nc_def_dim(ncid, "z", nz_, &z_dimid);
        nc_def_dim(ncid, "time", NC_UNLIMITED, &time_dimid);

        // Define time variable
        int time_varid;
        nc_def_var(ncid, "time", NC_DOUBLE, 1, &time_dimid, &time_varid);
        const char* time_units = "seconds since start";
        nc_put_att_text(ncid, time_varid, "units", strlen(time_units), time_units);

        // Define field variables
        for (const auto& name : fields_to_write) {
            auto it = export_fields.find(name);
            if (it == export_fields.end()) continue;

            int dimids[4] = {time_dimid, x_dimid, y_dimid, z_dimid};
            int varid;
            int ndims = (nz_ > 1) ? 4 : 3;
            nc_def_var(ncid, name.c_str(), NC_DOUBLE, ndims, dimids, &varid);

            const char* units = "kg/m2/s";
            nc_put_att_text(ncid, varid, "units", strlen(units), units);

            double fill_value = 9.96921e+36;
            nc_put_att_double(ncid, varid, "_FillValue", NC_DOUBLE, 1, &fill_value);
        }

        nc_enddef(ncid);
    }

    // Write time value
    int time_varid;
    rc = nc_inq_varid(ncid, "time", &time_varid);
    if (rc == NC_NOERR) {
        // Get current time dimension length to find record index
        int time_dimid;
        nc_inq_dimid(ncid, "time", &time_dimid);
        size_t time_len = 0;
        nc_inq_dimlen(ncid, time_dimid, &time_len);

        size_t start_t[1] = {time_len};
        size_t count_t[1] = {1};
        nc_put_vara_double(ncid, time_varid, start_t, count_t, &time_seconds_since_start);

        // Write field data
        for (const auto& name : fields_to_write) {
            auto it = export_fields.find(name);
            if (it == export_fields.end()) continue;

            DualView3D v = it->second;
            v.sync<Kokkos::HostSpace>();

            int varid;
            rc = nc_inq_varid(ncid, name.c_str(), &varid);
            if (rc != NC_NOERR) continue;

            size_t start[4] = {time_len, 0, 0, 0};
            size_t count[4] = {1, (size_t)nx_, (size_t)ny_, (size_t)nz_};
            int ndims = (nz_ > 1) ? 4 : 3;
            nc_put_vara_double(ncid, varid, start, count, v.view_host().data());
            (void)ndims;
        }
    }

    nc_close(ncid);
    record_count_++;
    return 0;
}

void AcesStandaloneWriter::Finalize() {
    initialized_ = false;
}

std::string AcesStandaloneWriter::ResolveFilename(double time_seconds_since_start) const {
    struct tm tm_start = {};
    strptime(start_time_iso8601_.c_str(), "%Y-%m-%dT%H:%M:%S", &tm_start);
    time_t t = mktime(&tm_start) + static_cast<time_t>(time_seconds_since_start);
    struct tm* tm_current = gmtime(&t);

    std::string result = config_.filename_pattern;
    auto replace_all = [](std::string& str, const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = str.find(from, pos)) != std::string::npos) {
            str.replace(pos, from.length(), to);
            pos += to.length();
        }
    };

    char buffer[16];
    strftime(buffer, sizeof(buffer), "%Y", tm_current);
    replace_all(result, "{YYYY}", buffer);
    strftime(buffer, sizeof(buffer), "%m", tm_current);
    replace_all(result, "{MM}", buffer);
    strftime(buffer, sizeof(buffer), "%d", tm_current);
    replace_all(result, "{DD}", buffer);
    strftime(buffer, sizeof(buffer), "%H", tm_current);
    replace_all(result, "{HH}", buffer);
    strftime(buffer, sizeof(buffer), "%M", tm_current);
    replace_all(result, "{mm}", buffer);
    strftime(buffer, sizeof(buffer), "%S", tm_current);
    replace_all(result, "{ss}", buffer);
    return result;
}

}  // namespace aces
