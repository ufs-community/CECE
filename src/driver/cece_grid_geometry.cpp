// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine

#include "cece/cece_grid_geometry.hpp"

#include <amio/amio.h>

#include <algorithm>
#include <axis/topology/mesh_builder.hpp>
#include <axis/topology/mesh_factory.hpp>
#include <axis/topology/named_grid_registry.hpp>
#include <axis/topology/structured_grid.hpp>
#include <bit>
#include <cctype>
#include <cmath>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "cece/cece_amio_utils.hpp"

namespace cece::io {

std::uint64_t grid_geometry_revision(const GridGeometry& geometry) {
    std::uint64_t hash = 1469598103934665603ULL;
    auto add = [&](std::uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            const unsigned int shift = static_cast<unsigned int>(byte * 8);
            hash ^= (value >> shift) & 0xffULL;
            hash *= 1099511628211ULL;
        }
    };
    auto add_values = [&](const std::vector<double>& values) {
        add(values.size());
        for (double value : values) add(std::bit_cast<std::uint64_t>(value));
    };
    add(static_cast<std::uint64_t>(geometry.nx));
    add(static_cast<std::uint64_t>(geometry.ny));
    add(static_cast<std::uint64_t>(geometry.coordinate_system));
    add(geometry.centers_curvilinear);
    add_values(geometry.center_lon);
    add_values(geometry.center_lat);
    add_values(geometry.corner_lon);
    add_values(geometry.corner_lat);
    add(geometry.mask.size());
    for (int value : geometry.mask) add(static_cast<std::uint64_t>(value));
    add(static_cast<std::uint64_t>(geometry.metadata.provenance));
    add(static_cast<std::uint64_t>(geometry.metadata.boundary_model));
    add(static_cast<std::uint64_t>(geometry.metadata.area_model));
    add(geometry.metadata.longitude_periodicity.periodic);
    add(std::bit_cast<std::uint64_t>(geometry.metadata.longitude_periodicity.period));
    add(geometry.metadata.longitude_periodicity.increasing);
    add(geometry.has_explicit_bounds);
    add(geometry.has_cell_geometry);
    for (unsigned char value : geometry.provenance) add(value);
    if (geometry.supplied_cells) {
        const auto& cells = *geometry.supplied_cells;
        const auto nodes = cells.node_coords();
        add(nodes.extent(0));
        add(nodes.extent(1));
        for (std::size_t node = 0; node < nodes.extent(0); ++node)
            for (std::size_t dimension = 0; dimension < nodes.extent(1); ++dimension) add(std::bit_cast<std::uint64_t>(nodes(node, dimension)));
        const auto offsets = cells.conn_offsets();
        add(offsets.extent(0));
        for (std::size_t i = 0; i < offsets.extent(0); ++i) add(static_cast<std::uint64_t>(offsets[i]));
        const auto indices = cells.conn_indices();
        add(indices.extent(0));
        for (std::size_t i = 0; i < indices.extent(0); ++i) add(static_cast<std::uint64_t>(indices[i]));
        const auto areas = cells.cell_areas();
        add(areas.extent(0));
        for (std::size_t i = 0; i < areas.extent(0); ++i) add(std::bit_cast<std::uint64_t>(areas[i]));
    }
    return hash;
}
namespace {
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string stage_name(RegridStage stage) {
    switch (stage) {
        case RegridStage::GeometryLoad:
            return "geometry load";
        case RegridStage::PrerequisiteValidation:
            return "prerequisite validation";
        case RegridStage::PlanBuild:
            return "plan build";
    }
    return "regridding";
}

std::string role_name(GridRole role) {
    switch (role) {
        case GridRole::Source:
            return "source grid";
        case GridRole::Destination:
            return "destination grid";
        case GridRole::Both:
            return "source and destination grids";
        case GridRole::None:
            return "request";
    }
    return "request";
}
}  // namespace

ReconstructionModel parse_reconstruction_model(const std::string& name) {
    const std::string value = lower(name);
    if (value.empty() || value == "none") return ReconstructionModel::None;
    if (value == "rectilinear_midpoint") return ReconstructionModel::RectilinearMidpoint;
    if (value == "gaussian_latlon") return ReconstructionModel::GaussianLatLon;
    if (value == "curvilinear_approximate") return ReconstructionModel::CurvilinearApproximate;
    throw std::invalid_argument("unsupported geometry reconstruction policy '" + name + "'");
}

static GridGeometry generate_named_grid_geometry(const std::string& name) {
    const auto parsed = axis::topology::NamedGridRegistry::parse(name);
    int nx = 0;
    int ny = 0;
    if (parsed.family == 'F') {
        nx = 4 * parsed.number;
        ny = 2 * parsed.number;
    } else if (parsed.family == 'R') {
        nx = 4 * parsed.number - 1;
        ny = 2 * parsed.number - 1;
    } else {
        throw std::invalid_argument("named target grid '" + name + "' is not a supported structured CECE grid");
    }

    // AXIS owns named-grid mesh creation and its authoritative Gaussian/regular
    // cell geometry. Keep this exact mesh; only derive coordinate axes needed
    // by CECE's existing field and writer interfaces.
    auto generated = axis::topology::MeshFactory::from_named<Kokkos::HostSpace>(name);
    const std::size_t n_cells = static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny);
    if (generated.n_cells() != n_cells) {
        throw std::invalid_argument("AXIS named grid '" + name + "' generated " + std::to_string(generated.n_cells()) + " cells; expected " +
                                    std::to_string(n_cells));
    }

    const auto offsets = generated.conn_offsets();
    const auto indices = generated.conn_indices();
    const auto nodes = generated.node_coords();
    std::vector<double> cell_lon(n_cells), cell_lat(n_cells);
    for (std::size_t cell = 0; cell < n_cells; ++cell) {
        const std::size_t first = static_cast<std::size_t>(offsets(cell));
        const std::size_t last = static_cast<std::size_t>(offsets(cell + 1));
        if (last - first < 3) throw std::invalid_argument("AXIS named grid contains a cell with fewer than three vertices");
        const double anchor = nodes(static_cast<std::size_t>(indices(first)), 0);
        double lon_sum = 0.0;
        double sin_lat_sum = 0.0;
        for (std::size_t k = first; k < last; ++k) {
            const std::size_t vertex = static_cast<std::size_t>(indices(k));
            double lon = nodes(vertex, 0);
            while (lon - anchor > 180.0) lon -= 360.0;
            while (lon - anchor < -180.0) lon += 360.0;
            lon_sum += lon;
            sin_lat_sum += std::sin(nodes(vertex, 1) * M_PI / 180.0);
        }
        const double arity = static_cast<double>(last - first);
        double center_lon = std::fmod(lon_sum / arity + 180.0, 360.0);
        if (center_lon < 0.0) center_lon += 360.0;
        cell_lon[cell] = center_lon - 180.0;
        cell_lat[cell] = std::asin(std::clamp(sin_lat_sum / arity, -1.0, 1.0)) * 180.0 / M_PI;
    }

    GridGeometry result;
    result.nx = nx;
    result.ny = ny;
    result.center_lon.resize(static_cast<std::size_t>(nx));
    result.center_lat.resize(static_cast<std::size_t>(ny));
    for (int j = 0; j < ny; ++j) {
        const std::size_t row = static_cast<std::size_t>(j) * nx;
        result.center_lat[static_cast<std::size_t>(j)] = cell_lat[row];
        for (int i = 0; i < nx; ++i) {
            const std::size_t ordered = row + static_cast<std::size_t>(i);
            if (j == 0)
                result.center_lon[static_cast<std::size_t>(i)] = cell_lon[ordered];
            else if (std::abs(result.center_lon[static_cast<std::size_t>(i)] - cell_lon[ordered]) > 1e-10)
                throw std::invalid_argument("AXIS named grid '" + name + "' is not a separable rectilinear grid");
        }
    }
    result.metadata = generated.geometry_metadata();
    result.supplied_cells = std::make_shared<const axis::topology::UnstructuredMesh<Kokkos::HostSpace>>(std::move(generated));
    result.has_cell_geometry = true;
    result.provenance = "AXIS NamedGridRegistry:" + name;
    result.revision = grid_geometry_revision(result);
    return result;
}

GridGeometry load_named_grid_geometry(const std::string& name) {
    // GridSpec resolution and the driver regrid path both request this value.
    // Share the AXIS mesh so it is generated once and never reconstructed from
    // center coordinates on the CECE side.
    static std::mutex cache_mutex;
    static std::unordered_map<std::string, GridGeometry> cache;
    std::lock_guard<std::mutex> lock(cache_mutex);
    const auto cached = cache.find(name);
    if (cached != cache.end()) return cached->second;
    auto generated = generate_named_grid_geometry(name);
    const auto [entry, inserted] = cache.emplace(name, std::move(generated));
    (void)inserted;
    return entry->second;
}

RegridError::RegridError(RegridStage stage, GridRole role, std::string method, std::string reason, std::string correction)
    : std::runtime_error([&] {
          std::ostringstream out;
          out << "[" << stage_name(stage) << "] " << role_name(role);
          if (!method.empty()) out << " for method '" << method << "'";
          out << ": " << reason;
          if (!correction.empty()) out << ". " << correction;
          return out.str();
      }()),
      stage_(stage),
      role_(role),
      method_(std::move(method)),
      reason_(std::move(reason)),
      correction_(std::move(correction)) {}

PreparedGeometry prepare_grid_geometry(std::shared_ptr<const GridGeometry> geometry, const GeometrySettings& settings) {
    if (!geometry)
        throw RegridError(RegridStage::GeometryLoad, GridRole::Both, {}, "geometry snapshot is null", "Load an owned geometry snapshot first");
    PreparedGeometry prepared;
    prepared.geometry = std::move(geometry);
    prepared.metadata = prepared.geometry->metadata;
    const auto& source = *prepared.geometry;
    if (source.nx <= 0 || source.ny <= 0)
        throw RegridError(RegridStage::GeometryLoad, GridRole::Both, {}, "grid dimensions must be positive", "Check coordinate dimensions");

    const std::size_t ncell = static_cast<std::size_t>(source.nx) * static_cast<std::size_t>(source.ny);
    if (!source.mask.empty() && source.mask.size() != ncell)
        throw RegridError(RegridStage::GeometryLoad, GridRole::Both, {}, "cell mask shape does not match grid dimensions",
                          "Provide one active/inactive mask value per grid cell");
    for (int value : source.mask)
        if (value != 0 && value != 1)
            throw RegridError(RegridStage::GeometryLoad, GridRole::Both, {}, "cell mask values must be 0 or 1",
                              "Use 1 for active cells and 0 for masked cells");
    const bool rectilinear =
        source.center_lon.size() == static_cast<std::size_t>(source.nx) && source.center_lat.size() == static_cast<std::size_t>(source.ny);
    const bool curvilinear = source.center_lon.size() == ncell && source.center_lat.size() == ncell;
    if (!rectilinear && !curvilinear)
        throw RegridError(RegridStage::GeometryLoad, GridRole::Both, {}, "center coordinate shape does not match grid dimensions",
                          "Provide rectilinear [nx]/[ny] or flattened [nx*ny] centers");
    const bool spherical = source.coordinate_system == axis::topology::CoordinateSystem::SphericalDeg ||
                           source.coordinate_system == axis::topology::CoordinateSystem::SphericalRad;
    const double latitude_limit = source.coordinate_system == axis::topology::CoordinateSystem::SphericalDeg ? 90.0 : 1.57079632679489661923;
    for (double x : source.center_lon)
        if (!std::isfinite(x))
            throw RegridError(RegridStage::GeometryLoad, GridRole::Both, {}, "center coordinate is not finite",
                              "Correct the input coordinate values");
    for (double y : source.center_lat)
        if (!std::isfinite(y) || (spherical && std::abs(y) > latitude_limit))
            throw RegridError(RegridStage::GeometryLoad, GridRole::Both, {}, "center coordinate is invalid for its coordinate system",
                              "Check coordinate units and geographic latitude range");

    if (source.supplied_cells) {
        if (source.supplied_cells->n_cells() != ncell)
            throw RegridError(RegridStage::GeometryLoad, GridRole::Both, {}, "supplied cell count does not match center layout",
                              "Verify that grid cells and field centers use the same ordering and dimensions");
        prepared.cells = *source.supplied_cells;
        prepared.metadata = source.metadata;
        return prepared;
    }

    const std::size_t corner_columns = static_cast<std::size_t>(source.nx) + 1;
    const std::size_t corner_rows = static_cast<std::size_t>(source.ny) + 1;
    const bool supplied_lattice = source.corner_lon.size() == corner_columns * corner_rows && source.corner_lat.size() == source.corner_lon.size();
    const auto seam = settings.longitude_periodicity.value_or(axis::topology::LongitudePeriodicity{});
    if (supplied_lattice || source.has_explicit_bounds) {
        if (!supplied_lattice)
            throw RegridError(RegridStage::GeometryLoad, GridRole::Both, {}, "explicit bounds are not a valid structured corner lattice",
                              "Correct the bounds dimensions for the declared grid");
        Kokkos::View<double*, Kokkos::HostSpace> lon("geometry_centers_lon", ncell), lat("geometry_centers_lat", ncell);
        for (std::size_t k = 0; k < ncell; ++k) {
            lon(k) = source.center_lon[curvilinear ? k : k % static_cast<std::size_t>(source.nx)];
            lat(k) = source.center_lat[curvilinear ? k : k / static_cast<std::size_t>(source.nx)];
        }
        Kokkos::View<double*, Kokkos::HostSpace> corner_lon("geometry_corner_lon", source.corner_lon.size());
        Kokkos::View<double*, Kokkos::HostSpace> corner_lat("geometry_corner_lat", source.corner_lat.size());
        for (std::size_t k = 0; k < source.corner_lon.size(); ++k) {
            corner_lon(k) = source.corner_lon[k];
            corner_lat(k) = source.corner_lat[k];
        }
        axis::topology::StructuredGrid<Kokkos::HostSpace> grid(source.nx, source.ny, std::move(lon), std::move(lat), source.coordinate_system);
        grid.set_corners(std::move(corner_lon), std::move(corner_lat));
        prepared.cells = grid.to_unstructured(axis::topology::CornerPolicy::RequireExplicit, seam);
        auto metadata = source.metadata;
        const auto builder_metadata = prepared.cells->geometry_metadata();
        if (metadata.provenance == axis::topology::GeometryProvenance::Unspecified) metadata.provenance = builder_metadata.provenance;
        if (metadata.boundary_model == axis::topology::BoundaryModel::Unspecified) metadata.boundary_model = builder_metadata.boundary_model;
        if (metadata.area_model == axis::topology::AreaModel::Unspecified) metadata.area_model = builder_metadata.area_model;
        const auto& source_periodicity = source.metadata.longitude_periodicity;
        const bool source_declared_periodicity = source_periodicity.periodic || source_periodicity.period != 360.0 || !source_periodicity.increasing;
        if (!settings.longitude_periodicity.has_value() && !source_declared_periodicity)
            metadata.longitude_periodicity = builder_metadata.longitude_periodicity;
        prepared.cells->set_geometry_metadata(metadata);
        prepared.metadata = metadata;
        return prepared;
    }

    switch (settings.reconstruction) {
        case ReconstructionModel::None:
            if (source.has_cell_geometry)
                throw RegridError(RegridStage::GeometryLoad, GridRole::Both, {},
                                  "cell geometry was declared but no supported boundary data was supplied",
                                  "Provide a supported structured corner lattice or grid adapter");
            return prepared;
        case ReconstructionModel::RectilinearMidpoint:
            if (!rectilinear)
                throw RegridError(RegridStage::GeometryLoad, GridRole::Both, {}, "rectilinear midpoint reconstruction requires separable axes",
                                  "Use a declared curvilinear model only when scientifically justified");
            break;
        case ReconstructionModel::CurvilinearApproximate:
            if (!curvilinear)
                throw RegridError(RegridStage::GeometryLoad, GridRole::Both, {}, "curvilinear approximation requires flattened structured centers",
                                  "Provide coordinates with [nx*ny] layout");
            break;
        case ReconstructionModel::GaussianLatLon:
            throw RegridError(RegridStage::GeometryLoad, GridRole::Both, {}, "Gaussian latitude reconstruction requires declared Gaussian weights",
                              "Supply a Gaussian geometry adapter or explicit bounds");
    }

    Kokkos::View<double*, Kokkos::HostSpace> lon("geometry_centers_lon", ncell), lat("geometry_centers_lat", ncell);
    for (std::size_t k = 0; k < ncell; ++k) {
        lon(k) = source.center_lon[curvilinear ? k : k % static_cast<std::size_t>(source.nx)];
        lat(k) = source.center_lat[curvilinear ? k : k / static_cast<std::size_t>(source.nx)];
    }
    axis::topology::StructuredGrid<Kokkos::HostSpace> grid(source.nx, source.ny, std::move(lon), std::move(lat), source.coordinate_system);
    const auto policy = settings.reconstruction == ReconstructionModel::RectilinearMidpoint ? axis::topology::CornerPolicy::RectilinearMidpoint
                                                                                            : axis::topology::CornerPolicy::CurvilinearApproximate;
    prepared.cells = grid.to_unstructured(policy, seam);
    prepared.metadata = prepared.cells->geometry_metadata();
    if (settings.reconstruction == ReconstructionModel::RectilinearMidpoint) {
        prepared.metadata.provenance = axis::topology::GeometryProvenance::DeclaredGridModel;
        prepared.cells->set_geometry_metadata(prepared.metadata);
    }
    return prepared;
}

GeometryPartition partition_grid_geometry(std::shared_ptr<const GridGeometry> geometry, int j0, int j1) {
    if (!geometry || geometry->nx <= 0 || geometry->ny <= 0 || j0 < 0 || j1 < j0 || j1 > geometry->ny)
        throw RegridError(RegridStage::GeometryLoad, GridRole::Destination, {}, "invalid geometry row partition",
                          "Use a loaded grid and a row interval within [0, ny]");
    GeometryPartition partition;
    partition.parent = std::move(geometry);
    partition.j0 = j0;
    partition.j1 = j1;
    partition.metadata = partition.parent->metadata;
    partition.global_cell_indices.reserve(static_cast<std::size_t>(j1 - j0) * partition.parent->nx);
    for (int j = j0; j < j1; ++j)
        for (int i = 0; i < partition.parent->nx; ++i)
            partition.global_cell_indices.push_back(static_cast<std::size_t>(j) * partition.parent->nx + i);

    if (!partition.parent->supplied_cells) return partition;
    const auto& global = *partition.parent->supplied_cells;
    const std::size_t expected_cells = static_cast<std::size_t>(partition.parent->nx) * partition.parent->ny;
    if (global.n_cells() != expected_cells)
        throw RegridError(RegridStage::GeometryLoad, GridRole::Destination, {},
                          "cell mesh cannot be partitioned because cell order/count differs from grid layout",
                          "Verify row-major field-to-cell correspondence before partitioning");

    const auto offsets = global.conn_offsets();
    const auto indices = global.conn_indices();
    const auto nodes = global.node_coords();
    const auto areas = global.cell_areas();
    const auto masks = global.cell_mask();
    std::unordered_map<axis::index_t, axis::index_t> local_nodes;
    std::vector<axis::index_t> local_conn;
    std::vector<axis::index_t> local_offsets{0};
    std::vector<double> local_areas;
    std::vector<int> local_masks;
    local_offsets.reserve(partition.global_cell_indices.size() + 1);
    for (const std::size_t global_cell : partition.global_cell_indices) {
        for (std::size_t k = offsets(global_cell); k < offsets(global_cell + 1); ++k) {
            const axis::index_t global_node = indices(k);
            auto [it, inserted] = local_nodes.emplace(global_node, static_cast<axis::index_t>(local_nodes.size()));
            if (inserted && (global_node < 0 || static_cast<std::size_t>(global_node) >= nodes.extent(0)))
                throw RegridError(RegridStage::GeometryLoad, GridRole::Destination, {}, "cell connectivity references an invalid node",
                                  "Correct the selected grid topology");
            local_conn.push_back(it->second);
        }
        local_offsets.push_back(static_cast<axis::index_t>(local_conn.size()));
        if (areas.size() == expected_cells) local_areas.push_back(areas(global_cell));
        if (masks.size() == expected_cells) local_masks.push_back(masks(global_cell));
    }
    Kokkos::View<double**, Kokkos::LayoutLeft, Kokkos::HostSpace> local_coords("geometry_partition_nodes", local_nodes.size(), nodes.extent(1));
    for (const auto& [global_node, local_node] : local_nodes)
        for (std::size_t d = 0; d < nodes.extent(1); ++d) local_coords(local_node, d) = nodes(global_node, d);
    Kokkos::View<axis::index_t*, Kokkos::HostSpace> conn_offsets("geometry_partition_offsets", local_offsets.size());
    Kokkos::View<axis::index_t*, Kokkos::HostSpace> conn_indices("geometry_partition_connectivity", local_conn.size());
    for (std::size_t k = 0; k < local_offsets.size(); ++k) conn_offsets(k) = local_offsets[k];
    for (std::size_t k = 0; k < local_conn.size(); ++k) conn_indices(k) = local_conn[k];
    Kokkos::View<double*, Kokkos::HostSpace> area_view;
    if (!local_areas.empty()) {
        area_view = Kokkos::View<double*, Kokkos::HostSpace>("geometry_partition_areas", local_areas.size());
        for (std::size_t k = 0; k < local_areas.size(); ++k) area_view(k) = local_areas[k];
    }
    Kokkos::View<int*, Kokkos::HostSpace> mask_view;
    if (!local_masks.empty()) {
        mask_view = Kokkos::View<int*, Kokkos::HostSpace>("geometry_partition_mask", local_masks.size());
        for (std::size_t k = 0; k < local_masks.size(); ++k) mask_view(k) = local_masks[k];
    }
    partition.cells.emplace(axis::topology::make_unstructured(std::move(local_coords), std::move(conn_offsets), std::move(conn_indices),
                                                              global.coord_system(), std::move(area_view), std::move(mask_view), partition.metadata));
    return partition;
}

RegridMethodSettings resolve_regrid_method(const std::string& method_name) {
    const std::string name = lower(method_name);
    RegridMethodSettings result;
    if (name == "nearest" || name == "near" || name == "nn") {
        result.method = RegridMethod::Nearest;
        result.canonical_name = "nearest";
    } else if (name == "bilinear" || name == "bilin" || name == "bi") {
        result.method = RegridMethod::Bilinear;
        result.canonical_name = "bilinear";
    } else if (name == "cubic" || name == "bicubic" || name == "cu") {
        result.method = RegridMethod::Bicubic;
        result.canonical_name = "bicubic";
    } else if (name == "conss" || name == "conservative2nd" || name == "cons2nd") {
        result.method = RegridMethod::Conservative2;
        result.canonical_name = "conservative2nd";
    } else if (name == "consd" || name == "conservative" || name == "cons" || name == "conservative1st") {
        result.method = RegridMethod::Conservative1;
        result.canonical_name = "conservative1st";
    } else if (name == "passthrough") {
        result.method = RegridMethod::Passthrough;
        result.canonical_name = "passthrough";
    } else if (name == "consf") {
        // consf implies source-area (source-fraction) normalization, which the host AXIS path does not
        // implement; the destination-normalized behavior would silently be consd. Reject until distinct
        // semantics exist (see ufs-community/CECE#124 review discussion).
        throw RegridError(RegridStage::PrerequisiteValidation, GridRole::None, method_name,
                          "consf is not implemented; its source-area normalization differs from consd",
                          "Use consd (destination-normalized first-order conservative)");
    } else if (name == "redist") {
        // ESMF's redistribute maps fields between grids with the same cell geometry under different
        // decompositions; CECE has no decomposition-communication path, so the alias to conservative
        // regridding is removed outright.
        throw RegridError(RegridStage::PrerequisiteValidation, GridRole::None, method_name,
                          "redist is not supported: it implies decomposition redistribution, which CECE does not provide",
                          "Use consd for conservative remapping or passthrough for same-grid copies");
    } else {
        throw RegridError(RegridStage::PrerequisiteValidation, GridRole::None, method_name, "unknown regridding method",
                          "Use nearest, bilinear, bicubic, consd, conss, or passthrough");
    }
    return result;
}

ValidatedRegridRequest validate_regrid_prerequisites(const GridGeometry& source, const GridGeometry& destination,
                                                     const RegridMethodSettings& settings, int j0, int j1, RegridBackend backend) {
    if (backend != RegridBackend::Host) {
        throw RegridError(RegridStage::PrerequisiteValidation, GridRole::Both, settings.canonical_name,
                          "the requested regridding backend is not supported", "Use the host backend for this geometry pipeline");
    }
    auto validate_grid = [&](const GridGeometry& grid, GridRole role) {
        const std::size_t expected = static_cast<std::size_t>(std::max(grid.nx, 0)) * std::max(grid.ny, 0);
        const bool rectilinear = grid.center_lon.size() == static_cast<std::size_t>(std::max(grid.nx, 0)) &&
                                 grid.center_lat.size() == static_cast<std::size_t>(std::max(grid.ny, 0));
        if (grid.nx <= 0 || grid.ny <= 0 || (!rectilinear && (grid.center_lon.size() != expected || grid.center_lat.size() != expected))) {
            throw RegridError(RegridStage::PrerequisiteValidation, role, settings.canonical_name,
                              "center coordinate shape does not match grid dimensions",
                              "Provide ordered centers with either [nx]/[ny] or [nx*ny]/[nx*ny] layout");
        }
        for (double value : grid.center_lon) {
            if (!std::isfinite(value))
                throw RegridError(RegridStage::PrerequisiteValidation, role, settings.canonical_name, "longitude center is not finite",
                                  "Correct the input coordinate");
        }
        if (!grid.mask.empty() && grid.mask.size() != expected)
            throw RegridError(RegridStage::PrerequisiteValidation, role, settings.canonical_name, "cell mask shape does not match grid dimensions",
                              "Provide one mask value per grid cell");
        for (int value : grid.mask)
            if (value != 0 && value != 1)
                throw RegridError(RegridStage::PrerequisiteValidation, role, settings.canonical_name, "cell mask values must be 0 or 1",
                                  "Use 1 for active cells and 0 for masked cells");
        const bool spherical = grid.coordinate_system == axis::topology::CoordinateSystem::SphericalDeg ||
                               grid.coordinate_system == axis::topology::CoordinateSystem::SphericalRad;
        const double latitude_limit = grid.coordinate_system == axis::topology::CoordinateSystem::SphericalDeg ? 90.0 : 1.57079632679489661923;
        for (double value : grid.center_lat) {
            if (!std::isfinite(value) || (spherical && std::abs(value) > latitude_limit))
                throw RegridError(RegridStage::PrerequisiteValidation, role, settings.canonical_name,
                                  "center coordinate is invalid for its coordinate system", "Correct the input coordinate or declare its units");
        }
        if (role == GridRole::Source && settings.method == RegridMethod::Bilinear && (grid.nx < 2 || grid.ny < 2))
            throw RegridError(RegridStage::PrerequisiteValidation, role, settings.canonical_name,
                              "bilinear interpolation requires structured source topology with at least 2 by 2 samples",
                              "Provide structured center topology or choose nearest-neighbor");
        if (role == GridRole::Source && settings.method == RegridMethod::Bicubic && (grid.nx < 4 || grid.ny < 4))
            throw RegridError(RegridStage::PrerequisiteValidation, role, settings.canonical_name,
                              "bicubic interpolation requires structured source topology with at least a 4 by 4 stencil",
                              "Provide a complete structured stencil or choose bilinear/nearest-neighbor");
        if (settings.method == RegridMethod::Conservative1 || settings.method == RegridMethod::Conservative2) {
            if (!grid.has_cell_geometry)
                throw RegridError(RegridStage::PrerequisiteValidation, role, settings.canonical_name,
                                  "conservative mapping requires prepared cell boundaries",
                                  "Supply authoritative bounds or declare a supported geometry reconstruction model");
            const auto provenance = grid.metadata.provenance;
            if (provenance != axis::topology::GeometryProvenance::SourceAuthoritative &&
                provenance != axis::topology::GeometryProvenance::DeclaredGridModel)
                throw RegridError(RegridStage::PrerequisiteValidation, role, settings.canonical_name,
                                  "cell boundaries are reconstructed or have unknown provenance",
                                  "Supply source bounds or explicitly declare and prepare a supported grid model");
            if (grid.metadata.boundary_model == axis::topology::BoundaryModel::Unspecified ||
                grid.metadata.area_model == axis::topology::AreaModel::Unspecified)
                throw RegridError(RegridStage::PrerequisiteValidation, role, settings.canonical_name,
                                  "conservative edge or area model is unspecified", "Declare a supported boundary and area model for this grid");
            if (grid.metadata.boundary_model == axis::topology::BoundaryModel::ConstantLatitude &&
                (settings.method == RegridMethod::Conservative2 || grid.metadata.area_model != axis::topology::AreaModel::ConstantLatitudeStrip))
                throw RegridError(RegridStage::PrerequisiteValidation, role, settings.canonical_name,
                                  "constant-latitude strips support host first-order conservative mapping only",
                                  "Use first-order host mapping with constant-latitude area models");
            if (grid.metadata.boundary_model != axis::topology::BoundaryModel::ConstantLatitude &&
                grid.metadata.boundary_model != axis::topology::BoundaryModel::GreatCircle)
                throw RegridError(RegridStage::PrerequisiteValidation, role, settings.canonical_name, "unsupported cell boundary model",
                                  "Use a verified spherical boundary model");
        }
    };
    validate_grid(source, GridRole::Source);
    validate_grid(destination, GridRole::Destination);
    if (source.coordinate_system != destination.coordinate_system)
        throw RegridError(RegridStage::PrerequisiteValidation, GridRole::Both, settings.canonical_name,
                          "source and destination coordinate systems differ", "Convert both geometries to the same coordinate system");
    // Mixed boundary/area models across the pair are allowed: each grid is
    // internally validated above, and AXIS conservative weight generation
    // clips the two prepared cell polygons on the sphere without consulting
    // the declared models. E.g. a rectilinear CF-bounds inventory
    // (ConstantLatitude/ConstantLatitudeStrip) onto a cubed-sphere target
    // (GreatCircle/SphericalExcess) is exact first-order conservative.
    if (j0 < 0 || j1 < j0 || j1 > destination.ny)
        throw RegridError(RegridStage::PrerequisiteValidation, GridRole::Destination, settings.canonical_name, "destination row band is out of range",
                          "Use a band contained in the global destination grid");
    return {settings, source.revision, destination.revision, j0, j1, backend};
}

GridGeometry load_grid_geometry(amio_dataset_handle dataset, const GeometrySettings& settings) {
    if (!dataset)
        throw RegridError(RegridStage::GeometryLoad, GridRole::Source, {}, "AMIO dataset handle is null",
                          "Open the source dataset before loading geometry");

    struct Coordinate {
        std::vector<double> values;
        int nx{0};
        int ny{0};
        std::string name;
        std::string units;
    };
    auto read_coordinate = [&](const std::vector<std::string>& names, const std::string& explicit_name, const char* role) {
        Coordinate coordinate;
        std::vector<std::string> candidates = explicit_name.empty() ? names : std::vector<std::string>{explicit_name};
        for (const auto& name : candidates) {
            amio_view_handle view = nullptr;
            if (amio_read(dataset, name.c_str(), 0, nullptr, &view) != AMIO_OK) {
                if (!explicit_name.empty())
                    throw RegridError(RegridStage::GeometryLoad, GridRole::Source, {},
                                      std::string("configured ") + role + " variable '" + name + "' could not be read",
                                      "Correct the configured geometry variable name");
                continue;
            }
            const void* data = nullptr;
            size_t size = 0;
            amio_shape_t shape{};
            amio_dtype_t dtype = AMIO_DTYPE_F64;
            if (amio_view_data(view, &data, &size) != AMIO_OK || amio_view_shape(view, &shape) != AMIO_OK || shape.rank < 1 || shape.rank > 3 ||
                amio_view_dtype(view, &dtype) != AMIO_OK) {
                amio_release_view(view);
                throw RegridError(RegridStage::GeometryLoad, GridRole::Source, {},
                                  std::string("configured ") + role + " variable '" + name + "' has invalid rank or metadata",
                                  "Use rank-1/2 coordinates or a rank-3 time-dependent coordinate array");
            }
            if (shape.rank == 1) {
                coordinate.nx = static_cast<int>(shape.extents[0]);
                coordinate.ny = 1;
            } else if (shape.rank == 2) {
                coordinate.ny = static_cast<int>(shape.extents[0]);
                coordinate.nx = static_cast<int>(shape.extents[1]);
            } else {
                coordinate.ny = static_cast<int>(shape.extents[1]);
                coordinate.nx = static_cast<int>(shape.extents[2]);
            }
            const std::size_t count = static_cast<std::size_t>(coordinate.nx) * coordinate.ny;
            const std::size_t element_size = cece::detail::amio_dtype_size(dtype);
            if (coordinate.nx <= 0 || coordinate.ny <= 0 || element_size == 0 || size < count * element_size) {
                amio_release_view(view);
                throw RegridError(RegridStage::GeometryLoad, GridRole::Source, {},
                                  std::string("configured ") + role + " variable '" + name + "' has malformed dimensions or truncated data",
                                  "Check coordinate dimensions and stored values");
            }
            double scale = 1.0, offset = 0.0, attr = 0.0;
            if (amio_get_var_attribute_double(dataset, name.c_str(), "scale_factor", &attr) == AMIO_OK) scale = attr;
            if (amio_get_var_attribute_double(dataset, name.c_str(), "add_offset", &attr) == AMIO_OK) offset = attr;
            if (!cece::detail::widen_amio_elements(data, dtype, count, scale, offset, coordinate.values)) {
                amio_release_view(view);
                throw RegridError(RegridStage::GeometryLoad, GridRole::Source, {},
                                  std::string("configured ") + role + " variable '" + name + "' uses an unsupported numeric type",
                                  "Store coordinates in a supported numeric type");
            }
            amio_release_view(view);
            coordinate.name = name;
            char units[256]{};
            size_t units_length = 0;
            if (amio_get_var_attribute_text(dataset, name.c_str(), "units", units, sizeof(units), &units_length) == AMIO_OK &&
                units_length < sizeof(units))
                coordinate.units.assign(units, units_length);
            if (coordinate.units.empty() && (name == "lonCell" || name == "latCell" || name == "lonVertex" || name == "latVertex"))
                coordinate.units = "radians";
            return coordinate;
        }
        return coordinate;
    };

    static const std::vector<std::string> longitude_names = {"grid_lont", "grid_lon",        "XLONG",         "lonCell",   "geolon",  "clon",
                                                             "glamt",     "mesh2d_face_lon", "lon",           "longitude", "LON",     "lon_rho",
                                                             "nav_lon",   "mesh_node_x",     "mesh2d_node_x", "node_x",    "grid_xt", "x"};
    static const std::vector<std::string> latitude_names = {"grid_latt", "grid_lat",        "XLAT",          "latCell",  "geolat",  "clat",
                                                            "gphit",     "mesh2d_face_lat", "lat",           "latitude", "LAT",     "lat_rho",
                                                            "nav_lat",   "mesh_node_y",     "mesh2d_node_y", "node_y",   "grid_yt", "y"};
    auto longitude = read_coordinate(longitude_names, settings.longitude_variable, "longitude");
    auto latitude = read_coordinate(latitude_names, settings.latitude_variable, "latitude");
    if (longitude.values.empty() || latitude.values.empty())
        throw RegridError(RegridStage::GeometryLoad, GridRole::Source, {}, "source longitude or latitude coordinates could not be discovered",
                          "Configure the coordinate variable names or use a supported naming convention");

    auto normalized_units = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    };
    const auto lon_units = normalized_units(longitude.units);
    const auto lat_units = normalized_units(latitude.units);
    if ((longitude.name == "x" || latitude.name == "y") && (lon_units.empty() || lat_units.empty()))
        throw RegridError(RegridStage::GeometryLoad, GridRole::Source, {}, "ambiguous x/y coordinates have no geographic units",
                          "Add geographic CF units or select longitude/latitude variables explicitly");
    auto is_degree = [](const std::string& units) {
        return units.empty() || units == "degree" || units == "degrees" || units == "degree_east" || units == "degrees_east" ||
               units == "degree_north" || units == "degrees_north";
    };
    auto is_radian = [](const std::string& units) { return units == "radian" || units == "radians" || units == "rad"; };
    if ((!is_degree(lon_units) && !is_radian(lon_units)) || (!is_degree(lat_units) && !is_radian(lat_units)))
        throw RegridError(RegridStage::GeometryLoad, GridRole::Source, {},
                          "projected or unsupported coordinate units are not supported by the spherical geometry adapter",
                          "Provide geographic longitude/latitude in degrees or radians");
    if (is_radian(lon_units) != is_radian(lat_units))
        throw RegridError(RegridStage::GeometryLoad, GridRole::Source, {}, "longitude and latitude units use different angular systems",
                          "Convert both coordinate variables to degrees or both to radians");
    if (is_radian(lon_units)) {
        constexpr double radians_to_degrees = 180.0 / 3.14159265358979323846;
        for (double& value : longitude.values) value *= radians_to_degrees;
        for (double& value : latitude.values) value *= radians_to_degrees;
    }

    GridGeometry geometry;
    const bool longitude_curvilinear = longitude.ny > 1;
    const bool latitude_curvilinear = latitude.ny > 1;
    const bool cell_center_names =
        longitude.name == "lonCell" || longitude.name == "mesh2d_face_lon" || longitude.name == "geolon" || longitude.name == "clon";
    if (longitude_curvilinear && latitude_curvilinear && (longitude.nx != latitude.nx || longitude.ny != latitude.ny))
        throw RegridError(RegridStage::GeometryLoad, GridRole::Source, {}, "longitude and latitude dimensions differ",
                          "Select coordinates with matching ordered grid dimensions");
    if (longitude_curvilinear) {
        geometry.nx = longitude.nx;
        geometry.ny = longitude.ny;
        geometry.centers_curvilinear = true;
    } else if (latitude_curvilinear) {
        geometry.nx = latitude.nx;
        geometry.ny = latitude.ny;
        geometry.centers_curvilinear = true;
    } else if (cell_center_names && longitude.nx == latitude.nx) {
        geometry.nx = longitude.nx;
        geometry.ny = 1;
        geometry.centers_curvilinear = true;
    } else {
        geometry.nx = longitude.nx;
        geometry.ny = latitude.nx;
    }
    geometry.center_lon = std::move(longitude.values);
    geometry.center_lat = std::move(latitude.values);
    geometry.provenance = longitude.name + ";" + latitude.name;
    const bool selected_gridspec = !settings.gridspec_file.empty() && settings.gridspec_file != "none" && settings.gridspec_file != "NONE";
    // CF rectilinear bounds are independent of the selected regridding method.
    // Preserve the cell boundary pairs as authoritative cells when both axes
    // advertise bounds; a one-sided declaration is malformed and must not be
    // silently replaced with reconstructed geometry.
    auto read_cf_bounds = [&](const Coordinate& coordinate, std::size_t expected, const char* role, std::vector<double>& values) {
        char bounds_name[256]{};
        std::size_t bounds_name_length = 0;
        const auto attribute_status =
            amio_get_var_attribute_text(dataset, coordinate.name.c_str(), "bounds", bounds_name, sizeof(bounds_name), &bounds_name_length);
        if (attribute_status != AMIO_OK) return false;
        if (bounds_name_length == 0 || bounds_name_length >= sizeof(bounds_name))
            throw RegridError(RegridStage::GeometryLoad, GridRole::Source, {}, std::string(role) + " bounds attribute is empty or too long",
                              "Set the CF bounds attribute to a valid variable name");
        amio_view_handle view = nullptr;
        if (amio_read(dataset, bounds_name, 0, nullptr, &view) != AMIO_OK)
            throw RegridError(RegridStage::GeometryLoad, GridRole::Source, {}, std::string(role) + " bounds variable could not be read",
                              "Correct the CF bounds variable name or file contents");
        const void* data = nullptr;
        std::size_t byte_count = 0;
        amio_shape_t shape{};
        amio_dtype_t dtype = AMIO_DTYPE_F64;
        const bool valid_view = amio_view_data(view, &data, &byte_count) == AMIO_OK && amio_view_shape(view, &shape) == AMIO_OK &&
                                amio_view_dtype(view, &dtype) == AMIO_OK;
        const bool first_axis = shape.rank == 2 && shape.extents[0] == expected && shape.extents[1] == 2;
        const bool transposed_axis = shape.rank == 2 && shape.extents[0] == 2 && shape.extents[1] == expected;
        const std::size_t element_size = cece::detail::amio_dtype_size(dtype);
        if (!valid_view || (!first_axis && !transposed_axis) || element_size == 0 || byte_count < expected * 2 * element_size) {
            amio_release_view(view);
            throw RegridError(RegridStage::GeometryLoad, GridRole::Source, {},
                              std::string(role) + " bounds have an unsupported shape or numeric type",
                              "Use a two-vertex CF bounds variable with one pair per coordinate");
        }
        double scale = 1.0, offset = 0.0, attribute_value = 0.0;
        if (amio_get_var_attribute_double(dataset, bounds_name, "scale_factor", &attribute_value) == AMIO_OK) scale = attribute_value;
        if (amio_get_var_attribute_double(dataset, bounds_name, "add_offset", &attribute_value) == AMIO_OK) offset = attribute_value;
        std::vector<double> raw;
        if (!cece::detail::widen_amio_elements(data, dtype, expected * 2, scale, offset, raw)) {
            amio_release_view(view);
            throw RegridError(RegridStage::GeometryLoad, GridRole::Source, {}, std::string(role) + " bounds use an unsupported numeric type",
                              "Store bounds in a supported numeric type");
        }
        amio_release_view(view);
        values.resize(expected * 2);
        for (std::size_t index = 0; index < expected; ++index) {
            values[2 * index] = raw[first_axis ? 2 * index : index];
            values[2 * index + 1] = raw[first_axis ? 2 * index + 1 : expected + index];
        }
        return true;
    };
    const bool has_lon_bounds = !selected_gridspec && !longitude_curvilinear &&
                                read_cf_bounds(longitude, static_cast<std::size_t>(geometry.nx), "longitude", geometry.corner_lon);
    const bool has_lat_bounds = !selected_gridspec && !latitude_curvilinear &&
                                read_cf_bounds(latitude, static_cast<std::size_t>(geometry.ny), "latitude", geometry.corner_lat);
    if (has_lon_bounds != has_lat_bounds)
        throw RegridError(RegridStage::GeometryLoad, GridRole::Source, {}, "only one rectilinear coordinate axis advertises CF bounds",
                          "Provide bounds for both longitude and latitude or select a complete gridspec");
    if (has_lon_bounds && has_lat_bounds) {
        if (is_radian(lon_units)) {
            constexpr double radians_to_degrees = 180.0 / 3.14159265358979323846;
            for (double& value : geometry.corner_lon) value *= radians_to_degrees;
            for (double& value : geometry.corner_lat) value *= radians_to_degrees;
        }
        const std::size_t n_cells = static_cast<std::size_t>(geometry.nx) * geometry.ny;
        constexpr double coordinate_tolerance = 1e-10;
        for (int i = 0; i < geometry.nx; ++i) {
            const std::size_t index = 2U * static_cast<std::size_t>(i);
            double& west = geometry.corner_lon[index];
            double& east = geometry.corner_lon[index + 1U];
            const double center = geometry.center_lon[static_cast<std::size_t>(i)];
            while (west - center > 180.0) west -= 360.0;
            while (west - center < -180.0) west += 360.0;
            while (east - center > 180.0) east -= 360.0;
            while (east - center < -180.0) east += 360.0;
            // Keep an explicitly declared single-cell global span intact.
            if (!(geometry.nx == 1 && std::abs(std::abs(east - west) - 360.0) <= coordinate_tolerance)) {
                while (east - west > 180.0) east -= 360.0;
                while (east - west < -180.0) east += 360.0;
            }
            const double width = std::abs(east - west);
            if (width <= coordinate_tolerance || width > 360.0 + coordinate_tolerance || center < std::min(west, east) - coordinate_tolerance ||
                center > std::max(west, east) + coordinate_tolerance)
                throw RegridError(RegridStage::GeometryLoad, GridRole::Source, {}, "CF longitude center is outside its advertised bounds",
                                  "Check bounds ordering, coordinate units, and dateline convention");
        }
        for (int j = 0; j < geometry.ny; ++j) {
            const std::size_t index = 2U * static_cast<std::size_t>(j);
            const double south = geometry.corner_lat[index], north = geometry.corner_lat[index + 1U];
            const double center = geometry.center_lat[static_cast<std::size_t>(j)];
            if (std::abs(north - south) <= coordinate_tolerance || center < std::min(south, north) - coordinate_tolerance ||
                center > std::max(south, north) + coordinate_tolerance)
                throw RegridError(RegridStage::GeometryLoad, GridRole::Source, {}, "CF latitude center is outside its advertised bounds",
                                  "Check bounds ordering, coordinate units, and latitude convention");
        }
        Kokkos::View<double**, Kokkos::LayoutLeft, Kokkos::HostSpace> nodes("cf_bounds_nodes", 4 * n_cells, 2);
        Kokkos::View<axis::index_t*, Kokkos::HostSpace> offsets("cf_bounds_offsets", n_cells + 1);
        Kokkos::View<axis::index_t*, Kokkos::HostSpace> indices("cf_bounds_indices", 4 * n_cells);
        Kokkos::View<double*, Kokkos::HostSpace> areas("cf_bounds_areas", n_cells);
        constexpr double radians = 3.14159265358979323846 / 180.0;
        for (int j = 0; j < geometry.ny; ++j) {
            const std::size_t lat_index = 2U * static_cast<std::size_t>(j);
            const double lat0 = geometry.corner_lat[lat_index], lat1 = geometry.corner_lat[lat_index + 1U];
            if (!std::isfinite(lat0) || !std::isfinite(lat1) || std::abs(lat0) > 90.0 || std::abs(lat1) > 90.0)
                throw RegridError(RegridStage::GeometryLoad, GridRole::Source, {}, "CF latitude bounds are non-finite or outside [-90,90]",
                                  "Correct the latitude bounds or coordinate units");
            for (int i = 0; i < geometry.nx; ++i) {
                const std::size_t cell = static_cast<std::size_t>(j) * geometry.nx + i;
                const std::size_t lon_index = 2U * static_cast<std::size_t>(i);
                const double lon0 = geometry.corner_lon[lon_index], lon1 = geometry.corner_lon[lon_index + 1U];
                if (!std::isfinite(lon0) || !std::isfinite(lon1))
                    throw RegridError(RegridStage::GeometryLoad, GridRole::Source, {}, "CF longitude bounds are non-finite",
                                      "Correct the longitude bounds");
                const std::size_t base = 4 * cell;
                const double vertex_lon[4] = {lon0, lon1, lon1, lon0};
                const double vertex_lat[4] = {lat0, lat0, lat1, lat1};
                offsets(cell) = static_cast<axis::index_t>(base);
                for (int vertex = 0; vertex < 4; ++vertex) {
                    nodes(base + vertex, 0) = vertex_lon[vertex];
                    nodes(base + vertex, 1) = vertex_lat[vertex];
                    indices(base + vertex) = static_cast<axis::index_t>(base + vertex);
                }
                const double delta_lon = std::abs(lon1 - lon0);
                areas(cell) = delta_lon * radians * (std::sin(std::max(lat0, lat1) * radians) - std::sin(std::min(lat0, lat1) * radians));
            }
        }
        offsets(n_cells) = static_cast<axis::index_t>(4 * n_cells);
        geometry.metadata = {axis::topology::GeometryProvenance::SourceAuthoritative,
                             axis::topology::BoundaryModel::ConstantLatitude,
                             axis::topology::AreaModel::ConstantLatitudeStrip,
                             {false, 360.0, true}};
        geometry.has_explicit_bounds = true;
        geometry.has_cell_geometry = true;
        geometry.supplied_cells = std::make_shared<const axis::topology::UnstructuredMesh<Kokkos::HostSpace>>(
            std::move(nodes), std::move(offsets), std::move(indices), axis::topology::CoordinateSystem::SphericalDeg, std::move(areas),
            Kokkos::View<int*, Kokkos::HostSpace>{}, geometry.metadata);
    }
    if (selected_gridspec) {
        auto cells = load_mesh_from_file(geometry.nx, geometry.ny, 0, settings.gridspec_file);
        const auto expected_cells = static_cast<std::size_t>(geometry.nx) * static_cast<std::size_t>(geometry.ny);
        if (cells.n_cells() != expected_cells)
            throw RegridError(RegridStage::GeometryLoad, GridRole::Source, {}, "gridspec cell count does not match loaded field centers",
                              "Verify the gridspec dimension, cell ordering, and field-to-cell correspondence");
        geometry.metadata = cells.geometry_metadata();
        geometry.has_cell_geometry = true;
        geometry.supplied_cells = std::make_shared<const axis::topology::UnstructuredMesh<Kokkos::HostSpace>>(std::move(cells));
    }
    geometry.revision = grid_geometry_revision(geometry);
    return geometry;
}

}  // namespace cece::io
