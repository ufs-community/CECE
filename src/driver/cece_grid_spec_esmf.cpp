// SPDX-License-Identifier: Apache-2.0
// Copyright (c) HELM Project Contributors

/**
 * @file cece_grid_spec_esmf.cpp
 * @brief GridSpec::from_esmf / describe — the parent-grid (NUOPC) side of
 * the shared grid contract.
 *
 * The Fortran cap performs the ESMF extraction (ESMF_GridGetCoord for 1-D
 * and 2-D coordinates, ESMF_MeshGet for unstructured node coordinates),
 * assembles the global arrays across PETs, and converts to degrees when the
 * source coordinate system is spherical-radians. This function applies the
 * remaining normalization shared with from_yaml — longitude wrapping and the
 * shape-based topology classification — and validates the result, so the
 * core receives the identical GridSpec shape rules regardless of which
 * driver produced it.
 */

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "cece/cece_grid_detail.hpp"
#include "cece/cece_simulation.hpp"

namespace cece {

GridSpec GridSpec::from_esmf(int nx, int ny, int nz, bool is_rad, const std::vector<double>& lon, const std::vector<double>& lat,
                             const std::string& gridspec_file) {
    GridSpec spec;
    spec.nx = nx;
    spec.ny = ny;
    spec.nz = nz;
    spec.gridspec_file = gridspec_file;

    if (nx <= 0 || ny <= 0 || nz <= 0) {
        throw std::invalid_argument("[GRID] ESMF grid: nx, ny, and nz must be positive (got nx=" + std::to_string(nx) + ", ny=" + std::to_string(ny) +
                                    ", nz=" + std::to_string(nz) + ")");
    }

    // Unit conversion: the cap converts SPH_RAD sources before the call; the
    // flag is honored here as well so a radian source can never slip through
    // un-normalized (the same is_radian rule from_yaml applies to file grids).
    // Kokkos note: these arrays are small target-grid metadata (O(nx+ny) or
    // O(nx*ny) host doubles) consumed once during grid construction, so this
    // normalization stays on the host; device-side execution would only add
    // deep-copy and launch overhead without any parallelism benefit.
    auto normalize = [&](const std::vector<double>& in, bool wrap_lon) {
        std::vector<double> out(in.size(), 0.0);
        for (size_t i = 0; i < in.size(); ++i) {
            double v = in[i];
            if (is_rad) {
                v = detail::radians_to_degrees(v);
            }
            out[i] = wrap_lon ? detail::wrap_longitude(v) : v;
        }
        return out;
    };

    // Shape-based classification. The ambiguity between a flattened
    // curvilinear grid (nx = cells, ny = 1) and an unstructured node list
    // (nx = nodes, ny = 1) cannot be decided from sizes alone; the cap names
    // the source (grid vs mesh) via the gridspec_file suffix or the caller's
    // explicit ny==1 convention, and both flatten to the same existing
    // (nx, 1) shape the writer already consumes.
    const size_t n_lon = lon.size();
    const size_t n_lat = lat.size();
    if (ny == 1) {
        if (n_lon != static_cast<size_t>(nx) || n_lat != static_cast<size_t>(nx)) {
            throw std::invalid_argument("[GRID] ESMF grid: flattened grid/mesh coordinates must carry nx=" + std::to_string(nx) +
                                        " values each (got lon=" + std::to_string(n_lon) + ", lat=" + std::to_string(n_lat) + ")");
        }
        spec.topology = GridTopology::Unstructured;
    } else if (n_lon == static_cast<size_t>(nx) && n_lat == static_cast<size_t>(ny)) {
        spec.topology = GridTopology::Rectilinear;
    } else if (n_lon == static_cast<size_t>(nx) * static_cast<size_t>(ny) && n_lat == static_cast<size_t>(nx) * static_cast<size_t>(ny)) {
        spec.topology = GridTopology::Curvilinear;
    } else {
        throw std::invalid_argument("[GRID] ESMF grid: unsupported coordinate shape lon=" + std::to_string(n_lon) + ", lat=" + std::to_string(n_lat) +
                                    " for nx=" + std::to_string(nx) + ", ny=" + std::to_string(ny) +
                                    " (expected 1-D lon[nx]/lat[ny], 2-D lon/lat[nx*ny], or flattened ny=1 node arrays)");
    }

    spec.lon_coords = normalize(lon, /*wrap_lon=*/true);
    spec.lat_coords = normalize(lat, /*wrap_lon=*/false);

    spec.validate();
    return spec;
}

std::string GridSpec::describe() const {
    const char* topo = "rectilinear";
    switch (topology) {
        case GridTopology::Curvilinear:
            topo = "curvilinear";
            break;
        case GridTopology::Unstructured:
            topo = "unstructured";
            break;
        case GridTopology::Rectilinear:
            break;
    }
    std::string s = std::string(topo) + " " + std::to_string(nx) + "x" + std::to_string(ny) + "x" + std::to_string(nz);
    if (!gridspec_file.empty()) {
        s += " (gridspec '" + gridspec_file + "')";
    }
    return s;
}

}  // namespace cece
