#include <amio/amio.h>
#include <mpi.h>
#include <yaml-cpp/yaml.h>

#include <Kokkos_Core.hpp>
#include <axis/topology/named_grid_registry.hpp>
#include <cmath>
#include <fstream>
#include <halo/communicator.hpp>
#include <halo/environment.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <tick/tick.hpp>
#include <unordered_map>
#include <vector>

#include "cece/cece_config.hpp"
#include "cece/cece_driver_facade.hpp"
#include "cece/cece_fatal.hpp"
#include "cece/cece_logger.hpp"

namespace {

constexpr inline double wrap_longitude(double lon) {
    if (lon >= 180.0) {
        return lon - 360.0;
    }
    if (lon < -180.0) {
        return lon + 360.0;
    }
    return lon;
}

constexpr inline double radians_to_degrees(double rad) {
    return rad * 180.0 / M_PI;
}

}  // namespace

// CECE Core C-Linkage Lifecycle functions
extern "C" {
void cece_set_config_file_path(const char* config_path, int path_len);
void cece_run_log_setup(const char* config_path, int path_len);
void cece_core_initialize_p1(void** data_ptr_ptr, int* rc);
void cece_core_realize(void* data_ptr, int* rc);
void cece_core_initialize_p2(void* data_ptr, int* nx, int* ny, int* nz, int* rc);
void cece_core_run(void* data_ptr, int hour, int day_of_week, int* rc);
void cece_core_finalize(void* data_ptr, int* rc);
void cece_core_writer_initialize(void* data_ptr, int nx, int ny, int nz, const char* start_time_iso8601, int start_time_len, int mpi_comm_f, int* rc);
void cece_core_writer_initialize_with_coords(void* data_ptr, int nx, int ny, int nz, const double* lon_coords, int lon_len, const double* lat_coords,
                                             int lat_len, const char* start_time_iso8601, int start_time_len, int mpi_comm_f, int* rc);
void cece_core_write_step(void* data_ptr, double time_seconds, int step_index, int* rc);
void cece_core_set_export_field(void* data_ptr, const char* name, int name_len, const double* field_data, int nx, int ny, int nz, int* rc);
}

extern "C" {
void cece_driver_create(const char* yaml_path, int path_len, int nx, int ny, int nz, const double* lon_coords, int lon_len, const double* lat_coords,
                        int lat_len, int mpi_comm_f, void** driver_ptr_out, int* rc);
}

int main(int argc, char* argv[]) {
    // 1. Initialize MPI with thread support
    int provided = 0;
    int mpi_rc = MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (mpi_rc != MPI_SUCCESS) {
        cece::LogFatal("[DRIVER FATAL] MPI_Init_thread failed with error code " + std::to_string(mpi_rc));
        return mpi_rc;
    }

    if (provided < MPI_THREAD_MULTIPLE) {
        CECE_LOG_WARNING("[DRIVER WARNING] MPI implementation provided thread level " + std::to_string(provided) +
                         ", which is less than requested MPI_THREAD_MULTIPLE (" + std::to_string(MPI_THREAD_MULTIPLE) +
                         "). Threaded operations may be restricted.");
    }

    // 2. Initialize Kokkos (allocates execution resources on GPU or CPU)
    Kokkos::initialize(argc, argv);
    {
        // Initialize the HALO Environment & Communicator
        halo::Environment::initialize();
        halo::Communicator world(MPI_COMM_WORLD);
        const int my_rank = world.rank();

        std::string config_file = "cece_control_mock.yaml";
        if (argc > 1) {
            config_file = argv[1];
        }

        // --- Load configuration up front (grid, timing, streams parsed below) ---
        YAML::Node config = YAML::LoadFile(config_file);

        // Configure run logging (optional log file, per-rank stdout suppression)
        // and print the startup banner. Shared with the NUOPC cap so behavior is
        // identical regardless of how CECE is launched.
        cece_run_log_setup(config_file.c_str(), static_cast<int>(config_file.length()));

        // Set config file path dynamically
        cece_set_config_file_path(config_file.c_str(), static_cast<int>(config_file.length()));

        // A. Grid Dimensions
        int nx = 0;
        int ny = 0;
        int nz = 0;
        std::string grid_name = "";
        if (config["driver"] && config["driver"]["grid"]) {
            auto grid_node = config["driver"]["grid"];
            if (grid_node["nz"]) {
                nz = grid_node["nz"].as<int>(1);
            } else {
                nz = 1;
            }
            if (grid_node["grid_name"]) {
                grid_name = grid_node["grid_name"].as<std::string>();
            }
            if (grid_name.empty()) {
                if (grid_node["nx"]) {
                    nx = grid_node["nx"].as<int>(0);
                }
                if (grid_node["ny"]) {
                    ny = grid_node["ny"].as<int>(0);
                }
            } else {
                try {
                    auto parsed = axis::topology::NamedGridRegistry::parse(grid_name);
                    if (parsed.family == 'F' || parsed.family == 'R') {
                        int expected_nx = 4 * parsed.number;
                        int expected_ny = 2 * parsed.number;

                        int declared_nx = grid_node["nx"].as<int>(0);
                        int declared_ny = grid_node["ny"].as<int>(0);
                        if (declared_nx != 0 && declared_ny != 0) {
                            if (declared_nx != expected_nx || declared_ny != expected_ny) {
                                CECE_LOG_ERROR("Grid dimensions nx=" + std::to_string(declared_nx) + ", ny=" + std::to_string(declared_ny) +
                                               " do not match the expected dimensions for Named Grid " + grid_name + " (" +
                                               std::to_string(expected_nx) + "x" + std::to_string(expected_ny) + ")!");
                                return -1;
                            }
                        }
                        nx = expected_nx;
                        ny = expected_ny;
                    } else {
                        CECE_LOG_ERROR(
                            "Only regular Gaussian grids (family 'F', e.g. 'F360') and regular lat-lon grids (family 'R', e.g. 'R360') are currently "
                            "supported as structured CECE target grids.");
                        return -1;
                    }
                } catch (const std::exception& e) {
                    CECE_LOG_ERROR("Failed to parse named grid '" + grid_name + "': " + std::string(e.what()));
                    return -1;
                }
            }
        } else {
            nz = 1;
        }

        CECE_LOG_DEBUG("[DRIVER] Parsed nx = " + std::to_string(nx) + ", ny = " + std::to_string(ny) + ", grid_name = '" + grid_name + "'");

        // B. Simulation Clock Timing
        std::string start_time_str = config["driver"]["start_time"].as<std::string>();
        std::string end_time_str = config["driver"]["end_time"].as<std::string>();
        int timestep_seconds = config["driver"]["timestep_seconds"].as<int>();

        // 3. Initialize TICK Clock
        tick::Gregorian_Calendar cal;
        tick::Time_Point sim_time = cal.to_time_point(tick::parse_iso8601(start_time_str));
        tick::Time_Point end_time = cal.to_time_point(tick::parse_iso8601(end_time_str));
        tick::Duration dt = tick::seconds(timestep_seconds);

        // 4. Initialize the CECE Compute Engine via C-linkage interface
        void* cece_data_ptr = nullptr;
        int rc = 0;

        // Phase 1: Allocate internal structures (StackingEngine, DiagnosticManager)
        cece_core_initialize_p1(&cece_data_ptr, &rc);
        if (rc < 0) {
            cece::LogFatal("[DRIVER FATAL] (rank " + std::to_string(my_rank) + ") cece_core_initialize_p1 failed with rc=" + std::to_string(rc));
            return rc;
        }

        // Realize: Validate and lock configuration
        cece_core_realize(cece_data_ptr, &rc);
        if (rc < 0) {
            cece::LogFatal("[DRIVER FATAL] (rank " + std::to_string(my_rank) + ") cece_core_realize failed with rc=" + std::to_string(rc));
            return rc;
        }

        // Phase 2: Complete grid-binding (dynamically sized)
        cece_core_initialize_p2(cece_data_ptr, &nx, &ny, &nz, &rc);
        if (rc < 0) {
            cece::LogFatal("[DRIVER FATAL] (rank " + std::to_string(my_rank) + ") cece_core_initialize_p2 failed with rc=" + std::to_string(rc));
            return rc;
        }

        // Register the export fields configured for output with persistent
        // memory buffers, via the parsed config — the single authoritative
        // interpretation of output.fields. Data fields only: the collection
        // also carries the writer-managed coordinate variables.
        std::unordered_map<std::string, std::vector<double>> export_fields_mem;
        const cece::CeceConfig parsed_config = cece::ParseConfig(config_file);
        for (const cece::CeceOutputField& field : parsed_config.output_config.fields.GetDataFields()) {
            export_fields_mem[field.name] = std::vector<double>(static_cast<std::size_t>(nx) * ny * nz, 0.0);
            cece_core_set_export_field(cece_data_ptr, field.name.c_str(), static_cast<int>(field.name.length()), export_fields_mem[field.name].data(),
                                       nx, ny, nz, &rc);
            if (rc < 0) {
                cece::LogFatal("[DRIVER FATAL] (rank " + std::to_string(my_rank) + ") cece_core_set_export_field failed for '" + field.name +
                               "' with rc=" + std::to_string(rc));
                return rc;
            }
        }

        // Setup CECE grid coordinate arrays (either generated dynamically from NamedGridRegistry, or calculated uniformly)
        std::vector<double> file_lons(nx, 0.0);
        std::vector<double> file_lats(ny == 1 ? nx : ny, 0.0);
        bool has_file_coords = false;

        if (!grid_name.empty()) {
            try {
                auto mesh = axis::topology::NamedGridRegistry::generate<Kokkos::HostSpace>(grid_name);
                auto coords = mesh.node_coords();
                for (int i = 0; i < nx; ++i) {
                    file_lons[i] = wrap_longitude(coords(i, 0));
                }
                for (int j = 0; j < ny; ++j) {
                    file_lats[j] = coords(j * nx, 1);
                }
                std::sort(file_lons.begin(), file_lons.end());
                std::sort(file_lats.begin(), file_lats.end());
                has_file_coords = true;
            } catch (const std::exception& e) {
                CECE_LOG_ERROR("Failed to retrieve coordinates from named grid '" + grid_name + "': " + std::string(e.what()));
                return -1;
            }
        } else {
            bool loaded_from_file = false;
            bool is_explicit_gridspec = false;
            std::string input_file_path = "";
            if (config["driver"] && config["driver"]["gridspec_file"]) {
                std::string gf = config["driver"]["gridspec_file"].as<std::string>();
                if (!gf.empty() && gf != "none" && gf != "NONE") {
                    input_file_path = gf;
                    is_explicit_gridspec = true;
                }
            }
            if (input_file_path.empty() && config["cece_data"] && config["cece_data"]["streams"]) {
                auto stream = config["cece_data"]["streams"][0];
                if (stream["file"]) {
                    input_file_path = stream["file"].as<std::string>();
                }
            }

            if (!input_file_path.empty()) {
                std::string read_manifest_path = "amio_coord_manifest.yaml";
                std::ofstream m_file_coords(read_manifest_path);
                m_file_coords << "backend: netcdf4\n"
                              << "path: " << input_file_path << "\n"
                              << "data_model: enhanced\n"
                              << "staging_pool:\n"
                              << "  buffer_count: 16\n"
                              << "  buffer_capacity_bytes: 104857600\n"
                              << "worker_pool:\n"
                              << "  threads: 1\n"
                              << "prefetch:\n"
                              << "  depth: 4\n"
                              << "  read_timeout_s: 60\n"
                              << "staging_timeout_ms: 10000\n";
                m_file_coords.close();

                amio_core_handle coord_core = nullptr;
                amio_dataset_handle coord_dataset = nullptr;
                amio_view_handle lon_view = nullptr;
                amio_view_handle lat_view = nullptr;

                amio_status_t amio_rc = amio_init(read_manifest_path.c_str(), &coord_core);
                if (amio_rc != AMIO_OK) {
                    CECE_LOG_ERROR("amio_init failed for coordinate manifest '" + read_manifest_path + "': " + std::string(amio_strerror(amio_rc)));
                } else {
                    amio_rc = amio_open_dataset(coord_core, read_manifest_path.c_str(), AMIO_MODE_READ, &coord_dataset);
                    if (amio_rc != AMIO_OK) {
                        CECE_LOG_ERROR("amio_open_dataset failed for dataset '" + input_file_path + "': " + std::string(amio_strerror(amio_rc)));
                    } else {
                        int file_nx = 0;
                        int file_ny = 0;
                        std::vector<double> file_lon_coords;
                        std::vector<double> file_lat_coords;

                        static const std::vector<std::string> kLonNames = {
                            "grid_lont", "grid_lon", "XLONG",   "lonCell", "geolon",      "clon",          "glamt",  "mesh2d_face_lon", "lon",
                            "longitude", "LON",      "lon_rho", "nav_lon", "mesh_node_x", "mesh2d_node_x", "node_x", "grid_xt",         "x"};
                        bool is_radian = false;
                        amio_status_t lon_status = static_cast<amio_status_t>(-1);
                        for (const auto& name : kLonNames) {
                            lon_status = amio_read(coord_dataset, name.c_str(), 0, nullptr, &lon_view);
                            if (lon_status == AMIO_OK) {
                                if (name == "lonCell" || name == "latCell" || name == "lonVertex" || name == "latVertex") {
                                    is_radian = true;
                                }
                                break;
                            }
                        }

                        if (lon_status == AMIO_OK) {
                            const void* view_data = nullptr;
                            size_t view_size = 0;
                            if (amio_view_data(lon_view, &view_data, &view_size) == AMIO_OK) {
                                amio_shape_t lon_shape{};
                                if (amio_view_shape(lon_view, &lon_shape) == AMIO_OK) {
                                    if (lon_shape.rank == 1) {
                                        file_nx = static_cast<int>(lon_shape.extents[0]);
                                    } else if (lon_shape.rank == 2) {
                                        file_nx = static_cast<int>(lon_shape.extents[1]);
                                    }
                                    int total_len = 1;
                                    for (int r = 0; r < lon_shape.rank; ++r) {
                                        total_len *= static_cast<int>(lon_shape.extents[r]);
                                    }
                                    bool is_float = (view_size == static_cast<size_t>(total_len) * 4);
                                    const float* float_data = static_cast<const float*>(view_data);
                                    const double* double_data = static_cast<const double*>(view_data);
                                    file_lon_coords.resize(total_len);
                                    for (int i = 0; i < total_len; ++i) {
                                        double val = is_float ? static_cast<double>(float_data[i]) : double_data[i];
                                        if (is_radian) {
                                            val = radians_to_degrees(val);
                                        }
                                        file_lon_coords[i] = wrap_longitude(val);
                                    }
                                }
                            }
                            amio_release_view(lon_view);
                        }

                        static const std::vector<std::string> kLatNames = {
                            "grid_latt", "grid_lat", "XLAT",    "latCell", "geolat",      "clat",          "gphit",  "mesh2d_face_lat", "lat",
                            "latitude",  "LAT",      "lat_rho", "nav_lat", "mesh_node_y", "mesh2d_node_y", "node_y", "grid_yt",         "y"};
                        amio_status_t lat_status = static_cast<amio_status_t>(-1);
                        for (const auto& name : kLatNames) {
                            lat_status = amio_read(coord_dataset, name.c_str(), 0, nullptr, &lat_view);
                            if (lat_status == AMIO_OK) {
                                break;
                            }
                        }

                        if (lat_status == AMIO_OK) {
                            const void* view_data = nullptr;
                            size_t view_size = 0;
                            if (amio_view_data(lat_view, &view_data, &view_size) == AMIO_OK) {
                                amio_shape_t lat_shape{};
                                if (amio_view_shape(lat_view, &lat_shape) == AMIO_OK) {
                                    if (lat_shape.rank == 1 || lat_shape.rank == 2) {
                                        file_ny = static_cast<int>(lat_shape.extents[0]);
                                    }
                                    int total_len = 1;
                                    for (int r = 0; r < lat_shape.rank; ++r) {
                                        total_len *= static_cast<int>(lat_shape.extents[r]);
                                    }
                                    bool is_float = (view_size == static_cast<size_t>(total_len) * 4);
                                    const float* float_data = static_cast<const float*>(view_data);
                                    const double* double_data = static_cast<const double*>(view_data);
                                    file_lat_coords.resize(total_len);
                                    for (int j = 0; j < total_len; ++j) {
                                        double val = is_float ? static_cast<double>(float_data[j]) : double_data[j];
                                        if (is_radian) {
                                            val = radians_to_degrees(val);
                                        }
                                        file_lat_coords[j] = val;
                                    }
                                }
                            }
                            amio_release_view(lat_view);
                        }

                        amio_close(coord_dataset);

                        // If nx and ny are not specified in the configuration, dynamically inherit them from the gridspec file
                        if (nx == 0 && file_nx > 0) {
                            nx = file_nx;
                        }
                        if (ny == 0 && file_ny > 0) {
                            ny = (file_ny == file_nx) ? 1 : file_ny;
                        }

                        if (nx == file_nx && (ny == file_ny || (ny == 1 && file_ny == file_nx)) && file_nx > 0 && file_ny > 0) {
                            file_lons = file_lon_coords;
                            file_lats = file_lat_coords;
                            loaded_from_file = true;
                        }
                    }
                    amio_finalize(coord_core);
                }
                std::remove(read_manifest_path.c_str());
            }

            if (is_explicit_gridspec && !loaded_from_file) {
                std::cerr << "FATAL ERROR: Failed to load gridspec coordinates from explicitly specified gridspec file '" << input_file_path << "'"
                          << std::endl;
                return -1;
            }

            if (!loaded_from_file) {
                if (nx <= 0 || ny <= 0) {
                    std::cerr << "ERROR: Grid dimensions (nx, ny) were not specified in driver.grid configuration and could not be determined from "
                                 "input files!"
                              << std::endl;
                    return -1;
                }

                double lon_min = -180.0;
                double lon_max = 180.0;
                double lat_min = -90.0;
                double lat_max = 90.0;

                if (config["driver"] && config["driver"]["grid"]) {
                    auto grid_node = config["driver"]["grid"];
                    lon_min = grid_node["lon_min"].as<double>(-180.0);
                    lon_max = grid_node["lon_max"].as<double>(180.0);
                    lat_min = grid_node["lat_min"].as<double>(-90.0);
                    lat_max = grid_node["lat_max"].as<double>(90.0);
                }

                double dlon = (lon_max - lon_min) / nx;
                double dlat = (lat_max - lat_min) / ny;

                file_lons.resize(nx, 0.0);
                file_lats.resize(ny == 1 ? nx : ny, 0.0);
                for (int i = 0; i < nx; ++i) {
                    file_lons[i] = lon_min + dlon * (i + 0.5);
                }
                for (int j = 0; j < ny; ++j) {
                    file_lats[j] = lat_min + dlat * (j + 0.5);
                }
            }
            has_file_coords = true;
        }

        if (nx <= 0 || ny <= 0 || nz <= 0) {
            std::cerr << "ERROR: Invalid grid dimensions nx=" << nx << ", ny=" << ny << ", nz=" << nz << std::endl;
            return -1;
        }

        // 5. Initialize the cece_driver orchestrator facade
        void* cece_driver_data = nullptr;
        int mpi_comm_f = MPI_Comm_c2f(MPI_COMM_WORLD);
        cece_driver_create(config_file.c_str(), static_cast<int>(config_file.length()), nx, ny, nz, file_lons.data(),
                           static_cast<int>(file_lons.size()), file_lats.data(), static_cast<int>(file_lats.size()), mpi_comm_f, &cece_driver_data,
                           &rc);
        if (rc < 0) {
            cece::LogFatal("[DRIVER FATAL] (rank " + std::to_string(my_rank) + ") cece_driver_create failed with rc=" + std::to_string(rc));
            return rc;
        }

        // Standalone Writer: Initialize output writing if configured
        int writer_comm_f = MPI_Comm_c2f(MPI_COMM_WORLD);
        if (has_file_coords) {
            cece_core_writer_initialize_with_coords(cece_data_ptr, nx, ny, nz, file_lons.data(), static_cast<int>(file_lons.size()), file_lats.data(),
                                                    static_cast<int>(file_lats.size()), start_time_str.c_str(), start_time_str.length(),
                                                    writer_comm_f, &rc);
        } else {
            cece_core_writer_initialize(cece_data_ptr, nx, ny, nz, start_time_str.c_str(), start_time_str.length(), writer_comm_f, &rc);
        }
        if (rc < 0) {
            cece::LogFatal("[DRIVER FATAL] (rank " + std::to_string(my_rank) + ") Writer initialization failed with rc=" + std::to_string(rc));
            return rc;
        }

        if (my_rank == 0) {
            CECE_LOG_INFO("[DRIVER] Initialization completed on " + std::to_string(nx) + "x" + std::to_string(ny) + "x" + std::to_string(nz) +
                          " grid. Entering run loop...");
        }

        // 6. Event-driven simulation run loop
        tick::Time_Point start_time = sim_time;
        int step_index = 0;
        while (sim_time < end_time) {
            tick::Date_Time current_dt = cal.to_date_time(sim_time);

            if (my_rank == 0) {
                CECE_LOG_INFO("[DRIVER] Advancing simulation to: " + tick::format_iso8601(current_dt));
            }

            std::string time_str = tick::format_iso8601(current_dt);

            // A. Let cece_driver handle all offline AMIO reading and AXIS regridding:
            cece_driver_advance_time(cece_driver_data, time_str.c_str(), static_cast<int>(time_str.length()), cece_data_ptr, &rc);
            if (rc < 0) {
                // Emit on both the log (real stdout, all ranks) and stderr so the
                // failure is never lost regardless of how output is captured.
                cece::LogFatal("[DRIVER FATAL] (rank " + std::to_string(my_rank) +
                               ") cece_driver_advance_time failed to ingest data step - aborting simulation!");
                throw std::runtime_error("cece_driver_advance_time failed");
            }

            // B. Execute the CECE Compute Engine
            int hour = current_dt.hour;
            int day_of_week = 1;  // Default Monday/Tuesday
            cece_core_run(cece_data_ptr, hour, day_of_week, &rc);
            if (rc < 0) {
                cece::LogFatal("[DRIVER FATAL] (rank " + std::to_string(my_rank) + ") cece_core_run failed with rc=" + std::to_string(rc));
                throw std::runtime_error("cece_core_run failed");
            }

            // D. Advance simulation clock by one timestep BEFORE writing, so elapsed time reflects the end of the step!
            sim_time += dt;
            step_index++;

            double elapsed_seconds = static_cast<double>((sim_time - start_time).nanos()) / 1e9;

            // C. Write output timestep via standalone writer
            cece_core_write_step(cece_data_ptr, elapsed_seconds, step_index, &rc);
            if (rc < 0) {
                cece::LogFatal("[DRIVER FATAL] (rank " + std::to_string(my_rank) + ") cece_core_write_step failed with rc=" + std::to_string(rc));
                throw std::runtime_error("cece_core_write_step failed");
            }
        }

        // 7. Cleanup and release resources
        if (my_rank == 0) {
            CECE_LOG_INFO("[DRIVER] Standalone execution completed. Cleaning up...");
        }

        cece_driver_destroy(cece_driver_data);
        cece_core_finalize(cece_data_ptr, &rc);
        if (rc < 0) {
            cece::LogFatal("[DRIVER FATAL] (rank " + std::to_string(my_rank) + ") cece_core_finalize failed with rc=" + std::to_string(rc));
            return rc;
        }
    }
    Kokkos::finalize();
    MPI_Finalize();
    return 0;
}
