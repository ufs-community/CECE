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

static axis::topology::UnstructuredMesh<Kokkos::HostSpace> load_mesh_from_file(int ni, int nj, const std::string& gridspec_file) {
    // Build the gridspec manifest in memory and pass it directly to AMIO. Writing a
    // per-rank manifest file to a shared-disk workdir (e.g. Lustre) is unnecessary and
    // leaves stray files behind; the in-memory API avoids both the I/O and any race.
    std::ostringstream manifest;
    manifest << "backend: netcdf4\n"
             << "path: " << gridspec_file << "\n"
             << "data_model: enhanced\n"
             << "staging_pool:\n"
             << "  buffer_count: 16\n"
             << "  buffer_capacity_bytes: 33554432\n"
             << "worker_pool:\n"
             << "  threads: 1\n";
    const std::string manifest_content = manifest.str();

    amio_core_handle core = nullptr;
    amio_dataset_handle dataset = nullptr;
    amio_view_handle edges_on_cell_view = nullptr;
    amio_view_handle vertices_on_cell_view = nullptr;

    amio_status_t amio_rc = amio_init_from_string(manifest_content.c_str(), "yaml", &core);
    if (amio_rc != AMIO_OK) {
        throw std::runtime_error("amio_init_from_string failed");
    }

    amio_rc = amio_open_dataset_from_string(core, manifest_content.c_str(), "yaml", AMIO_MODE_READ, &dataset);
    if (amio_rc != AMIO_OK) {
        amio_finalize(core);
        throw std::runtime_error("amio_open_dataset_from_string failed");
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
            throw;
        }
    }

    // C. Try CF-conventions coordinates
    std::string cf_lon_name = "";
    std::string cf_lat_name = "";
    amio_shape_t dummy_shape{};
    int64_t dummy_ts = 0;

    // 1. Determine the longitude variable name
    if (amio_describe(dataset, "lon", &dummy_shape, &dummy_ts) == AMIO_OK) {
        cf_lon_name = "lon";
    } else if (amio_describe(dataset, "longitude", &dummy_shape, &dummy_ts) == AMIO_OK) {
        cf_lon_name = "longitude";
    }

    // 2. Determine the latitude variable name
    if (amio_describe(dataset, "lat", &dummy_shape, &dummy_ts) == AMIO_OK) {
        cf_lat_name = "lat";
    } else if (amio_describe(dataset, "latitude", &dummy_shape, &dummy_ts) == AMIO_OK) {
        cf_lat_name = "latitude";
    }

    // If both coordinate variables were found, proceed with CF parsing
    if (!cf_lon_name.empty() && !cf_lat_name.empty()) {
        try {
            // Helper lambda to execute the two-call amio_get_var_attribute_text pattern
            auto get_bounds_name = [&](const std::string& var_name, const std::string& fallback) {
                size_t out_len = 0;
                // First pass: request length by passing NULL for out_buf
                if (amio_get_var_attribute_text(dataset, var_name.c_str(), "bounds", nullptr, 0, &out_len) == AMIO_OK) {
                    // Second pass: allocate buffer (out_len + 1 for NUL-terminator) and read
                    std::vector<char> buf(out_len + 1, '\0');
                    if (amio_get_var_attribute_text(dataset, var_name.c_str(), "bounds", buf.data(), buf.size(), &out_len) == AMIO_OK) {
                        return std::string(buf.data());
                    }
                }
                return fallback;  // Return standard fallback if attribute is missing
            };

            // 3. Query the 'bounds' attribute for both coordinates
            std::string lon_bounds_name = get_bounds_name(cf_lon_name, "lon_bnds");
            std::string lat_bounds_name = get_bounds_name(cf_lat_name, "lat_bnds");

            // 4. Read the boundary arrays using the names we just resolved
            int total_lon_bnd_pts = 0;
            int total_lat_bnd_pts = 0;
            std::vector<double> cf_lon_bnds = read_coordinate_array(dataset, lon_bounds_name, false, true, total_lon_bnd_pts);
            std::vector<double> cf_lat_bnds = read_coordinate_array(dataset, lat_bounds_name, false, false, total_lat_bnd_pts);

            amio_close(dataset);
            amio_finalize(core);

            // 5. Determine if bounds are 1D rectilinear (2 pts) or 2D explicitly defined (4 pts)
            int grid_corners = 4;
            size_t n_cells = 0;
            size_t n_vertices = 0;
            bool is_1d_rectilinear = false;

            // If lengths match and are divisible by 4, assume 2D explicit corners
            if (total_lon_bnd_pts == total_lat_bnd_pts && total_lon_bnd_pts % 4 == 0) {
                n_cells = total_lon_bnd_pts / 4;
                n_vertices = total_lon_bnd_pts;
            }
            // If they are divisible by 2, they are 1D bounds (e.g., size nx*2 and ny*2)
            else if (total_lon_bnd_pts % 2 == 0 && total_lat_bnd_pts % 2 == 0) {
                is_1d_rectilinear = true;
                size_t nx = total_lon_bnd_pts / 2;
                size_t ny = total_lat_bnd_pts / 2;
                n_cells = nx * ny;
                n_vertices = n_cells * 4;
            } else {
                throw std::runtime_error("Unsupported CF bounds array dimensions.");
            }

            Kokkos::View<double**, Kokkos::LayoutLeft, Kokkos::HostSpace> node_coords("node_coords", n_vertices, 2);
            Kokkos::View<axis::index_t*, Kokkos::HostSpace> conn_offsets("conn_offsets", n_cells + 1);
            Kokkos::View<axis::index_t*, Kokkos::HostSpace> conn_indices("conn_indices", n_vertices);

            // 6. Populate the mesh based on the detected layout
            if (is_1d_rectilinear) {
                size_t nx = total_lon_bnd_pts / 2;
                size_t ny = total_lat_bnd_pts / 2;

                for (size_t j = 0; j < ny; ++j) {
                    for (size_t i = 0; i < nx; ++i) {
                        size_t cell_idx = j * nx + i;
                        conn_offsets(cell_idx) = cell_idx * grid_corners;

                        double lon_min = cf_lon_bnds[i * 2];
                        double lon_max = cf_lon_bnds[i * 2 + 1];
                        double lat_min = cf_lat_bnds[j * 2];
                        double lat_max = cf_lat_bnds[j * 2 + 1];

                        size_t base_v = cell_idx * 4;

                        // Construct 4 corners counterclockwise
                        node_coords(base_v + 0, 0) = lon_min;
                        node_coords(base_v + 0, 1) = lat_min;
                        node_coords(base_v + 1, 0) = lon_max;
                        node_coords(base_v + 1, 1) = lat_min;
                        node_coords(base_v + 2, 0) = lon_max;
                        node_coords(base_v + 2, 1) = lat_max;
                        node_coords(base_v + 3, 0) = lon_min;
                        node_coords(base_v + 3, 1) = lat_max;

                        for (int v = 0; v < 4; ++v) {
                            conn_indices(base_v + v) = base_v + v;
                        }
                    }
                }
            } else {
                // Existing explicitly defined 4-corner logic
                for (size_t i = 0; i < n_cells; ++i) {
                    conn_offsets(i) = i * grid_corners;
                    for (int v = 0; v < grid_corners; ++v) {
                        size_t v_idx = i * grid_corners + v;
                        node_coords(v_idx, 0) = cf_lon_bnds[v_idx];
                        node_coords(v_idx, 1) = cf_lat_bnds[v_idx];
                        conn_indices(v_idx) = v_idx;
                    }
                }
            }
            conn_offsets(n_cells) = n_vertices;

            amio_close(dataset);
            amio_finalize(core);

            return axis::topology::UnstructuredMesh<Kokkos::HostSpace>(node_coords, conn_offsets, conn_indices,
                                                                       axis::topology::CoordinateSystem::SphericalDeg);

        } catch (const std::exception& e) {
            // Resource cleanup is important if an exception gets thrown inside the try-block
            amio_close(dataset);
            amio_finalize(core);
            throw;
        }
    }

    amio_close(dataset);
    amio_finalize(core);
    throw std::runtime_error("Unsupported gridspec mesh topology convention (neither SCRIP nor MPAS/UGRID nor CF found)");
}

axis::topology::UnstructuredMesh<Kokkos::HostSpace> build_axis_mesh(int ni, int nj, const std::vector<double>& lons, const std::vector<double>& lats,
                                                                    const std::string& gridspec_file, const std::string& map_algo) {
    if (!gridspec_file.empty() && gridspec_file != "none" && gridspec_file != "NONE") {
        try {
            return load_mesh_from_file(ni, nj, gridspec_file);
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

    // Ensure that for conservative mapping, the center lat/lon coordinates have constant spacing,
    // since to_unstructured relies on this to calculate grid corners.
    if (map_algo == "consd" || map_algo == "conservative" || map_algo == "cons" || map_algo == "consf" || map_algo == "conservative1st" ||
        map_algo == "conss" || map_algo == "conservative2nd" || map_algo == "cons2nd" || map_algo == "consf") {
        // Validate constant spacing since to_unstructured relies on it to calculate grid corners
        bool constant_spacing = true;
        const double tol = 1e-5;

        if (!curvilinear) {
            // Fast path for 1D rectilinear coordinate arrays: O(ni + nj)
            if (ni > 1) {
                double dlon = lons[1] - lons[0];
                for (int i = 2; i < ni; ++i) {
                    if (std::abs((lons[i] - lons[i - 1]) - dlon) > tol) {
                        constant_spacing = false;
                        break;
                    }
                }
            }
            if (nj > 1 && constant_spacing) {
                double dlat = lats[1] - lats[0];
                for (int j = 2; j < nj; ++j) {
                    if (std::abs((lats[j] - lats[j - 1]) - dlat) > tol) {
                        constant_spacing = false;
                        break;
                    }
                }
            }
        } else {
            // Slow path for fully expanded 2D curvilinear grids: O(ni * nj)
            if (ni > 1) {
                double dlon = center_lon(1) - center_lon(0);
                for (int j = 0; j < nj && constant_spacing; ++j) {
                    for (int i = 1; i < ni; ++i) {
                        size_t idx = static_cast<size_t>(j) * ni + i;
                        if (std::abs((center_lon(idx) - center_lon(idx - 1)) - dlon) > tol) {
                            constant_spacing = false;
                            break;
                        }
                    }
                }
            }
            if (nj > 1 && constant_spacing) {
                double dlat = center_lat(ni) - center_lat(0);
                for (int j = 1; j < nj && constant_spacing; ++j) {
                    for (int i = 0; i < ni; ++i) {
                        size_t idx = static_cast<size_t>(j) * ni + i;
                        size_t prev_idx = static_cast<size_t>(j - 1) * ni + i;
                        if (std::abs((center_lat(idx) - center_lat(prev_idx)) - dlat) > tol) {
                            constant_spacing = false;
                            break;
                        }
                    }
                }
            }
        }

        if (!constant_spacing) {
            throw std::runtime_error(
                "Dynamic unstructured grid fallback requires center lat and lon coordinates to have constant spacing. \
                Please provide a gridspec_file for non-uniform grids or use a different mapping algorithm.");
        }
    }

    axis::topology::StructuredGrid<Kokkos::HostSpace> grid(ni, nj, center_lon, center_lat, axis::topology::CoordinateSystem::SphericalDeg);

    return grid.to_unstructured();
}

// Build the destination sub-mesh for the rectilinear band [j0, j1) with
// GLOBALLY-CONSISTENT corners. StructuredGrid::synthesize_corners would
// extrapolate the band's outer latitude edges from band-local centers only,
// which is correct on a uniform grid but WRONG on a non-uniform latitude grid
// (the true band-boundary edge is the midpoint to the neighbour row owned by an
// adjacent rank). Rather than reimplement the edge geometry here (which can
// drift from AXIS), this expands the rectilinear centers to the FULL global
// nx x ny grid and delegates to AXIS's single shared corner-synthesis kernel
// (axis::topology::synthesize_band_corners) — the SAME 2x2 midpoint / periodic-
// wrap / one-sided-boundary logic the global mesh uses — restricted to the
// band's corner rows [j0, j1]. The result is provably identical to rows
// [j0, j1] of the global mesh's synthesized corners, so the band conservative
// regrid matches the corresponding global rows at the seams and a whole-grid
// (single-rank) band is byte-for-byte the global mesh.
axis::topology::UnstructuredMesh<Kokkos::HostSpace> build_band_mesh_with_global_corners(int nx, int j0, int j1, const std::vector<double>& full_lons,
                                                                                        const std::vector<double>& full_lats) {
    const int nband = j1 - j0;
    const std::size_t ny_global = full_lats.size();

    // Band-local cell centers (row-major, band rows [j0, j1)) — the StructuredGrid
    // extent for the band.
    Kokkos::View<double*, Kokkos::HostSpace> band_center_lon("band_center_lon", static_cast<size_t>(nx) * nband);
    Kokkos::View<double*, Kokkos::HostSpace> band_center_lat("band_center_lat", static_cast<size_t>(nx) * nband);
    for (int jr = 0; jr < nband; ++jr) {
        for (int i = 0; i < nx; ++i) {
            const size_t idx = static_cast<size_t>(jr) * nx + i;
            band_center_lon(idx) = full_lons[i];
            band_center_lat(idx) = full_lats[static_cast<size_t>(j0) + jr];
        }
    }

    // FULL global center arrays (column-major, index i + j*nx) that the shared
    // AXIS kernel synthesizes corners from, so the band's boundary edges see the
    // neighbour rows owned by adjacent ranks.
    Kokkos::View<double*, Kokkos::HostSpace> global_center_lon("global_center_lon", static_cast<size_t>(nx) * ny_global);
    Kokkos::View<double*, Kokkos::HostSpace> global_center_lat("global_center_lat", static_cast<size_t>(nx) * ny_global);
    for (std::size_t j = 0; j < ny_global; ++j) {
        for (int i = 0; i < nx; ++i) {
            const size_t idx = j * static_cast<size_t>(nx) + i;
            global_center_lon(idx) = full_lons[i];
            global_center_lat(idx) = full_lats[j];
        }
    }

    Kokkos::View<double*, Kokkos::HostSpace> corner_lon, corner_lat;
    axis::topology::synthesize_band_corners<Kokkos::HostSpace>(nx, ny_global, global_center_lon, global_center_lat, static_cast<std::size_t>(j0),
                                                               static_cast<std::size_t>(j1), corner_lon, corner_lat);

    axis::topology::StructuredGrid<Kokkos::HostSpace> grid(nx, nband, band_center_lon, band_center_lat,
                                                           axis::topology::CoordinateSystem::SphericalDeg);
    grid.set_corners(std::move(corner_lon), std::move(corner_lat));
    return grid.to_unstructured();
}

// Build the destination sub-mesh for a CURVILINEAR latitude band [j0, j1) with
// GLOBALLY-CONSISTENT 2-D corners. A curvilinear boundary corner depends on the
// neighbour centers in BOTH longitude and latitude, so — like the rectilinear
// case — it must be derived from the FULL global center arrays, not a band-local
// slice. This delegates to the same AXIS shared kernel
// (axis::topology::synthesize_band_corners) used for the rectilinear band, so
// both grid types share one corner-geometry implementation (periodic-longitude
// wrap, one-sided domain-boundary convention, 2x2 averaging) and cannot drift
// from the global mesh. `full_center_lon`/`full_center_lat` are the flattened
// global curvilinear centers of length nx * ny_global (index i + j*nx);
// `band_center_lon`/`band_center_lat` are the band's nx * (j1-j0) centers.
axis::topology::UnstructuredMesh<Kokkos::HostSpace> build_band_mesh_curvilinear_with_global_corners(int nx, int j0, int j1,
                                                                                                    const std::vector<double>& full_center_lon,
                                                                                                    const std::vector<double>& full_center_lat,
                                                                                                    const std::vector<double>& band_center_lon,
                                                                                                    const std::vector<double>& band_center_lat) {
    const int nband = j1 - j0;
    const std::size_t ny_global = full_center_lon.size() / static_cast<std::size_t>(nx);

    Kokkos::View<double*, Kokkos::HostSpace> global_clon("global_clon", full_center_lon.size());
    Kokkos::View<double*, Kokkos::HostSpace> global_clat("global_clat", full_center_lat.size());
    for (std::size_t k = 0; k < full_center_lon.size(); ++k) {
        global_clon(k) = full_center_lon[k];
        global_clat(k) = full_center_lat[k];
    }

    Kokkos::View<double*, Kokkos::HostSpace> band_clon("band_clon", static_cast<std::size_t>(nx) * nband);
    Kokkos::View<double*, Kokkos::HostSpace> band_clat("band_clat", static_cast<std::size_t>(nx) * nband);
    for (std::size_t k = 0; k < band_clon.extent(0); ++k) {
        band_clon(k) = band_center_lon[k];
        band_clat(k) = band_center_lat[k];
    }

    Kokkos::View<double*, Kokkos::HostSpace> corner_lon, corner_lat;
    axis::topology::synthesize_band_corners<Kokkos::HostSpace>(nx, ny_global, global_clon, global_clat, static_cast<std::size_t>(j0),
                                                               static_cast<std::size_t>(j1), corner_lon, corner_lat);

    axis::topology::StructuredGrid<Kokkos::HostSpace> grid(nx, nband, band_clon, band_clat, axis::topology::CoordinateSystem::SphericalDeg);
    grid.set_corners(std::move(corner_lon), std::move(corner_lat));
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
                       const std::vector<double>& target_lats, const std::string& map_algo, int j0, int j1, const std::string& src_gridspec_file,
                       const std::string& dst_gridspec_file, RegridPlan& plan) {
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
        // Identity apply reads source rows [j0, j1) directly (source grid ==
        // target grid), so the exact window is the band itself.
        plan.src_j0 = j0;
        plan.src_rows = j1 - j0;
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
    auto src_mesh = build_axis_mesh(plan.file_nx, plan.file_ny, src_lons, src_lats, src_gridspec_file, map_algo);

    const bool curvilinear_target = (target_lons.size() == static_cast<size_t>(nx) * ny && ny > 1);

    std::vector<double> band_lons;
    std::vector<double> band_lats;

    if (curvilinear_target) {
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

    auto normalize_lons = [&](std::vector<double>& lons) {
        if (use_360_range) {
            for (auto& lon : lons) {
                if (lon < 0.0)
                    lon += 360.0;
                else if (lon >= 360.0)
                    lon -= 360.0;
            }
        } else {
            for (auto& lon : lons) {
                if (lon >= 180.0)
                    lon -= 360.0;
                else if (lon < -180.0)
                    lon += 360.0;
            }
        }
    };

    // Build the rank-local destination sub-mesh. For a rectilinear target the
    // band's outer latitude edges MUST be the true GLOBAL cell edges (midpoint
    // to the neighbour row owned by an adjacent rank), not the one-sided
    // extrapolation StructuredGrid::synthesize_corners would derive from the
    // band-local center slice. On a non-uniform latitude grid those differ, and
    // the extrapolated edge changes the conservative overlap areas of the band's
    // first/last rows, breaking band==global equivalence and conservation at the
    // seam (uniform grids are unaffected because the extrapolation coincides).
    // Both build_band_mesh_*_with_global_corners derive every corner from the
    // FULL global target coordinate arrays via the shared AXIS kernel
    // (axis::topology::synthesize_band_corners) and pin them via set_corners, so
    // the band regrid matches the corresponding rows of the global regrid exactly
    // — for rectilinear AND curvilinear grids. A gridspec/file target supplies its
    // own explicit corners and keeps the prior path.
    std::vector<double> full_center_lon;  // normalized full global centers (curvilinear only)
    if (curvilinear_target) {
        full_center_lon = target_lons;
        normalize_lons(full_center_lon);
        band_lons.assign(full_center_lon.begin() + static_cast<size_t>(j0) * nx, full_center_lon.begin() + static_cast<size_t>(j1) * nx);
    } else {
        normalize_lons(band_lons);
    }

    axis::topology::UnstructuredMesh<Kokkos::HostSpace> dst_mesh =
        dst_gridspec_file.empty()
            ? (curvilinear_target ? build_band_mesh_curvilinear_with_global_corners(nx, j0, j1, full_center_lon, target_lats, band_lons, band_lats)
                                  : build_band_mesh_with_global_corners(nx, j0, j1, band_lons, target_lats))
            : build_axis_mesh(nx, nband, band_lons, band_lats, dst_gridspec_file, map_algo);

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

    // Source-row window (band-scoped reads): the exact latitude rows of the
    // SOURCE grid this rank's weight matrix references, derived from the COO
    // column range (still valid after to_csr). read_slab uses it to fetch
    // rows [src_j0, src_j0+src_rows) instead of the full record: under MPI
    // every rank otherwise pulls the whole global field from disk and
    // discards all but its band (~nranks x replicated IO). Exact for any
    // mapalgo because it is computed from the actual nonzeros, not guessed
    // from the destination band. Left at src_rows == 0 ("no window") when the
    // matrix is empty or geometry is unexpected; callers then read full records.
    const std::size_t plan_nnz = plan.matrix.nnz();
    if (plan_nnz > 0 && plan.file_nx > 0 && plan.file_ny > 0) {
        const axis::index_t* cols = plan.matrix.factor_col().data_handle();
        axis::index_t min_col = cols[0];
        axis::index_t max_col = cols[0];
        for (std::size_t k = 1; k < plan_nnz; ++k) {
            if (cols[k] < min_col) min_col = cols[k];
            if (cols[k] > max_col) max_col = cols[k];
        }
        const int r0 = static_cast<int>(min_col) / plan.file_nx;
        const int r1 = static_cast<int>(max_col) / plan.file_nx;
        if (r0 >= 0 && r1 < plan.file_ny) {
            plan.src_j0 = r0;
            plan.src_rows = r1 - r0 + 1;
        }
    }
    if (plan.src_rows > 0) {
        CECE_LOG_INFO("[DRIVER] band-scoped read window: source rows [" + std::to_string(plan.src_j0) + "," +
                      std::to_string(plan.src_j0 + plan.src_rows) + ") of " + std::to_string(plan.file_ny) + " (dst band [" + std::to_string(j0) +
                      "," + std::to_string(j1) + "))");
    }

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
    // For double input the file buffer is already a contiguous double array;
    // wrap it directly (offset by time_offset). For float input we must
    // materialize a widened double buffer.
    const size_t src_len = static_cast<size_t>(file_nx) * file_ny;
    Kokkos::View<double*, Kokkos::HostSpace> src_field;  // only allocated for the float-widening case
    axis::field_view<const double, 1> src_view;
    if (is_float) {
        const float* float_data = static_cast<const float*>(view_data);
        src_field = Kokkos::View<double*, Kokkos::HostSpace>("src_field", src_len);
        for (int j = 0; j < file_ny; ++j) {
            for (int i = 0; i < file_nx; ++i) {
                size_t src_idx = time_offset + static_cast<size_t>(j) * file_nx + i;
                src_field(static_cast<size_t>(j) * file_nx + i) = static_cast<double>(float_data[src_idx]);
            }
        }
        src_view = axis::field_view<const double, 1>(src_field.data(), src_len);
    } else {
        const double* double_data = static_cast<const double*>(view_data);
        src_view = axis::field_view<const double, 1>(double_data + time_offset, src_len);
    }

    // E. Apply cached weights directly into local_dst [nx * nband]; apply
    // overwrites the destination, so no separate staging buffer is needed.
    axis::field_view<double, 1> dst_view(local_dst.data(), static_cast<size_t>(nx) * nband);
    axis::solver::apply(plan.matrix, src_view, dst_view);

    return true;
}

}  // namespace cece::io
