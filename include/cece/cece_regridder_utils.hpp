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

/// Build the destination sub-mesh for a rectilinear latitude band [j0, j1) with
/// GLOBALLY-CONSISTENT cell corners. Unlike a plain build_axis_mesh over a
/// band-local coordinate slice (whose outer latitude edges AXIS would extrapolate
/// one-sidedly from band-local centers — correct only on a uniform grid), this
/// derives every latitude edge from the FULL global `full_lats` center array and
/// every longitude edge from the full `full_lons` (periodic), so the band's
/// boundary rows carry the true global cell edges (the midpoint to the neighbour
/// row owned by an adjacent rank). This makes the band conservative regrid match
/// the corresponding rows of the global regrid exactly on non-uniform grids.
/// `full_lons` has length nx (rectilinear); `full_lats` is the full global
/// latitude center array of length >= j1.
axis::topology::UnstructuredMesh<Kokkos::HostSpace> build_band_mesh_with_global_corners(
    int nx, int j0, int j1, const std::vector<double>& full_lons, const std::vector<double>& full_lats);

/// Build the destination sub-mesh for a CURVILINEAR latitude band [j0, j1) with
/// GLOBALLY-CONSISTENT 2-D cell corners. The band's boundary corners depend on
/// neighbour centers in both longitude and latitude, so they are derived from the
/// FULL global curvilinear center arrays via the same AXIS shared corner-synthesis
/// kernel used for the rectilinear band (axis::topology::synthesize_band_corners),
/// then pinned with set_corners. This makes the curvilinear band conservative
/// regrid match the corresponding rows of the global regrid at the seams, and
/// leaves a whole-grid (single-rank) band byte-for-byte identical to the global
/// mesh. `full_center_lon`/`full_center_lat` are the flattened global centers
/// (length nx * ny_global, index i + j*nx); `band_center_lon`/`band_center_lat`
/// are the band's nx * (j1 - j0) centers (the global rows [j0, j1)).
axis::topology::UnstructuredMesh<Kokkos::HostSpace> build_band_mesh_curvilinear_with_global_corners(
    int nx, int j0, int j1, const std::vector<double>& full_center_lon, const std::vector<double>& full_center_lat,
    const std::vector<double>& band_center_lon, const std::vector<double>& band_center_lat);

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
    /// Exact source latitude-row window [src_j0, src_j0 + src_rows) that the
    /// weight matrix actually references (min/max column / file_nx). Band-scoped
    /// reads fetch only these rows instead of the full source record: under MPI
    /// this is the difference between every rank pulling the whole global field
    /// (16x replicated IO) and each rank pulling only its band's footprint.
    /// src_rows == 0 means "no window computed" (callers read full records);
    /// build_regrid_plan always sets it for built plans.
    int src_j0 = 0;
    int src_rows = 0;
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
