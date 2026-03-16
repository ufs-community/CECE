#include "aces/aces_standalone_writer.hpp"

#include <sys/stat.h>

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

            rc = ESMC_FieldWrite(field, filepath.c_str(), NULL, 0,
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
    int counts[3] = {nx_, ny_, nz_};
    ESMC_InterArrayInt iCounts;
    ESMC_InterArrayIntSet(&iCounts, counts, (nz_ > 1 ? 3 : 2));

    int rc_grid;
    // Create grid - NoPeriDim(maxIndex, coordSys, typeKind, indexFlag, rc)
    ESMC_Grid grid = ESMC_GridCreateNoPeriDim(&iCounts, NULL, NULL, NULL, &rc_grid);
    if (rc_grid != ESMF_SUCCESS) {
        std::cerr << "ERROR: ESMC_GridCreateNoPeriDim failed" << std::endl;
        return 1;
    }

    int rc_state;
    ESMC_State state = ESMC_StateCreate("OutputState", &rc_state);
    if (rc_state != ESMF_SUCCESS) {
        std::cerr << "ERROR: ESMC_StateCreate failed" << std::endl;
        ESMC_GridDestroy(&grid);
        return 1;
    }

    std::vector<ESMC_Field> created_fields;

    // Temporarily populate config_.fields if empty, so the delegate method knows what to write.
    std::vector<std::string> original_fields = config_.fields;
    bool fields_was_empty = config_.fields.empty();

    if (fields_was_empty) {
        for (const auto& [name, view] : export_fields) {
            config_.fields.push_back(name);
        }
    }

    for (const auto& [name, view] : export_fields) {
        // Ensure data is on host
        DualView3D v = view;
        v.sync<Kokkos::HostSpace>();

        int rc_field;
        // ESMC_FieldCreateGridTypeKind(grid, typekind, staggerloc, gridToFieldMap, ungriddedLBound,
        // ungriddedUBound, name, rc)
        ESMC_Field field =
            ESMC_FieldCreateGridTypeKind(grid, ESMC_TYPEKIND_R8, ESMC_STAGGERLOC_CENTER, NULL, NULL,
                                         NULL, name.c_str(), &rc_field);

        if (rc_field != ESMF_SUCCESS) {
            std::cerr << "ERROR: ESMC_FieldCreateGridTypeKind failed for " << name << std::endl;
            continue;
        }

        created_fields.push_back(field);

        int rc_ptr;
        void* ptr = ESMC_FieldGetPtr(field, 0, &rc_ptr);
        if (rc_ptr != ESMF_SUCCESS) {
            std::cerr << "ERROR: ESMC_FieldGetPtr failed" << std::endl;
            continue;
        }

        size_t size = (size_t)nx_ * (size_t)ny_ * (size_t)nz_;
        std::memcpy(ptr, v.view_host().data(), size * sizeof(double));

        ESMC_StateAddField(state, field);
    }

    // Delegate to the ESMF implementation
    int result = WriteTimeStep(state, time_seconds_since_start, step_index);

    // Restore config
    config_.fields = original_fields;

    // Cleanup
    for (auto& f : created_fields) {
        ESMC_FieldDestroy(&f);
    }
    ESMC_StateDestroy(&state);
    ESMC_GridDestroy(&grid);

    return result;
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
