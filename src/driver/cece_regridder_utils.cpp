// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors

#include "cece/cece_regridder_utils.hpp"

#include <mpi.h>

#include <algorithm>
#include <axis/topology/mesh_builder.hpp>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "cece/cece_amio_utils.hpp"
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

    // Read through the view's declared element type: the payload size alone
    // cannot tell an integer coordinate from a float one.
    amio_dtype_t dtype = AMIO_DTYPE_F64;
    if (amio_view_dtype(view, &dtype) != AMIO_OK) {
        amio_release_view(view);
        throw std::runtime_error("Failed to retrieve element type for coordinate variable: " + name);
    }
    const std::size_t elem_size = cece::detail::amio_dtype_size(dtype);
    if (elem_size == 0 || size < static_cast<std::size_t>(total_pts) * elem_size) {
        amio_release_view(view);
        throw std::runtime_error("Unsupported or truncated payload for coordinate variable: " + name);
    }

    double scale = 1.0;
    double offset = 0.0;
    {
        double attr = 0.0;
        if (amio_get_var_attribute_double(dataset, name.c_str(), "scale_factor", &attr) == AMIO_OK) scale = attr;
        if (amio_get_var_attribute_double(dataset, name.c_str(), "add_offset", &attr) == AMIO_OK) offset = attr;
    }

    std::vector<double> values;
    if (!cece::detail::widen_amio_elements(data, dtype, static_cast<std::size_t>(total_pts), scale, offset, values)) {
        amio_release_view(view);
        throw std::runtime_error("Could not widen element type of coordinate variable: " + name);
    }

    for (int i = 0; i < total_pts; ++i) {
        double val = values[i];
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

// Decide whether a dataset is an FV3/UFS native supergrid rather than some other
// file that merely happens to define horizontal coordinates named "x" and "y"
// (a projected-grid CF file, for instance, names its cartesian coordinates x/y
// too). A native supergrid stores cell-corner longitude/latitude on a
// double-resolution logically-rectangular lattice whose extents are odd
// (2*n + 1), with CF metadata identifying the arrays as geographic degrees.
// Requiring all of these keeps the heuristic from misfiring on projected grids,
// whose coordinates carry metre units and projection standard names.
static bool looks_like_fv3_supergrid(amio_dataset_handle dataset, int nx, int nband, int j0) {
    amio_shape_t x_shape{};
    amio_shape_t y_shape{};
    int64_t timesteps = 0;
    if (amio_describe(dataset, "x", &x_shape, &timesteps) != AMIO_OK || amio_describe(dataset, "y", &y_shape, &timesteps) != AMIO_OK) {
        return false;
    }
    if (x_shape.rank != 2 || y_shape.rank != 2) {
        return false;
    }
    if (x_shape.extents[0] != y_shape.extents[0] || x_shape.extents[1] != y_shape.extents[1]) {
        return false;
    }
    // Corner lattice must be the double-resolution (2*n + 1) grid, so both extents
    // are odd and the x extent is exactly 2*nx + 1 (e.g. 193 corners for nx = 96).
    const int64_t nx_corners = 2 * static_cast<int64_t>(nx) + 1;
    const int64_t min_j_corners = 2 * static_cast<int64_t>(j0 + nband) + 1;
    if (x_shape.extents[1] != nx_corners || x_shape.extents[0] < min_j_corners) {
        return false;
    }
    if (x_shape.extents[0] % 2 == 0 || x_shape.extents[1] % 2 == 0) {
        return false;
    }

    // Finally, confirm the CF metadata describes geographic degrees.
    auto read_attr = [&](const char* var, const char* attr, char* buf, size_t cap) {
        size_t len = 0;
        buf[0] = '\0';
        return amio_get_var_attribute_text(dataset, var, attr, buf, cap, &len) == AMIO_OK;
    };
    constexpr size_t kAttrCap = 64;
    char units[kAttrCap];
    const bool units_are_degrees =
        read_attr("x", "units", units, kAttrCap) && (std::string(units) == "degree_east" || std::string(units) == "degrees_east") &&
        read_attr("y", "units", units, kAttrCap) && (std::string(units) == "degree_north" || std::string(units) == "degrees_north");
    if (units_are_degrees) {
        return true;
    }
    char std_name[kAttrCap];
    return read_attr("x", "standard_name", std_name, kAttrCap) && std::string(std_name) == "geographic_longitude" &&
           read_attr("y", "standard_name", std_name, kAttrCap) && std::string(std_name) == "geographic_latitude";
}

// Read a CF rectilinear axis and its explicitly named cell bounds. Do not infer
// polar edges from centres or wrap longitudes: either operation changes the
// supplied grid. Unsupported/malformed bounds must fail, not silently regrid.
static std::vector<double> read_rectilinear_bounds(amio_dataset_handle dataset, const char* axis_name, int count, bool latitude) {
    auto text_attribute = [&](const char* attribute) {
        char value[1024]{};
        size_t length = 0;
        if (amio_get_var_attribute_text(dataset, axis_name, attribute, value, sizeof(value), &length) != AMIO_OK || length == 0 ||
            length >= sizeof(value)) {
            throw std::runtime_error(std::string("CF rectilinear axis lacks valid ") + attribute + ": " + axis_name);
        }
        return std::string(value);
    };
    const auto units = text_attribute("units");
    if (latitude ? (units != "degrees_north" && units != "degree_north") : (units != "degrees_east" && units != "degree_east")) {
        throw std::runtime_error(std::string("CF rectilinear axis must use geographic degree units: ") + axis_name);
    }
    const auto bounds_name = text_attribute("bounds");
    amio_shape_t shape{};
    int64_t timesteps = 0;
    if (amio_describe(dataset, bounds_name.c_str(), &shape, &timesteps) != AMIO_OK || shape.rank != 2 || shape.extents[0] != count ||
        shape.extents[1] != 2) {
        throw std::runtime_error("CF rectilinear bounds must have shape [axis, 2]: " + bounds_name);
    }
    int points = 0;
    const auto centres = read_coordinate_array(dataset, axis_name, false, false, points);
    if (points != count) throw std::runtime_error("CF rectilinear centre count mismatch");
    const auto bounds = read_coordinate_array(dataset, bounds_name, false, false, points);
    if (static_cast<size_t>(points) != 2 * static_cast<size_t>(count)) throw std::runtime_error("CF rectilinear bounds count mismatch");
    for (int i = 0; i < count; ++i) {
        const size_t offset = 2 * static_cast<size_t>(i);
        double lo = bounds[offset], hi = bounds[offset + 1];
        const double centre = centres[i];
        if (!std::isfinite(lo) || !std::isfinite(hi) || !std::isfinite(centre) || std::abs(hi - lo) <= 1e-12) {
            throw std::runtime_error("Invalid CF rectilinear cell bounds: " + bounds_name);
        }
        if (latitude) {
            const double low = std::min(lo, hi), high = std::max(lo, hi);
            if (low < -90.0 || high > 90.0 || centre < low - 1e-10 || centre > high + 1e-10)
                throw std::runtime_error("Invalid CF rectilinear cell bounds: " + bounds_name);
        } else {
            // This adapter has no grid-wide seam declaration, so longitude
            // pairs must be consistently ordered and cannot cross the seam.
            if (!(lo < hi) || hi - lo > 180.0 || centre < lo - 1e-10 || centre > hi + 1e-10)
                throw std::runtime_error("Invalid CF rectilinear cell bounds: " + bounds_name);
        }
        if (i > 0 && (centres[i] == centres[i - 1] || (i > 1 && ((centres[i] > centres[i - 1]) != (centres[1] > centres[0]))))) {
            throw std::runtime_error(std::string("Nonmonotonic CF rectilinear axis: ") + axis_name);
        }
    }
    return bounds;
}

// Build a constant-latitude-strip mesh from CF 1-D axis bounds. Cell (i, j)
// takes its four corners from the [i] and [j0 + j] bound pairs, so rows with
// gapped bounds (e.g. a polar-cap edge at -88° beneath a row starting at -84°)
// stay exact instead of sharing a lattice node grid. AXIS `make_unstructured`
// validates the CSR buffers, and the declared metadata lets conservative
// prerequisite checks accept the strips without inferring geometry.
static axis::topology::UnstructuredMesh<Kokkos::HostSpace> build_rectilinear_strip_mesh(int nx, int nband, int j0,
                                                                                        const std::vector<double>& lon_bounds,
                                                                                        const std::vector<double>& lat_bounds) {
    const size_t cells = static_cast<size_t>(nx) * nband;
    if (cells > static_cast<size_t>(std::numeric_limits<axis::index_t>::max()) / 4) throw std::runtime_error("CF rectilinear mesh too large");
    Kokkos::View<double**, Kokkos::LayoutLeft, Kokkos::HostSpace> nodes("cf_rectilinear_nodes", cells * 4, 2);
    Kokkos::View<axis::index_t*, Kokkos::HostSpace> offsets("cf_rectilinear_offsets", cells + 1);
    Kokkos::View<axis::index_t*, Kokkos::HostSpace> indices("cf_rectilinear_indices", cells * 4);
    Kokkos::View<double*, Kokkos::HostSpace> areas("cf_rectilinear_areas", cells);
    constexpr double radians = 3.14159265358979323846 / 180.0;
    for (int j = 0; j < nband; ++j) {
        for (int i = 0; i < nx; ++i) {
            const size_t cell = static_cast<size_t>(j) * nx + i, first = cell * 4;
            const size_t longitude_offset = 2 * static_cast<size_t>(i);
            const size_t latitude_offset = 2 * (static_cast<size_t>(j0) + static_cast<size_t>(j));
            const double west = lon_bounds[longitude_offset], east = lon_bounds[longitude_offset + 1];
            const double south = std::min(lat_bounds[latitude_offset], lat_bounds[latitude_offset + 1]);
            const double north = std::max(lat_bounds[latitude_offset], lat_bounds[latitude_offset + 1]);
            if (!(west < east) || std::abs(east - west) > 360.0 || south < -90.0 || south > 90.0 || north < -90.0 || north > 90.0)
                throw std::runtime_error("CF rectilinear cell bounds are outside the supported spherical domain");
            nodes(first, 0) = west;
            nodes(first, 1) = south;
            nodes(first + 1, 0) = east;
            nodes(first + 1, 1) = south;
            nodes(first + 2, 0) = east;
            nodes(first + 2, 1) = north;
            nodes(first + 3, 0) = west;
            nodes(first + 3, 1) = north;
            offsets(cell) = static_cast<axis::index_t>(first);
            for (size_t v = 0; v < 4; ++v) indices(first + v) = static_cast<axis::index_t>(first + v);
            areas(cell) = std::abs(east - west) * radians * (std::sin(std::max(south, north) * radians) - std::sin(std::min(south, north) * radians));
        }
    }
    offsets(cells) = static_cast<axis::index_t>(cells * 4);
    axis::topology::GeometryMetadata metadata{axis::topology::GeometryProvenance::SourceAuthoritative,
                                              axis::topology::BoundaryModel::ConstantLatitude,
                                              axis::topology::AreaModel::ConstantLatitudeStrip,
                                              {false, 360.0, true}};
    return axis::topology::make_unstructured(nodes, offsets, indices, axis::topology::CoordinateSystem::SphericalDeg, areas,
                                             Kokkos::View<int*, Kokkos::HostSpace>{}, metadata);
}

// Build a great-circle polygon mesh from per-cell explicit corners and give it
// authoritative spherical-excess cell areas. SCRIP, MPAS, FV3 grid_spec,
// FV3 supergrid, and CF curvilinear files all supply the corner coordinates,
// so AXIS computes the areas rather than each loader hand-rolling them.
static axis::topology::UnstructuredMesh<Kokkos::HostSpace> seal_great_circle_mesh(Kokkos::View<double**, Kokkos::LayoutLeft, Kokkos::HostSpace> nodes,
                                                                                  Kokkos::View<axis::index_t*, Kokkos::HostSpace> offsets,
                                                                                  Kokkos::View<axis::index_t*, Kokkos::HostSpace> indices) {
    auto mesh =
        axis::topology::make_unstructured(std::move(nodes), std::move(offsets), std::move(indices), axis::topology::CoordinateSystem::SphericalDeg);
    mesh.compute_areas();
    mesh.set_geometry_metadata({axis::topology::GeometryProvenance::SourceAuthoritative,
                                axis::topology::BoundaryModel::GreatCircle,
                                axis::topology::AreaModel::SphericalExcess,
                                {false, 360.0, true}});
    return mesh;
}

static axis::topology::UnstructuredMesh<Kokkos::HostSpace> load_rectilinear_mesh(amio_dataset_handle dataset, int nx, int nband, int j0) {
    amio_shape_t lon_shape{}, lat_shape{};
    int64_t timesteps = 0;
    if (nx <= 0 || nband < 0 || j0 < 0 || amio_describe(dataset, "lon", &lon_shape, &timesteps) != AMIO_OK ||
        amio_describe(dataset, "lat", &lat_shape, &timesteps) != AMIO_OK || lon_shape.rank != 1 || lat_shape.rank != 1 ||
        lon_shape.extents[0] != nx || nx > std::numeric_limits<int>::max() / 2 || lat_shape.extents[0] <= 0 ||
        lat_shape.extents[0] > std::numeric_limits<int>::max() / 2 || static_cast<int64_t>(j0) + nband > lat_shape.extents[0]) {
        throw std::runtime_error("CF rectilinear gridspec dimensions do not match the requested band");
    }
    const auto lon_bounds = read_rectilinear_bounds(dataset, "lon", nx, false);
    const auto lat_bounds = read_rectilinear_bounds(dataset, "lat", static_cast<int>(lat_shape.extents[0]), true);
    return build_rectilinear_strip_mesh(nx, nband, j0, lon_bounds, lat_bounds);
}

axis::topology::UnstructuredMesh<Kokkos::HostSpace> load_mesh_from_file(int nx, int nband, int j0, const std::string& gridspec_file) {
    // CECE decodes source-specific file layouts through AMIO; AXIS owns mesh
    // construction and validates each resulting node/CSR buffer via
    // make_unstructured. Structured formats with centers and corners can use
    // AXIS StructuredGrid instead (see build_axis_mesh below).
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

    auto close_amio = [&]() {
        if (dataset) {
            amio_close(dataset);
            dataset = nullptr;
        }
        if (core) {
            amio_finalize(core);
            core = nullptr;
        }
    };

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
            if (grid_size <= 0 || grid_corners < 3 || scrip_lons.size() != scrip_lats.size() ||
                scrip_lons.size() != static_cast<std::size_t>(grid_size) * static_cast<std::size_t>(grid_corners))
                throw std::runtime_error("SCRIP corner arrays have inconsistent cell or vertex dimensions");

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
                conn_offsets(i) = static_cast<axis::index_t>(i * grid_corners);
                for (int v = 0; v < grid_corners; ++v) {
                    conn_indices(i * grid_corners + v) = static_cast<axis::index_t>(i * grid_corners + v);
                }
            }
            conn_offsets(n_cells) = static_cast<axis::index_t>(n_vertices);

            auto mesh = seal_great_circle_mesh(node_coords, conn_offsets, conn_indices);
            amio_close(dataset);
            dataset = nullptr;
            amio_finalize(core);
            core = nullptr;
            return mesh;
        } catch (const std::exception& e) {
            if (dataset) amio_close(dataset);
            if (core) amio_finalize(core);
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
            int n_cells = nx;
            int max_edges = 0;

            if (amio_read(dataset, "nEdgesOnCell", 0, nullptr, &edges_on_cell_view) != AMIO_OK) {
                throw std::runtime_error("Failed to read nEdgesOnCell");
            }
            const void* data_edges = nullptr;
            size_t size_edges = 0;
            if (amio_view_data(edges_on_cell_view, &data_edges, &size_edges) == AMIO_OK) {
                amio_shape_t shape{};
                if (amio_view_shape(edges_on_cell_view, &shape) == AMIO_OK) {
                    if (shape.rank == 1 && static_cast<int>(shape.extents[0]) == n_cells) {
                        n_edges_on_cell.resize(n_cells);
                        for (int i = 0; i < n_cells; ++i) {
                            n_edges_on_cell[i] = static_cast<const int*>(data_edges)[i];
                        }
                    }
                }
            }
            amio_release_view(edges_on_cell_view);
            edges_on_cell_view = nullptr;
            if (n_edges_on_cell.size() != static_cast<std::size_t>(n_cells))
                throw std::runtime_error("nEdgesOnCell dimensions do not match center cells");

            if (amio_read(dataset, "verticesOnCell", 0, nullptr, &vertices_on_cell_view) != AMIO_OK) {
                throw std::runtime_error("Failed to read verticesOnCell");
            }
            const void* data_verts = nullptr;
            size_t size_verts = 0;
            if (amio_view_data(vertices_on_cell_view, &data_verts, &size_verts) == AMIO_OK) {
                amio_shape_t shape{};
                if (amio_view_shape(vertices_on_cell_view, &shape) == AMIO_OK) {
                    if (shape.rank == 2 && static_cast<int>(shape.extents[0]) == n_cells) {
                        max_edges = static_cast<int>(shape.extents[1]);
                        vertices_on_cell.resize(static_cast<size_t>(n_cells) * static_cast<size_t>(max_edges));
                        for (int i = 0; i < n_cells * max_edges; ++i) {
                            vertices_on_cell[i] = static_cast<const int*>(data_verts)[i];
                        }
                    }
                }
            }
            amio_release_view(vertices_on_cell_view);
            vertices_on_cell_view = nullptr;

            size_t n_vertices = lat_vertices.size();
            if (n_vertices == 0 || lon_vertices.size() != n_vertices || n_edges_on_cell.size() != static_cast<std::size_t>(n_cells) ||
                max_edges < 3 || vertices_on_cell.size() != static_cast<std::size_t>(n_cells) * static_cast<std::size_t>(max_edges))
                throw std::runtime_error("MPAS vertex and cell connectivity arrays have inconsistent dimensions");

            Kokkos::View<double**, Kokkos::LayoutLeft, Kokkos::HostSpace> node_coords("node_coords", n_vertices, 2);
            for (size_t i = 0; i < n_vertices; ++i) {
                node_coords(i, 0) = lon_vertices[i];
                node_coords(i, 1) = lat_vertices[i];
            }

            size_t total_conn = 0;
            for (int i = 0; i < n_cells; ++i) {
                if (n_edges_on_cell[static_cast<std::size_t>(i)] < 3 || n_edges_on_cell[static_cast<std::size_t>(i)] > max_edges)
                    throw std::runtime_error("MPAS nEdgesOnCell contains an invalid polygon arity");
                total_conn += n_edges_on_cell[i];
            }

            Kokkos::View<axis::index_t*, Kokkos::HostSpace> conn_offsets("conn_offsets", n_cells + 1);
            Kokkos::View<axis::index_t*, Kokkos::HostSpace> conn_indices("conn_indices", total_conn);

            size_t offset = 0;
            for (int i = 0; i < n_cells; ++i) {
                conn_offsets(i) = static_cast<axis::index_t>(offset);
                int n_edges = n_edges_on_cell[i];
                for (int v = 0; v < n_edges; ++v) {
                    int v_idx = vertices_on_cell[i * max_edges + v];
                    if (v_idx > 0 && v_idx <= static_cast<int>(n_vertices)) {
                        conn_indices(offset + v) = static_cast<axis::index_t>(v_idx - 1);
                    } else {
                        throw std::runtime_error("MPAS verticesOnCell contains an out-of-range vertex index");
                    }
                }
                offset += n_edges;
            }
            conn_offsets(n_cells) = static_cast<axis::index_t>(offset);

            auto mesh = seal_great_circle_mesh(node_coords, conn_offsets, conn_indices);
            amio_close(dataset);
            dataset = nullptr;
            amio_finalize(core);
            core = nullptr;
            return mesh;
        } catch (const std::exception& e) {
            if (edges_on_cell_view) amio_release_view(edges_on_cell_view);
            if (vertices_on_cell_view) amio_release_view(vertices_on_cell_view);
            if (dataset) amio_close(dataset);
            if (core) amio_finalize(core);
            throw;
        }
    }

    // C. Try UFS/FV3 grid_spec convention (grid_lon, grid_lat 2D corner arrays)
    amio_view_handle fv3_spec_peek = nullptr;
    if (amio_read(dataset, "grid_lon", 0, nullptr, &fv3_spec_peek) == AMIO_OK) {
        amio_release_view(fv3_spec_peek);
        try {
            int total_lon_pts = 0, total_lat_pts = 0;
            std::vector<double> corner_lons = read_coordinate_array(dataset, "grid_lon", false, true, total_lon_pts);
            std::vector<double> corner_lats = read_coordinate_array(dataset, "grid_lat", false, false, total_lat_pts);

            int nx_b = nx + 1;  // FV3 spec bounds are nx + 1
            size_t min_required = static_cast<size_t>(j0 + nband + 1) * nx_b;

            if (total_lon_pts != total_lat_pts || static_cast<size_t>(total_lon_pts) < min_required) {
                throw std::runtime_error("FV3 grid_spec dimension mismatch in '" + gridspec_file + "': required at least " +
                                         std::to_string(min_required) + " points for nx=" + std::to_string(nx) + ", nband=" + std::to_string(nband) +
                                         ", j0=" + std::to_string(j0) + " but found " + std::to_string(total_lon_pts));
            }

            size_t n_cells = static_cast<size_t>(nband) * nx;
            size_t n_vertices = n_cells * 4;

            Kokkos::View<double**, Kokkos::LayoutLeft, Kokkos::HostSpace> node_coords("node_coords", n_vertices, 2);
            Kokkos::View<axis::index_t*, Kokkos::HostSpace> conn_offsets("conn_offsets", n_cells + 1);
            Kokkos::View<axis::index_t*, Kokkos::HostSpace> conn_indices("conn_indices", n_vertices);

            size_t v_idx = 0;
            for (int j = 0; j < nband; ++j) {
                int global_j = j0 + j;  // MPI j0 offset
                for (int i = 0; i < nx; ++i) {
                    size_t cell_idx = static_cast<size_t>(j) * nx + i;

                    int bl = global_j * nx_b + i;
                    int br = global_j * nx_b + (i + 1);
                    int tr = (global_j + 1) * nx_b + (i + 1);
                    int tl = (global_j + 1) * nx_b + i;

                    node_coords(v_idx, 0) = corner_lons[bl];
                    node_coords(v_idx, 1) = corner_lats[bl];
                    conn_indices(v_idx) = static_cast<axis::index_t>(v_idx);
                    v_idx++;
                    node_coords(v_idx, 0) = corner_lons[br];
                    node_coords(v_idx, 1) = corner_lats[br];
                    conn_indices(v_idx) = static_cast<axis::index_t>(v_idx);
                    v_idx++;
                    node_coords(v_idx, 0) = corner_lons[tr];
                    node_coords(v_idx, 1) = corner_lats[tr];
                    conn_indices(v_idx) = static_cast<axis::index_t>(v_idx);
                    v_idx++;
                    node_coords(v_idx, 0) = corner_lons[tl];
                    node_coords(v_idx, 1) = corner_lats[tl];
                    conn_indices(v_idx) = static_cast<axis::index_t>(v_idx);
                    v_idx++;

                    conn_offsets(cell_idx) = static_cast<axis::index_t>(cell_idx * 4);
                }
            }
            conn_offsets(n_cells) = static_cast<axis::index_t>(n_vertices);

            close_amio();
            return seal_great_circle_mesh(node_coords, conn_offsets, conn_indices);
        } catch (const std::exception& e) {
            close_amio();
            throw;
        }
    }

    // D. Try UFS/FV3 direct grid convention (x, y on double-resolution supergrid).
    // This convention is keyed on the bare coordinate names "x"/"y", which other
    // grid descriptions also use, so it is deliberately tried last -- after SCRIP,
    // MPAS, and grid_spec -- and additionally gated on the supergrid-specific
    // shape and CF metadata so a projected-grid file with cartesian x/y is not
    // mistaken for one.
    if (looks_like_fv3_supergrid(dataset, nx, nband, j0)) {
        try {
            int total_x_pts = 0, total_y_pts = 0;
            std::vector<double> corner_lons = read_coordinate_array(dataset, "x", false, true, total_x_pts);
            std::vector<double> corner_lats = read_coordinate_array(dataset, "y", false, false, total_y_pts);

            // In native supergrid file, nxp/nyp are 2 * nx + 1 (e.g. 193 for nx = 96)
            int nx_b = (nx * 2) + 1;
            size_t min_required = static_cast<size_t>(2 * (j0 + nband) + 1) * nx_b;

            if (total_x_pts != total_y_pts || static_cast<size_t>(total_x_pts) < min_required) {
                throw std::runtime_error("FV3 supergrid dimension mismatch in '" + gridspec_file + "': required at least " +
                                         std::to_string(min_required) + " points for nx=" + std::to_string(nx) + ", nband=" + std::to_string(nband) +
                                         ", j0=" + std::to_string(j0) + " but found " + std::to_string(total_x_pts));
            }

            size_t n_cells = static_cast<size_t>(nband) * nx;
            size_t n_vertices = n_cells * 4;

            Kokkos::View<double**, Kokkos::LayoutLeft, Kokkos::HostSpace> node_coords("node_coords", n_vertices, 2);
            Kokkos::View<axis::index_t*, Kokkos::HostSpace> conn_offsets("conn_offsets", n_cells + 1);
            Kokkos::View<axis::index_t*, Kokkos::HostSpace> conn_indices("conn_indices", n_vertices);

            size_t v_idx = 0;
            for (int j = 0; j < nband; ++j) {
                int global_j = (j0 + j) * 2;  // Stride by 2 for the supergrid
                for (int i = 0; i < nx; ++i) {
                    size_t cell_idx = static_cast<size_t>(j) * nx + i;
                    int i_super = i * 2;

                    int bl = global_j * nx_b + i_super;
                    int br = global_j * nx_b + (i_super + 2);
                    int tr = (global_j + 2) * nx_b + (i_super + 2);
                    int tl = (global_j + 2) * nx_b + i_super;

                    node_coords(v_idx, 0) = corner_lons[bl];
                    node_coords(v_idx, 1) = corner_lats[bl];
                    conn_indices(v_idx) = static_cast<axis::index_t>(v_idx);
                    v_idx++;
                    node_coords(v_idx, 0) = corner_lons[br];
                    node_coords(v_idx, 1) = corner_lats[br];
                    conn_indices(v_idx) = static_cast<axis::index_t>(v_idx);
                    v_idx++;
                    node_coords(v_idx, 0) = corner_lons[tr];
                    node_coords(v_idx, 1) = corner_lats[tr];
                    conn_indices(v_idx) = static_cast<axis::index_t>(v_idx);
                    v_idx++;
                    node_coords(v_idx, 0) = corner_lons[tl];
                    node_coords(v_idx, 1) = corner_lats[tl];
                    conn_indices(v_idx) = static_cast<axis::index_t>(v_idx);
                    v_idx++;

                    conn_offsets(cell_idx) = static_cast<axis::index_t>(cell_idx * 4);
                }
            }
            conn_offsets(n_cells) = static_cast<axis::index_t>(n_vertices);

            close_amio();
            return seal_great_circle_mesh(node_coords, conn_offsets, conn_indices);
        } catch (const std::exception& e) {
            close_amio();
            throw;
        }
    }

    // E. CF rectilinear grids with explicit bounds, including HEMCO polar caps.
    amio_shape_t cf_lon{}, cf_lat{};
    int64_t cf_times = 0;
    if (amio_describe(dataset, "lon", &cf_lon, &cf_times) == AMIO_OK && amio_describe(dataset, "lat", &cf_lat, &cf_times) == AMIO_OK &&
        cf_lon.rank == 1 && cf_lat.rank == 1) {
        try {
            auto mesh = load_rectilinear_mesh(dataset, nx, nband, j0);
            close_amio();
            return mesh;
        } catch (...) {
            close_amio();
            throw;
        }
    }

    // F. Try CF-conventions coordinates for non-rectilinear
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

            // 4. Classify the bounds arrays by their NetCDF shapes instead of
            //    flattened element counts: rank-2 arrays whose minor dimension
            //    is 2 are 1-D rectilinear axis bounds (nv, nx)/(nv, ny), while
            //    rank-3 (ny, nx, 4) arrays are explicit per-cell corners. The
            //    old count heuristic misread square 1-D grids (nx == ny even)
            //    as 2-D corner lists.
            amio_shape_t lon_bnd_shape{}, lat_bnd_shape{};
            int64_t bnd_ts = 0;
            const bool has_lon_bnd_shape = amio_describe(dataset, lon_bounds_name.c_str(), &lon_bnd_shape, &bnd_ts) == AMIO_OK;
            const bool has_lat_bnd_shape = amio_describe(dataset, lat_bounds_name.c_str(), &lat_bnd_shape, &bnd_ts) == AMIO_OK;
            auto axis_is_1d_bounds = [](const amio_shape_t& shape) {
                // CF axis bounds are (axis, nv) or (nv, axis) with nv == 2.
                // Per-cell corner lists carry a dimension of 4 instead.
                return shape.rank == 2 && (shape.extents[0] == 2 || shape.extents[1] == 2) && shape.extents[0] != 4 && shape.extents[1] != 4;
            };
            // Return the bounds as interleaved [count][2] pairs. A (count, nv)
            // array already flattens that way; a (nv, count) array stores the
            // low and high edges as two contiguous blocks and must be reordered.
            auto axis_bound_pairs = [](const std::vector<double>& values, const amio_shape_t& shape, size_t& count) {
                count = 0;
                if (shape.rank != 2) return std::vector<double>(values);
                const size_t major = static_cast<size_t>(shape.extents[0]);
                const size_t minor = static_cast<size_t>(shape.extents[1]);
                if (values.size() != major * minor) return std::vector<double>(values);
                if (minor == 2) {
                    count = major;
                    return std::vector<double>(values);
                }
                if (major == 2) {
                    count = minor;
                    std::vector<double> pairs(values.size());
                    for (size_t k = 0; k < minor; ++k) {
                        pairs[2 * k] = values[k];
                        pairs[2 * k + 1] = values[minor + k];
                    }
                    return pairs;
                }
                return std::vector<double>(values);
            };
            const bool is_1d_rectilinear =
                has_lon_bnd_shape && has_lat_bnd_shape && axis_is_1d_bounds(lon_bnd_shape) && axis_is_1d_bounds(lat_bnd_shape);

            // 5. Read the boundary arrays using the names we just resolved.
            //    Wrapping +180° longitudes is only safe for per-cell corner
            //    lists; 1-D axis bounds must stay monotone so an eastern cell
            //    [175, 180] is not inverted across the seam.
            int total_lon_bnd_pts = 0;
            int total_lat_bnd_pts = 0;
            std::vector<double> cf_lon_bnds = read_coordinate_array(dataset, lon_bounds_name, false, !is_1d_rectilinear, total_lon_bnd_pts);
            std::vector<double> cf_lat_bnds = read_coordinate_array(dataset, lat_bounds_name, false, false, total_lat_bnd_pts);

            close_amio();

            // 6. Build the requested (j0, nband) row slice, not the whole file.
            if (is_1d_rectilinear) {
                size_t file_nx = 0, file_ny = 0;
                const auto lon_pairs = axis_bound_pairs(cf_lon_bnds, lon_bnd_shape, file_nx);
                const auto lat_pairs = axis_bound_pairs(cf_lat_bnds, lat_bnd_shape, file_ny);
                if (file_nx != static_cast<size_t>(nx) || static_cast<size_t>(j0) + static_cast<size_t>(nband) > file_ny) {
                    throw std::runtime_error("CF 1D bounds dimensions do not match requested (nx, j0, nband) slice: " + gridspec_file);
                }
                return build_rectilinear_strip_mesh(nx, nband, j0, lon_pairs, lat_pairs);
            }
            if (total_lon_bnd_pts != total_lat_bnd_pts || total_lon_bnd_pts % 4 != 0) {
                throw std::runtime_error("Unsupported CF bounds array dimensions in '" + gridspec_file + "'.");
            }
            const size_t expected_cells = static_cast<size_t>(nx) * static_cast<size_t>(nband);
            const size_t total_cells_in_file = static_cast<size_t>(total_lon_bnd_pts) / 4;
            const size_t cell_offset = static_cast<size_t>(j0) * static_cast<size_t>(nx);
            if (cell_offset + expected_cells > total_cells_in_file) {
                throw std::runtime_error("CF 2D bounds array is smaller than requested (nx, j0, nband) slice: " + gridspec_file);
            }
            Kokkos::View<double**, Kokkos::LayoutLeft, Kokkos::HostSpace> node_coords("node_coords", expected_cells * 4, 2);
            Kokkos::View<axis::index_t*, Kokkos::HostSpace> conn_offsets("conn_offsets", expected_cells + 1);
            Kokkos::View<axis::index_t*, Kokkos::HostSpace> conn_indices("conn_indices", expected_cells * 4);
            for (size_t cell = 0; cell < expected_cells; ++cell) {
                conn_offsets(cell) = static_cast<axis::index_t>(cell * 4);
                for (int v = 0; v < 4; ++v) {
                    const size_t src_v_idx = (cell_offset + cell) * 4 + static_cast<size_t>(v);
                    const size_t dst_v_idx = cell * 4 + static_cast<size_t>(v);
                    node_coords(dst_v_idx, 0) = cf_lon_bnds[src_v_idx];
                    node_coords(dst_v_idx, 1) = cf_lat_bnds[src_v_idx];
                    conn_indices(dst_v_idx) = static_cast<axis::index_t>(dst_v_idx);
                }
            }
            conn_offsets(expected_cells) = static_cast<axis::index_t>(expected_cells * 4);
            return seal_great_circle_mesh(node_coords, conn_offsets, conn_indices);

        } catch (const std::exception& e) {
            // Resource cleanup is important if an exception gets thrown inside the try-block
            close_amio();
            throw;
        }
    }

    close_amio();
    throw std::runtime_error("Unsupported gridspec mesh topology convention in '" + gridspec_file +
                             "' (expected SCRIP [grid_corner_lon], MPAS [latVertex], UFS/FV3 grid_spec [grid_lon/grid_lat], UFS/FV3 supergrid [x/y], "
                             "CF rectilinear [1-D lon/lat with bounds], or CF curvilinear [explicit corner bounds])");
}

axis::topology::UnstructuredMesh<Kokkos::HostSpace> build_axis_mesh(int nx, int nband, int j0, const std::vector<double>& lons,
                                                                    const std::vector<double>& lats, const std::string& gridspec_file) {
    if (!gridspec_file.empty() && gridspec_file != "none" && gridspec_file != "NONE") {
        return load_mesh_from_file(nx, nband, j0, gridspec_file);
    }

    if (nband == 1) {
        // Build unstructured mesh of nx quadrilaterals dynamically to support nband = 1 in standalone driver
        size_t n_cells = static_cast<size_t>(nx);
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

            conn_offsets(i) = static_cast<axis::index_t>(4 * i);

            conn_indices(4 * i + 0) = static_cast<axis::index_t>(4 * i + 0);
            conn_indices(4 * i + 1) = static_cast<axis::index_t>(4 * i + 1);
            conn_indices(4 * i + 2) = static_cast<axis::index_t>(4 * i + 2);
            conn_indices(4 * i + 3) = static_cast<axis::index_t>(4 * i + 3);
        }
        conn_offsets(n_cells) = static_cast<axis::index_t>(4 * n_cells);

        return axis::topology::make_unstructured(node_coords, conn_offsets, conn_indices, axis::topology::CoordinateSystem::SphericalDeg);
    }

    size_t n_cells = static_cast<size_t>(nx) * nband;
    Kokkos::View<double*, Kokkos::HostSpace> center_lon("center_lon", n_cells);
    Kokkos::View<double*, Kokkos::HostSpace> center_lat("center_lat", n_cells);

    bool curvilinear = (lons.size() == n_cells && lats.size() == n_cells);

    for (int j = 0; j < nband; ++j) {
        for (int i = 0; i < nx; ++i) {
            size_t idx = static_cast<size_t>(j) * nx + i;
            if (curvilinear) {
                center_lon(idx) = lons[idx];
                center_lat(idx) = lats[idx];
            } else {
                center_lon(idx) = lons[i];
                center_lat(idx) = lats[j];
            }
        }
    }

    axis::topology::StructuredGrid<Kokkos::HostSpace> grid(nx, nband, center_lon, center_lat, axis::topology::CoordinateSystem::SphericalDeg);

    auto mesh = grid.to_unstructured();
    auto metadata = mesh.geometry_metadata();
    metadata.provenance = axis::topology::GeometryProvenance::Reconstructed;
    mesh.set_geometry_metadata(metadata);
    return mesh;
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
    auto mesh = grid.to_unstructured();
    auto metadata = mesh.geometry_metadata();
    metadata.provenance = axis::topology::GeometryProvenance::Reconstructed;
    mesh.set_geometry_metadata(metadata);
    return mesh;
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
    // Keep model metadata aligned with the global curvilinear mesh. Since
    // corners are explicit, this policy does not reconstruct them; it declares
    // the model represented by that supplied corner lattice.
    auto mesh = grid.to_unstructured(axis::topology::CornerPolicy::CurvilinearApproximate, axis::topology::LongitudePeriodicity{false, 360.0, true});
    auto metadata = mesh.geometry_metadata();
    metadata.provenance = axis::topology::GeometryProvenance::Reconstructed;
    // Explicit corners suppress AXIS's center-layout auto-detection. Mirror
    // the global builder's selected model from the full centers so degenerate
    // curvilinear inputs that are actually separable (for example nx=2 at a
    // particular warp phase) retain rectilinear strip area semantics.
    bool rectilinear_centers = true;
    for (std::size_t j = 0; j < ny_global; ++j) {
        for (int i = 1; i < nx; ++i) {
            const std::size_t current = j * static_cast<std::size_t>(nx) + static_cast<std::size_t>(i);
            const std::size_t row_first = j * static_cast<std::size_t>(nx);
            const std::size_t column_first = static_cast<std::size_t>(i);
            if (std::abs(full_center_lat[current] - full_center_lat[row_first]) > 1e-12 ||
                std::abs(full_center_lon[current] - full_center_lon[column_first]) > 1e-12) {
                rectilinear_centers = false;
                break;
            }
        }
        if (!rectilinear_centers) break;
    }
    if (rectilinear_centers) {
        for (std::size_t j = 1; j < ny_global; ++j) {
            for (int i = 0; i < nx; ++i) {
                const std::size_t current = j * static_cast<std::size_t>(nx) + static_cast<std::size_t>(i);
                if (std::abs(full_center_lon[current] - full_center_lon[static_cast<std::size_t>(i)]) > 1e-12) {
                    rectilinear_centers = false;
                    break;
                }
            }
            if (!rectilinear_centers) break;
        }
    }
    if (rectilinear_centers) {
        metadata.boundary_model = axis::topology::BoundaryModel::ConstantLatitude;
        metadata.area_model = axis::topology::AreaModel::ConstantLatitudeStrip;
    }
    mesh.set_geometry_metadata(metadata);
    if (rectilinear_centers) {
        // The global RectilinearMidpoint builder stores exact strip areas on
        // the mesh. Explicit band corners bypass that AXIS area allocation,
        // so attach the same unit-sphere strip areas to retain identical
        // conservative weights.
        const auto nodes = mesh.node_coords();
        const auto offsets = mesh.conn_offsets();
        const auto indices = mesh.conn_indices();
        Kokkos::View<double*, Kokkos::HostSpace> areas("curvilinear_band_strip_areas", static_cast<std::size_t>(nx) * nband);
        constexpr double radians = 3.14159265358979323846 / 180.0;
        for (int j = 0; j < nband; ++j) {
            for (int i = 0; i < nx; ++i) {
                const std::size_t cell = static_cast<std::size_t>(j) * nx + i;
                const auto first = indices(offsets(cell));
                const auto second = indices(offsets(cell) + 1);
                const auto fourth = indices(offsets(cell) + 3);
                const double west = nodes(first, 0) * radians;
                const double east = nodes(second, 0) * radians;
                const double south = nodes(first, 1) * radians;
                const double north = nodes(fourth, 1) * radians;
                areas(cell) = std::abs(east - west) * (std::sin(std::max(north, south)) - std::sin(std::min(north, south)));
            }
        }
        Kokkos::View<double**, Kokkos::LayoutLeft, Kokkos::HostSpace> node_view("curvilinear_band_nodes", nodes.extent(0), nodes.extent(1));
        Kokkos::View<axis::index_t*, Kokkos::HostSpace> offset_view("curvilinear_band_offsets", offsets.extent(0));
        Kokkos::View<axis::index_t*, Kokkos::HostSpace> index_view("curvilinear_band_indices", indices.extent(0));
        for (std::size_t node = 0; node < nodes.extent(0); ++node)
            for (std::size_t dim = 0; dim < nodes.extent(1); ++dim) node_view(node, dim) = nodes(node, dim);
        for (std::size_t k = 0; k < offsets.extent(0); ++k) offset_view(k) = offsets(k);
        for (std::size_t k = 0; k < indices.extent(0); ++k) index_view(k) = indices(k);
        mesh = axis::topology::make_unstructured(std::move(node_view), std::move(offset_view), std::move(index_view), mesh.coord_system(),
                                                 std::move(areas), Kokkos::View<int*, Kokkos::HostSpace>{}, metadata);
    }
    return mesh;
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
                       const std::string& dst_gridspec_file, RegridPlan& plan, const GeometrySettings& source_settings,
                       const GeometrySettings& destination_settings) {
    plan = RegridPlan{};
    // Stage 1: load an algorithm-independent owned source geometry snapshot.
    GeometrySettings source_geometry_settings = source_settings;
    source_geometry_settings.gridspec_file = src_gridspec_file;
    const GridGeometry loaded_source = load_grid_geometry(read_dataset, source_geometry_settings);
    plan.source_geometry_revision = loaded_source.revision;
    const auto source_geometry_owner = std::make_shared<const GridGeometry>(loaded_source);
    const PreparedGeometry prepared_source = prepare_grid_geometry(source_geometry_owner, source_geometry_settings);
    const bool source_has_prepared_cells = prepared_source.cells.has_value();
    GeometrySettings prepared_destination_settings = destination_settings;
    prepared_destination_settings.gridspec_file = dst_gridspec_file;
    plan.file_nx = loaded_source.nx;
    plan.file_ny = loaded_source.ny;
    const std::vector<double>& src_lons = loaded_source.center_lon;
    const std::vector<double>& src_lats = loaded_source.center_lat;

    {
        double min_lon = *std::min_element(src_lons.begin(), src_lons.end());
        double max_lon = *std::max_element(src_lons.begin(), src_lons.end());
        double min_lat = *std::min_element(src_lats.begin(), src_lats.end());
        double max_lat = *std::max_element(src_lats.begin(), src_lats.end());
        CECE_LOG_DEBUG("[DRIVER DEBUG] AMIO retrieved source coordinates successfully! file_nx=" + std::to_string(plan.file_nx) +
                       ", file_ny=" + std::to_string(plan.file_ny) + ", lon_range=[" + std::to_string(min_lon) + ", " + std::to_string(max_lon) +
                       "], lat_range=[" + std::to_string(min_lat) + ", " + std::to_string(max_lat) + "]");
    }

    // Stage 2 starts only after source coordinates have been loaded and their
    // layout is known. This canonical setting is then passed into stage 3.
    const RegridMethodSettings method_settings = resolve_regrid_method(map_algo);

    plan.j0 = j0;
    plan.j1 = j1;

    if (method_settings.method == RegridMethod::Passthrough) {
        GridGeometry source_geometry = loaded_source;
        GridGeometry destination_geometry;
        destination_geometry.nx = nx;
        destination_geometry.ny = ny;
        destination_geometry.center_lon = target_lons;
        destination_geometry.center_lat = target_lats;
        (void)validate_regrid_prerequisites(source_geometry, destination_geometry, method_settings, j0, j1);
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
    const bool destination_has_gridspec = !dst_gridspec_file.empty() && dst_gridspec_file != "none" && dst_gridspec_file != "NONE";
    std::shared_ptr<const GridGeometry> named_destination_geometry;
    if (!prepared_destination_settings.named_grid.empty()) {
        try {
            auto named = load_named_grid_geometry(prepared_destination_settings.named_grid);
            const auto same_axis = [](const std::vector<double>& a, const std::vector<double>& b) {
                if (a.size() != b.size()) return false;
                for (std::size_t i = 0; i < a.size(); ++i)
                    if (std::abs(a[i] - b[i]) > 1e-10) return false;
                return true;
            };
            if (named.nx == nx && named.ny == ny && same_axis(named.center_lon, target_lons) && same_axis(named.center_lat, target_lats))
                named_destination_geometry = std::make_shared<const GridGeometry>(std::move(named));
        } catch (const std::exception& error) {
            CECE_LOG_DEBUG("[DRIVER] Could not use AXIS named destination mesh: " + std::string(error.what()));
        }
    }
    const bool destination_has_named_grid = static_cast<bool>(named_destination_geometry);
    auto build_prepared_destination_geometry = [&]() {
        if (destination_has_named_grid) {
            return std::make_shared<GridGeometry>(*named_destination_geometry);
        }
        auto global_geometry = std::make_shared<GridGeometry>();
        global_geometry->nx = nx;
        global_geometry->ny = ny;
        global_geometry->center_lon = target_lons;
        global_geometry->center_lat = target_lats;
        if (destination_has_gridspec) {
            auto global_mesh = build_axis_mesh(nx, ny, 0, target_lons, target_lats, dst_gridspec_file);
            global_geometry->metadata = global_mesh.geometry_metadata();
            global_geometry->has_cell_geometry = global_mesh.n_cells() > 0;
            global_geometry->supplied_cells = std::make_shared<const axis::topology::UnstructuredMesh<Kokkos::HostSpace>>(std::move(global_mesh));
        } else {
            const auto prepared = prepare_grid_geometry(global_geometry, prepared_destination_settings);
            if (!prepared.cells)
                throw RegridError(RegridStage::GeometryLoad, GridRole::Destination, {}, "destination reconstruction produced no cells",
                                  "Supply destination bounds or choose a supported reconstruction model");
            global_geometry->metadata = prepared.metadata;
            global_geometry->has_cell_geometry = true;
            global_geometry->supplied_cells = std::make_shared<const axis::topology::UnstructuredMesh<Kokkos::HostSpace>>(*prepared.cells);
        }
        global_geometry->revision = grid_geometry_revision(*global_geometry);
        return global_geometry;
    };
    auto build_prepared_destination_band = [&]() {
        auto global_geometry = build_prepared_destination_geometry();
        auto partition = partition_grid_geometry(std::move(global_geometry), j0, j1);
        if (!partition.cells)
            throw RegridError(RegridStage::GeometryLoad, GridRole::Destination, {}, "destination band partition has no cell mesh",
                              "Verify the selected destination geometry contains cells");
        return std::move(*partition.cells);
    };
    if ((method_settings.method == RegridMethod::Conservative1 || method_settings.method == RegridMethod::Conservative2) &&
        (!source_has_prepared_cells ||
         (!destination_has_gridspec && !destination_has_named_grid && prepared_destination_settings.reconstruction == ReconstructionModel::None))) {
        const bool missing_source = !source_has_prepared_cells;
        const bool missing_destination =
            !destination_has_gridspec && !destination_has_named_grid && prepared_destination_settings.reconstruction == ReconstructionModel::None;
        const auto role = missing_source && missing_destination ? GridRole::Both : missing_source ? GridRole::Source : GridRole::Destination;
        throw RegridError(RegridStage::PrerequisiteValidation, role, method_settings.canonical_name,
                          "conservative mapping needs prepared cell boundaries; center-only geometry cannot be reconstructed implicitly",
                          "Supply authoritative gridspec bounds or an explicit supported geometry declaration");
    }

    const double source_min_longitude = *std::min_element(src_lons.begin(), src_lons.end());
    const double source_max_longitude = *std::max_element(src_lons.begin(), src_lons.end());
    const bool sample_use_360_range = source_max_longitude > 180.0 && source_min_longitude >= -1e-5;

    // Center-only nearest-neighbor is a sample operation. Keep it independent
    // of polygon synthesis and preserve the input cell ordering in matrix
    // columns, including for curvilinear centers.
    if (method_settings.method == RegridMethod::Nearest || method_settings.method == RegridMethod::Bilinear ||
        method_settings.method == RegridMethod::Bicubic) {
        const std::size_t source_count = static_cast<std::size_t>(plan.file_nx) * plan.file_ny;
        const std::size_t destination_count = static_cast<std::size_t>(nx) * nband;
        GridGeometry source_geometry;
        source_geometry.nx = plan.file_nx;
        source_geometry.ny = plan.file_ny;
        source_geometry.center_lon = src_lons;
        source_geometry.center_lat = src_lats;
        GridGeometry destination_geometry;
        destination_geometry.nx = nx;
        destination_geometry.ny = ny;
        destination_geometry.center_lon = target_lons;
        destination_geometry.center_lat = target_lats;
        const auto validated_request = validate_regrid_prerequisites(source_geometry, destination_geometry, method_settings, j0, j1);

        axis::solver::SampleGrid source_samples, destination_samples;
        source_samples.positions = Kokkos::View<double**, Kokkos::LayoutLeft, Kokkos::HostSpace>("source_samples", source_count, 2);
        source_samples.nx = plan.file_nx;
        source_samples.ny = plan.file_ny;
        for (std::size_t k = 0; k < source_count; ++k) {
            source_samples.positions(k, 0) = src_lons.size() == source_count ? src_lons[k] : src_lons[k % plan.file_nx];
            source_samples.positions(k, 1) = src_lats.size() == source_count ? src_lats[k] : src_lats[k / plan.file_nx];
        }
        destination_samples.positions = Kokkos::View<double**, Kokkos::LayoutLeft, Kokkos::HostSpace>("destination_samples", destination_count, 2);
        destination_samples.nx = nband > 0 ? static_cast<std::size_t>(nx) : 0;
        destination_samples.ny = nband > 0 ? static_cast<std::size_t>(nband) : 0;
        for (int j = 0; j < nband; ++j) {
            for (int i = 0; i < nx; ++i) {
                const std::size_t local = static_cast<std::size_t>(j) * nx + i;
                const std::size_t global = static_cast<std::size_t>(j + j0) * nx + i;
                double lon = target_lons.size() == static_cast<std::size_t>(nx) * ny ? target_lons[global] : target_lons[i];
                if (sample_use_360_range) {
                    if (lon < 0.0)
                        lon += 360.0;
                    else if (lon >= 360.0)
                        lon -= 360.0;
                } else if (lon >= 180.0) {
                    lon -= 360.0;
                } else if (lon < -180.0) {
                    lon += 360.0;
                }
                destination_samples.positions(local, 0) = lon;
                destination_samples.positions(local, 1) =
                    target_lats.size() == static_cast<std::size_t>(nx) * ny ? target_lats[global] : target_lats[j + j0];
            }
        }
        axis::solver::RegridConfig sample_config;
        switch (validated_request.settings.method) {
            case RegridMethod::Nearest:
                sample_config.method = axis::solver::InterpolationMethod::NearestNeighbor;
                break;
            case RegridMethod::Bilinear:
                sample_config.method = axis::solver::InterpolationMethod::Bilinear;
                break;
            case RegridMethod::Bicubic:
                sample_config.method = axis::solver::InterpolationMethod::Bicubic;
                break;
            default:
                throw std::logic_error("sample interpolation received a non-sample method");
        }
        sample_config.norm_type = axis::solver::NormType::DstArea;
        sample_config.unmapped = axis::solver::UnmappedAction::Ignore;
        plan.matrix = axis::solver::WeightGenerator::generate_from_samples(source_samples, destination_samples,
                                                                           axis::topology::CoordinateSystem::SphericalDeg, sample_config);
        plan.matrix.to_csr();
        if (plan.matrix.nnz() > 0) {
            const auto* cols = plan.matrix.factor_col().data_handle();
            axis::index_t min_col = cols[0], max_col = cols[0];
            for (std::size_t k = 1; k < plan.matrix.nnz(); ++k) {
                min_col = std::min(min_col, cols[k]);
                max_col = std::max(max_col, cols[k]);
            }
            const int first_row = static_cast<int>(min_col) / plan.file_nx;
            const int last_row = static_cast<int>(max_col) / plan.file_nx;
            if (first_row >= 0 && last_row < plan.file_ny) {
                plan.src_j0 = first_row;
                plan.src_rows = last_row - first_row + 1;
            }
        }
        plan.built = true;
        return true;
    }

    if (nband <= 0) {
        // An empty rank still loads and assesses the complete endpoint
        // geometry. It merely skips local weight generation.
        auto source_mesh =
            prepared_source.cells ? *prepared_source.cells : build_axis_mesh(plan.file_nx, plan.file_ny, 0, src_lons, src_lats, src_gridspec_file);
        auto destination_mesh =
            (destination_has_gridspec || destination_has_named_grid || prepared_destination_settings.reconstruction != ReconstructionModel::None)
                ? *build_prepared_destination_geometry()->supplied_cells
                : build_axis_mesh(nx, ny, 0, target_lons, target_lats, dst_gridspec_file);
        GridGeometry source_geometry;
        source_geometry.nx = plan.file_nx;
        source_geometry.ny = plan.file_ny;
        source_geometry.center_lon = src_lons;
        source_geometry.center_lat = src_lats;
        source_geometry.has_cell_geometry = source_mesh.n_cells() > 0;
        source_geometry.metadata = source_mesh.geometry_metadata();
        GridGeometry destination_geometry;
        destination_geometry.nx = nx;
        destination_geometry.ny = ny;
        destination_geometry.center_lon = target_lons;
        destination_geometry.center_lat = target_lats;
        destination_geometry.has_cell_geometry = destination_mesh.n_cells() > 0;
        destination_geometry.metadata = destination_mesh.geometry_metadata();
        (void)validate_regrid_prerequisites(source_geometry, destination_geometry, method_settings, j0, j1);
        plan.built = true;
        return true;
    }

    // A. Build the (global) source mesh and the rank-local destination sub-mesh.
    auto src_mesh =
        prepared_source.cells ? *prepared_source.cells : build_axis_mesh(plan.file_nx, plan.file_ny, 0, src_lons, src_lats, src_gridspec_file);

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
    // — for rectilinear AND curvilinear grids. A gridspec/file target supplies
    // explicit cells, which are loaded globally then partitioned with stable
    // global row order and compact local connectivity.
    std::vector<double> full_center_lon;  // normalized full global centers (curvilinear only)
    if (curvilinear_target) {
        full_center_lon = target_lons;
        normalize_lons(full_center_lon);
        band_lons.assign(full_center_lon.begin() + static_cast<size_t>(j0) * nx, full_center_lon.begin() + static_cast<size_t>(j1) * nx);
    } else {
        normalize_lons(band_lons);
    }

    axis::topology::UnstructuredMesh<Kokkos::HostSpace> dst_mesh =
        (!destination_has_gridspec && !destination_has_named_grid && prepared_destination_settings.reconstruction == ReconstructionModel::None)
            ? (curvilinear_target ? build_band_mesh_curvilinear_with_global_corners(nx, j0, j1, full_center_lon, target_lats, band_lons, band_lats)
                                  : build_band_mesh_with_global_corners(nx, j0, j1, band_lons, target_lats))
            : build_prepared_destination_band();

    // Stage 2: assess the fully prepared source and destination snapshots. Mesh
    // construction above does not depend on the selected interpolation method;
    // no weights exist until this check succeeds.
    GridGeometry source_geometry;
    source_geometry.nx = plan.file_nx;
    source_geometry.ny = plan.file_ny;
    source_geometry.center_lon = src_lons;
    source_geometry.center_lat = src_lats;
    source_geometry.has_cell_geometry = src_mesh.n_cells() > 0;
    source_geometry.metadata = src_mesh.geometry_metadata();
    GridGeometry destination_geometry;
    destination_geometry.nx = nx;
    destination_geometry.ny = ny;
    destination_geometry.center_lon = target_lons;
    destination_geometry.center_lat = target_lats;
    destination_geometry.has_cell_geometry = dst_mesh.n_cells() > 0;
    destination_geometry.metadata = dst_mesh.geometry_metadata();
    const auto validated_request = validate_regrid_prerequisites(source_geometry, destination_geometry, method_settings, j0, j1);
    (void)validated_request;

    // B. Configure weight generation method.
    axis::solver::RegridConfig regrid_cfg;
    switch (validated_request.settings.method) {
        case RegridMethod::Nearest:
            regrid_cfg.method = axis::solver::InterpolationMethod::NearestNeighbor;
            break;
        case RegridMethod::Bilinear:
            regrid_cfg.method = axis::solver::InterpolationMethod::Bilinear;
            break;
        case RegridMethod::Bicubic:
            regrid_cfg.method = axis::solver::InterpolationMethod::Bicubic;
            break;
        case RegridMethod::Conservative2:
            regrid_cfg.method = axis::solver::InterpolationMethod::Conservative2ndOrder;
            break;
        case RegridMethod::Conservative1:
            regrid_cfg.method = axis::solver::InterpolationMethod::Conservative1stOrder;
            break;
        case RegridMethod::Passthrough:
            break;  // handled before mesh construction
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

bool publish_regrid_plan_if_ready(bool all_ranks_ready, const std::string& key, std::optional<RegridPlan>& candidate,
                                  std::unordered_map<std::string, RegridPlan>& plans) {
    if (!all_ranks_ready || !candidate || !candidate->built) return false;
    plans.insert_or_assign(key, std::move(*candidate));
    candidate.reset();
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
