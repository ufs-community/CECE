// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors

#include "cece/cece_regridder_utils.hpp"

#include <mpi.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

#include "cece/cece_logger.hpp"

namespace cece::io {

static std::vector<double> read_coordinate_array(amio_dataset_handle dataset, const std::string& name, bool is_radian, bool wrap_lon,
                                                 int& num_elements) {
    amio_view_handle view = nullptr;
    if (amio_read(dataset, name.c_str(), 0, nullptr, &view) != AMIO_OK) {
        throw std::runtime_error("Failed to read coordinate variable: " + name);
    }

    const void* data = nullptr;
    size_t size = 0;
    if (amio_view_data(view, &data, &size) != AMIO_OK) {
        amio_release_view(view);
        throw std::runtime_error("Failed to retrieve data pointer for coordinate variable: " + name);
    }

    amio_shape_t shape{};
    if (amio_view_shape(view, &shape) != AMIO_OK) {
        amio_release_view(view);
        throw std::runtime_error("Failed to retrieve shape for coordinate variable: " + name);
    }

    int total_pts = 1;
    for (int r = 0; r < shape.rank; ++r) {
        total_pts *= static_cast<int>(shape.extents[r]);
    }
    num_elements = total_pts;

    std::vector<double> values(total_pts);
    bool is_float = (size == static_cast<size_t>(total_pts) * 4);
    const float* float_data = static_cast<const float*>(data);
    const double* double_data = static_cast<const double*>(data);

    for (int i = 0; i < total_pts; ++i) {
        double val = is_float ? static_cast<double>(float_data[i]) : double_data[i];
        if (is_radian) {
            val *= 180.0 / M_PI;
        }
        if (wrap_lon) {
            if (val >= 180.0)
                val -= 360.0;
            else if (val < -180.0)
                val += 360.0;
        }
        values[i] = val;
    }

    amio_release_view(view);
    return values;
}

static axis::topology::UnstructuredMesh<Kokkos::HostSpace> load_mesh_from_file(int ni, const std::string& gridspec_file) {
    int rank = 0;
    int mpi_initialized = 0;
    MPI_Initialized(&mpi_initialized);
    if (mpi_initialized) {
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    }
    std::string manifest_path = "amio_GS_mesh_manifest_rank" + std::to_string(rank) + ".yaml";
    std::ofstream m_file(manifest_path);
    m_file << "backend: netcdf4\n"
           << "path: " << gridspec_file << "\n"
           << "data_model: enhanced\n"
           << "staging_pool:\n"
           << "  buffer_count: 16\n"
           << "  buffer_capacity_bytes: 104857600\n"
           << "worker_pool:\n"
           << "  threads: 1\n";
    m_file.close();

    amio_core_handle core = nullptr;
    amio_dataset_handle dataset = nullptr;
    amio_view_handle edges_on_cell_view = nullptr;
    amio_view_handle vertices_on_cell_view = nullptr;

    amio_status_t amio_rc = amio_init(manifest_path.c_str(), &core);
    if (amio_rc != AMIO_OK) {
        std::remove(manifest_path.c_str());
        throw std::runtime_error("amio_init failed");
    }

    amio_rc = amio_open_dataset(core, manifest_path.c_str(), AMIO_MODE_READ, &dataset);
    if (amio_rc != AMIO_OK) {
        amio_finalize(core);
        std::remove(manifest_path.c_str());
        throw std::runtime_error("amio_open_dataset failed");
    }

    // A. Try SCRIP-conventions coordinates first
    amio_view_handle scrip_lon_peek = nullptr;
    if (amio_read(dataset, "grid_corner_lon", 0, nullptr, &scrip_lon_peek) == AMIO_OK) {
        amio_release_view(scrip_lon_peek);  // Release peek view

        try {
            int total_lon_pts = 0;
            int total_lat_pts = 0;
            std::vector<double> scrip_lons = read_coordinate_array(dataset, "grid_corner_lon", false, true, total_lon_pts);
            std::vector<double> scrip_lats = read_coordinate_array(dataset, "grid_corner_lat", false, false, total_lat_pts);

            amio_view_handle scrip_view = nullptr;
            int grid_size = 0;
            int grid_corners = 0;
            if (amio_read(dataset, "grid_corner_lon", 0, nullptr, &scrip_view) == AMIO_OK) {
                amio_shape_t shape{};
                if (amio_view_shape(scrip_view, &shape) == AMIO_OK && shape.rank == 2) {
                    grid_size = static_cast<int>(shape.extents[0]);
                    grid_corners = static_cast<int>(shape.extents[1]);
                }
                amio_release_view(scrip_view);
            }

            amio_close(dataset);
            amio_finalize(core);
            std::remove(manifest_path.c_str());

            size_t n_vertices = scrip_lons.size();
            size_t n_cells = grid_size;

            Kokkos::View<double**, Kokkos::LayoutLeft, Kokkos::HostSpace> node_coords("node_coords", n_vertices, 2);
            for (size_t i = 0; i < n_vertices; ++i) {
                node_coords(i, 0) = scrip_lons[i];
                node_coords(i, 1) = scrip_lats[i];
            }

            Kokkos::View<axis::index_t*, Kokkos::HostSpace> conn_offsets("conn_offsets", n_cells + 1);
            Kokkos::View<axis::index_t*, Kokkos::HostSpace> conn_indices("conn_indices", n_vertices);

            for (size_t i = 0; i < n_cells; ++i) {
                conn_offsets(i) = i * grid_corners;
                for (int v = 0; v < grid_corners; ++v) {
                    conn_indices(i * grid_corners + v) = i * grid_corners + v;
                }
            }
            conn_offsets(n_cells) = n_vertices;

            return axis::topology::UnstructuredMesh<Kokkos::HostSpace>(node_coords, conn_offsets, conn_indices,
                                                                       axis::topology::CoordinateSystem::SphericalDeg);
        } catch (const std::exception& e) {
            amio_close(dataset);
            amio_finalize(core);
            std::remove(manifest_path.c_str());
            throw;
        }
    }

    // B. Try MPAS-conventions coordinates
    amio_view_handle lat_vertex_peek = nullptr;
    if (amio_read(dataset, "latVertex", 0, nullptr, &lat_vertex_peek) == AMIO_OK) {
        amio_release_view(lat_vertex_peek);

        try {
            int total_lat_pts = 0;
            int total_lon_pts = 0;
            std::vector<double> lat_vertices = read_coordinate_array(dataset, "latVertex", true, false, total_lat_pts);
            std::vector<double> lon_vertices = read_coordinate_array(dataset, "lonVertex", true, true, total_lon_pts);

            std::vector<int> n_edges_on_cell;
            std::vector<int> vertices_on_cell;
            int n_cells = ni;
            int max_edges = 0;

            if (amio_read(dataset, "nEdgesOnCell", 0, nullptr, &edges_on_cell_view) != AMIO_OK) {
                throw std::runtime_error("Failed to read nEdgesOnCell");
            }
            const void* data_edges = nullptr;
            size_t size_edges = 0;
            if (amio_view_data(edges_on_cell_view, &data_edges, &size_edges) == AMIO_OK) {
                amio_shape_t shape{};
                if (amio_view_shape(edges_on_cell_view, &shape) == AMIO_OK) {
                    int nc = static_cast<int>(shape.extents[0]);
                    n_edges_on_cell.resize(nc);
                    for (int i = 0; i < nc; ++i) {
                        n_edges_on_cell[i] = static_cast<const int*>(data_edges)[i];
                    }
                }
            }
            amio_release_view(edges_on_cell_view);

            if (amio_read(dataset, "verticesOnCell", 0, nullptr, &vertices_on_cell_view) != AMIO_OK) {
                throw std::runtime_error("Failed to read verticesOnCell");
            }
            const void* data_verts = nullptr;
            size_t size_verts = 0;
            if (amio_view_data(vertices_on_cell_view, &data_verts, &size_verts) == AMIO_OK) {
                amio_shape_t shape{};
                if (amio_view_shape(vertices_on_cell_view, &shape) == AMIO_OK) {
                    max_edges = static_cast<int>(shape.extents[1]);
                    int nc = static_cast<int>(shape.extents[0]);
                    vertices_on_cell.resize(static_cast<size_t>(nc) * static_cast<size_t>(max_edges));
                    for (int i = 0; i < nc * max_edges; ++i) {
                        vertices_on_cell[i] = static_cast<const int*>(data_verts)[i];
                    }
                }
            }
            amio_release_view(vertices_on_cell_view);

            amio_close(dataset);
            amio_finalize(core);
            std::remove(manifest_path.c_str());

            size_t n_vertices = lat_vertices.size();

            Kokkos::View<double**, Kokkos::LayoutLeft, Kokkos::HostSpace> node_coords("node_coords", n_vertices, 2);
            for (size_t i = 0; i < n_vertices; ++i) {
                node_coords(i, 0) = lon_vertices[i];
                node_coords(i, 1) = lat_vertices[i];
            }

            size_t total_conn = 0;
            for (int i = 0; i < n_cells; ++i) {
                total_conn += n_edges_on_cell[i];
            }

            Kokkos::View<axis::index_t*, Kokkos::HostSpace> conn_offsets("conn_offsets", n_cells + 1);
            Kokkos::View<axis::index_t*, Kokkos::HostSpace> conn_indices("conn_indices", total_conn);

            size_t offset = 0;
            for (int i = 0; i < n_cells; ++i) {
                conn_offsets(i) = offset;
                int n_edges = n_edges_on_cell[i];
                for (int v = 0; v < n_edges; ++v) {
                    int v_idx = vertices_on_cell[i * max_edges + v];
                    if (v_idx > 0 && v_idx <= static_cast<int>(n_vertices)) {
                        conn_indices(offset + v) = v_idx - 1;
                    } else {
                        conn_indices(offset + v) = 0;
                    }
                }
                offset += n_edges;
            }
            conn_offsets(n_cells) = offset;

            return axis::topology::UnstructuredMesh<Kokkos::HostSpace>(node_coords, conn_offsets, conn_indices,
                                                                       axis::topology::CoordinateSystem::SphericalDeg);
        } catch (const std::exception& e) {
            amio_close(dataset);
            amio_finalize(core);
            std::remove(manifest_path.c_str());
            throw;
        }
    }

    amio_close(dataset);
    amio_finalize(core);
    std::remove(manifest_path.c_str());
    throw std::runtime_error("Unsupported gridspec mesh topology convention (neither SCRIP nor MPAS/UGRID found)");
}

axis::topology::UnstructuredMesh<Kokkos::HostSpace> build_axis_mesh(int ni, int nj, const std::vector<double>& lons, const std::vector<double>& lats,
                                                                    const std::string& gridspec_file) {
    if (nj == 1 && !gridspec_file.empty() && gridspec_file != "none" && gridspec_file != "NONE") {
        try {
            return load_mesh_from_file(ni, gridspec_file);
        } catch (const std::exception& e) {
            std::cerr << "WARNING: build_axis_mesh failed to load from gridspec_file '" << gridspec_file << "': " << e.what()
                      << ". Falling back to dynamic fallback grid." << std::endl;
        }
    }

    if (nj == 1) {
        // Build unstructured mesh of ni quadrilaterals dynamically to support nj = 1 in standalone driver
        size_t n_cells = static_cast<size_t>(ni);
        size_t n_nodes = 4 * n_cells;

        Kokkos::View<double**, Kokkos::LayoutLeft, Kokkos::HostSpace> node_coords("node_coords", n_nodes, 2);
        Kokkos::View<axis::index_t*, Kokkos::HostSpace> conn_offsets("conn_offsets", n_cells + 1);
        Kokkos::View<axis::index_t*, Kokkos::HostSpace> conn_indices("conn_indices", 4 * n_cells);

        // Dynamically compute longitude cell spacing based on adjacent cells
        std::vector<double> dlons(n_cells, 0.0);
        if (n_cells > 1) {
            dlons[0] = std::abs(lons[1] - lons[0]);
            for (size_t i = 1; i < n_cells - 1; ++i) {
                dlons[i] = 0.5 * (std::abs(lons[i] - lons[i - 1]) + std::abs(lons[i + 1] - lons[i]));
            }
            dlons[n_cells - 1] = std::abs(lons[n_cells - 1] - lons[n_cells - 2]);
        } else {
            dlons[0] = 360.0;
        }

        for (size_t i = 0; i < n_cells; ++i) {
            double lon = lons[i];
            double lat = lats[i];

            double dlon_i = dlons[i];
            double cos_lat = std::cos(lat * M_PI / 180.0);
            if (cos_lat < 1e-3) cos_lat = 1e-3;  // safeguard near poles
            double dy_i = dlon_i * cos_lat;

            double x0 = lon - 0.5 * dlon_i;
            double x1 = lon + 0.5 * dlon_i;
            double y0 = lat - 0.5 * dy_i;
            double y1 = lat + 0.5 * dy_i;

            // clamp latitude to sphere bounds
            if (y0 < -90.0) y0 = -90.0;
            if (y1 > 90.0) y1 = 90.0;

            node_coords(4 * i + 0, 0) = x0;  // lon
            node_coords(4 * i + 0, 1) = y0;  // lat

            node_coords(4 * i + 1, 0) = x1;
            node_coords(4 * i + 1, 1) = y0;

            node_coords(4 * i + 2, 0) = x1;
            node_coords(4 * i + 2, 1) = y1;

            node_coords(4 * i + 3, 0) = x0;
            node_coords(4 * i + 3, 1) = y1;

            conn_offsets(i) = 4 * i;

            conn_indices(4 * i + 0) = 4 * i + 0;
            conn_indices(4 * i + 1) = 4 * i + 1;
            conn_indices(4 * i + 2) = 4 * i + 2;
            conn_indices(4 * i + 3) = 4 * i + 3;
        }
        conn_offsets(n_cells) = 4 * n_cells;

        return axis::topology::UnstructuredMesh<Kokkos::HostSpace>(node_coords, conn_offsets, conn_indices,
                                                                   axis::topology::CoordinateSystem::SphericalDeg);
    }

    size_t n_cells = static_cast<size_t>(ni) * nj;
    Kokkos::View<double*, Kokkos::HostSpace> center_lon("center_lon", n_cells);
    Kokkos::View<double*, Kokkos::HostSpace> center_lat("center_lat", n_cells);

    bool curvilinear = (lons.size() == n_cells && lats.size() == n_cells);

    for (int j = 0; j < nj; ++j) {
        for (int i = 0; i < ni; ++i) {
            size_t idx = static_cast<size_t>(j) * ni + i;
            if (curvilinear) {
                center_lon(idx) = lons[idx];
                center_lat(idx) = lats[idx];
            } else {
                center_lon(idx) = lons[i];
                center_lat(idx) = lats[j];
            }
        }
    }

    axis::topology::StructuredGrid<Kokkos::HostSpace> grid(ni, nj, center_lon, center_lat, axis::topology::CoordinateSystem::SphericalDeg);

    return grid.to_unstructured();
}

namespace {

bool coordinate_at(const std::vector<double>& values, int nx, int ny, int i, int j, bool longitude, double& value) {
    const size_t ncell = static_cast<size_t>(nx) * ny;
    if (values.size() == ncell) {
        value = values[static_cast<size_t>(j) * nx + i];
        return true;
    }
    if (longitude && values.size() == static_cast<size_t>(nx)) {
        value = values[i];
        return true;
    }
    if (!longitude && values.size() == static_cast<size_t>(ny)) {
        value = values[j];
        return true;
    }
    return false;
}

bool coordinates_equal(double source, double target, bool longitude, double tolerance) {
    if (!std::isfinite(source) || !std::isfinite(target)) {
        return false;
    }
    double difference = source - target;
    if (longitude) {
        difference = std::fmod(difference, 360.0);
        if (difference > 180.0) {
            difference -= 360.0;
        } else if (difference < -180.0) {
            difference += 360.0;
        }
    }
    return std::abs(difference) <= tolerance;
}

}  // namespace

bool same_spherical_grid_coordinates(int nx, int ny, const std::vector<double>& source_lons, const std::vector<double>& source_lats,
                                     const std::vector<double>& target_lons, const std::vector<double>& target_lats, double tolerance) {
    if (nx <= 0 || ny <= 0 || tolerance < 0.0) {
        return false;
    }

    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            double source_lon = 0.0;
            double source_lat = 0.0;
            double target_lon = 0.0;
            double target_lat = 0.0;
            if (!coordinate_at(source_lons, nx, ny, i, j, true, source_lon) || !coordinate_at(source_lats, nx, ny, i, j, false, source_lat) ||
                !coordinate_at(target_lons, nx, ny, i, j, true, target_lon) || !coordinate_at(target_lats, nx, ny, i, j, false, target_lat) ||
                !coordinates_equal(source_lon, target_lon, true, tolerance) || !coordinates_equal(source_lat, target_lat, false, tolerance)) {
                return false;
            }
        }
    }
    return true;
}

bool build_regrid_plan(amio_dataset_handle read_dataset, int nx, int ny, const std::vector<double>& target_lons,
                       const std::vector<double>& target_lats, const std::string& map_algo, int j0, int j1, const std::string& gridspec_file,
                       RegridPlan& plan) {
    // Read a 1-D, 2-D or 3-D coordinate variable, trying several common naming conventions.
    auto read_coord = [&](const std::vector<std::string>& candidate_names, std::vector<double>& out, int& nx_val, int& ny_val, bool wrap_lon) {
        for (const auto& name : candidate_names) {
            amio_view_handle view = nullptr;
            if (amio_read(read_dataset, name.c_str(), 0, nullptr, &view) != AMIO_OK) {
                continue;
            }
            const void* data = nullptr;
            size_t size = 0;
            if (amio_view_data(view, &data, &size) == AMIO_OK) {
                amio_shape_t shape{};
                if (amio_view_shape(view, &shape) == AMIO_OK && shape.rank > 0) {
                    int len = 1;
                    for (int r = 0; r < shape.rank; ++r) {
                        len *= static_cast<int>(shape.extents[r]);
                    }

                    int slice_len = 0;
                    if (shape.rank == 1) {
                        nx_val = static_cast<int>(shape.extents[0]);
                        ny_val = 1;
                        slice_len = nx_val;
                    } else if (shape.rank == 2) {
                        ny_val = static_cast<int>(shape.extents[0]);
                        nx_val = static_cast<int>(shape.extents[1]);
                        slice_len = ny_val * nx_val;
                    } else if (shape.rank == 3) {
                        // For 3-D time-dependent coordinate arrays (e.g. WRF XLONG(Time, Y, X)),
                        // read only the first time slice.
                        ny_val = static_cast<int>(shape.extents[1]);
                        nx_val = static_cast<int>(shape.extents[2]);
                        slice_len = ny_val * nx_val;
                    } else {
                        nx_val = 0;
                        ny_val = 0;
                        slice_len = 0;
                    }

                    if (slice_len > 0) {
                        out.resize(slice_len);
                        bool is_float = (size == static_cast<size_t>(len) * 4);
                        for (int i = 0; i < slice_len; ++i) {
                            double val = is_float ? static_cast<double>(static_cast<const float*>(data)[i]) : static_cast<const double*>(data)[i];
                            if (wrap_lon) {
                                if (val >= 180.0) {
                                    val -= 360.0;
                                } else if (val < -180.0) {
                                    val += 360.0;
                                }
                            }
                            out[i] = val;
                        }
                    }
                }
            }
            amio_release_view(view);
            if (!out.empty()) {
                return;
            }
        }
    };

    // 1. Read source longitude coordinates. Candidate names cover common CF,
    //    UGRID, and UFS/FV3/MPAS conventions.
    //      - CF / generic:  lon, longitude, x, Longitude, LON
    //      - UGRID mesh:    mesh_node_x, mesh2d_node_x, node_x
    //      - UFS/FV3:       grid_xt, grid_lont, geolon, lon_rho, nav_lon
    //      - MPAS:          lonCell
    static const std::vector<std::string> kLonNames = {"grid_lont", "grid_lon",        "XLONG",         "lonCell",   "geolon",  "clon",
                                                       "glamt",     "mesh2d_face_lon", "lon",           "longitude", "LON",     "lon_rho",
                                                       "nav_lon",   "mesh_node_x",     "mesh2d_node_x", "node_x",    "grid_xt", "x"};
    int lon_nx = 0, lon_ny = 0;
    std::vector<double> src_lons;
    read_coord(kLonNames, src_lons, lon_nx, lon_ny, false);

    // 2. Read source latitude coordinates (same convention families as above).
    static const std::vector<std::string> kLatNames = {"grid_latt", "grid_lat",        "XLAT",          "latCell",  "geolat",  "clat",
                                                       "gphit",     "mesh2d_face_lat", "lat",           "latitude", "LAT",     "lat_rho",
                                                       "nav_lat",   "mesh_node_y",     "mesh2d_node_y", "node_y",   "grid_yt", "y"};
    int lat_nx = 0, lat_ny = 0;
    std::vector<double> src_lats;
    read_coord(kLatNames, src_lats, lat_nx, lat_ny, false);

    if (src_lons.empty() || src_lats.empty()) {
        std::cerr << "[DRIVER ERROR] build_regrid_plan: could not read source coordinates. Tried longitude names {"
                  << "lon, longitude, x, geolon, grid_xt, grid_lont, lon_rho, nav_lon, lonCell, mesh_node_x, ...} and matching "
                  << "latitude names. src_lons=" << src_lons.size() << ", src_lats=" << src_lats.size() << std::endl;
        return false;
    }

    bool lon_is_curv = (lon_ny > 1);
    bool lat_is_curv = (lat_ny > 1);

    if (lon_is_curv && lat_is_curv) {
        if (lon_nx != lat_nx || lon_ny != lat_ny) {
            std::ostringstream oss;
            oss << "build_regrid_plan: Mismatched curvilinear coordinate dimensions! "
                << "Longitude: " << lon_nx << "x" << lon_ny << ", "
                << "Latitude: " << lat_nx << "x" << lat_ny;
            throw std::runtime_error(oss.str());
        }
        plan.file_nx = lon_nx;
        plan.file_ny = lon_ny;
    } else if (lon_is_curv) {
        plan.file_nx = lon_nx;
        plan.file_ny = lon_ny;
    } else if (lat_is_curv) {
        plan.file_nx = lat_nx;
        plan.file_ny = lat_ny;
    } else {
        plan.file_nx = lon_nx;
        plan.file_ny = lat_nx;
    }

    {
        double min_lon = *std::min_element(src_lons.begin(), src_lons.end());
        double max_lon = *std::max_element(src_lons.begin(), src_lons.end());
        double min_lat = *std::min_element(src_lats.begin(), src_lats.end());
        double max_lat = *std::max_element(src_lats.begin(), src_lats.end());
        std::cout << "[DRIVER DEBUG] AMIO retrieved source coordinates successfully! "
                  << "file_nx=" << plan.file_nx << ", file_ny=" << plan.file_ny << ", "
                  << "lon_range=[" << min_lon << ", " << max_lon << "], "
                  << "lat_range=[" << min_lat << ", " << max_lat << "]" << std::endl;
    }

    plan.j0 = j0;
    plan.j1 = j1;

    if (map_algo == "passthrough") {
        if (nx != plan.file_nx || ny != plan.file_ny) {
            CECE_LOG_ERROR(
                "[DRIVER ERROR] passthrough regridding requested but grid dimensions do not match! Source grid: " + std::to_string(plan.file_nx) +
                "x" + std::to_string(plan.file_ny) + ", Target grid: " + std::to_string(nx) + "x" + std::to_string(ny));
            throw std::runtime_error("passthrough regridding dimension mismatch");
        }
        if (!same_spherical_grid_coordinates(nx, ny, src_lons, src_lats, target_lons, target_lats)) {
            throw std::runtime_error("passthrough regridding coordinate mismatch");
        }

        plan.identity = true;
        plan.built = true;
        CECE_LOG_INFO(
            "[DRIVER] passthrough verified identical source and target coordinates; "
            "skipping AXIS regridding");
        return true;
    }

    const int nband = j1 - j0;
    if (nband <= 0) {
        // No destination rows assigned to this rank — nothing to build.
        plan.built = true;
        return true;
    }

    // A. Build the (global) source mesh and the rank-local destination sub-mesh.
    auto src_mesh = build_axis_mesh(plan.file_nx, plan.file_ny, src_lons, src_lats);

    std::vector<double> band_lons;
    std::vector<double> band_lats;

    if (target_lons.size() == static_cast<size_t>(nx) * ny && ny > 1) {
        // Curvilinear coordinate arrays: slice [j0 * nx, j1 * nx] for both axes
        band_lons.assign(target_lons.begin() + static_cast<size_t>(j0) * nx, target_lons.begin() + static_cast<size_t>(j1) * nx);
        band_lats.assign(target_lats.begin() + static_cast<size_t>(j0) * nx, target_lats.begin() + static_cast<size_t>(j1) * nx);
    } else {
        // Rectilinear coordinate arrays: slice [j0, j1] for latitude, keep lons as-is
        band_lons = target_lons;
        band_lats.assign(target_lats.begin() + j0, target_lats.begin() + j1);
    }

    // Align the longitude range of the destination tile with the range of the source file.
    // This keeps the source grid perfectly monotonic (avoiding any non-monotonic coordinate jumps
    // or StructuredGrid distortions inside AXIS) and prevents disjoint coordinate range errors.
    double src_min_lon = *std::min_element(src_lons.begin(), src_lons.end());
    double src_max_lon = *std::max_element(src_lons.begin(), src_lons.end());
    bool use_360_range = (src_max_lon > 180.0 && src_min_lon >= -1e-5);

    if (use_360_range) {
        for (auto& lon : band_lons) {
            if (lon < 0.0)
                lon += 360.0;
            else if (lon >= 360.0)
                lon -= 360.0;
        }
    } else {
        for (auto& lon : band_lons) {
            if (lon >= 180.0)
                lon -= 360.0;
            else if (lon < -180.0)
                lon += 360.0;
        }
    }

    auto dst_mesh = build_axis_mesh(nx, nband, band_lons, band_lats, gridspec_file);

    // B. Configure weight generation method.
    axis::solver::RegridConfig regrid_cfg;
    regrid_cfg.method = axis::solver::InterpolationMethod::Conservative1stOrder;
    if (map_algo == "nearest" || map_algo == "near" || map_algo == "nn") {
        regrid_cfg.method = axis::solver::InterpolationMethod::NearestNeighbor;
    } else if (map_algo == "bilinear" || map_algo == "bilin" || map_algo == "bi") {
        regrid_cfg.method = axis::solver::InterpolationMethod::Bilinear;
    } else if (map_algo == "cubic" || map_algo == "bicubic" || map_algo == "cu") {
        regrid_cfg.method = axis::solver::InterpolationMethod::Bicubic;
    } else if (map_algo == "conss" || map_algo == "conservative2nd" || map_algo == "cons2nd") {
        regrid_cfg.method = axis::solver::InterpolationMethod::Conservative2ndOrder;
    } else if (map_algo == "consd" || map_algo == "conservative" || map_algo == "cons" || map_algo == "conservative1st") {
        regrid_cfg.method = axis::solver::InterpolationMethod::Conservative1stOrder;
    }
    regrid_cfg.norm_type = axis::solver::NormType::DstArea;
    regrid_cfg.unmapped = axis::solver::UnmappedAction::Ignore;

    // C. Generate the sparse weight matrix once and convert to CSR for fast apply.
    plan.matrix = axis::solver::WeightGenerator::generate<Kokkos::HostSpace>(src_mesh, dst_mesh, regrid_cfg);
    plan.matrix.to_csr();
    plan.built = true;
    return true;
}

bool apply_regrid_plan(const RegridPlan& plan, size_t time_offset, bool is_float, const void* view_data, int file_nx, int file_ny, int nx,
                       std::vector<double>& local_dst) {
    const int nband = plan.j1 - plan.j0;
    local_dst.assign(static_cast<size_t>(nx) * std::max(nband, 0), 0.0);
    if (nband <= 0) {
        return true;  // No rows on this rank.
    }

    if (plan.identity) {
        if (file_nx != plan.file_nx || file_ny != plan.file_ny || nx != file_nx || plan.j0 < 0 || plan.j1 > file_ny) {
            CECE_LOG_ERROR("[DRIVER ERROR] identity-plan field dimensions do not match the verified source grid");
            return false;
        }
        const float* float_data = static_cast<const float*>(view_data);
        const double* double_data = static_cast<const double*>(view_data);
        for (int local_j = 0; local_j < nband; ++local_j) {
            const int source_j = plan.j0 + local_j;
            for (int i = 0; i < nx; ++i) {
                const size_t source_index = time_offset + static_cast<size_t>(source_j) * file_nx + i;
                const size_t destination_index = static_cast<size_t>(local_j) * nx + i;
                local_dst[destination_index] = is_float ? static_cast<double>(float_data[source_index]) : double_data[source_index];
            }
        }
        return true;
    }

    // D. Prepare the (global) source field view [file_nx * file_ny].
    Kokkos::View<double*, Kokkos::HostSpace> src_field("src_field", static_cast<size_t>(file_nx) * file_ny);
    const float* float_data = static_cast<const float*>(view_data);
    const double* double_data = static_cast<const double*>(view_data);
    for (int j = 0; j < file_ny; ++j) {
        for (int i = 0; i < file_nx; ++i) {
            size_t src_idx = time_offset + static_cast<size_t>(j) * file_nx + i;
            src_field(static_cast<size_t>(j) * file_nx + i) = is_float ? static_cast<double>(float_data[src_idx]) : double_data[src_idx];
        }
    }

    // E. Apply cached weights to produce the rank-local destination band [nx * nband].
    Kokkos::View<double*, Kokkos::HostSpace> dst_field("dst_field", static_cast<size_t>(nx) * nband);
    axis::field_view<const double, 1> src_view(src_field.data(), static_cast<size_t>(file_nx) * file_ny);
    axis::field_view<double, 1> dst_view(dst_field.data(), static_cast<size_t>(nx) * nband);
    axis::solver::apply(plan.matrix, src_view, dst_view);

    double src_sum = 0.0;
    for (size_t k = 0; k < src_field.extent(0); ++k) src_sum += src_field(k);
    double dst_sum = 0.0;
    for (size_t k = 0; k < dst_field.extent(0); ++k) dst_sum += dst_field(k);
    std::cout << "[DEBUG REGRID] src_sum: " << src_sum << ", dst_sum: " << dst_sum << std::endl;

    for (size_t k = 0; k < static_cast<size_t>(nx) * nband; ++k) {
        local_dst[k] = dst_field(k);
    }
    return true;
}

}  // namespace cece::io
