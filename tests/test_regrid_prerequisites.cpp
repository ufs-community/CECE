#include <gtest/gtest.h>

#include "cece/cece_regridder_utils.hpp"

TEST(RegridPrerequisites, UnknownMethodIncludesStageAndCorrection) {
    try {
        (void)cece::io::resolve_regrid_method("not-a-method");
        FAIL() << "expected unknown method error";
    } catch (const cece::io::RegridError& error) {
        EXPECT_EQ(error.stage(), cece::io::RegridStage::PrerequisiteValidation);
        EXPECT_EQ(error.method(), "not-a-method");
        EXPECT_NE(std::string(error.what()).find("Use nearest"), std::string::npos);
    }
}

TEST(RegridPrerequisites, InvalidLatitudeRejectedBeforePlanBuild) {
    cece::io::GridGeometry source, destination;
    for (auto* grid : {&source, &destination}) {
        grid->nx = grid->ny = 1;
        grid->center_lon = {0.0};
        grid->center_lat = {0.0};
    }
    source.center_lat[0] = 91.0;
    const auto settings = cece::io::resolve_regrid_method("nn");
    EXPECT_THROW(cece::io::validate_regrid_prerequisites(source, destination, settings, 0, 1), cece::io::RegridError);
}

TEST(RegridPrerequisites, ConservativeRequiresTrustedCompatibleGeometry) {
    cece::io::GridGeometry source, destination;
    for (auto* grid : {&source, &destination}) {
        grid->nx = grid->ny = 1;
        grid->center_lon = {0.0};
        grid->center_lat = {0.0};
        grid->has_cell_geometry = true;
        grid->metadata.provenance = axis::topology::GeometryProvenance::SourceAuthoritative;
        grid->metadata.boundary_model = axis::topology::BoundaryModel::GreatCircle;
        grid->metadata.area_model = axis::topology::AreaModel::SphericalExcess;
    }
    const auto settings = cece::io::resolve_regrid_method("consd");
    EXPECT_NO_THROW(cece::io::validate_regrid_prerequisites(source, destination, settings, 0, 1));
    source.metadata.provenance = axis::topology::GeometryProvenance::Reconstructed;
    EXPECT_THROW(cece::io::validate_regrid_prerequisites(source, destination, settings, 0, 1), cece::io::RegridError);
}

TEST(RegridPrerequisites, StripSecondOrderRejectedBeforeGeneration) {
    cece::io::GridGeometry source, destination;
    for (auto* grid : {&source, &destination}) {
        grid->nx = grid->ny = 1;
        grid->center_lon = {0.0};
        grid->center_lat = {0.0};
        grid->has_cell_geometry = true;
        grid->metadata.provenance = axis::topology::GeometryProvenance::DeclaredGridModel;
        grid->metadata.boundary_model = axis::topology::BoundaryModel::ConstantLatitude;
        grid->metadata.area_model = axis::topology::AreaModel::ConstantLatitudeStrip;
    }
    EXPECT_THROW(cece::io::validate_regrid_prerequisites(source, destination, cece::io::resolve_regrid_method("conss"), 0, 1), cece::io::RegridError);
}

TEST(RegridPrerequisites, ConservativeReportsWhichGridLacksBounds) {
    cece::io::GridGeometry source, destination;
    for (auto* grid : {&source, &destination}) {
        grid->nx = grid->ny = 1;
        grid->center_lon = {0.0};
        grid->center_lat = {0.0};
        grid->has_cell_geometry = true;
        grid->metadata.provenance = axis::topology::GeometryProvenance::SourceAuthoritative;
        grid->metadata.boundary_model = axis::topology::BoundaryModel::GreatCircle;
        grid->metadata.area_model = axis::topology::AreaModel::SphericalExcess;
    }
    destination.has_cell_geometry = false;
    try {
        (void)cece::io::validate_regrid_prerequisites(source, destination, cece::io::resolve_regrid_method("consd"), 0, 1);
        FAIL() << "expected missing destination geometry to fail";
    } catch (const cece::io::RegridError& error) {
        EXPECT_EQ(error.role(), cece::io::GridRole::Destination);
        EXPECT_EQ(error.stage(), cece::io::RegridStage::PrerequisiteValidation);
    }
}

TEST(RegridPrerequisites, NearestAcceptsCenterOnlyNonuniformSamples) {
    cece::io::GridGeometry source, destination;
    source.nx = 3;
    source.ny = 2;
    source.center_lon = {0.0, 0.1, 35.0};
    source.center_lat = {-20.0, 25.0};
    destination.nx = destination.ny = 1;
    destination.center_lon = {0.09};
    destination.center_lat = {0.0};
    EXPECT_NO_THROW(cece::io::validate_regrid_prerequisites(source, destination, cece::io::resolve_regrid_method("nearest"), 0, 1));
    EXPECT_FALSE(source.has_cell_geometry);
    EXPECT_FALSE(destination.has_cell_geometry);
}

TEST(RegridPrerequisites, BilinearRejectsMissingStructuredStencil) {
    cece::io::GridGeometry source, destination;
    source.nx = 3;
    source.ny = 1;
    source.center_lon = {0.0, 1.0, 2.0};
    source.center_lat = {0.0};
    destination.nx = destination.ny = 1;
    destination.center_lon = {0.5};
    destination.center_lat = {0.5};
    try {
        (void)cece::io::validate_regrid_prerequisites(source, destination, cece::io::resolve_regrid_method("bilinear"), 0, 1);
        FAIL() << "expected incomplete stencil to fail";
    } catch (const cece::io::RegridError& error) {
        EXPECT_EQ(error.role(), cece::io::GridRole::Source);
        EXPECT_NE(error.reason().find("structured source topology"), std::string::npos);
    }
}

TEST(RegridPrerequisites, PassthroughDoesNotRequireCellPolygons) {
    cece::io::GridGeometry source, destination;
    for (auto* grid : {&source, &destination}) {
        grid->nx = 2;
        grid->ny = 1;
        grid->center_lon = {0.0, 1.0};
        grid->center_lat = {0.0};
    }
    EXPECT_NO_THROW(cece::io::validate_regrid_prerequisites(source, destination, cece::io::resolve_regrid_method("passthrough"), 0, 1));
    EXPECT_FALSE(source.has_cell_geometry);
    EXPECT_FALSE(destination.has_cell_geometry);
}

TEST(RegridPrerequisites, DeviceBackendFailsBeforePlanGeneration) {
    cece::io::GridGeometry source, destination;
    for (auto* grid : {&source, &destination}) {
        grid->nx = grid->ny = 1;
        grid->center_lon = {0.0};
        grid->center_lat = {0.0};
    }
    try {
        (void)cece::io::validate_regrid_prerequisites(source, destination, cece::io::resolve_regrid_method("nearest"), 0, 1,
                                                      cece::io::RegridBackend::Device);
        FAIL() << "expected unsupported backend to fail";
    } catch (const cece::io::RegridError& error) {
        EXPECT_EQ(error.stage(), cece::io::RegridStage::PrerequisiteValidation);
        EXPECT_EQ(error.role(), cece::io::GridRole::Both);
        EXPECT_NE(error.reason().find("backend"), std::string::npos);
    }
}

// Mixed source/destination geometry models are accepted (ufs-community/CECE#124
// review): a rectilinear CF-bounds inventory onto a cubed-sphere target is
// exact first-order conservative because AXIS clips the prepared cell polygons
// on the sphere regardless of the declared models. Each grid is still
// internally validated, so the per-grid rejects below must persist.
TEST(RegridPrerequisites, MixedConservativeModelsAcceptedForPlanGeneration) {
    cece::io::GridGeometry source, destination;
    for (auto* grid : {&source, &destination}) {
        grid->nx = grid->ny = 1;
        grid->center_lon = {0.0};
        grid->center_lat = {0.0};
        grid->has_cell_geometry = true;
        grid->metadata.provenance = axis::topology::GeometryProvenance::SourceAuthoritative;
        grid->metadata.boundary_model = axis::topology::BoundaryModel::GreatCircle;
        grid->metadata.area_model = axis::topology::AreaModel::SphericalExcess;
    }
    destination.metadata.boundary_model = axis::topology::BoundaryModel::ConstantLatitude;
    destination.metadata.area_model = axis::topology::AreaModel::ConstantLatitudeStrip;
    EXPECT_NO_THROW(cece::io::validate_regrid_prerequisites(source, destination, cece::io::resolve_regrid_method("consd"), 0, 1));
    source.metadata.boundary_model = axis::topology::BoundaryModel::ConstantLatitude;
    source.metadata.area_model = axis::topology::AreaModel::ConstantLatitudeStrip;
    EXPECT_NO_THROW(cece::io::validate_regrid_prerequisites(source, destination, cece::io::resolve_regrid_method("consd"), 0, 1));
}

// Per-grid internal consistency still fails even though cross-pair equality
// was relaxed: a constant-latitude strip grid cannot serve second-order
// conservative mapping, and an unspecified model is never accepted.
TEST(RegridPrerequisites, MixedPairsStillRequirePerGridConsistency) {
    cece::io::GridGeometry source, destination;
    for (auto* grid : {&source, &destination}) {
        grid->nx = grid->ny = 1;
        grid->center_lon = {0.0};
        grid->center_lat = {0.0};
        grid->has_cell_geometry = true;
        grid->metadata.provenance = axis::topology::GeometryProvenance::SourceAuthoritative;
        grid->metadata.boundary_model = axis::topology::BoundaryModel::GreatCircle;
        grid->metadata.area_model = axis::topology::AreaModel::SphericalExcess;
    }
    destination.metadata.boundary_model = axis::topology::BoundaryModel::ConstantLatitude;
    destination.metadata.area_model = axis::topology::AreaModel::Unspecified;
    EXPECT_THROW(cece::io::validate_regrid_prerequisites(source, destination, cece::io::resolve_regrid_method("consd"), 0, 1), cece::io::RegridError);
}
