#include "cece/cece_standalone_writer.hpp"

#include <amio/amio.h>
#include <mpi.h>

#include "cece/cece_regridder_utils.hpp"

extern "C" {
void amio_set_parent_communicator(MPI_Fint comm);
}

#include <algorithm>
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

std::string GetCurrentTimestamp() {
    std::time_t now = std::time(nullptr);
    std::tm* gmt = std::gmtime(&now);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmt);
    return std::string(buf);
}

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

    // Band decomposition of the global latitude rows across comm_ (single source
    // of truth for the Output_Gather). Short-circuits to whole_grid for the
    // single-rank / no-MPI path, so ny_local == ny_ and the gather is skipped.
    band_ = cece::BandDecomposition::compute(ny_, comm_);

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
                                               const std::vector<double>& lat_coords, const std::string& gridspec_file) {
    if (!config_.enabled) return 0;

    // Check for duplicate longitude values (only for rectilinear grids)
    if (ny > 1 && lon_coords.size() == static_cast<size_t>(nx)) {
        std::set<double> unique_lons(lon_coords.begin(), lon_coords.end());
        if (unique_lons.size() < lon_coords.size()) {
            CECE_LOG_ERROR("[CECE] Duplicate longitude coordinates detected in input array!");
            return -1;
        }
    }

    // Check for duplicate latitude values (only for rectilinear grids)
    if (ny > 1 && lat_coords.size() == static_cast<size_t>(ny)) {
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
    gridspec_file_ = gridspec_file;

    // Band decomposition of the global latitude rows across comm_ (single source
    // of truth for the Output_Gather). Short-circuits to whole_grid for the
    // single-rank / no-MPI path, so ny_local == ny_ and the gather is skipped.
    band_ = cece::BandDecomposition::compute(ny_, comm_);

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

    CECE_LOG_INFO("[CECE] Writing time step " + std::to_string(step) + " (t=" + std::to_string(time_seconds) + ") via AMIO");

    std::string filename = ResolveFilename(time_seconds);
    CECE_LOG_INFO("[CECE] Output file: " + filename);

    // The writer manifest is built in memory (rank 0) and broadcast to the peer ranks,
    // then handed directly to AMIO via amio_init_from_string / amio_open_dataset_from_string.
    // Writing it to a shared-disk file races when multiple ranks per node access the same
    // path (torn/empty reads, MANIFEST_NOT_FOUND from a peer deleting it first); the
    // in-memory path avoids the file entirely.
    std::string manifest_content;

    amio_core_handle core = nullptr;
    amio_dataset_handle dataset = nullptr;

    try {
        if (rank == 0) {
            // Step 1: Build the dynamic AMIO netcdf4 manifest YAML in memory (rank 0 only,
            // then broadcast so every rank opens with byte-identical content).
            std::ostringstream m_file;

            m_file << "backend: netcdf4\n"
                   << "path: " << filename << "\n"
                   << "data_model: enhanced\n"
                   << "staging_pool:\n"
                   << "  buffer_count: 1\n"
                   << "  buffer_capacity_bytes: 33554432\n"
                   << "worker_pool:\n"
                   << "  threads: 1\n"
                   << "prefetch:\n"
                   << "  depth: 1\n"
                   << "  read_timeout_s: 60\n"
                   << "staging_timeout_ms: 10000\n";

            std::map<std::string, std::string> final_attrs;
            final_attrs["title"] = "CECE Standalone Emissions Simulation Output";
            final_attrs["Conventions"] = (ny_ == 1 ? "CF-1.9 UGRID-1.0" : "CF-1.9");
            final_attrs["institution"] = "National Oceanic and Atmospheric Administration";
            final_attrs["source"] = "CECE Standalone Driver, regridded dynamically via HELM AXIS topology engine";
            final_attrs["history"] = "Simulated on " + GetCurrentTimestamp() + " UTC";
            final_attrs["references"] = "CECE Documentation: https://ufs-community.github.io/CECE, Repository: https://github.com/ufs-community/cece";
            final_attrs["comment"] = "Target spatial grid: " + std::to_string(nx_) + "x" + std::to_string(ny_) + "x" + std::to_string(nz_);
            final_attrs["gridspec_file"] = (gridspec_file_.empty() ? "none" : gridspec_file_);

            for (const auto& [key, value] : config_.global_attributes) {
                final_attrs[key] = value;
            }

            m_file << "global_attributes:\n";
            for (const auto& [key, value] : final_attrs) {
                m_file << "  " << key << ": \"" << value << "\"\n";
            }
            // The collection is seeded with the coordinate variables and
            // carries time's units from config initialization (SetTimeUnits),
            // so it renders the whole variable side of the manifest.
            m_file << "variable_names: [\"lon\", \"lat\", \"lev\", \"time\", \"lon_bnds\", \"lat_bnds\"";
            if (ny_ == 1) {
                m_file << ", \"mesh\"";
            }
            for (const auto& field : config_.fields) {
                if (field.name != "lon" && field.name != "lat" && field.name != "lev" && field.name != "time" && field.name != "lon_bnds" &&
                    field.name != "lat_bnds" && field.name != "mesh") {
                    m_file << ", \"" << field.name << "\"";
                }
            }
            m_file << "]\nvariables:\n"
                   << "  lon:\n"
                   << "    attributes:\n"
                   << "      units: \"degrees_east\"\n"
                   << "      long_name: \"longitude\"\n"
                   << "      bounds: \"lon_bnds\"\n"
                   << "  lat:\n"
                   << "    attributes:\n"
                   << "      units: \"degrees_north\"\n"
                   << "      long_name: \"latitude\"\n"
                   << "      bounds: \"lat_bnds\"\n"
                   << "  lev:\n"
                   << "    attributes:\n"
                   << "      units: \"level\"\n"
                   << "      long_name: \"vertical level\"\n"
                   << "  time:\n"
                   << "    attributes:\n"
                   << "      units: \"seconds since " << start_time_iso8601_ << "\"\n"
                   << "      long_name: \"time\"\n"
                   << "  lon_bnds:\n"
                   << "    attributes:\n"
                   << "      units: \"degrees_east\"\n"
                   << "  lat_bnds:\n"
                   << "    attributes:\n"
                   << "      units: \"degrees_north\"\n";

            if (ny_ == 1) {
                m_file << "  mesh:\n"
                       << "    attributes:\n"
                       << "      cf_role: \"mesh_topology\"\n"
                       << "      topology_dimension: 2\n"
                       << "      face_coordinates: \"lon lat\"\n"
                       << "      face_bounds: \"lon_bnds lat_bnds\"\n";
            }

            for (const auto& field : config_.fields) {
                if (field.name != "lon" && field.name != "lat" && field.name != "lev" && field.name != "time" && field.name != "lon_bnds" &&
                    field.name != "lat_bnds" && field.name != "mesh") {
                    m_file << "  " << field.name << ":\n"
                           << "    attributes:\n";
                    for (const auto& [attr_name, attr_value] : field.attributes) {
                        m_file << "      " << attr_name << ": \"" << attr_value << "\"\n";
                    }
                    m_file << "      coordinates: \"time lev lat lon\"\n";
                    if (ny_ == 1) {
                        m_file << "      mesh: \"mesh\"\n"
                               << "      location: \"face\"\n";
                    }
                }
            }
            manifest_content = m_file.str();
        }

        // Broadcast the in-memory manifest from rank 0 to every rank so all ranks open the
        // dataset with identical content, without ever touching the shared filesystem.
        if (mpi_initialized && comm_ != MPI_COMM_NULL) {
            int manifest_len = static_cast<int>(manifest_content.size());
            MPI_Bcast(&manifest_len, 1, MPI_INT, 0, comm_);
            manifest_content.resize(static_cast<size_t>(manifest_len));
            if (manifest_len > 0) {
                MPI_Bcast(manifest_content.data(), manifest_len, MPI_CHAR, 0, comm_);
            }
        }

        // Step 2: Initialize AMIO Core
        if (mpi_initialized && comm_ != MPI_COMM_NULL) {
            amio_set_parent_communicator(MPI_Comm_c2f(comm_));
        } else if (mpi_initialized) {
            amio_set_parent_communicator(MPI_Comm_c2f(MPI_COMM_SELF));
        }
        check_amio_rc(amio_init_from_string(manifest_content.c_str(), "yaml", &core), "amio_init_from_string");

        // Step 3: Open Dataset
        check_amio_rc(amio_open_dataset_from_string(core, manifest_content.c_str(), "yaml", AMIO_MODE_WRITE, &dataset),
                      "amio_open_dataset_from_string");

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
        if (use_custom_coords_ && lon_values.size() == static_cast<size_t>(nx_) * ny_ && ny_ > 1) {
            lon_shape.rank = 2;
            lon_shape.extents[0] = ny_;
            lon_shape.extents[1] = nx_;
        } else {
            lon_shape.rank = 1;
            lon_shape.extents[0] = nx_;
        }
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
        if (use_custom_coords_ && lat_values.size() == static_cast<size_t>(nx_) * ny_ && ny_ > 1) {
            lat_shape.rank = 2;
            lat_shape.extents[0] = ny_;
            lat_shape.extents[1] = nx_;
        } else {
            lat_shape.rank = 1;
            lat_shape.extents[0] = (ny_ == 1) ? nx_ : ny_;
        }
        amio_io_handle lat_io = nullptr;
        check_amio_rc(amio_write(dataset, "lat", lat_values.data(), AMIO_DTYPE_F64, &lat_shape, &lat_io), "amio_write(lat)");

        // Step 5b: Compute and write cell boundary coordinate variables (bounds) using the AXIS mesh directly!
        std::vector<double> lon_bnds_values;
        std::vector<double> lat_bnds_values;
        amio_shape_t lon_bnds_shape{};
        amio_shape_t lat_bnds_shape{};

        // Build the destination AXIS mesh dynamically using our unified mesh builder
        auto dst_mesh = cece::io::build_axis_mesh(nx_, ny_, lon_values, lat_values, gridspec_file_);

        auto node_coords = dst_mesh.node_coords();
        auto conn_offsets = dst_mesh.conn_offsets();
        auto conn_indices = dst_mesh.conn_indices();

        enum class GridType { Rectilinear, Curvilinear, Unstructured };

        GridType grid_type = GridType::Rectilinear;
        if (use_custom_coords_ && lon_values.size() == static_cast<size_t>(nx_) * ny_ && ny_ > 1) {
            grid_type = GridType::Curvilinear;
        } else if (ny_ == 1) {
            grid_type = GridType::Unstructured;
        }

        switch (grid_type) {
            case GridType::Curvilinear: {
                // 1. Curvilinear case: shapes (ny_, nx_, 4)
                size_t n_cells = static_cast<size_t>(nx_) * ny_;
                lon_bnds_values.resize(n_cells * 4);
                lat_bnds_values.resize(n_cells * 4);

                for (int j = 0; j < ny_; ++j) {
                    for (int i = 0; i < nx_; ++i) {
                        size_t idx = static_cast<size_t>(j) * nx_ + i;
                        size_t offset = conn_offsets(idx);
                        for (int v = 0; v < 4; ++v) {
                            axis::index_t node_idx = conn_indices(offset + v);
                            lon_bnds_values[4 * idx + v] = node_coords(node_idx, 0);
                            lat_bnds_values[4 * idx + v] = node_coords(node_idx, 1);
                        }
                    }
                }

                lon_bnds_shape.rank = 3;
                lon_bnds_shape.extents[0] = ny_;
                lon_bnds_shape.extents[1] = nx_;
                lon_bnds_shape.extents[2] = 4;

                lat_bnds_shape.rank = 3;
                lat_bnds_shape.extents[0] = ny_;
                lat_bnds_shape.extents[1] = nx_;
                lat_bnds_shape.extents[2] = 4;
                break;
            }
            case GridType::Unstructured: {
                // 2. Unstructured case (MPAS, SCRIP, etc.): shapes (nx_, max_vertices)
                size_t n_cells = static_cast<size_t>(nx_);
                int max_vertices = 0;
                for (size_t i = 0; i < n_cells; ++i) {
                    int n_verts = static_cast<int>(conn_offsets(i + 1) - conn_offsets(i));
                    if (n_verts > max_vertices) max_vertices = n_verts;
                }

                lon_bnds_values.resize(n_cells * max_vertices, 0.0);
                lat_bnds_values.resize(n_cells * max_vertices, 0.0);

                for (size_t i = 0; i < n_cells; ++i) {
                    int n_verts = static_cast<int>(conn_offsets(i + 1) - conn_offsets(i));
                    size_t offset = conn_offsets(i);
                    for (int v = 0; v < max_vertices; ++v) {
                        int local_v = (v < n_verts) ? v : (n_verts - 1);
                        axis::index_t node_idx = conn_indices(offset + local_v);
                        lon_bnds_values[i * max_vertices + v] = node_coords(node_idx, 0);
                        lat_bnds_values[i * max_vertices + v] = node_coords(node_idx, 1);
                    }
                }

                lon_bnds_shape.rank = 2;
                lon_bnds_shape.extents[0] = nx_;
                lon_bnds_shape.extents[1] = max_vertices;

                lat_bnds_shape.rank = 2;
                lat_bnds_shape.extents[0] = nx_;
                lat_bnds_shape.extents[1] = max_vertices;
                break;
            }
            case GridType::Rectilinear: {
                // 3. Rectilinear case: shapes (nx_, 2) and (ny_, 2)
                lon_bnds_values.resize(static_cast<size_t>(nx_) * 2);
                lat_bnds_values.resize(static_cast<size_t>(ny_) * 2);

                // Longitude bounds: query nodes from the first row of cells (j = 0)
                for (int i = 0; i < nx_; ++i) {
                    size_t offset = conn_offsets(i);
                    axis::index_t node0 = conn_indices(offset + 0);
                    axis::index_t node1 = conn_indices(offset + 1);
                    lon_bnds_values[2 * i + 0] = node_coords(node0, 0);
                    lon_bnds_values[2 * i + 1] = node_coords(node1, 0);
                }

                // Latitude bounds: query nodes from the first column of cells (i = 0)
                for (int j = 0; j < ny_; ++j) {
                    size_t offset = conn_offsets(j * nx_);
                    axis::index_t node0 = conn_indices(offset + 0);
                    axis::index_t node3 = conn_indices(offset + 3);
                    lat_bnds_values[2 * j + 0] = node_coords(node0, 1);
                    lat_bnds_values[2 * j + 1] = node_coords(node3, 1);
                }

                lon_bnds_shape.rank = 2;
                lon_bnds_shape.extents[0] = nx_;
                lon_bnds_shape.extents[1] = 2;

                lat_bnds_shape.rank = 2;
                lat_bnds_shape.extents[0] = ny_;
                lat_bnds_shape.extents[1] = 2;
                break;
            }
        }

        // Clamp latitude bounds to sphere limits
        for (size_t i = 0; i < lat_bnds_values.size(); ++i) {
            if (lat_bnds_values[i] < -90.0) lat_bnds_values[i] = -90.0;
            if (lat_bnds_values[i] > 90.0) lat_bnds_values[i] = 90.0;
        }

        amio_io_handle lon_bnds_io = nullptr;
        check_amio_rc(amio_write(dataset, "lon_bnds", lon_bnds_values.data(), AMIO_DTYPE_F64, &lon_bnds_shape, &lon_bnds_io), "amio_write(lon_bnds)");

        amio_io_handle lat_bnds_io = nullptr;
        check_amio_rc(amio_write(dataset, "lat_bnds", lat_bnds_values.data(), AMIO_DTYPE_F64, &lat_bnds_shape, &lat_bnds_io), "amio_write(lat_bnds)");

        // Step 5c: Write mesh topology variable for unstructured UGRID mesh
        if (ny_ == 1) {
            int mesh_val = 1;
            amio_shape_t mesh_shape;
            std::memset(&mesh_shape, 0, sizeof(mesh_shape));
            mesh_shape.rank = 1;
            mesh_shape.extents[0] = 1;
            amio_io_handle mesh_io = nullptr;
            check_amio_rc(amio_write(dataset, "mesh", &mesh_val, AMIO_DTYPE_I32, &mesh_shape, &mesh_io), "amio_write(mesh)");
        }

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

        // --- Output_Gather setup (task 8.1) -------------------------------------
        //
        // Export fields are now BAND-LOCAL: each field host view is
        // nx_ x band_.ny_local x nz_ (LayoutLeft, (i, jrel, k)). To write the
        // global NetCDF field, every rank packs its band into a contiguous
        // [level][jrel][i] send buffer and issues one MPI_Gatherv PER LEVEL into a
        // global [level][j][i] buffer on rank 0, using row_counts[r]*nx_ /
        // row_displs[r]*nx_ (plain MPI_DOUBLE, mirroring the old per-level
        // MPI_Allgatherv transfer choice from RegridToDestinationBuffer, which
        // benchmarked markedly faster than a strided datatype in the container's
        // OpenMPI). Surplus ranks (ny_local == 0) contribute a zero-count band.
        //
        // Collective correctness (Req 6.6): iterating an unordered_map is NOT
        // order-deterministic across ranks, so we first collect the names to
        // write and SORT them, then iterate that rank-invariant order. Every rank
        // therefore issues exactly nz_ MPI_Gatherv calls per written field, in the
        // same field order, so the collective sequence is identical on all ranks.
        int mpi_size = 1;
        const bool use_mpi = (mpi_initialized && comm_ != MPI_COMM_NULL);
        if (use_mpi) {
            MPI_Comm_size(comm_, &mpi_size);
        }
        // The gather is only needed when there is genuinely more than one rank AND
        // the band actually splits the grid. In the single-rank / no-MPI path the
        // band host view already IS the global field (ny_local == ny_), so we use
        // it directly and issue no collective — byte-for-byte today's behavior.
        const bool do_gather = use_mpi && mpi_size > 1;

        std::vector<std::string> write_names;
        write_names.reserve(fields.size());
        for (const auto& [name, view] : fields) {
            if (IsCoordinateName(name)) {
                continue;
            }
            if (write_all || config_.fields.Contains(name)) {
                write_names.push_back(name);
            }
        }
        std::sort(write_names.begin(), write_names.end());

        const int ny_local = band_.ny_local;
        const size_t band_level_elems = static_cast<size_t>(nx_) * ny_local;  // nx_ * ny_local
        const size_t global_level_elems = static_cast<size_t>(nx_) * ny_;     // nx_ * ny_

        // Per-level recvcounts / displs for MPI_Gatherv, scaled by nx_ (Req 6.3).
        // Rank-invariant: derived purely from band_.row_counts/row_displs.
        std::vector<int> recvcounts;
        std::vector<int> recvdispls;
        if (do_gather) {
            recvcounts.resize(mpi_size);
            recvdispls.resize(mpi_size);
            for (int r = 0; r < mpi_size; ++r) {
                recvcounts[r] = band_.row_counts[r] * nx_;
                recvdispls[r] = band_.row_displs[r] * nx_;
            }
        }

        for (const auto& name : write_names) {
            {
                auto it = fields.find(name);
                const DualView3D& view = it->second;
                auto& view_rw = const_cast<DualView3D&>(view);
                view_rw.sync<Kokkos::HostSpace>();
                auto h_view = view_rw.view_host();

                // Band-local size check: nx_ * ny_local * nz_.
                const size_t band_total = static_cast<size_t>(nx_) * ny_local * nz_;
                if (h_view.size() != band_total) {
                    CECE_LOG_ERROR("Size mismatch in field '" + name + "': h_view.size()=" + std::to_string(h_view.size()) +
                                   " expected band-local=" + std::to_string(band_total) + " (nx_=" + std::to_string(nx_) +
                                   " ny_local=" + std::to_string(ny_local) + " nz_=" + std::to_string(nz_) + ")");
                    return -1;
                }

                // Pack the band host view (LayoutLeft (i, jrel, k)) into a
                // contiguous per-level send buffer laid out [level][jrel][i]:
                // element (k, jrel, i) at k*(ny_local*nx_) + jrel*nx_ + i.
                std::vector<double> send_buf(static_cast<size_t>(nz_) * band_level_elems);
                for (int k = 0; k < nz_; ++k) {
                    for (int jrel = 0; jrel < ny_local; ++jrel) {
                        for (int i = 0; i < nx_; ++i) {
                            size_t kokkos_idx =
                                static_cast<size_t>(i) + static_cast<size_t>(jrel) * nx_ + static_cast<size_t>(k) * nx_ * ny_local;
                            size_t send_idx = static_cast<size_t>(k) * band_level_elems + static_cast<size_t>(jrel) * nx_ + i;
                            send_buf[send_idx] = h_view.data()[kokkos_idx];
                        }
                    }
                }

                // global_field is the assembled global [level][j][i] buffer
                // (size nz_ * ny_ * nx_). In the single-rank path it IS send_buf
                // (already the whole grid). In the multi-rank path rank 0 fills it
                // via the per-level MPI_Gatherv below, then broadcasts it so ALL
                // ranks hold identical authoritative data for the collective write.
                std::vector<double> global_field;
                if (!do_gather) {
                    // Single-rank / no-MPI: ny_local == ny_, so send_buf is already
                    // the global [level][j][i] field. Move it in directly.
                    global_field = std::move(send_buf);
                } else {
                    if (rank == 0) {
                        global_field.assign(static_cast<size_t>(nz_) * global_level_elems, 0.0);
                    }
                    // One MPI_Gatherv PER LEVEL (Req 6.1, 6.3, 6.6). Every rank
                    // (including rank 0 and surplus ranks with ny_local == 0)
                    // issues exactly nz_ calls, in the same field order, so the
                    // collective sequence is identical across ranks. Surplus ranks
                    // send a zero-count band (sendcount 0).
                    const int sendcount = static_cast<int>(band_level_elems);  // nx_ * ny_local
                    for (int k = 0; k < nz_; ++k) {
                        const double* level_send = send_buf.data() + static_cast<size_t>(k) * band_level_elems;
                        double* level_recv = (rank == 0) ? (global_field.data() + static_cast<size_t>(k) * global_level_elems) : nullptr;
                        const int gather_rc =
                            MPI_Gatherv(level_send, sendcount, MPI_DOUBLE, level_recv, (rank == 0) ? recvcounts.data() : nullptr,
                                        (rank == 0) ? recvdispls.data() : nullptr, MPI_DOUBLE, 0, comm_);
                        if (gather_rc != MPI_SUCCESS) {
                            CECE_LOG_ERROR("Output_Gather MPI_Gatherv failed for field '" + name + "' (level " + std::to_string(k) +
                                           ", rc=" + std::to_string(gather_rc) + ")");
                            return -1;
                        }
                    }
                    // The Output_Gather leaves the authoritative assembled global
                    // field on rank 0 only. The AMIO netcdf4 backend, however,
                    // writes COLLECTIVELY when comm size > 1: it opens the file
                    // with nc_create_par and sets NC_COLLECTIVE var access, so the
                    // underlying nc_put_vara is an MPI collective that EVERY rank
                    // in comm_ must enter (on its worker thread) with the SAME
                    // hyperslab. Every rank writes the full variable at start
                    // [0,0,0,0]; parallel-HDF5 collective semantics require the
                    // data each rank contributes to that shared region be identical
                    // (this exactly matches the pre-rework replicated writer, where
                    // every rank held and wrote the full global field). A rank-0-
                    // only field write would desynchronize the collective and
                    // deadlock. So broadcast rank 0's authoritative global_field to
                    // all ranks, then every rank builds an identical netcdf_buffer
                    // and issues the same collective amio_write below (Req 6.2, 6.6).
                    if (rank != 0) {
                        global_field.assign(static_cast<size_t>(nz_) * global_level_elems, 0.0);
                    }
                    const int bcast_rc =
                        MPI_Bcast(global_field.data(), static_cast<int>(static_cast<size_t>(nz_) * global_level_elems), MPI_DOUBLE, 0, comm_);
                    if (bcast_rc != MPI_SUCCESS) {
                        CECE_LOG_ERROR("Output_Gather MPI_Bcast failed for field '" + name + "' (rc=" + std::to_string(bcast_rc) + ")");
                        return -1;
                    }
                }

                // --- Global write assembly (task 8.2) ---------------------------
                // Build the NetCDF [time, lev, lat, lon] buffer from the assembled
                // global field. After the gather+broadcast above, global_field holds
                // the authoritative global [level][j][i] field IDENTICALLY on every
                // rank (rank 0 gathered it; all others received the broadcast; the
                // single-rank path moved its own whole-grid band in). It is already
                // laid out [k][j][i] (== k*ny_*nx_ + j*nx_ + i), which is exactly the
                // NetCDF ordering, so this is a straight copy, kept as an explicit
                // transpose loop to mirror the original for clarity. Every rank
                // produces the same netcdf_buffer, so the collective amio_write below
                // contributes identical data across ranks (Req 6.2, 6.4, 6.6).
                size_t total_elements = static_cast<size_t>(nx_) * ny_ * nz_;
                std::vector<double> netcdf_buffer(total_elements);
                for (int k = 0; k < nz_; k++) {
                    for (int j = 0; j < ny_; j++) {
                        for (int i = 0; i < nx_; i++) {
                            size_t src_idx = static_cast<size_t>(k) * ny_ * nx_ + static_cast<size_t>(j) * nx_ + i;
                            size_t netcdf_idx = k * static_cast<size_t>(ny_) * nx_ + static_cast<size_t>(j) * nx_ + i;
                            netcdf_buffer[netcdf_idx] = global_field[src_idx];
                        }
                    }
                }

                amio_shape_t field_shape;
                std::memset(&field_shape, 0, sizeof(field_shape));
                // Write with a leading (unlimited) time axis so per-timestep files
                // form a proper CF time series. For unstructured grids (where ny_ == 1),
                // we write as a 3D variable [time=1, lev, nCells] to follow the UGRID convention.
                if (ny_ == 1) {
                    field_shape.rank = 3;
                    field_shape.extents[0] = 1;
                    field_shape.extents[1] = nz_;
                    field_shape.extents[2] = nx_;
                } else {
                    field_shape.rank = 4;
                    field_shape.extents[0] = 1;
                    field_shape.extents[1] = nz_;
                    field_shape.extents[2] = ny_;
                    field_shape.extents[3] = nx_;
                }

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

        CECE_LOG_INFO("[CECE] Successfully wrote " + filename + " via AMIO");

    } catch (const std::exception& e) {
        CECE_LOG_ERROR("[CECE] Failed to write NetCDF file via AMIO: " + std::string(e.what()));
        if (dataset) amio_close(dataset);
        if (core) amio_finalize(core);
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
