#include "cece/cece_standalone_writer.hpp"

#include <amio/amio.h>
#include <mpi.h>

extern "C" {
void amio_set_parent_communicator(MPI_Fint comm);
}

#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <vector>

#include "cece/cece_logger.hpp"

namespace fs = std::filesystem;

namespace cece {

namespace {

std::tm ParseISO8601(const std::string& iso_time) {
    std::tm tm = {};
    std::istringstream ss(iso_time);
    char delimiter;
    if (ss >> tm.tm_year) {
        tm.tm_year -= 1900;
        if (ss >> delimiter && delimiter == '-') {
            if (ss >> tm.tm_mon) {
                tm.tm_mon -= 1;
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
    tm.tm_isdst = -1;
    return tm;
}

std::string FormatTime(const std::tm& tm, const std::string& pattern) {
    std::string result = pattern;
    char buffer[64];

    std::strftime(buffer, sizeof(buffer), "%Y", &tm);
    size_t pos;
    while ((pos = result.find("{YYYY}")) != std::string::npos) result.replace(pos, 6, buffer);

    std::strftime(buffer, sizeof(buffer), "%m", &tm);
    while ((pos = result.find("{MM}")) != std::string::npos) result.replace(pos, 4, buffer);

    std::strftime(buffer, sizeof(buffer), "%d", &tm);
    while ((pos = result.find("{DD}")) != std::string::npos) result.replace(pos, 4, buffer);

    std::strftime(buffer, sizeof(buffer), "%H", &tm);
    while ((pos = result.find("{HH}")) != std::string::npos) result.replace(pos, 4, buffer);

    std::strftime(buffer, sizeof(buffer), "%M", &tm);
    while ((pos = result.find("{mm}")) != std::string::npos) result.replace(pos, 4, buffer);

    std::strftime(buffer, sizeof(buffer), "%S", &tm);
    while ((pos = result.find("{ss}")) != std::string::npos) result.replace(pos, 4, buffer);

    return result;
}

void check_amio_rc(amio_status_t status, const std::string& context) {
    if (status != AMIO_OK) {
        std::string err = "AMIO Error in " + context + ": " + std::string(amio_strerror(status));
        CECE_LOG_ERROR(err);
        throw std::runtime_error(err);
    }
}

}  // namespace

CeceStandaloneWriter::CeceStandaloneWriter(const CeceOutputConfig& config, MPI_Comm comm)
    : config_(config), initialized_(false), use_custom_coords_(false), nx_(0), ny_(0), nz_(0), comm_(comm) {}

CeceStandaloneWriter::~CeceStandaloneWriter() {
    Finalize();
}

int CeceStandaloneWriter::Initialize(const std::string& start_time_iso8601, int nx, int ny, int nz) {
    if (!config_.enabled) return 0;

    start_time_iso8601_ = start_time_iso8601;
    nx_ = nx;
    ny_ = ny;
    nz_ = nz;

    CECE_LOG_INFO("[CECE] Initializing AMIO standalone writer with start time: " + start_time_iso8601);

    if (!fs::exists(config_.directory)) {
        try {
            fs::create_directories(config_.directory);
            CECE_LOG_INFO("[CECE] Created output directory: " + config_.directory);
        } catch (const std::exception& e) {
            CECE_LOG_ERROR("[CECE] Failed to create output directory: " + std::string(e.what()));
            return -1;
        }
    }

    initialized_ = true;
    return 0;
}

int CeceStandaloneWriter::InitializeWithCoords(const std::string& start_time_iso8601, int nx, int ny, int nz, const std::vector<double>& lon_coords,
                                               const std::vector<double>& lat_coords) {
    if (!config_.enabled) return 0;

    // Check for duplicate longitude values
    {
        std::set<double> unique_lons(lon_coords.begin(), lon_coords.end());
        if (unique_lons.size() < lon_coords.size()) {
            CECE_LOG_ERROR("[CECE] Duplicate longitude coordinates detected in input array!");
            return -1;
        }
    }

    // Check for duplicate latitude values
    {
        std::set<double> unique_lats(lat_coords.begin(), lat_coords.end());
        if (unique_lats.size() < lat_coords.size()) {
            CECE_LOG_ERROR("[CECE] Duplicate latitude coordinates detected in input array!");
            return -1;
        }
    }

    start_time_iso8601_ = start_time_iso8601;
    nx_ = nx;
    ny_ = ny;
    nz_ = nz;
    lon_coords_ = lon_coords;
    lat_coords_ = lat_coords;
    use_custom_coords_ = true;

    CECE_LOG_INFO("[CECE] Initializing AMIO standalone writer with coordinates: " + start_time_iso8601);

    if (!fs::exists(config_.directory)) {
        try {
            fs::create_directories(config_.directory);
        } catch (const std::exception& e) {
            CECE_LOG_ERROR("[CECE] Failed to create output directory: " + std::string(e.what()));
            return -1;
        }
    }

    initialized_ = true;
    return 0;
}

std::string CeceStandaloneWriter::ResolveFilename(double time_seconds_since_start) const {
    std::tm tm = ParseISO8601(start_time_iso8601_);
    std::time_t time = std::mktime(&tm);
    time += static_cast<std::time_t>(time_seconds_since_start);
    std::tm* new_tm = std::localtime(&time);

    fs::path p = fs::path(config_.directory) / FormatTime(*new_tm, config_.filename_pattern);
    return p.string();
}

int CeceStandaloneWriter::WriteTimeStep(const std::unordered_map<std::string, DualView3D>& fields, double time_seconds, int step) {
    if (!initialized_ || !config_.enabled) return 0;

    if (step % config_.frequency_steps != 0) return 0;

    int rank = 0;
    int mpi_initialized = 0;
    MPI_Initialized(&mpi_initialized);
    if (mpi_initialized && comm_ != MPI_COMM_NULL) {
        MPI_Comm_rank(comm_, &rank);
    }

    if (rank != 0) {
        return 0;  // Standalone writing is strictly serial and executed on Rank 0 only to avoid conflicts
    }

    CECE_LOG_INFO("[CECE] Writing time step " + std::to_string(step) + " (t=" + std::to_string(time_seconds) + ") via AMIO");

    std::string filename = ResolveFilename(time_seconds);
    CECE_LOG_INFO("[CECE] Output file: " + filename);

    std::string manifest_path = filename + "_manifest.yaml";

    amio_core_handle core = nullptr;
    amio_dataset_handle dataset = nullptr;

    try {
        if (rank == 0) {
            // Step 1: Write dynamic AMIO netcdf4 manifest YAML (Rank 0 only to avoid parallel write conflicts)
            std::ofstream m_file(manifest_path);
            if (!m_file.is_open()) {
                CECE_LOG_ERROR("Failed to open manifest file for writing: " + manifest_path);
                return -1;
            }

            int write_threads = config_.amio_worker_threads;
            if (write_threads < 1) {
                write_threads = 1;
            }

            m_file << "backend: netcdf4\n"
                   << "path: " << filename << "\n"
                   << "data_model: enhanced\n"
                   << "staging_pool:\n"
                   << "  buffer_count: 16\n"
                   << "  buffer_capacity_bytes: 104857600\n"
                   << "worker_pool:\n"
                   << "  threads: " << write_threads << "\n"
                   << "prefetch:\n"
                   << "  depth: 4\n"
                   << "  read_timeout_s: 60\n"
                   << "staging_timeout_ms: 10000\n"
                   << "global_attributes:\n"
                   << "  title: \"CECE-HELM Standalone Simulation Output\"\n"
                   << "  Conventions: \"CF-1.8\"\n";
            // The collection is seeded with the coordinate variables and
            // carries time's units from config initialization (SetTimeUnits),
            // so it renders the whole variable side of the manifest.
            m_file << config_.fields.CreateIOManifest();
            m_file.close();
        }

        // Step 2: Initialize AMIO Core
        if (mpi_initialized) {
            amio_set_parent_communicator(MPI_Comm_c2f(MPI_COMM_SELF));
        }
        check_amio_rc(amio_init(manifest_path.c_str(), &core), "amio_init");

        // Step 3: Open Dataset
        check_amio_rc(amio_open_dataset(core, manifest_path.c_str(), AMIO_MODE_WRITE, &dataset), "amio_open_dataset");

        // Step 4: Write lon coordinate variable
        std::vector<double> lon_values;
        if (use_custom_coords_) {
            lon_values = lon_coords_;
        } else {
            lon_values.resize(nx_);
            for (int i = 0; i < nx_; i++) {
                lon_values[i] = -180.0 + (360.0 * (i + 0.5)) / nx_;
            }
        }
        amio_shape_t lon_shape;
        std::memset(&lon_shape, 0, sizeof(lon_shape));
        lon_shape.rank = 1;
        lon_shape.extents[0] = nx_;
        amio_io_handle lon_io = nullptr;
        check_amio_rc(amio_write(dataset, "lon", lon_values.data(), AMIO_DTYPE_F64, &lon_shape, &lon_io), "amio_write(lon)");

        // Step 5: Write lat coordinate variable
        std::vector<double> lat_values;
        if (use_custom_coords_) {
            lat_values = lat_coords_;
        } else {
            lat_values.resize(ny_);
            for (int j = 0; j < ny_; j++) {
                lat_values[j] = -90.0 + (180.0 * (j + 0.5)) / ny_;
            }
        }
        amio_shape_t lat_shape;
        std::memset(&lat_shape, 0, sizeof(lat_shape));
        lat_shape.rank = 1;
        lat_shape.extents[0] = ny_;
        amio_io_handle lat_io = nullptr;
        check_amio_rc(amio_write(dataset, "lat", lat_values.data(), AMIO_DTYPE_F64, &lat_shape, &lat_io), "amio_write(lat)");

        // Step 6: Write lev coordinate variable
        std::vector<double> lev_values(nz_);
        for (int k = 0; k < nz_; k++) {
            lev_values[k] = k + 1.0;
        }
        amio_shape_t lev_shape;
        std::memset(&lev_shape, 0, sizeof(lev_shape));
        lev_shape.rank = 1;
        lev_shape.extents[0] = nz_;
        amio_io_handle lev_io = nullptr;
        check_amio_rc(amio_write(dataset, "lev", lev_values.data(), AMIO_DTYPE_F64, &lev_shape, &lev_io), "amio_write(lev)");

        // Step 7: Write time coordinate variable
        double time_val = time_seconds;
        amio_shape_t time_shape;
        std::memset(&time_shape, 0, sizeof(time_shape));
        time_shape.rank = 1;
        time_shape.extents[0] = 1;
        amio_io_handle time_io = nullptr;
        check_amio_rc(amio_write(dataset, "time", &time_val, AMIO_DTYPE_F64, &time_shape, &time_io), "amio_write(time)");

        // Step 8: Write fields. No configured data fields means write all
        // export fields (the collection itself is never empty — it always
        // holds the coordinate variables).
        const bool write_all = config_.fields.GetDataFields().empty();
        for (const auto& [name, view] : fields) {
            if (IsCoordinateName(name)) {
                continue;
            }

            const bool should_write = write_all || config_.fields.Contains(name);
            if (should_write) {
                auto& view_rw = const_cast<DualView3D&>(view);
                view_rw.sync<Kokkos::HostSpace>();
                auto h_view = view_rw.view_host();

                size_t total_elements = static_cast<size_t>(nx_) * ny_ * nz_;
                if (h_view.size() != total_elements) {
                    CECE_LOG_ERROR("Size mismatch in field '" + name + "': h_view.size()=" + std::to_string(h_view.size()) +
                                   " expected=" + std::to_string(total_elements));
                    return -1;
                }

                std::vector<double> netcdf_buffer(total_elements);
                for (int k = 0; k < nz_; k++) {
                    for (int j = 0; j < ny_; j++) {
                        for (int i = 0; i < nx_; i++) {
                            size_t kokkos_idx = static_cast<size_t>(i) + static_cast<size_t>(j) * nx_ + k * static_cast<size_t>(nx_) * ny_;
                            size_t netcdf_idx = k * static_cast<size_t>(ny_) * nx_ + static_cast<size_t>(j) * nx_ + i;
                            netcdf_buffer[netcdf_idx] = h_view.data()[kokkos_idx];
                        }
                    }
                }

                amio_shape_t field_shape;
                std::memset(&field_shape, 0, sizeof(field_shape));
                // Write with a leading (unlimited) time axis so per-timestep files
                // form a proper CF time series: [time=1, lev, ny, nx]. The data
                // layout is unchanged since the leading time extent is 1.
                field_shape.rank = 4;
                field_shape.extents[0] = 1;
                field_shape.extents[1] = nz_;
                field_shape.extents[2] = ny_;
                field_shape.extents[3] = nx_;

                amio_io_handle field_io = nullptr;
                check_amio_rc(amio_write(dataset, name.c_str(), netcdf_buffer.data(), AMIO_DTYPE_F64, &field_shape, &field_io),
                              "amio_write(" + name + ")");
            }
        }

        // Step 9: Flush, Close, and Finalize
        check_amio_rc(amio_flush(dataset, 0), "amio_flush");
        check_amio_rc(amio_close(dataset), "amio_close");
        dataset = nullptr;

        check_amio_rc(amio_finalize(core), "amio_finalize");
        core = nullptr;

        // Cleanup temporary manifest file
        fs::remove(manifest_path);

        CECE_LOG_INFO("[CECE] Successfully wrote " + filename + " via AMIO");

    } catch (const std::exception& e) {
        CECE_LOG_ERROR("[CECE] Failed to write NetCDF file via AMIO: " + std::string(e.what()));
        if (dataset) amio_close(dataset);
        if (core) amio_finalize(core);
        if (fs::exists(manifest_path)) fs::remove(manifest_path);
        return -1;
    }

    return 0;
}

void CeceStandaloneWriter::Finalize() {
    if (!initialized_) return;

    std::cout << "[RANK:0000] [INFO] [CECE] CeceStandaloneWriter finalizing...\n";

    lon_coords_.clear();
    lat_coords_.clear();

    initialized_ = false;
    std::cout << "[RANK:0000] [INFO] [CECE] CeceStandaloneWriter finalized successfully\n";
}

}  // namespace cece

extern "C" {

#include <Kokkos_Core.hpp>

#include "cece/cece_internal.hpp"

std::unique_ptr<cece::CeceStandaloneWriter> g_standalone_writer;

void cece_core_write_step(void* data_ptr, double time_seconds, int step_index, int* rc) {
    if (rc != nullptr) *rc = 0;

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: cece_core_write_step - data_ptr is null" << std::endl;
        if (rc != nullptr) *rc = -1;
        return;
    }

    auto* d = static_cast<cece::CeceInternalData*>(data_ptr);

    if (!d->standalone_mode || !g_standalone_writer) return;

    // Skip the initial step (hour 0 / time = 0) since no integration/advancement has occurred
    if (step_index == 0 || time_seconds <= 0.0) return;

    const int freq = d->config.output_config.frequency_steps;
    if (step_index % freq != 0) return;

    // Critical: ensure all computations complete before writing
    Kokkos::fence();

    int w = g_standalone_writer->WriteTimeStep(d->export_state.fields, time_seconds, step_index);

    // Critical: sync to ensure all I/O completes before returning
    Kokkos::fence();

    if (w != 0) {
        std::cerr << "WARNING: cece_core_write_step - WriteTimeStep returned " << w << std::endl;
        if (rc != nullptr) *rc = w;
    }
}

}  // extern "C"
