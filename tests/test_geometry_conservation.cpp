#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <axis/solver/weight_generator.hpp>
#include <cmath>
#include <vector>

#include "cece/cece_regridder_utils.hpp"

namespace {
double unit_sphere_strip_area(double west_deg, double east_deg, double south_deg, double north_deg) {
    constexpr double pi = 3.14159265358979323846;
    const double width = (east_deg - west_deg) * pi / 180.0;
    return width * (std::sin(north_deg * pi / 180.0) - std::sin(south_deg * pi / 180.0));
}
}  // namespace

TEST(GeometryConservation, IndependentConstantLatitudeReferenceIsAdditive) {
    const double whole = unit_sphere_strip_area(-180.0, 180.0, -90.0, 90.0);
    const double south = unit_sphere_strip_area(-180.0, 180.0, -90.0, 15.0);
    const double north = unit_sphere_strip_area(-180.0, 180.0, 15.0, 90.0);
    EXPECT_NEAR(whole, 4.0 * 3.14159265358979323846, 1e-14);
    EXPECT_NEAR(south + north, whole, 1e-14);
}

TEST(GeometryConservation, AxisFirstOrderPreservesAnalyticStripIntegralAndConstantField) {
    using namespace cece::io;
    using Mesh = axis::topology::UnstructuredMesh<Kokkos::HostSpace>;
    const std::vector<double> src_lon{-90.0, 90.0}, src_lat{-45.0, 45.0};
    auto src_mesh = build_axis_mesh(2, 2, src_lon, src_lat);
    auto dst_mesh = build_axis_mesh(2, 2, src_lon, src_lat);
    auto declare_strip = [](Mesh& mesh) {
        auto metadata = mesh.geometry_metadata();
        metadata.provenance = axis::topology::GeometryProvenance::DeclaredGridModel;
        metadata.boundary_model = axis::topology::BoundaryModel::ConstantLatitude;
        metadata.area_model = axis::topology::AreaModel::ConstantLatitudeStrip;
        mesh.set_geometry_metadata(metadata);
    };
    declare_strip(src_mesh);
    declare_strip(dst_mesh);

    GridGeometry source, destination;
    source.nx = 2;
    source.ny = 2;
    source.center_lon = src_lon;
    source.center_lat = src_lat;
    source.has_cell_geometry = true;
    source.metadata = src_mesh.geometry_metadata();
    destination.nx = 2;
    destination.ny = 2;
    destination.center_lon = src_lon;
    destination.center_lat = src_lat;
    destination.has_cell_geometry = true;
    destination.metadata = dst_mesh.geometry_metadata();
    const auto request = validate_regrid_prerequisites(source, destination, resolve_regrid_method("consd"), 0, 2);
    EXPECT_EQ(request.settings.method, RegridMethod::Conservative1);

    axis::solver::RegridConfig config;
    config.method = axis::solver::InterpolationMethod::Conservative1stOrder;
    config.norm_type = request.settings.normalization == "dstarea" ? axis::solver::NormType::DstArea : axis::solver::NormType::FracArea;
    config.unmapped = axis::solver::UnmappedAction::Ignore;
    RegridPlan plan;
    plan.file_nx = 2;
    plan.file_ny = 2;
    plan.j0 = 0;
    plan.j1 = 2;
    plan.matrix = axis::solver::WeightGenerator::generate<Kokkos::HostSpace>(src_mesh, dst_mesh, config);
    plan.matrix.to_csr();
    plan.built = true;

    const std::vector<double> source_values{1.0, 3.0, 2.0, 4.0};
    std::vector<double> result;
    ASSERT_TRUE(apply_regrid_plan(plan, 0, false, source_values.data(), 2, 2, 2, result));
    ASSERT_EQ(result.size(), 4u);

    // Independent analytic expectation integrates each source strip using its
    // latitude/longitude edges. The target subdivides the same full sphere.
    const double analytic_integral = unit_sphere_strip_area(-180.0, 0.0, -90.0, 0.0) + 3.0 * unit_sphere_strip_area(0.0, 180.0, -90.0, 0.0) +
                                     2.0 * unit_sphere_strip_area(-180.0, 0.0, 0.0, 90.0) + 4.0 * unit_sphere_strip_area(0.0, 180.0, 0.0, 90.0);
    double source_integral = 0.0;
    for (std::size_t cell = 0; cell < source_values.size(); ++cell) source_integral += source_values[cell] * src_mesh.cell_areas()(cell);
    EXPECT_NEAR(source_integral, analytic_integral, 1e-12);

    double destination_integral = 0.0;
    for (std::size_t cell = 0; cell < result.size(); ++cell) destination_integral += result[cell] * dst_mesh.cell_areas()(cell);
    EXPECT_NEAR(destination_integral, analytic_integral, 1e-10 * analytic_integral);

    RegridPlan constant_plan;
    constant_plan.file_nx = 2;
    constant_plan.file_ny = 2;
    constant_plan.j1 = 2;
    constant_plan.matrix = axis::solver::WeightGenerator::generate<Kokkos::HostSpace>(src_mesh, dst_mesh, config);
    constant_plan.matrix.to_csr();
    constant_plan.built = true;
    const std::vector<double> ones(4, 1.0);
    ASSERT_TRUE(apply_regrid_plan(constant_plan, 0, false, ones.data(), 2, 2, 2, result));
    for (double value : result) EXPECT_NEAR(value, 1.0, 1e-12);
}

TEST(GeometryConservation, UnsupportedStripAndMixedRequestsRejectAtPrerequisiteStage) {
    using namespace cece::io;
    GridGeometry source, destination;
    for (auto* grid : {&source, &destination}) {
        grid->nx = grid->ny = 1;
        grid->center_lon = {0.0};
        grid->center_lat = {0.0};
        grid->has_cell_geometry = true;
        grid->metadata.provenance = axis::topology::GeometryProvenance::DeclaredGridModel;
        grid->metadata.boundary_model = axis::topology::BoundaryModel::ConstantLatitude;
        grid->metadata.area_model = axis::topology::AreaModel::ConstantLatitudeStrip;
    }
    EXPECT_THROW(validate_regrid_prerequisites(source, destination, resolve_regrid_method("conss"), 0, 1), RegridError);
    EXPECT_THROW(validate_regrid_prerequisites(source, destination, resolve_regrid_method("consd"), 0, 1, RegridBackend::Device), RegridError);
    destination.metadata.area_model = axis::topology::AreaModel::SphericalExcess;
    EXPECT_THROW(validate_regrid_prerequisites(source, destination, resolve_regrid_method("consd"), 0, 1), RegridError);
}
