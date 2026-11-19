// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <axis/solver/interpolation_matrix.hpp>
#include <axis/solver/regrid_config.hpp>
#include <axis/solver/weight_generator.hpp>
#include <axis/topology/structured_grid.hpp>
#include <cmath>
#include <numbers>
#include <numeric>

#include "cece/cece_fire_input.hpp"

namespace {
using Matrix = axis::solver::InterpolationMatrix<Kokkos::HostSpace>;
using Norm = axis::solver::NormType;

template <typename T>
Kokkos::View<T*, Kokkos::HostSpace> HostValues(const std::vector<T>& values) {
    Kokkos::View<T*, Kokkos::HostSpace> result("fire_test_values", values.size());
    for (std::size_t i = 0; i < values.size(); ++i) result(i) = values[i];
    return result;
}

Matrix MakeMatrix(std::vector<double> weights = {0.5, 0.5, 1}, std::vector<axis::index_t> rows = {0, 0, 1},
                  std::vector<axis::index_t> columns = {0, 1, 1}, std::vector<double> source_fraction = {1, 1},
                  std::vector<double> target_fraction = {1, 1}, std::vector<double> source_area = {25, 75},
                  std::vector<double> target_area = {50, 50}) {
    return Matrix(HostValues(weights), HostValues(rows), HostValues(columns), HostValues(source_fraction), HostValues(target_fraction),
                  HostValues(source_area), HostValues(target_area), source_area.size(), target_area.size());
}

std::shared_ptr<const cece::FireGrid> GridWithAreas(std::vector<double> areas, std::string id = "synthetic-grid") {
    const auto cells = areas.size();
    return std::make_shared<const cece::FireGrid>(
        cece::FireGridDefinition{std::move(id), std::string(64, 'b'), cells, 1, std::vector<double>(cells, 0), std::vector<double>(cells, 0),
                                 std::vector<std::array<double, 4>>(cells, {-1, 1, 1, -1}), std::vector<std::array<double, 4>>(cells, {-1, -1, 1, 1}),
                                 std::move(areas), "EPSG:4326", cece::FireLongitudeConvention::Minus180To180});
}

cece::FireBulkInventory Inventory(std::shared_ptr<const cece::FireGrid> grid = GridWithAreas({25, 75}), std::vector<double> masses = {10, 30, 20, 60},
                                  std::vector<double> coverage = {1, 1}) {
    const auto cells = grid->CellCount();
    const cece::FireProfileIdentity biome{"synthetic-biome", "1", std::string(64, 'c')};
    const cece::FireProfileIdentity composition_a{"synthetic-composition-a", "1", std::string(64, 'd')};
    const cece::FireProfileIdentity composition_b{"synthetic-composition-b", "1", std::string(64, 'e')};
    return cece::FireBulkInventory(
        {cece::FireInputIdentity({1, "source", "product", "algorithm", "delivery", std::string(64, 'a'), cece::FireUtcTime("2022-07-01T23:00:00Z"),
                                  cece::FireUtcTime("2022-07-02T01:00:00Z"), cece::FireUtcTime("2022-07-02T01:00:00Z"),
                                  cece::FireUtcTime("2022-07-02T01:00:00Z"), cece::FireUtcTime("2022-07-02T02:00:00Z"), cece::FireCutoffMode::Fixed,
                                  std::move(grid)}),
         {cece::FireBulkSpecies::CO},
         {"a", "b"},
         {{"a", {biome, composition_a}}, {"b", {biome, composition_b}}},
         std::move(masses),
         std::vector<cece::FireQualityState>(cells, cece::FireQualityState::Observed),
         std::move(coverage),
         {std::string(64, 'a')},
         std::nullopt,
         std::nullopt,
         std::nullopt,
         std::nullopt,
         std::nullopt});
}

TEST(test_fire_conservation, HalfOpenOverlapRetainsUtcBoundaryAndPartialSupport) {
    const cece::FireUtcTime start("2022-07-01T23:00:00Z"), middle("2022-07-02T00:00:00Z"), end("2022-07-02T01:00:00Z");
    EXPECT_EQ(cece::FireIntervalOverlapSeconds(start, middle, middle, end), 0);
    EXPECT_EQ(cece::FireIntervalOverlapSeconds(start, end, middle, end), 3600);
    EXPECT_THROW(cece::FireIntervalOverlapSeconds(end, start, middle, end), std::invalid_argument);
    const auto inventory = Inventory();
    const auto target = GridWithAreas({50, 50}, "target");
    const auto matrix = MakeMatrix();
    const auto first = cece::FireAllocateConservativeMass(inventory, target, matrix, Norm::DstArea, start, middle);
    const auto second = cece::FireAllocateConservativeMass(inventory, target, matrix, Norm::DstArea, middle, end);
    for (std::size_t i = 0; i < first.mass_kg.size(); ++i) {
        EXPECT_NEAR(first.mass_kg[i] + second.mass_kg[i], i < 2 ? 20.0 : 40.0, 1e-12);
    }
    EXPECT_EQ(first.overlap_seconds, 3600);
    EXPECT_EQ(first.component_ids, inventory.Data().component_ids);
    EXPECT_EQ(first.component_profiles.at("b").composition.id, "synthetic-composition-b");
    const double mapped_a = first.mass_kg[0] + first.mass_kg[1] + second.mass_kg[0] + second.mass_kg[1];
    const double mapped_b = first.mass_kg[2] + first.mass_kg[3] + second.mass_kg[2] + second.mass_kg[3];
    EXPECT_NEAR(mapped_a + 3 * mapped_b, 280.0, 280.0e-6);
    ASSERT_EQ(first.species_budgets.size(), 1);
    EXPECT_EQ(first.species_budgets[0].species, cece::FireBulkSpecies::CO);
    EXPECT_FALSE(first.species_budgets[0].component_id);
    EXPECT_NEAR(first.species_budgets[0].supported_input_kg, 60, 60e-6);
    EXPECT_NEAR(first.species_budgets[0].output_kg, 60, 60e-6);
}

TEST(test_fire_conservation, PartialCoverageIsNotRenormalizedOrReportedAsValidZero) {
    const auto inventory = Inventory(GridWithAreas({25, 75}), {10, 30, 20, 60}, {1, 0.5});
    const auto matrix = MakeMatrix({0.5, 0.5}, {0, 0}, {0, 1}, {1, 1.0 / 3}, {1, 0});
    const auto result = cece::FireAllocateConservativeMass(inventory, GridWithAreas({50, 50}, "target"), matrix, Norm::DstArea,
                                                           cece::FireUtcTime("2022-07-02T00:00:00Z"), cece::FireUtcTime("2022-07-02T02:00:00Z"));
    EXPECT_EQ(result.overlap_seconds, 3600);
    EXPECT_DOUBLE_EQ(result.mass_kg[0], 10);
    EXPECT_DOUBLE_EQ(result.mass_kg[1], 0);
    EXPECT_DOUBLE_EQ(result.mass_kg[2], 20);
    EXPECT_NEAR(result.coverage_fraction[0], 0.375, 1e-12);
    EXPECT_DOUBLE_EQ(result.coverage_fraction[1], 0);
    EXPECT_EQ(result.quality_state[1], cece::FireQualityState::Unavailable);
    ASSERT_EQ(result.component_budgets.size(), 2);
    EXPECT_EQ(result.component_budgets[0].component_id, "a");
    EXPECT_DOUBLE_EQ(result.component_budgets[0].input_kg, 40);
    EXPECT_DOUBLE_EQ(result.component_budgets[0].supported_input_kg, 10);
    EXPECT_DOUBLE_EQ(result.component_budgets[0].output_kg, 10);
}

TEST(test_fire_conservation, MixedSourceComponentsRemainSpatiallyDistinctUntilTheirFactorsApply) {
    const auto inventory = Inventory(GridWithAreas({25, 75}), {10, 0, 0, 30});
    const cece::FireUtcTime start("2022-07-01T23:00:00Z"), end("2022-07-02T01:00:00Z");
    const auto target = GridWithAreas({50, 50}, "target");
    const auto allocated = cece::FireAllocateConservativeMass(inventory, target, MakeMatrix(), Norm::DstArea, start, end);
    EXPECT_DOUBLE_EQ(allocated.mass_kg[0], 10);
    EXPECT_DOUBLE_EQ(allocated.mass_kg[1], 0);
    EXPECT_DOUBLE_EQ(allocated.mass_kg[2], 10);
    EXPECT_DOUBLE_EQ(allocated.mass_kg[3], 20);
    auto source_mapped = inventory.Data();
    source_mapped.mass_kg = {10, 0, 0, 90};
    const auto mapped_first =
        cece::FireAllocateConservativeMass(cece::FireBulkInventory(source_mapped), target, MakeMatrix(), Norm::DstArea, start, end);
    for (std::size_t cell = 0; cell < 2; ++cell) {
        const double map_after_remap = allocated.mass_kg[cell] + 3 * allocated.mass_kg[2 + cell];
        EXPECT_NEAR(map_after_remap, cell == 0 ? 40 : 60, 1e-6);
        EXPECT_NEAR(map_after_remap, mapped_first.mass_kg[cell] + mapped_first.mass_kg[2 + cell], 1e-6);
    }
}

TEST(test_fire_conservation, DensityNormalizationsRecoverTheSameExtensiveMass) {
    const auto inventory = Inventory(GridWithAreas({25}), {8, 16}, {1});
    const auto target = GridWithAreas({50}, "target");
    const cece::FireUtcTime start("2022-07-01T23:00:00Z"), end("2022-07-02T01:00:00Z");
    const auto dst =
        cece::FireAllocateConservativeMass(inventory, target, MakeMatrix({0.5}, {0}, {0}, {1}, {0.5}, {25}, {50}), Norm::DstArea, start, end);
    const auto frac =
        cece::FireAllocateConservativeMass(inventory, target, MakeMatrix({1}, {0}, {0}, {1}, {0.5}, {25}, {50}), Norm::FracArea, start, end);
    EXPECT_EQ(dst.mass_kg, frac.mass_kg);
    EXPECT_DOUBLE_EQ(dst.mass_kg[0], 8);
    EXPECT_DOUBLE_EQ(dst.mass_kg[1], 16);
    EXPECT_DOUBLE_EQ(frac.coverage_fraction[0], 0.5);
    EXPECT_THROW(cece::FireAllocateConservativeMass(inventory, target, MakeMatrix({1}, {0}, {0}, {1}, {0.5}, {25}, {50}), Norm::DstArea, start, end),
                 std::invalid_argument);
}

TEST(test_fire_conservation, NoTemporalSupportRemainsUnavailableAndInvalidOperatorsFail) {
    const auto inventory = Inventory();
    const auto target = GridWithAreas({50, 50}, "target");
    const cece::FireUtcTime start("2022-07-02T01:00:00Z"), end("2022-07-02T02:00:00Z");
    const auto result = cece::FireAllocateConservativeMass(inventory, target, MakeMatrix(), Norm::DstArea, start, end);
    EXPECT_EQ(result.overlap_seconds, 0);
    EXPECT_DOUBLE_EQ(std::accumulate(result.mass_kg.begin(), result.mass_kg.end(), 0.0), 0);
    EXPECT_EQ(result.quality_state[0], cece::FireQualityState::Unavailable);
    EXPECT_DOUBLE_EQ(result.coverage_fraction[0], 0);
    EXPECT_THROW(cece::FireAllocateConservativeMass(inventory, target, MakeMatrix({-0.5, 0.5, 1}), Norm::DstArea, start, end), std::invalid_argument);
    EXPECT_THROW(cece::FireAllocateConservativeMass(inventory, target, MakeMatrix({0.5, 0.5, 1}, {0, 0, 2}), Norm::DstArea, start, end),
                 std::invalid_argument);
    EXPECT_THROW(cece::FireAllocateConservativeMass(inventory, GridWithAreas({50, 75}), MakeMatrix(), Norm::DstArea, start, end),
                 std::invalid_argument);
}

TEST(test_fire_conservation, AllocationRetainsChemicalConventionsAndOptionalEnergyBudgets) {
    auto data = Inventory().Data();
    const cece::FireProfileIdentity profile{"synthetic-voc-pm", "1", std::string(64, 'c')};
    data.species = {cece::FireBulkSpecies::CO, cece::FireBulkSpecies::NOX, cece::FireBulkSpecies::VOC, cece::FireBulkSpecies::PM25};
    data.mass_kg = std::vector<double>(16, 1);
    data.nox_reference_basis = cece::FireNoxReferenceBasis::NO2;
    data.voc_profile = profile;
    data.particulate_overlap = cece::FireParticulateOverlap{profile, {{cece::FireBulkSpecies::PM25, "synthetic-pm25"}}};
    data.fre_mj = std::vector<double>{100, 300};
    data.dry_matter_kg = std::vector<double>{50, 150};
    const auto result = cece::FireAllocateConservativeMass(cece::FireBulkInventory(data), GridWithAreas({50, 50}), MakeMatrix(), Norm::DstArea,
                                                           cece::FireUtcTime("2022-07-01T23:00:00Z"), cece::FireUtcTime("2022-07-02T01:00:00Z"));
    EXPECT_EQ(result.accepted_source_sha256, data.accepted_source_sha256);
    EXPECT_EQ(result.nox_reference_basis, cece::FireNoxReferenceBasis::NO2);
    ASSERT_TRUE(result.voc_profile);
    EXPECT_EQ(result.voc_profile->content_sha256, profile.content_sha256);
    ASSERT_TRUE(result.particulate_overlap);
    EXPECT_EQ(result.particulate_overlap->pool_definitions.at(cece::FireBulkSpecies::PM25), "synthetic-pm25");
    ASSERT_TRUE(result.fre_mj);
    ASSERT_TRUE(result.fre_budget);
    EXPECT_DOUBLE_EQ(result.fre_mj->at(0), 200);
    EXPECT_DOUBLE_EQ(result.fre_mj->at(1), 200);
    EXPECT_DOUBLE_EQ(result.fre_budget->output_mj, 400);
    ASSERT_TRUE(result.dry_matter_kg);
    ASSERT_TRUE(result.dry_matter_budget);
    EXPECT_DOUBLE_EQ(result.dry_matter_budget->output_kg, 200);
    EXPECT_EQ(result.species_budgets.size(), 4);
    EXPECT_EQ(result.component_budgets.size(), 8);
}

TEST(test_fire_conservation, ValidZeroAndReconstructedQualitySurviveWithoutInventedDiagnostics) {
    auto data = Inventory().Data();
    data.mass_kg = std::vector<double>(4, 0);
    data.quality_state = std::vector<cece::FireQualityState>(2, cece::FireQualityState::ValidZero);
    const auto zero = cece::FireAllocateConservativeMass(cece::FireBulkInventory(data), GridWithAreas({50, 50}), MakeMatrix(), Norm::DstArea,
                                                         cece::FireUtcTime("2022-07-01T23:00:00Z"), cece::FireUtcTime("2022-07-02T01:00:00Z"));
    EXPECT_EQ(zero.quality_state[0], cece::FireQualityState::ValidZero);
    EXPECT_FALSE(zero.fre_mj);
    EXPECT_FALSE(zero.fre_budget);
    EXPECT_FALSE(zero.dry_matter_kg);
    data.quality_state[0] = cece::FireQualityState::Reconstructed;
    const auto reconstructed =
        cece::FireAllocateConservativeMass(cece::FireBulkInventory(data), GridWithAreas({50, 50}), MakeMatrix(), Norm::DstArea,
                                           cece::FireUtcTime("2022-07-01T23:00:00Z"), cece::FireUtcTime("2022-07-02T01:00:00Z"));
    EXPECT_EQ(reconstructed.quality_state[0], cece::FireQualityState::Reconstructed);
    EXPECT_EQ(reconstructed.quality_state[1], cece::FireQualityState::ValidZero);
}

cece::FireGridDefinition SphericalGrid(std::size_t nx, std::size_t ny, double west) {
    cece::FireGridDefinition grid;
    grid.grid_id = "spherical-" + std::to_string(nx) + "-" + std::to_string(west);
    grid.grid_sha256 = std::string(64, 'b');
    grid.nx = nx;
    grid.ny = ny;
    grid.crs = "EPSG:4326";
    grid.longitude_convention = west == 0 ? cece::FireLongitudeConvention::ZeroTo360 : cece::FireLongitudeConvention::Minus180To180;
    constexpr double rad = std::numbers::pi / 180;
    for (std::size_t j = 0; j < ny; ++j) {
        const double south = -90 + 180.0 * j / ny;
        const double north = -90 + 180.0 * (j + 1) / ny;
        for (std::size_t i = 0; i < nx; ++i) {
            const double left = west + 360.0 * i / nx, right = west + 360.0 * (i + 1) / nx;
            grid.longitude_deg.push_back((left + right) / 2);
            grid.latitude_deg.push_back((south + north) / 2);
            grid.longitude_bounds_deg.push_back({left, right, right, left});
            grid.latitude_bounds_deg.push_back({south, south, north, north});
            grid.area_m2.push_back(6371000.0 * 6371000.0 * (right - left) * rad * (std::sin(north * rad) - std::sin(south * rad)));
        }
    }
    return grid;
}

axis::topology::UnstructuredMesh<Kokkos::HostSpace> AxisMesh(const cece::FireGridDefinition& definition) {
    axis::topology::StructuredGrid<Kokkos::HostSpace> grid(definition.nx, definition.ny, HostValues(definition.longitude_deg),
                                                           HostValues(definition.latitude_deg), axis::topology::CoordinateSystem::SphericalDeg);
    std::vector<double> lons, lats;
    for (std::size_t j = 0; j <= definition.ny; ++j) {
        for (std::size_t i = 0; i <= definition.nx; ++i) {
            const auto cell = std::min(j, definition.ny - 1) * definition.nx + std::min(i, definition.nx - 1);
            const auto corner = j == definition.ny ? (i == definition.nx ? 2 : 3) : (i == definition.nx ? 1 : 0);
            lons.push_back(definition.longitude_bounds_deg[cell][corner]);
            lats.push_back(definition.latitude_bounds_deg[cell][corner]);
        }
    }
    grid.set_corners(HostValues(lons), HostValues(lats));
    return grid.to_unstructured();
}

TEST(test_fire_conservation, AxisGeneratedSphericalWeightsConserveAcrossDatelineAndPolarCells) {
    const auto source = std::make_shared<const cece::FireGrid>(SphericalGrid(36, 18, 0));
    const auto target = std::make_shared<const cece::FireGrid>(SphericalGrid(18, 9, -180));
    axis::solver::RegridConfig config;
    config.method = axis::solver::InterpolationMethod::Conservative1stOrder;
    config.norm_type = Norm::DstArea;
    config.extrap_method = axis::solver::ExtrapolationAction::None;
    const auto matrix =
        axis::solver::WeightGenerator::generate<Kokkos::HostSpace>(AxisMesh(source->Definition()), AxisMesh(target->Definition()), config);
    const auto inventory = Inventory(source, std::vector<double>(source->CellCount() * 2, 1), std::vector<double>(source->CellCount(), 1));
    const auto result = cece::FireAllocateConservativeMass(inventory, target, matrix, Norm::DstArea, cece::FireUtcTime("2022-07-01T23:00:00Z"),
                                                           cece::FireUtcTime("2022-07-02T01:00:00Z"));
    for (const auto& budget : result.component_budgets) {
        EXPECT_NEAR(budget.output_kg, source->CellCount(), source->CellCount() * 1e-6);
        EXPECT_NEAR(budget.output_kg, budget.supported_input_kg, budget.supported_input_kg * 1e-6);
    }
    for (std::size_t i = 0; i < target->CellCount(); ++i) EXPECT_NEAR(result.coverage_fraction[i], 1, 1e-6);
}
}  // namespace
