// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors

#include "cece/cece_regridder_utils.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>

namespace cece::io {

axis::topology::UnstructuredMesh<Kokkos::HostSpace> build_axis_mesh(int ni, int nj, const std::vector<double>& lons,
                                                                    const std::vector<double>& lats) {
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

bool build_regrid_plan(amio_dataset_handle read_dataset, int nx, int ny, const std::vector<double>& target_lons,
                       const std::vector<double>& target_lats, const std::string& map_algo, int j0, int j1, RegridPlan& plan) {
    // Read a 1-D, 2-D or 3-D coordinate variable, trying several common naming conventions.
    auto read_coord = [&](const std::vector<std::string>& candidate_names, std::vector<double>& out, int& nx_val, int& ny_val) {
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
                            out[i] = is_float ? static_cast<const float*>(data)[i] : static_cast<const double*>(data)[i];
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
    static const std::vector<std::string> kLonNames = {"lon",           "longitude", "x",       "Longitude", "LON",     "geolon",
                                                       "grid_xt",       "grid_lont", "lon_rho", "nav_lon",   "lonCell", "mesh_node_x",
                                                       "mesh2d_node_x", "node_x",    "XLONG"};
    int lon_nx = 0, lon_ny = 0;
    std::vector<double> src_lons;
    read_coord(kLonNames, src_lons, lon_nx, lon_ny);

    // 2. Read source latitude coordinates (same convention families as above).
    static const std::vector<std::string> kLatNames = {"lat",           "latitude",  "y",       "Latitude", "LAT",     "geolat",
                                                       "grid_yt",       "grid_latt", "lat_rho", "nav_lat",  "latCell", "mesh_node_y",
                                                       "mesh2d_node_y", "node_y",    "XLAT"};
    int lat_nx = 0, lat_ny = 0;
    std::vector<double> src_lats;
    read_coord(kLatNames, src_lats, lat_nx, lat_ny);

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

    if (map_algo == "passthrough") {
        if (nx != plan.file_nx || ny != plan.file_ny) {
            std::cerr << "[DRIVER ERROR] passthrough regridding requested but grid dimensions do not match! "
                      << "Source grid: " << plan.file_nx << "x" << plan.file_ny << ", Target grid: " << nx << "x" << ny << std::endl;
            throw std::runtime_error("passthrough regridding dimension mismatch");
        }
    }

    plan.j0 = j0;
    plan.j1 = j1;

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

    auto dst_mesh = build_axis_mesh(nx, nband, band_lons, band_lats);

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
