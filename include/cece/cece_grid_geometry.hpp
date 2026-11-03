// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine

#ifndef CECE_GRID_GEOMETRY_HPP
#define CECE_GRID_GEOMETRY_HPP

#include <amio/amio.h>

#include <axis/topology/enums.hpp>
#include <axis/topology/unstructured_mesh.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace cece::io {

enum class ReconstructionModel : std::uint8_t { None, RectilinearMidpoint, GaussianLatLon, CurvilinearApproximate };
ReconstructionModel parse_reconstruction_model(const std::string& name);

/// Geometry choices are deliberately independent of the interpolation method.
struct GeometrySettings {
    std::string gridspec_file;
    std::string named_grid;
    std::string longitude_variable;
    std::string latitude_variable;
    ReconstructionModel reconstruction{ReconstructionModel::None};
    std::optional<axis::topology::LongitudePeriodicity> longitude_periodicity;
    std::optional<axis::topology::BoundaryModel> boundary_model;
    std::optional<axis::topology::AreaModel> area_model;
    bool same_order_declared{false};
};

/// Pipeline stage that produced a geometry/regridding diagnostic.
enum class RegridStage : std::uint8_t { GeometryLoad, PrerequisiteValidation, PlanBuild };

/// Source or destination endpoint to which a diagnostic applies.
enum class GridRole : std::uint8_t { Source, Destination, Both, None };

/// A diagnostic with enough context to suggest a configuration correction.
class RegridError : public std::runtime_error {
   public:
    RegridError(RegridStage stage, GridRole role, std::string method, std::string reason, std::string correction);

    RegridStage stage() const noexcept {
        return stage_;
    }
    GridRole role() const noexcept {
        return role_;
    }
    const std::string& method() const noexcept {
        return method_;
    }
    const std::string& reason() const noexcept {
        return reason_;
    }
    const std::string& correction() const noexcept {
        return correction_;
    }

   private:
    RegridStage stage_;
    GridRole role_;
    std::string method_;
    std::string reason_;
    std::string correction_;
};

/// Owned coordinate snapshot; center positions remain available when no cell
/// boundary geometry was supplied. Vectors use row-major cell ordering.
struct GridGeometry {
    int nx{0};
    int ny{0};
    bool centers_curvilinear{false};
    axis::topology::CoordinateSystem coordinate_system{axis::topology::CoordinateSystem::SphericalDeg};
    std::vector<double> center_lon;
    std::vector<double> center_lat;
    std::vector<double> corner_lon;
    std::vector<double> corner_lat;
    std::vector<int> mask;
    std::shared_ptr<const axis::topology::UnstructuredMesh<Kokkos::HostSpace>> supplied_cells;
    axis::topology::GeometryMetadata metadata{};
    bool has_explicit_bounds{false};
    bool has_cell_geometry{false};
    std::uint64_t revision{0};
    std::string provenance;
};

/// Generate a supported named target grid through AXIS, retaining its
/// authoritative cell mesh and returning center coordinates in matching order.
GridGeometry load_named_grid_geometry(const std::string& name);

/// Deterministic fingerprint of the owned geometry snapshot, including ordered
/// coordinates, bounds, masks, and model/provenance metadata.
std::uint64_t grid_geometry_revision(const GridGeometry& geometry);

/// A row partition references its immutable global snapshot and keeps global
/// destination indices even when its local band is empty.
struct GeometryPartition {
    std::shared_ptr<const GridGeometry> parent;
    int j0{0};
    int j1{0};
    std::vector<std::size_t> global_cell_indices;
    axis::topology::GeometryMetadata metadata{};
    std::optional<axis::topology::UnstructuredMesh<Kokkos::HostSpace>> cells;
};

/// Select a contiguous row band while retaining global cell identity. When the
/// snapshot has cells, produce a band mesh with node connectivity rebased to
/// the compact local node array.
GeometryPartition partition_grid_geometry(std::shared_ptr<const GridGeometry> geometry, int j0, int j1);

/// Prepared host geometry keeps the original center arrays and metadata next
/// to optional finalized cell geometry. The mesh is absent for sample-only
/// grids and its metadata must agree with the inherited model declaration.
struct PreparedGeometry {
    std::shared_ptr<const GridGeometry> geometry;
    std::optional<axis::topology::UnstructuredMesh<Kokkos::HostSpace>> cells;
    axis::topology::GeometryMetadata metadata{};
};

/// Load a selected mesh convention from a geometry file. The adapter owns
/// topology decoding; callers separately validate method eligibility.
axis::topology::UnstructuredMesh<Kokkos::HostSpace> load_mesh_from_file(int nx, int nband, int j0, const std::string& gridspec_file);

/// Read an AMIO dataset's ordered centers and any selected geometry without
/// depending on a regridding algorithm.
GridGeometry load_grid_geometry(amio_dataset_handle dataset, const GeometrySettings& settings = {});

/// Stage 1: apply only the geometry policy explicitly present in settings.
PreparedGeometry prepare_grid_geometry(std::shared_ptr<const GridGeometry> geometry, const GeometrySettings& settings);

enum class RegridMethod : std::uint8_t { Nearest, Bilinear, Bicubic, Conservative1, Conservative2, Passthrough };

struct RegridMethodSettings {
    RegridMethod method{RegridMethod::Conservative1};
    std::string canonical_name{"consd"};
    std::string normalization{"dstarea"};
    std::string unmapped{"ignore"};
};

enum class RegridBackend : std::uint8_t { Host, Device };

struct PlanKey {
    std::uint64_t source_revision{0};
    std::uint64_t destination_revision{0};
    RegridMethod method{RegridMethod::Conservative1};
    RegridBackend backend{RegridBackend::Host};
    int j0{0};
    int j1{0};
    std::string normalization{"dstarea"};
    std::string unmapped{"ignore"};

    bool operator==(const PlanKey&) const = default;
};

/// Immutable-by-convention request produced only after both endpoints pass
/// method-specific prerequisite assessment.
struct ValidatedRegridRequest {
    RegridMethodSettings settings;
    std::uint64_t source_revision{0};
    std::uint64_t destination_revision{0};
    int j0{0};
    int j1{0};
    RegridBackend backend{RegridBackend::Host};

    PlanKey plan_key() const {
        return {source_revision, destination_revision, settings.method, backend, j0, j1, settings.normalization, settings.unmapped};
    }
};

}  // namespace cece::io

#endif  // CECE_GRID_GEOMETRY_HPP
