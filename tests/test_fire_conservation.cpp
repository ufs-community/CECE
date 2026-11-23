// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <axis/solver/interpolation_matrix.hpp>
#include <axis/solver/regrid_config.hpp>
#include <axis/solver/weight_generator.hpp>
#include <axis/topology/structured_grid.hpp>
#include <cmath>
#include <filesystem>
#include <limits>
#include <numbers>
#include <numeric>
#include <string>
#include <utility>

#include "cece/cece_fire_input.hpp"

namespace {
using Matrix = axis::solver::InterpolationMatrix<Kokkos::HostSpace>;
using Norm = axis::solver::NormType;

axis::topology::UnstructuredMesh<Kokkos::HostSpace> AxisMesh(const cece::FireGridDefinition& definition);

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

cece::FireBulkInventory ProductInventory(const cece::FireProductEmissionsInterval& product) {
    const auto& data = product.Data();
    const auto& metadata = data.product_identity.common_identity.Metadata();
    const auto cells = metadata.grid_definition->CellCount();
    const auto index_of = [&](cece::FireBulkSpecies requested) {
        const auto found = std::find(data.species.begin(), data.species.end(), requested);
        if (found == data.species.end()) throw std::invalid_argument("product fixture lacks a requested fire species");
        return static_cast<std::size_t>(std::distance(data.species.begin(), found));
    };
    const auto co_offset = index_of(cece::FireBulkSpecies::CO) * cells;
    const auto pm25_offset = index_of(cece::FireBulkSpecies::PM25) * cells;
    std::vector<double> masses(2 * cells);
    for (std::size_t cell = 0; cell < cells; ++cell) {
        masses[cell] = product.MassKg()[co_offset + cell];
        masses[cells + cell] = product.MassKg()[pm25_offset + cell];
    }

    return cece::FireBulkInventory(
        {cece::FireInputIdentity(metadata),
         {cece::FireBulkSpecies::CO, cece::FireBulkSpecies::PM25},
         {"sample-product"},
         {{"sample-product", {{"sample-biome", "test-v1", std::string(64, 'c')}, {"sample-composition", "test-v1", std::string(64, 'd')}}}},
         std::move(masses),
         data.quality_state,
         data.coverage_fraction,
         {metadata.content_sha256},
         std::nullopt,
         std::nullopt,
         std::nullopt,
         data.optional_diagnostics.fre_mj,
         data.optional_diagnostics.dry_matter_kg});
}

Matrix ConservativeMatrix(const cece::FireGrid& source, const cece::FireGrid& target);

std::shared_ptr<const cece::FireGrid> CoarsenedGrid(const cece::FireGrid& source) {
    const auto& input = source.Definition();
    double west = input.longitude_bounds_deg.front()[0];
    double east = west;
    double south = input.latitude_bounds_deg.front()[0];
    double north = south;
    for (const auto& bounds : input.longitude_bounds_deg) {
        for (const auto value : bounds) {
            west = std::min(west, value);
            east = std::max(east, value);
        }
    }
    for (const auto& bounds : input.latitude_bounds_deg) {
        for (const auto value : bounds) {
            south = std::min(south, value);
            north = std::max(north, value);
        }
    }
    cece::FireGridDefinition definition{"coarsened-" + input.grid_id,
                                        std::string(64, 'c'),
                                        1,
                                        1,
                                        {(west + east) / 2},
                                        {(south + north) / 2},
                                        {{{west, east, east, west}}},
                                        {{{south, south, north, north}}},
                                        {1.0},
                                        input.crs,
                                        input.longitude_convention};
    const cece::FireGrid candidate(definition);
    const auto matrix = ConservativeMatrix(source, candidate);
    const auto operator_source_area = matrix.area_a();
    const auto operator_target_area = matrix.area_b();
    const double area_scale = input.area_m2.front() / operator_source_area[0];
    definition.area_m2[0] = operator_target_area[0] * area_scale;
    return std::make_shared<const cece::FireGrid>(std::move(definition));
}

Matrix ConservativeMatrix(const cece::FireGrid& source, const cece::FireGrid& target) {
    axis::solver::RegridConfig config;
    config.method = axis::solver::InterpolationMethod::Conservative1stOrder;
    config.norm_type = Norm::DstArea;
    config.extrap_method = axis::solver::ExtrapolationAction::None;
    return axis::solver::WeightGenerator::generate<Kokkos::HostSpace>(AxisMesh(source.Definition()), AxisMesh(target.Definition()), config);
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

cece::FireTemporalAllocationProfile TemporalProfile() {
    return {{"synthetic-diurnal", "1", std::string(64, 'f')},
            cece::FireTemporalProfileTimeBasis::Utc,
            7200,
            {{0, 3600, 0.75 / 3600}, {3600, 7200, 0.25 / 3600}}};
}

TEST(test_fire_conservation, NativeDiagnosticIdentityAndMassSurviveAllocation) {
    auto data = Inventory().Data();
    data.species = {cece::FireBulkSpecies::CO, cece::FireBulkSpecies::NOX};
    data.mass_kg = {10, 30, 2, 6, 20, 60, 4, 12};
    data.native_diagnostic_species = {cece::FireBulkSpecies::NOX};
    const cece::FireBulkInventory inventory(std::move(data));
    const auto result = cece::FireAllocateConservativeMass(inventory, GridWithAreas({50, 50}, "target"), MakeMatrix(), Norm::DstArea,
                                                           cece::FireUtcTime("2022-07-01T23:00:00Z"), cece::FireUtcTime("2022-07-02T01:00:00Z"));
    EXPECT_EQ(result.native_diagnostic_species, std::vector<cece::FireBulkSpecies>{cece::FireBulkSpecies::NOX});
    ASSERT_EQ(result.species_budgets.size(), 2);
    EXPECT_NEAR(result.species_budgets[1].input_kg, 24.0, 1e-12);
    EXPECT_NEAR(result.species_budgets[1].output_kg, 24.0, 1e-12);
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

TEST(test_fire_conservation, ProfileAllocationConservesMassAndRetainsComponentProvenance) {
    const auto inventory = Inventory();
    const auto target = GridWithAreas({50, 50}, "target");
    const auto matrix = MakeMatrix();
    const auto profile = TemporalProfile();
    const auto first_half_hour =
        cece::FireAllocateConservativeMass(inventory, target, matrix, Norm::DstArea, cece::FireUtcTime("2022-07-01T23:00:00Z"),
                                           cece::FireUtcTime("2022-07-01T23:30:00Z"), cece::FireTimeAllocation::Profile, profile);
    EXPECT_EQ(first_half_hour.time_allocation, cece::FireTimeAllocation::Profile);
    ASSERT_TRUE(first_half_hour.time_profile);
    EXPECT_EQ(first_half_hour.time_profile->id, profile.identity.id);
    EXPECT_EQ(first_half_hour.component_ids, inventory.Data().component_ids);
    EXPECT_EQ(first_half_hour.component_profiles.at("b").composition.id, "synthetic-composition-b");
    ASSERT_EQ(first_half_hour.component_budgets.size(), 2);
    EXPECT_DOUBLE_EQ(first_half_hour.component_budgets[0].supported_input_kg, 15);
    EXPECT_DOUBLE_EQ(first_half_hour.component_budgets[1].supported_input_kg, 30);
    ASSERT_EQ(first_half_hour.species_budgets.size(), 1);
    EXPECT_DOUBLE_EQ(first_half_hour.species_budgets[0].input_kg, 120);
    EXPECT_DOUBLE_EQ(first_half_hour.species_budgets[0].supported_input_kg, 45);
    EXPECT_DOUBLE_EQ(first_half_hour.species_budgets[0].output_kg, 45);

    const auto second_half_hour =
        cece::FireAllocateConservativeMass(inventory, target, matrix, Norm::DstArea, cece::FireUtcTime("2022-07-01T23:30:00Z"),
                                           cece::FireUtcTime("2022-07-02T00:00:00Z"), cece::FireTimeAllocation::Profile, profile);
    const auto second_hour =
        cece::FireAllocateConservativeMass(inventory, target, matrix, Norm::DstArea, cece::FireUtcTime("2022-07-02T00:00:00Z"),
                                           cece::FireUtcTime("2022-07-02T01:00:00Z"), cece::FireTimeAllocation::Profile, profile);
    const auto whole_interval =
        cece::FireAllocateConservativeMass(inventory, target, matrix, Norm::DstArea, cece::FireUtcTime("2022-07-01T23:00:00Z"),
                                           cece::FireUtcTime("2022-07-02T01:00:00Z"), cece::FireTimeAllocation::Profile, profile);
    for (std::size_t i = 0; i < whole_interval.mass_kg.size(); ++i) {
        EXPECT_NEAR(first_half_hour.mass_kg[i] + second_half_hour.mass_kg[i] + second_hour.mass_kg[i], whole_interval.mass_kg[i], 1e-12);
    }
    EXPECT_DOUBLE_EQ(whole_interval.species_budgets[0].supported_input_kg, 120);
    EXPECT_DOUBLE_EQ(whole_interval.species_budgets[0].output_kg, 120);

    const auto source_resolved =
        cece::FireAllocateConservativeMass(inventory, target, matrix, Norm::DstArea, cece::FireUtcTime("2022-07-01T23:00:00Z"),
                                           cece::FireUtcTime("2022-07-02T00:00:00Z"), cece::FireTimeAllocation::SourceResolved);
    EXPECT_EQ(source_resolved.time_allocation, cece::FireTimeAllocation::SourceResolved);
    EXPECT_DOUBLE_EQ(source_resolved.species_budgets[0].output_kg, 60);
}

TEST(test_fire_conservation, ProfileAllocationRejectsInvalidOrRepeatedWeights) {
    const auto inventory = Inventory();
    const auto target = GridWithAreas({50, 50}, "target");
    const auto matrix = MakeMatrix();
    const auto start = cece::FireUtcTime("2022-07-01T23:00:00Z");
    const auto end = cece::FireUtcTime("2022-07-02T01:00:00Z");
    EXPECT_THROW(cece::FireAllocateConservativeMass(inventory, target, matrix, Norm::DstArea, start, end, cece::FireTimeAllocation::Profile),
                 std::invalid_argument);

    auto invalid_integral = TemporalProfile();
    invalid_integral.segments[0].weight_per_second = 0.74 / 3600;
    EXPECT_THROW(
        cece::FireAllocateConservativeMass(inventory, target, matrix, Norm::DstArea, start, end, cece::FireTimeAllocation::Profile, invalid_integral),
        std::invalid_argument);
    auto invalid_coverage = TemporalProfile();
    invalid_coverage.segments[1].start_offset_seconds = 3601;
    EXPECT_THROW(
        cece::FireAllocateConservativeMass(inventory, target, matrix, Norm::DstArea, start, end, cece::FireTimeAllocation::Profile, invalid_coverage),
        std::invalid_argument);
    auto incompatible_cadence = TemporalProfile();
    incompatible_cadence.source_cadence_seconds = 3600;
    EXPECT_THROW(cece::FireAllocateConservativeMass(inventory, target, matrix, Norm::DstArea, start, end, cece::FireTimeAllocation::Profile,
                                                    incompatible_cadence),
                 std::invalid_argument);
    auto local_solar = TemporalProfile();
    local_solar.time_basis = cece::FireTemporalProfileTimeBasis::LocalSolar;
    EXPECT_THROW(
        cece::FireAllocateConservativeMass(inventory, target, matrix, Norm::DstArea, start, end, cece::FireTimeAllocation::Profile, local_solar),
        std::invalid_argument);
    EXPECT_THROW(cece::FireAllocateConservativeMass(inventory, target, matrix, Norm::DstArea, start, end, cece::FireTimeAllocation::Uniform,
                                                    TemporalProfile()),
                 std::invalid_argument);

    auto weighted_data = inventory.Data();
    auto metadata = weighted_data.identity.Metadata();
    weighted_data.identity = cece::FireInputIdentity(std::move(metadata), TemporalProfile().identity);
    const cece::FireBulkInventory already_weighted(std::move(weighted_data));
    EXPECT_THROW(cece::FireAllocateConservativeMass(already_weighted, target, matrix, Norm::DstArea, start, end, cece::FireTimeAllocation::Profile,
                                                    TemporalProfile()),
                 std::invalid_argument);
}

TEST(test_fire_conservation, LocalSolarProfileShiftsAllocationByCellLongitude) {
    const auto source = std::make_shared<const cece::FireGrid>(cece::FireGridDefinition{"local-solar-grid",
                                                                                        std::string(64, 'b'),
                                                                                        2,
                                                                                        1,
                                                                                        {0, 180},
                                                                                        {0, 0},
                                                                                        {{{-1, 0, 0, -1}, {179, 180, 180, 179}}},
                                                                                        {{{-1, -1, 1, 1}, {-1, -1, 1, 1}}},
                                                                                        {25, 75},
                                                                                        "EPSG:4326",
                                                                                        cece::FireLongitudeConvention::Minus180To180});
    auto inventory_data = Inventory(source, {100, 100, 100, 100}).Data();
    auto metadata = inventory_data.identity.Metadata();
    metadata.interval_start = cece::FireUtcTime("2022-07-01T00:00:00Z");
    metadata.interval_end = cece::FireUtcTime("2022-07-02T00:00:00Z");
    metadata.acquired_at = metadata.interval_start;
    metadata.available_at = metadata.interval_start;
    metadata.availability_cutoff = cece::FireUtcTime("2022-07-02T01:00:00Z");
    inventory_data.identity = cece::FireInputIdentity(std::move(metadata));
    const cece::FireBulkInventory inventory(std::move(inventory_data));
    const cece::FireTemporalAllocationProfile profile{{"synthetic-local-solar", "1", std::string(64, 'f')},
                                                      cece::FireTemporalProfileTimeBasis::LocalSolar,
                                                      86400,
                                                      {{0, 43200, 1.0 / 43200}, {43200, 86400, 0}}};
    const auto result = cece::FireAllocateConservativeMass(inventory, source, MakeMatrix({1, 1}, {0, 1}, {0, 1}, {1, 1}, {1, 1}, {25, 75}, {25, 75}),
                                                           Norm::DstArea, cece::FireUtcTime("2022-07-01T00:00:00Z"),
                                                           cece::FireUtcTime("2022-07-01T06:00:00Z"), cece::FireTimeAllocation::Profile, profile);
    ASSERT_EQ(result.mass_kg.size(), 4);
    EXPECT_DOUBLE_EQ(result.mass_kg[0], 50);
    EXPECT_DOUBLE_EQ(result.mass_kg[1], 0);
    EXPECT_DOUBLE_EQ(result.mass_kg[2], 50);
    EXPECT_DOUBLE_EQ(result.mass_kg[3], 0);
    ASSERT_EQ(result.species_budgets.size(), 1);
    EXPECT_DOUBLE_EQ(result.species_budgets[0].input_kg, 400);
    EXPECT_DOUBLE_EQ(result.species_budgets[0].supported_input_kg, 100);
    EXPECT_DOUBLE_EQ(result.species_budgets[0].output_kg, 100);
}

TEST(test_fire_conservation, RectilinearTargetGridUnwrapsLongitudesAcrossDateline) {
    const auto grid = cece::MakeRectilinearFireGrid(4, 2, {0, 90, -180, -90}, {-45, 45});
    const auto& definition = grid->Definition();
    EXPECT_EQ(definition.longitude_convention, cece::FireLongitudeConvention::ZeroTo360);
    EXPECT_EQ(definition.longitude_deg, (std::vector<double>{0, 90, 180, 270, 0, 90, 180, 270}));
    EXPECT_EQ(definition.area_m2.size(), 8);
    EXPECT_TRUE(std::all_of(definition.area_m2.begin(), definition.area_m2.end(), [](double area) { return area > 0; }));
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

TEST(test_fire_conservation, ActualRaveDayConservesAcrossTwoSubstepsAndChangingTargetGeometry) {
    constexpr std::array<cece::FireBulkSpecies, 2> species{cece::FireBulkSpecies::CO, cece::FireBulkSpecies::PM25};
    constexpr std::array<double, 2> expected_total_kg{71184.0390625, 4368.746002197266};
    std::array<double, 2> actual_total_kg{};
    const auto fixture_root = std::filesystem::path(__FILE__).parent_path() / "data/fire/product_samples/rave-24h/";

    for (std::size_t hour = 0; hour < 24; ++hour) {
        const auto filename = fixture_root / ("hour-" + (hour < 10 ? std::string("0") : std::string()) + std::to_string(hour) + ".nc");
        cece::FireProductDecodeOptions options;
        options.availability_cutoff = cece::FireUtcTime("2026-07-03T00:00:00Z");
        options.cutoff_mode = cece::FireCutoffMode::Fixed;
        const auto product = cece::DecodeRaveProductFile(filename.string(), options);
        const auto inventory = ProductInventory(product);
        const auto source_grid = inventory.Data().identity.Metadata().grid_definition;
        const auto coarse_grid = CoarsenedGrid(*source_grid);
        const auto interval_start = inventory.Data().identity.Metadata().interval_start;
        const auto interval_end = inventory.Data().identity.Metadata().interval_end;
        const auto middle = cece::FireUtcTime::FromEpochSeconds(interval_start.EpochSeconds() + 1800);

        for (std::size_t substep = 0; substep < 2; ++substep) {
            const auto target = substep == 0 ? source_grid : coarse_grid;
            const auto matrix = ConservativeMatrix(*source_grid, *target);
            const auto start = substep == 0 ? interval_start : middle;
            const auto end = substep == 0 ? middle : interval_end;
            const auto allocated = cece::FireAllocateConservativeMass(inventory, target, matrix, Norm::DstArea, start, end);
            ASSERT_EQ(allocated.species_budgets.size(), species.size());
            for (std::size_t index = 0; index < species.size(); ++index) {
                EXPECT_EQ(allocated.species_budgets[index].species, species[index]);
                EXPECT_NEAR(allocated.species_budgets[index].output_kg, allocated.species_budgets[index].input_kg * 0.5,
                            std::max(1e-12, allocated.species_budgets[index].input_kg * 1e-6));
                actual_total_kg[index] += allocated.species_budgets[index].output_kg;
            }
        }
    }

    for (std::size_t index = 0; index < species.size(); ++index) {
        EXPECT_NEAR(actual_total_kg[index], expected_total_kg[index], std::max(1e-12, expected_total_kg[index] * 1e-6));
    }
}

TEST(test_fire_conservation, RaveRectilinearTargetUsesGreatCircleCellAreas) {
    cece::FireProductDecodeOptions options;
    options.availability_cutoff = cece::FireUtcTime("2026-07-03T00:00:00Z");
    options.cutoff_mode = cece::FireCutoffMode::Fixed;
    const auto filename = std::filesystem::path(__FILE__).parent_path() / "data/fire/product_samples/RAVE-v2r0-crop.nc";
    const auto inventory = ProductInventory(cece::DecodeRaveProductFile(filename.string(), options));
    const auto source_grid = inventory.Data().identity.Metadata().grid_definition;
    const auto& source = source_grid->Definition();
    double west = std::numeric_limits<double>::max();
    double east = -std::numeric_limits<double>::max();
    double south = std::numeric_limits<double>::max();
    double north = -std::numeric_limits<double>::max();
    for (std::size_t cell = 0; cell < source_grid->CellCount(); ++cell) {
        for (const auto lon : source.longitude_bounds_deg[cell]) {
            west = std::min(west, lon);
            east = std::max(east, lon);
        }
        for (const auto lat : source.latitude_bounds_deg[cell]) {
            south = std::min(south, lat);
            north = std::max(north, lat);
        }
    }
    const auto target =
        cece::MakeRectilinearFireGrid(2, 2, {west + (east - west) * 0.25, west + (east - west) * 0.75},
                                      {south + (north - south) * 0.25, south + (north - south) * 0.75}, cece::FireGridAreaModel::GreatCircleEdges);
    const auto matrix = ConservativeMatrix(*source_grid, *target);
    const auto interval = inventory.Data().identity.Metadata();
    const auto allocated =
        cece::FireAllocateConservativeMass(inventory, target, matrix, Norm::DstArea, interval.interval_start, interval.interval_end);
    ASSERT_EQ(allocated.species_budgets.size(), 2);
    for (const auto& budget : allocated.species_budgets) {
        EXPECT_NEAR(budget.output_kg, budget.supported_input_kg, std::max(1e-12, budget.supported_input_kg * 1e-6));
    }
}

TEST(test_fire_conservation, RaveScaledAndUnscaledSelectionsConserveTheirSelectedMasses) {
    const auto filename = std::filesystem::path(__FILE__).parent_path() / "data/fire/product_samples/RAVE-v2r0-crop.nc";
    cece::FireProductDecodeOptions scaled_options;
    scaled_options.availability_cutoff = cece::FireUtcTime("2026-07-03T00:00:00Z");
    scaled_options.cutoff_mode = cece::FireCutoffMode::Fixed;
    scaled_options.scaling = cece::FireProductScalingKind::Applied;
    const auto scaled = ProductInventory(cece::DecodeRaveProductFile(filename.string(), scaled_options));

    auto unscaled_options = scaled_options;
    unscaled_options.scaling = cece::FireProductScalingKind::Unscaled;
    const auto unscaled = ProductInventory(cece::DecodeRaveProductFile(filename.string(), unscaled_options));
    const auto grid = scaled.Data().identity.Metadata().grid_definition;
    const auto matrix = ConservativeMatrix(*grid, *grid);
    const auto start = scaled.Data().identity.Metadata().interval_start;
    const auto middle = cece::FireUtcTime::FromEpochSeconds(start.EpochSeconds() + 1800);
    const auto end = scaled.Data().identity.Metadata().interval_end;

    const auto split_total = [&](const cece::FireBulkInventory& inventory) {
        double mass = 0;
        for (const auto& bounds : {std::pair{start, middle}, std::pair{middle, end}}) {
            const auto allocated = cece::FireAllocateConservativeMass(inventory, grid, matrix, Norm::DstArea, bounds.first, bounds.second);
            mass += allocated.species_budgets.at(1).output_kg;
        }
        return mass;
    };

    const double scaled_pm25 = split_total(scaled);
    const double unscaled_pm25 = split_total(unscaled);
    EXPECT_NE(scaled_pm25, unscaled_pm25);
    EXPECT_NEAR(scaled_pm25, scaled.SpeciesBudgetKg(cece::FireBulkSpecies::PM25), scaled_pm25 * 1e-6);
    EXPECT_NEAR(unscaled_pm25, unscaled.SpeciesBudgetKg(cece::FireBulkSpecies::PM25), unscaled_pm25 * 1e-6);
}

TEST(test_fire_conservation, ActualGbbepxDayConservesAcrossTwoSubstepsAndRejectsMissingSpecies) {
    cece::FireProductDecodeOptions options;
    options.availability_cutoff = cece::FireUtcTime("2023-02-26T00:00:00Z");
    options.cutoff_mode = cece::FireCutoffMode::Fixed;
    const auto filename = std::filesystem::path(__FILE__).parent_path() / "data/fire/product_samples/GBBEPx-v4r0-crop.nc";
    const auto product = cece::DecodeGbbepxProductFile(filename.string(), options);
    EXPECT_THROW(product.RequireSpecies({cece::FireBulkSpecies::CO, cece::FireBulkSpecies::CH4, cece::FireBulkSpecies::VOC}), std::invalid_argument);

    const auto inventory = ProductInventory(product);
    const auto source_grid = inventory.Data().identity.Metadata().grid_definition;
    const auto coarse_grid = CoarsenedGrid(*source_grid);
    const auto interval_start = inventory.Data().identity.Metadata().interval_start;
    const auto interval_end = inventory.Data().identity.Metadata().interval_end;
    const auto middle = cece::FireUtcTime::FromEpochSeconds(interval_start.EpochSeconds() + 43200);
    std::array<double, 2> allocated_total_kg{};
    const std::array<cece::FireBulkSpecies, 2> species{cece::FireBulkSpecies::CO, cece::FireBulkSpecies::PM25};

    for (std::size_t substep = 0; substep < 2; ++substep) {
        const auto target = substep == 0 ? source_grid : coarse_grid;
        const auto matrix = ConservativeMatrix(*source_grid, *target);
        const auto start = substep == 0 ? interval_start : middle;
        const auto end = substep == 0 ? middle : interval_end;
        const auto allocated = cece::FireAllocateConservativeMass(inventory, target, matrix, Norm::DstArea, start, end);
        ASSERT_EQ(allocated.species_budgets.size(), species.size());
        for (std::size_t index = 0; index < species.size(); ++index) {
            EXPECT_EQ(allocated.species_budgets[index].species, species[index]);
            allocated_total_kg[index] += allocated.species_budgets[index].output_kg;
        }
    }

    for (std::size_t index = 0; index < species.size(); ++index) {
        EXPECT_NEAR(allocated_total_kg[index], inventory.SpeciesBudgetKg(species[index]),
                    std::max(1e-12, inventory.SpeciesBudgetKg(species[index]) * 1e-6));
    }
}
}  // namespace
