// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Copyright (c) HELM Project Contributors

#ifndef CECE_REGRIDDER_UTILS_HPP
#define CECE_REGRIDDER_UTILS_HPP

#include <amio/amio.h>

#include <Kokkos_Core.hpp>
#include <axis/axis.hpp>
#include <string>
#include <vector>

namespace cece::io {

/// Build an AXIS UnstructuredMesh from rectilinear coordinate arrays.
axis::topology::UnstructuredMesh<Kokkos::HostSpace> build_axis_mesh(int ni, int nj, const std::vector<double>& lons, const std::vector<double>& lats,
                                                                    const std::string& gridspec_file = "");

/// A precomputed, reusable regridding plan for one stream variable.
///
/// The interpolation weights are expensive to build (ArborX BVH + polygon
/// overlap) but only depend on the source and destination grids, so they are
/// generated once and reused across every timestep.
///
/// For MPI parallelism the destination grid is partitioned into contiguous
/// latitude-row bands: this plan owns the weights for the rows [j0, j1) that
/// belong to the local rank. The source mesh remains global so overlaps near
/// band boundaries stay exact.
struct RegridPlan {
    axis::solver::InterpolationMatrix<Kokkos::HostSpace> matrix;  ///< CSR weights: global-source -> local-dst-band
    int j0 = 0;                                                   ///< first destination row owned by this rank
    int j1 = 0;                                                   ///< one-past-last destination row owned by this rank
    int file_nx = 0;                                              ///< source longitude count (from coords)
    int file_ny = 0;                                              ///< source latitude count (from coords)
    bool identity = false;                                        ///< copy source cells directly; no AXIS weights are applied
    bool built = false;                                           ///< true once weights are generated
};

/// Return true when source and target longitude/latitude coordinates describe
/// the same ordered spherical grid. Rectilinear (lon[nx], lat[ny]) and flattened curvilinear
/// (lon[nx*ny], lat[nx*ny]) representations may be mixed. Longitude values
/// that differ only by a 360-degree convention are considered equal.
bool same_spherical_grid_coordinates(int nx, int ny, const std::vector<double>& source_lons, const std::vector<double>& source_lats,
                                     const std::vector<double>& target_lons, const std::vector<double>& target_lats, double tolerance = 1.0e-10);

/// Build the interpolation weights for a rank-local destination row band
/// [j0, j1). Reads the source `lon`/`lat` coordinate variables from the open
/// AMIO dataset to construct the (global) source mesh, builds the destination
/// sub-mesh for the band, and generates the sparse weight matrix.
///
/// @return true on success; false if coordinates could not be read.
bool build_regrid_plan(amio_dataset_handle read_dataset, int nx, int ny, const std::vector<double>& target_lons,
                       const std::vector<double>& target_lats, const std::string& map_algo, int j0, int j1, const std::string& gridspec_file,
                       RegridPlan& plan);

/// Apply a previously built plan to one source field snapshot, producing the
/// rank-local destination slice `local_dst` of size nx * (j1 - j0), laid out
/// row-major within the band (matching the global j-major layout).
///
/// @return true on success.
bool apply_regrid_plan(const RegridPlan& plan, size_t time_offset, bool is_float, const void* view_data, int file_nx, int file_ny, int nx,
                       std::vector<double>& local_dst);

}  // namespace cece::io

#endif  // CECE_REGRIDDER_UTILS_HPP
