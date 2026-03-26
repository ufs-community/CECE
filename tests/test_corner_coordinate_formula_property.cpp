/**
 * @file test_corner_coordinate_formula_property.cpp
 * @brief Property 1: Corner coordinate formula correctness.
 *
 * For any nx ∈ [1,360] and ny ∈ [1,180], the corner coordinate formula
 *   lon_corner(i,j) = (i-1)*(360.0/nx) - 180.0   for i ∈ [1, nx+1]
 *   lat_corner(i,j) = (j-1)*(180.0/ny) - 90.0    for j ∈ [1, ny+1]
 * must hold at all indices.
 *
 * Validates: Requirements 1.1, 1.2
 */

#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include <cmath>
#include <vector>

namespace {

/** Compute corner longitudes for a given nx. Returns (nx+1) values. */
std::vector<double> corner_lons(int nx) {
    double dlon = 360.0 / nx;
    std::vector<double> lons(nx + 1);
    for (int i = 1; i <= nx + 1; ++i) {
        lons[i - 1] = (i - 1) * dlon - 180.0;
    }
    return lons;
}

/** Compute corner latitudes for a given ny. Returns (ny+1) values. */
std::vector<double> corner_lats(int ny) {
    double dlat = 180.0 / ny;
    std::vector<double> lats(ny + 1);
    for (int j = 1; j <= ny + 1; ++j) {
        lats[j - 1] = (j - 1) * dlat - 90.0;
    }
    return lats;
}

}  // namespace

/**
 * @test CornerCoordFormula_LonBoundsCorrect
 * Verifies that the first corner longitude is -180 and the last is +180.
 */
RC_GTEST_PROP(CornerCoordinateFormulaProperty, LonBoundsCorrect, ()) {
    const int nx = *rc::gen::inRange(1, 361);
    auto lons = corner_lons(nx);

    RC_ASSERT(std::abs(lons.front() - (-180.0)) < 1e-10);
    RC_ASSERT(std::abs(lons.back() - 180.0) < 1e-10);
}

/**
 * @test CornerCoordFormula_LatBoundsCorrect
 * Verifies that the first corner latitude is -90 and the last is +90.
 */
RC_GTEST_PROP(CornerCoordinateFormulaProperty, LatBoundsCorrect, ()) {
    const int ny = *rc::gen::inRange(1, 181);
    auto lats = corner_lats(ny);

    RC_ASSERT(std::abs(lats.front() - (-90.0)) < 1e-10);
    RC_ASSERT(std::abs(lats.back() - 90.0) < 1e-10);
}

/**
 * @test CornerCoordFormula_LonSpacingUniform
 * Verifies that consecutive corner longitudes are uniformly spaced by 360/nx.
 */
RC_GTEST_PROP(CornerCoordinateFormulaProperty, LonSpacingUniform, ()) {
    const int nx = *rc::gen::inRange(1, 361);
    auto lons = corner_lons(nx);
    const double expected_dlon = 360.0 / nx;

    for (int i = 0; i < nx; ++i) {
        double spacing = lons[i + 1] - lons[i];
        RC_ASSERT(std::abs(spacing - expected_dlon) < 1e-10);
    }
}

/**
 * @test CornerCoordFormula_LatSpacingUniform
 * Verifies that consecutive corner latitudes are uniformly spaced by 180/ny.
 */
RC_GTEST_PROP(CornerCoordinateFormulaProperty, LatSpacingUniform, ()) {
    const int ny = *rc::gen::inRange(1, 181);
    auto lats = corner_lats(ny);
    const double expected_dlat = 180.0 / ny;

    for (int j = 0; j < ny; ++j) {
        double spacing = lats[j + 1] - lats[j];
        RC_ASSERT(std::abs(spacing - expected_dlat) < 1e-10);
    }
}

/**
 * @test CornerCoordFormula_LonCountIsNxPlusOne
 * Verifies that the corner longitude array has exactly nx+1 elements.
 */
RC_GTEST_PROP(CornerCoordinateFormulaProperty, LonCountIsNxPlusOne, ()) {
    const int nx = *rc::gen::inRange(1, 361);
    auto lons = corner_lons(nx);
    RC_ASSERT(static_cast<int>(lons.size()) == nx + 1);
}

/**
 * @test CornerCoordFormula_LatCountIsNyPlusOne
 * Verifies that the corner latitude array has exactly ny+1 elements.
 */
RC_GTEST_PROP(CornerCoordinateFormulaProperty, LatCountIsNyPlusOne, ()) {
    const int ny = *rc::gen::inRange(1, 181);
    auto lats = corner_lats(ny);
    RC_ASSERT(static_cast<int>(lats.size()) == ny + 1);
}

/**
 * @test CornerCoordFormula_FormulaExact
 * Verifies the exact formula at every index for arbitrary nx, ny.
 */
RC_GTEST_PROP(CornerCoordinateFormulaProperty, FormulaExact, ()) {
    const int nx = *rc::gen::inRange(1, 361);
    const int ny = *rc::gen::inRange(1, 181);
    const double dlon = 360.0 / nx;
    const double dlat = 180.0 / ny;

    for (int i = 1; i <= nx + 1; ++i) {
        double expected_lon = (i - 1) * dlon - 180.0;
        double actual_lon = corner_lons(nx)[i - 1];
        RC_ASSERT(std::abs(actual_lon - expected_lon) < 1e-10);
    }

    for (int j = 1; j <= ny + 1; ++j) {
        double expected_lat = (j - 1) * dlat - 90.0;
        double actual_lat = corner_lats(ny)[j - 1];
        RC_ASSERT(std::abs(actual_lat - expected_lat) < 1e-10);
    }
}
