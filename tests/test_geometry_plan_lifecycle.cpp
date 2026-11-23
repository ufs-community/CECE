#include <gtest/gtest.h>
#include <mpi.h>

#include <optional>
#include <unordered_map>

#include "cece/cece_driver_facade.hpp"
#include "cece/cece_regridder_utils.hpp"

namespace cece {
struct CollectiveGateTestAccess {
    static bool AllReady(MPI_Comm comm, bool local_ready, const std::string& context, std::string& detail) {
        return CeceDriverOrchestrator::CallCollectiveAllReady(comm, local_ready, context, detail);
    }
};
}  // namespace cece

TEST(GeometryPlanLifecycle, KeyChangesWithGeometryMethodAndPartition) {
    cece::io::ValidatedRegridRequest request;
    request.settings = cece::io::resolve_regrid_method("nn");
    request.source_revision = 101;
    request.destination_revision = 202;
    request.j0 = 0;
    request.j1 = 5;
    const auto key = request.plan_key();
    EXPECT_EQ(key, request.plan_key());

    request.destination_revision++;
    EXPECT_NE(key, request.plan_key());
    request.destination_revision--;
    request.source_revision++;
    EXPECT_NE(key, request.plan_key());
    request.source_revision--;
    request.j0++;
    EXPECT_NE(key, request.plan_key());
    request.j0--;
    request.settings = cece::io::resolve_regrid_method("bilinear");
    EXPECT_NE(key, request.plan_key());
    request.settings = cece::io::resolve_regrid_method("nn");
    request.j1++;
    EXPECT_NE(key, request.plan_key());
    request.j1--;
    request.backend = cece::io::RegridBackend::Device;
    EXPECT_NE(key, request.plan_key());
    request.backend = cece::io::RegridBackend::Host;
    request.settings.normalization = "fracarea";
    EXPECT_NE(key, request.plan_key());
    request.settings.normalization = "dstarea";
    request.settings.unmapped = "error";
    EXPECT_NE(key, request.plan_key());
}

TEST(GeometryPlanLifecycle, GeometryRevisionTracksOrderedCoordinatesAndModels) {
    cece::io::GridGeometry geometry;
    geometry.nx = 2;
    geometry.ny = 1;
    geometry.center_lon = {10.0, 20.0};
    geometry.center_lat = {0.0};
    const auto original = cece::io::grid_geometry_revision(geometry);
    EXPECT_EQ(original, cece::io::grid_geometry_revision(geometry));

    auto changed = geometry;
    changed.center_lon[0] = 20.0;
    changed.center_lon[1] = 10.0;
    EXPECT_NE(original, cece::io::grid_geometry_revision(changed));

    changed = geometry;
    changed.metadata.area_model = axis::topology::AreaModel::SphericalExcess;
    EXPECT_NE(original, cece::io::grid_geometry_revision(changed));

    changed = geometry;
    changed.corner_lon = {-1.0, 1.0, -1.0, 1.0, -1.0, 1.0};
    changed.corner_lat = {-1.0, -1.0, 1.0, 1.0, -1.0, -1.0};
    EXPECT_NE(original, cece::io::grid_geometry_revision(changed));
    changed = geometry;
    changed.mask = {1, 0};
    EXPECT_NE(original, cece::io::grid_geometry_revision(changed));
}

TEST(GeometryPlanLifecycle, GeometryRevisionIncludesSuppliedCellTopologyAndAreas) {
    using Mesh = axis::topology::UnstructuredMesh<Kokkos::HostSpace>;
    auto make_mesh = [](double east, double area) {
        Kokkos::View<double**, Kokkos::LayoutLeft, Kokkos::HostSpace> nodes("nodes", 4, 2);
        const double coordinates[4][2] = {{0.0, 0.0}, {east, 0.0}, {east, 1.0}, {0.0, 1.0}};
        for (std::size_t i = 0; i < 4; ++i)
            for (std::size_t d = 0; d < 2; ++d) nodes(i, d) = coordinates[i][d];
        Kokkos::View<axis::index_t*, Kokkos::HostSpace> offsets("offsets", 2);
        offsets(0) = 0;
        offsets(1) = 4;
        Kokkos::View<axis::index_t*, Kokkos::HostSpace> indices("indices", 4);
        for (axis::index_t i = 0; i < 4; ++i) indices(i) = i;
        Kokkos::View<double*, Kokkos::HostSpace> areas("areas", 1);
        areas(0) = area;
        return std::make_shared<const Mesh>(nodes, offsets, indices, axis::topology::CoordinateSystem::SphericalDeg, areas);
    };

    cece::io::GridGeometry geometry;
    geometry.nx = geometry.ny = 1;
    geometry.center_lon = {0.5};
    geometry.center_lat = {0.5};
    geometry.has_cell_geometry = true;
    geometry.supplied_cells = make_mesh(1.0, 1.0);
    const auto original = cece::io::grid_geometry_revision(geometry);
    geometry.supplied_cells = make_mesh(2.0, 1.0);
    EXPECT_NE(original, cece::io::grid_geometry_revision(geometry));
    geometry.supplied_cells = make_mesh(1.0, 2.0);
    EXPECT_NE(original, cece::io::grid_geometry_revision(geometry));
}

TEST(GeometryPlanLifecycle, FailedRankPreventsCandidatePublicationAndEndpointInvalidation) {
    int initialized = 0;
    MPI_Initialized(&initialized);
    ASSERT_TRUE(initialized) << "this regression must run through the MPI test launcher";
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // Mirror the facade's plan transaction: keep a local candidate, perform
    // the production readiness collective, and commit/invalidate only when all
    // ranks report ready. Rank zero injects a plan-build failure.
    cece::io::RegridPlan local_candidate;
    local_candidate.built = true;
    std::optional<cece::io::RegridPlan> candidate{std::move(local_candidate)};
    std::unordered_map<std::string, cece::io::RegridPlan> published_plans;
    bool endpoint_valid = true;
    std::string failure_detail;
    const bool all_ready = cece::CollectiveGateTestAccess::AllReady(MPI_COMM_WORLD, rank != 0, "injected plan-build failure", failure_detail);
    const bool committed = cece::io::publish_regrid_plan_if_ready(all_ready, "stream", candidate, published_plans);
    if (committed) {
        endpoint_valid = false;
    }
    EXPECT_FALSE(all_ready);
    EXPECT_FALSE(committed);
    EXPECT_TRUE(published_plans.empty());
    EXPECT_TRUE(endpoint_valid);
    EXPECT_TRUE(candidate.has_value());
    EXPECT_FALSE(failure_detail.empty());
}
