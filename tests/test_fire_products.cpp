// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>
#include <Kokkos_Core.hpp>
#include <netcdf.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "cece/cece_fire_runtime.hpp"
#include "cece/physics/cece_fire.hpp"

namespace {

std::shared_ptr<const cece::FireGrid> ProductGrid(double area_m2 = 100.0) {
    return std::make_shared<const cece::FireGrid>(cece::FireGridDefinition{
        "synthetic-product-grid", std::string(64, 'b'), 1, 1, {0}, {0}, {{{-1, 1, 1, -1}}}, {{{-1, -1, 1, 1}}}, {area_m2}, "EPSG:4326",
        cece::FireLongitudeConvention::Minus180To180});
}

cece::FireProductEmissionsInterval ProductInterval(const std::string& start, const std::string& end, cece::FireProductQuantityKind kind,
                                                   std::string units, std::vector<double> values,
                                                   cece::FireCutoffMode cutoff_mode = cece::FireCutoffMode::Fixed) {
    const cece::FireUtcTime interval_start(start), interval_end(end);
    auto grid = ProductGrid();
    cece::FireIdentityMetadata metadata{1,
                                        "GBBEPx",
                                        "product-v1",
                                        "decoder-v1",
                                        "delivery-a",
                                        std::string(64, 'a'),
                                        interval_start,
                                        interval_end,
                                        interval_start,
                                        interval_start,
                                        cece::FireUtcTime("2022-07-03T00:00:00Z"),
                                        cutoff_mode,
                                        std::move(grid)};

    cece::FireProductEmissionsIntervalData data(
        {"GBBEPx", "product-v1", "unscaled", cece::FireInputIdentity(std::move(metadata))});
    data.species = {cece::FireBulkSpecies::CO};
    for (const auto species : {cece::FireBulkSpecies::CO2, cece::FireBulkSpecies::CO, cece::FireBulkSpecies::CH4, cece::FireBulkSpecies::PM25,
                               cece::FireBulkSpecies::TPM, cece::FireBulkSpecies::SO2, cece::FireBulkSpecies::OC, cece::FireBulkSpecies::BC,
                               cece::FireBulkSpecies::NOX, cece::FireBulkSpecies::NH3, cece::FireBulkSpecies::VOC}) {
        data.species_availability.emplace(species, species == cece::FireBulkSpecies::CO ? cece::FireProductSpeciesAvailability::Available
                                                                                         : cece::FireProductSpeciesAvailability::Unavailable);
    }
    data.quantity_semantics.emplace(cece::FireBulkSpecies::CO, cece::FireProductQuantitySemantics{kind, std::move(units)});
    data.source_values = std::move(values);
    data.quality_state = {cece::FireQualityState::Observed};
    data.coverage_fraction = {1};
    return cece::FireProductEmissionsInterval(std::move(data));
}

std::string ProductFixturePath(std::string_view name) {
    return std::string(CECE_SOURCE_DIR) + "/tests/data/fire/product_samples/" + std::string(name);
}

cece::FireProductDecodeOptions ProductDecodeOptions(std::string_view cutoff) {
    cece::FireProductDecodeOptions options;
    options.availability_cutoff = cece::FireUtcTime(cutoff);
    options.cutoff_mode = cece::FireCutoffMode::Fixed;
    return options;
}

std::size_t SpeciesIndex(const cece::FireProductEmissionsInterval& product, cece::FireBulkSpecies species) {
    const auto& available = product.Data().species;
    const auto found = std::find(available.begin(), available.end(), species);
    if (found == available.end()) throw std::invalid_argument("test requested a species absent from decoded fixture");
    return static_cast<std::size_t>(std::distance(available.begin(), found));
}

TEST(test_fire_products, DailyMassAndHourlyRateNormalizeToCellIntervalKg) {
    const auto daily = ProductInterval("2022-07-01T00:00:00Z", "2022-07-02T00:00:00Z", cece::FireProductQuantityKind::IntervalMass,
                                       "kg/cell/interval", {86400});
    const auto hourly = ProductInterval("2022-07-01T00:00:00Z", "2022-07-01T01:00:00Z", cece::FireProductQuantityKind::MeanRate, "kg s-1", {2});
    ASSERT_EQ(daily.MassKg().size(), 1);
    EXPECT_DOUBLE_EQ(daily.MassKg()[0], 86400);
    EXPECT_DOUBLE_EQ(hourly.MassKg()[0], 7200);
    EXPECT_EQ(daily.Data().product_identity.common_identity.DurationSeconds(), 86400);
    EXPECT_EQ(hourly.Data().product_identity.common_identity.DurationSeconds(), 3600);
}

TEST(test_fire_products, FluxUsesSuppliedSourceCellAreaAndIntervalDuration) {
    const auto flux = ProductInterval("2022-07-01T00:00:00Z", "2022-07-01T01:00:00Z", cece::FireProductQuantityKind::Flux, "kg m-2 s-1", {0.1});
    EXPECT_DOUBLE_EQ(flux.MassKg()[0], 36000);
}

TEST(test_fire_products, AmbiguousUnitsAndInconsistentIdentityAreRejected) {
    auto invalid_units = ProductInterval("2022-07-01T00:00:00Z", "2022-07-01T01:00:00Z", cece::FireProductQuantityKind::IntervalMass,
                                         "kg/cell/interval", {1})
                             .Data();
    invalid_units.quantity_semantics.at(cece::FireBulkSpecies::CO).units = "kg";
    EXPECT_THROW(cece::FireProductEmissionsInterval(std::move(invalid_units)), std::invalid_argument);

    auto invalid_identity = ProductInterval("2022-07-01T00:00:00Z", "2022-07-01T01:00:00Z", cece::FireProductQuantityKind::IntervalMass,
                                            "kg/cell/interval", {1})
                                .Data();
    invalid_identity.product_identity.version = "different-version";
    EXPECT_THROW(cece::FireProductEmissionsInterval(std::move(invalid_identity)), std::invalid_argument);
}

TEST(test_fire_products, MissingSpeciesAreDistinctFromValidZero) {
    const auto product = ProductInterval("2022-07-01T00:00:00Z", "2022-07-01T01:00:00Z", cece::FireProductQuantityKind::IntervalMass,
                                         "kg/cell/interval", {0});
    EXPECT_NO_THROW(product.RequireSpecies({cece::FireBulkSpecies::CO}));
    EXPECT_THROW(product.RequireSpecies({cece::FireBulkSpecies::NOX}), std::invalid_argument);
    EXPECT_DOUBLE_EQ(product.MassKg()[0], 0);
    EXPECT_EQ(product.Data().species_availability.at(cece::FireBulkSpecies::CO), cece::FireProductSpeciesAvailability::Available);
    EXPECT_EQ(product.Data().species_availability.at(cece::FireBulkSpecies::NOX), cece::FireProductSpeciesAvailability::Unavailable);
}

struct TemporaryProductFile {
    std::filesystem::path path;
    int file = -1;
    ~TemporaryProductFile() {
        if (file >= 0) nc_close(file);
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};

struct TemporaryProductDirectory {
    std::filesystem::path path;
    TemporaryProductDirectory() {
        path = std::filesystem::temp_directory_path() /
               ("cece-prepared-fire-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(path);
    }
    ~TemporaryProductDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

TEST(test_fire_products, WritesNormalizedRaveGridMassAndIntervalMetadata) {
    const auto product = cece::DecodeRaveProductFile(ProductFixturePath("RAVE-v2r0-crop.nc"), ProductDecodeOptions("2026-07-03T00:00:00Z"));
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto path = std::filesystem::temp_directory_path() / ("cece-normalized-fire-" + suffix + ".nc");
    TemporaryProductFile temporary{path};
    cece::FireNormalizedProductWriteOptions options{"rave-reference", "source_resolved", std::nullopt};
    cece::WriteNormalizedFireProductFile(product, options, path.string());

    ASSERT_EQ(nc_open(path.c_str(), NC_NOWRITE, &temporary.file), NC_NOERR);
    int schema_version = 0;
    ASSERT_EQ(nc_get_att_int(temporary.file, NC_GLOBAL, "fire_normalized_schema_version", &schema_version), NC_NOERR);
    EXPECT_EQ(schema_version, 1);
    const auto& metadata = product.Data().product_identity.common_identity.Metadata();
    int source_schema_version = 0;
    ASSERT_EQ(nc_get_att_int(temporary.file, NC_GLOBAL, "source_identity_schema_version", &source_schema_version), NC_NOERR);
    EXPECT_EQ(source_schema_version, metadata.schema_version);
    for (const auto& [name, expected] : std::array<std::pair<const char*, std::string>, 3>{
             std::pair{"source_id", metadata.source_id},
             std::pair{"source_manifest_id", options.source_id},
             std::pair{"delivery_id", metadata.delivery_id}}) {
        std::size_t attribute_length = 0;
        ASSERT_EQ(nc_inq_attlen(temporary.file, NC_GLOBAL, name, &attribute_length), NC_NOERR);
        std::string value(attribute_length, '\0');
        ASSERT_EQ(nc_get_att_text(temporary.file, NC_GLOBAL, name, value.data()), NC_NOERR);
        EXPECT_EQ(value, expected);
    }
    std::size_t available_length = 0;
    ASSERT_EQ(nc_inq_attlen(temporary.file, NC_GLOBAL, "available_species", &available_length), NC_NOERR);
    std::string available_species(available_length, '\0');
    ASSERT_EQ(nc_get_att_text(temporary.file, NC_GLOBAL, "available_species", available_species.data()), NC_NOERR);
    EXPECT_NE(available_species.find("CO2"), std::string::npos);
    int mass_variable = -1;
    ASSERT_EQ(nc_inq_varid(temporary.file, "fire_mass_CO2", &mass_variable), NC_NOERR);
    std::array<double, 4> mass{};
    const std::array<std::size_t, 3> start{0, 0, 0};
    const std::array<std::size_t, 3> count{1, 2, 2};
    EXPECT_EQ(nc_get_vara_double(temporary.file, mass_variable, start.data(), count.data(), mass.data()), NC_NOERR);
    EXPECT_DOUBLE_EQ(mass[0], product.MassKg()[SpeciesIndex(product, cece::FireBulkSpecies::CO2) * 4]);
    std::size_t units_length = 0;
    ASSERT_EQ(nc_inq_attlen(temporary.file, mass_variable, "units", &units_length), NC_NOERR);
    std::string units(units_length, '\0');
    ASSERT_EQ(nc_get_att_text(temporary.file, mass_variable, "units", units.data()), NC_NOERR);
    EXPECT_EQ(units, "kg cell-1 interval-1");

    int time_bounds_variable = -1;
    ASSERT_EQ(nc_inq_varid(temporary.file, "time_bounds", &time_bounds_variable), NC_NOERR);
    std::array<double, 2> time_bounds{};
    EXPECT_EQ(nc_get_var_double(temporary.file, time_bounds_variable, time_bounds.data()), NC_NOERR);
    EXPECT_DOUBLE_EQ(time_bounds[0], static_cast<double>(metadata.interval_start.EpochSeconds()));
    EXPECT_DOUBLE_EQ(time_bounds[1], static_cast<double>(metadata.interval_end.EpochSeconds()));
}

TEST(test_fire_products, PreparedRuntimeLoadsProfiledProductAndPreservesComposition) {
    Kokkos::ScopeGuard kokkos;
    TemporaryProductDirectory temporary;
    const auto profile_path = temporary.path / "temporal_profile.yaml";
    {
        std::ofstream profile(profile_path);
        profile << "schema_version: 1\n"
                   "profile_id: runtime-profile\n"
                   "version: test-v1\n"
                   "time_basis: UTC\n"
                   "supported_source_cadence_seconds: 86400\n"
                   "weights:\n"
                   "  - {offset_seconds: 0, duration_seconds: 21600, weight_per_second: 0.00002314814814814815}\n"
                   "  - {offset_seconds: 21600, duration_seconds: 64800, weight_per_second: 0.00000771604938271605}\n";
        ASSERT_TRUE(profile.good());
    }
    const cece::FireProfileIdentity temporal_identity{"runtime-profile", "test-v1", cece::FireFileSha256(profile_path.string())};
    const auto product = ProductInterval("2022-07-01T00:00:00Z", "2022-07-02T00:00:00Z",
                                         cece::FireProductQuantityKind::IntervalMass, "kg/cell/interval", {5},
                                         cece::FireCutoffMode::HostSupplied);
    const auto& source_metadata = product.Data().product_identity.common_identity.Metadata();
    const auto normalized_path = temporary.path / "interval.nc";
    cece::WriteNormalizedFireProductFile(product, {"synthetic-source", "profile", temporal_identity}, normalized_path.string());
    const auto normalized_hash = cece::FireFileSha256(normalized_path.string());
    const cece::FireProfileIdentity biome{"synthetic-biome", "test-v1", std::string(64, 'c')};
    const cece::FireProfileIdentity composition{"synthetic-composition", "test-v1", std::string(64, 'd')};
    const auto cutoff_schedule_path = temporary.path / "cutoff_schedule.yaml";
    {
        std::ofstream schedule(cutoff_schedule_path);
        schedule << "schema_version: 1\n"
                    "intervals:\n"
                    "  - {interval_start: 2022-07-01T00:00:00Z, interval_end: 2022-07-01T06:00:00Z, "
                    "availability_cutoff: 2022-07-03T00:00:00Z}\n";
        ASSERT_TRUE(schedule.good());
    }
    const auto cutoff_schedule_hash = cece::FireFileSha256(cutoff_schedule_path.string());
    const auto manifest_path = temporary.path / "manifest.yaml";
    {
        std::ofstream manifest(manifest_path);
        manifest << "schema_version: 1\n"
                    "mode: product\n"
                    "product_adapter: rave-v2r0\n"
                    "product_time_allocation: profile\n"
                    "product_time_profile:\n"
                    "  profile_id: " << temporal_identity.id << "\n"
                 << "  version: " << temporal_identity.version << "\n"
                 << "  content_sha256: " << temporal_identity.content_sha256 << "\n"
                    "chemistry_state: bulk\n"
                    "cutoff_mode: host_supplied\n"
                    "cutoff_schedule_sha256: " << cutoff_schedule_hash << "\n"
                    "sources:\n"
                    "  - source_id: synthetic-source\n"
                    "    acquired_at: 2022-07-01T00:00:00Z\n"
                    "    available_at: 2022-07-01T00:00:00Z\n"
                    "    content_sha256: " << source_metadata.content_sha256 << "\n"
                    "intervals:\n"
                    "  - source_id: synthetic-source\n"
                    "    product_id: GBBEPx\n"
                    "    product_version: product-v1\n"
                    "    algorithm_version: decoder-v1\n"
                    "    variant: unscaled\n"
                    "    interval_start: 2022-07-01T00:00:00Z\n"
                    "    interval_end: 2022-07-02T00:00:00Z\n"
                    "    availability_cutoff: 2022-07-03T00:00:00Z\n"
                    "    cutoff_mode: host_supplied\n"
                    "    grid_sha256: " << source_metadata.grid_definition->Definition().grid_sha256 << "\n"
                    "    source_content_sha256: " << source_metadata.content_sha256 << "\n"
                    "    component_id: component-a\n"
                    "    component_profile:\n"
                    "      biome: {id: " << biome.id << ", version: " << biome.version << ", content_sha256: " << biome.content_sha256 << "}\n"
                    "      composition: {id: " << composition.id << ", version: " << composition.version << ", content_sha256: "
                 << composition.content_sha256 << "}\n"
                    "    path: interval.nc\n"
                    "    content_sha256: " << normalized_hash << "\n";
        ASSERT_TRUE(manifest.good());
    }

    cece::FireConfig config{cece::FireInputMode::Product, cece::FireInputProvider::Prepared, cece::FireCutoffMode::HostSupplied};
    config.input_manifest = manifest_path.string();
    config.cutoff_schedule = cutoff_schedule_path.string();
    config.product_adapter = "rave-v2r0";
    config.product_time_allocation = cece::FireTimeAllocation::Profile;
    config.product_time_profile = profile_path.string();
    config.requested_species = {cece::FireBulkSpecies::CO};
    auto adapter = cece::CreateFireRuntimeAdapter(cece::FireInputProvider::Prepared);
    ASSERT_NE(adapter, nullptr);
    adapter->SetTargetGrid(source_metadata.grid_definition);
    adapter->Initialize(config, 1, 1, 1);
    const auto inventory_view = adapter->PrepareInterval({cece::FireUtcTime("2022-07-01T00:00:00Z"),
                                                          cece::FireUtcTime("2022-07-01T06:00:00Z"),
                                                          std::nullopt, 21600});
    ASSERT_TRUE(std::holds_alternative<std::reference_wrapper<const cece::FireBulkInventory>>(inventory_view));
    const auto& inventory = std::get<std::reference_wrapper<const cece::FireBulkInventory>>(inventory_view).get();
    ASSERT_EQ(inventory.Data().mass_kg.size(), 1);
    EXPECT_NEAR(inventory.Data().mass_kg.front(), 2.5, 1e-12);
    EXPECT_EQ(inventory.Data().component_ids, std::vector<std::string>{"component-a"});
    EXPECT_EQ(inventory.Data().component_profiles.at("component-a").biome, biome);
    EXPECT_EQ(inventory.Data().component_profiles.at("component-a").composition, composition);
    ASSERT_TRUE(inventory.Data().identity.AppliedTemporalProfile());
    EXPECT_EQ(*inventory.Data().identity.AppliedTemporalProfile(), temporal_identity);
    adapter->CommitInterval();
    adapter->Finalize();
}

TEST(test_fire_products, DecodesRaveV2r0SampleCropAndPreservesItsIntervalMasses) {
    const auto product = cece::DecodeRaveProductFile(ProductFixturePath("RAVE-v2r0-crop.nc"), ProductDecodeOptions("2026-07-03T00:00:00Z"));
    const auto& data = product.Data();
    const auto& metadata = data.product_identity.common_identity.Metadata();

    EXPECT_EQ(data.product_identity.product_id, "RAVE-HrlyEmiss-13km");
    EXPECT_EQ(data.product_identity.version, "v2r0");
    EXPECT_EQ(data.product_identity.variant, "unscaled");
    EXPECT_EQ(metadata.interval_start.ToString(), "2026-07-01T00:00:00Z");
    EXPECT_EQ(metadata.interval_end.ToString(), "2026-07-01T01:00:00Z");
    EXPECT_EQ(metadata.availability_cutoff.ToString(), "2026-07-03T00:00:00Z");
    EXPECT_EQ(metadata.cutoff_mode, cece::FireCutoffMode::Fixed);
    ASSERT_EQ(metadata.grid_definition->Definition().nx, 2);
    ASSERT_EQ(metadata.grid_definition->Definition().ny, 2);
    EXPECT_EQ(data.species.size(), 11);
    EXPECT_EQ(data.source_values.size(), 44);
    EXPECT_DOUBLE_EQ(data.source_values[SpeciesIndex(product, cece::FireBulkSpecies::CO2) * 4], 172479.125);
    EXPECT_DOUBLE_EQ(data.source_values[SpeciesIndex(product, cece::FireBulkSpecies::CO) * 4], 11099.603515625);
    EXPECT_DOUBLE_EQ(product.MassKg()[SpeciesIndex(product, cece::FireBulkSpecies::CO2) * 4], 172479.125);
    ASSERT_TRUE(data.optional_diagnostics.frp_mw);
    ASSERT_TRUE(data.optional_diagnostics.fre_mj);
    EXPECT_DOUBLE_EQ(data.optional_diagnostics.frp_mw->front(), 82.14042663574219);
    EXPECT_DOUBLE_EQ(data.optional_diagnostics.fre_mj->front(), 295705.53125);
    EXPECT_EQ(data.quality_state.front(), cece::FireQualityState::Observed);
    EXPECT_EQ(data.quality_state[1], cece::FireQualityState::ValidZero);
    EXPECT_DOUBLE_EQ(data.coverage_fraction[1], 1.0);
    EXPECT_EQ(data.species_availability.at(cece::FireBulkSpecies::VOC), cece::FireProductSpeciesAvailability::Available);
    EXPECT_EQ(data.species_availability.at(cece::FireBulkSpecies::PM25), cece::FireProductSpeciesAvailability::Available);
    EXPECT_EQ(data.quantity_semantics.at(cece::FireBulkSpecies::CO).kind, cece::FireProductQuantityKind::IntervalMass);
    EXPECT_EQ(data.quantity_semantics.at(cece::FireBulkSpecies::CO).units, "kg/cell/interval");
}

TEST(test_fire_products, DecodesTwentyFourConsecutiveRaveHoursWithStableGridAndReferenceMasses) {
    constexpr std::array<cece::FireBulkSpecies, 11> species{
        cece::FireBulkSpecies::CO2, cece::FireBulkSpecies::CO,  cece::FireBulkSpecies::CH4, cece::FireBulkSpecies::PM25,
        cece::FireBulkSpecies::OC,  cece::FireBulkSpecies::BC,  cece::FireBulkSpecies::SO2, cece::FireBulkSpecies::TPM,
        cece::FireBulkSpecies::NH3, cece::FireBulkSpecies::NOX, cece::FireBulkSpecies::VOC};
    constexpr std::array<double, 11> expected_total_kg{
        1106144.125, 71184.0390625, 3977.9316101074219, 4368.7460021972656, 2470.5048522949219, 293.1107234954834,
        558.30615997314453, 9002.6873779296875, 1514.4056396484375, 2170.4151000976562, 5303.9086608886719};
    const cece::FireUtcTime first_start("2026-07-01T00:00:00Z");
    const auto cutoff = ProductDecodeOptions("2026-07-03T00:00:00Z");
    std::array<double, 11> actual_total_kg{};
    std::set<std::string> content_hashes;
    std::string stable_grid_hash;
    cece::FireGridDefinition stable_grid;

    for (std::size_t hour = 0; hour < 24; ++hour) {
        const auto filename = std::string("rave-24h/hour-") + (hour < 10 ? "0" : "") + std::to_string(hour) + ".nc";
        const auto product = cece::DecodeRaveProductFile(ProductFixturePath(filename), cutoff);
        const auto& data = product.Data();
        const auto& metadata = data.product_identity.common_identity.Metadata();
        const auto expected_start = cece::FireUtcTime::FromEpochSeconds(first_start.EpochSeconds() + static_cast<std::int64_t>(hour) * 3600);
        const auto expected_end = cece::FireUtcTime::FromEpochSeconds(expected_start.EpochSeconds() + 3600);

        EXPECT_EQ(metadata.interval_start, expected_start);
        EXPECT_EQ(metadata.interval_end, expected_end);
        EXPECT_EQ(metadata.availability_cutoff, cutoff.availability_cutoff);
        ASSERT_EQ(data.species.size(), species.size());
        ASSERT_EQ(data.source_values.size(), species.size() * 4);
        ASSERT_TRUE(metadata.grid_definition);
        const auto& grid = metadata.grid_definition->Definition();
        if (hour == 0) {
            stable_grid_hash = grid.grid_sha256;
            stable_grid = grid;
        } else {
            EXPECT_EQ(grid.grid_sha256, stable_grid_hash);
            EXPECT_EQ(grid.longitude_deg, stable_grid.longitude_deg);
            EXPECT_EQ(grid.latitude_deg, stable_grid.latitude_deg);
            EXPECT_EQ(grid.longitude_bounds_deg, stable_grid.longitude_bounds_deg);
            EXPECT_EQ(grid.latitude_bounds_deg, stable_grid.latitude_bounds_deg);
            EXPECT_EQ(grid.area_m2, stable_grid.area_m2);
        }
        content_hashes.insert(metadata.content_sha256);

        const auto& masses = product.MassKg();
        for (std::size_t species_index = 0; species_index < species.size(); ++species_index) {
            ASSERT_EQ(data.species[species_index], species[species_index]);
            for (std::size_t cell = 0; cell < 4; ++cell) actual_total_kg[species_index] += masses[species_index * 4 + cell];
        }
    }

    EXPECT_EQ(content_hashes.size(), 24);
    for (std::size_t species_index = 0; species_index < species.size(); ++species_index) {
        EXPECT_NEAR(actual_total_kg[species_index], expected_total_kg[species_index], 1e-6);
    }
}

TEST(test_fire_products, DecodesRaveEmbeddedScaledVariantExactlyOnce) {
    auto options = ProductDecodeOptions("2026-07-03T00:00:00Z");
    options.scaling = cece::FireProductScalingKind::Applied;
    const auto scaled = cece::DecodeRaveProductFile(ProductFixturePath("RAVE-v2r0-crop.nc"), options);
    options.scaling = cece::FireProductScalingKind::Unscaled;
    const auto unscaled = cece::DecodeRaveProductFile(ProductFixturePath("RAVE-v2r0-crop.nc"), options);

    EXPECT_EQ(scaled.Data().product_identity.variant, "scaled");
    EXPECT_EQ(scaled.Data().scaling_state.kind, cece::FireProductScalingKind::Applied);
    ASSERT_TRUE(scaled.Data().scaling_state.profile);
    EXPECT_EQ(scaled.Data().scaling_state.profile->id, "RAVE-embedded-scaled-variant");
    const auto pm25_index = SpeciesIndex(scaled, cece::FireBulkSpecies::PM25) * 4;
    EXPECT_NE(scaled.Data().source_values[pm25_index], unscaled.Data().source_values[pm25_index]);
    EXPECT_DOUBLE_EQ(scaled.Data().source_values[pm25_index], 1226.1798095703125);
    EXPECT_THROW(scaled.ValidateScaling(scaled.Data().scaling_state.profile), std::invalid_argument);
}

TEST(test_fire_products, DecodesGbbepxV4r0FluxCropAndMarksReducedSpeciesSet) {
    const auto product =
        cece::DecodeGbbepxProductFile(ProductFixturePath("GBBEPx-v4r0-crop.nc"), ProductDecodeOptions("2023-02-26T00:00:00Z"));
    const auto& data = product.Data();
    const auto& metadata = data.product_identity.common_identity.Metadata();

    EXPECT_EQ(data.product_identity.product_id, "GBBEPx-all01GRID");
    EXPECT_EQ(data.product_identity.version, "v4r0");
    EXPECT_EQ(metadata.interval_start.ToString(), "2023-02-24T00:00:00Z");
    EXPECT_EQ(metadata.interval_end.ToString(), "2023-02-25T00:00:00Z");
    EXPECT_EQ(metadata.availability_cutoff.ToString(), "2023-02-26T00:00:00Z");
    ASSERT_EQ(metadata.grid_definition->Definition().nx, 2);
    ASSERT_EQ(metadata.grid_definition->Definition().ny, 2);
    EXPECT_EQ(metadata.grid_definition->Definition().longitude_convention, cece::FireLongitudeConvention::ZeroTo360);
    EXPECT_EQ(data.species.size(), 8);
    EXPECT_EQ(data.species_availability.at(cece::FireBulkSpecies::CH4), cece::FireProductSpeciesAvailability::Unavailable);
    EXPECT_EQ(data.species_availability.at(cece::FireBulkSpecies::TPM), cece::FireProductSpeciesAvailability::Unavailable);
    EXPECT_EQ(data.species_availability.at(cece::FireBulkSpecies::VOC), cece::FireProductSpeciesAvailability::Unavailable);
    EXPECT_EQ(data.species_availability.at(cece::FireBulkSpecies::CO), cece::FireProductSpeciesAvailability::Available);
    EXPECT_EQ(data.quantity_semantics.at(cece::FireBulkSpecies::CO).kind, cece::FireProductQuantityKind::Flux);
    EXPECT_EQ(data.quantity_semantics.at(cece::FireBulkSpecies::CO).units, "kg m-2 s-1");
    EXPECT_DOUBLE_EQ(data.source_values[SpeciesIndex(product, cece::FireBulkSpecies::CO) * 4], 1.463242327304215e-08);
    EXPECT_NEAR(product.MassKg()[SpeciesIndex(product, cece::FireBulkSpecies::CO) * 4], 118329.8014639274, 1e-5);
    EXPECT_EQ(data.quality_state.front(), cece::FireQualityState::Observed);
    EXPECT_EQ(data.quality_state[1], cece::FireQualityState::ValidZero);
    ASSERT_TRUE(data.optional_diagnostics.frp_mw);
    EXPECT_DOUBLE_EQ(data.optional_diagnostics.frp_mw->front(), 24.62002182006836);
    EXPECT_THROW(product.RequireSpecies({cece::FireBulkSpecies::CH4}), std::invalid_argument);
    EXPECT_THROW(product.RequireSpecies({cece::FireBulkSpecies::TPM}), std::invalid_argument);
    EXPECT_THROW(product.RequireSpecies({cece::FireBulkSpecies::VOC}), std::invalid_argument);
}

TEST(test_fire_products, GbbepxDatelineCropKeepsNormalizedCentersAndFiniteCellAreas) {
    const auto product =
        cece::DecodeGbbepxProductFile(ProductFixturePath("GBBEPx-v4r0-dateline-crop.nc"), ProductDecodeOptions("2023-02-26T00:00:00Z"));
    const auto& grid = product.Data().product_identity.common_identity.Metadata().grid_definition->Definition();

    ASSERT_EQ(grid.longitude_deg.size(), 4);
    EXPECT_NEAR(grid.longitude_deg[0], 359.9, 2e-5);
    EXPECT_DOUBLE_EQ(grid.longitude_deg[1], 0.0);
    EXPECT_TRUE(std::all_of(grid.area_m2.begin(), grid.area_m2.end(), [](double area) { return std::isfinite(area) && area > 0; }));
    EXPECT_TRUE(std::all_of(grid.longitude_bounds_deg.begin(), grid.longitude_bounds_deg.end(), [](const auto& bounds) {
        return std::all_of(bounds.begin(), bounds.end(), [](double longitude) { return longitude >= 0 && longitude <= 360; });
    }));
}

TEST(test_fire_products, ProductDecodersRejectUnsupportedScalingAndWindows) {
    cece::FireProductDecodeOptions scaled;
    scaled.availability_cutoff = cece::FireUtcTime("2023-02-26T00:00:00Z");
    scaled.scaling = cece::FireProductScalingKind::Applied;
    EXPECT_THROW(cece::DecodeGbbepxProductFile(ProductFixturePath("GBBEPx-v4r0-crop.nc"), scaled), std::invalid_argument);

    cece::FireProductDecodeOptions outside;
    outside.availability_cutoff = cece::FireUtcTime("2026-07-03T00:00:00Z");
    outside.window = cece::FireProductDecodeWindow{2, 0, 1, 1};
    EXPECT_THROW(cece::DecodeRaveProductFile(ProductFixturePath("RAVE-v2r0-crop.nc"), outside), std::invalid_argument);
}

TEST(test_fire_products, ProductDecodersRequireExplicitAvailabilityCutoff) {
    EXPECT_THROW(cece::DecodeRaveProductFile(ProductFixturePath("RAVE-v2r0-crop.nc")), std::invalid_argument);
    EXPECT_THROW(cece::DecodeGbbepxProductFile(ProductFixturePath("GBBEPx-v4r0-crop.nc")), std::invalid_argument);
}

TEST(test_fire_products, OverlappingProductVariantsAreRejectedRegardlessOfVersion) {
    const auto first = ProductInterval("2022-07-01T00:00:00Z", "2022-07-01T01:00:00Z", cece::FireProductQuantityKind::IntervalMass,
                                       "kg/cell/interval", {1});
    auto second_data = first.Data();
    second_data.product_identity.version = "product-v2";
    second_data.product_identity.variant = "scaled";
    auto second_metadata = second_data.product_identity.common_identity.Metadata();
    second_metadata.product_version = "product-v2";
    second_data.product_identity.common_identity = cece::FireInputIdentity(std::move(second_metadata));
    const cece::FireProductEmissionsInterval second(std::move(second_data));
    EXPECT_THROW(cece::ValidateFireProductIntervalSet({std::cref(first), std::cref(second)}), std::invalid_argument);

    const auto next = ProductInterval("2022-07-01T01:00:00Z", "2022-07-01T02:00:00Z", cece::FireProductQuantityKind::IntervalMass,
                                      "kg/cell/interval", {1});
    EXPECT_NO_THROW(cece::ValidateFireProductIntervalSet({std::cref(first), std::cref(next)}));
}

TEST(test_fire_products, ScalingIdentityIsPreservedAndRepeatedApplicationRejected) {
    auto data = ProductInterval("2022-07-01T00:00:00Z", "2022-07-01T01:00:00Z", cece::FireProductQuantityKind::IntervalMass,
                                "kg/cell/interval", {10})
                    .Data();
    const cece::FireProfileIdentity scaling{"synthetic-scale", "1", std::string(64, 'c')};
    data.scaling_state = {cece::FireProductScalingKind::Applied, scaling};
    const cece::FireProductEmissionsInterval scaled(std::move(data));
    EXPECT_EQ(scaled.Data().scaling_state.profile->id, "synthetic-scale");
    EXPECT_NO_THROW(scaled.ValidateScaling(std::nullopt));
    EXPECT_THROW(scaled.ValidateScaling(scaling), std::invalid_argument);
}

TEST(test_fire_products, ExactTargetProductBypassesBulkChemistryMapping) {
    auto data = ProductInterval("2022-07-01T00:00:00Z", "2022-07-01T01:00:00Z", cece::FireProductQuantityKind::IntervalMass,
                                "kg/cell/interval", {0})
                    .Data();
    data.quality_state = {cece::FireQualityState::Observed};
    data.chemistry_state = cece::FireProductChemistryState::TargetSpecific;
    data.target_identity = cece::FireProductTargetIdentity{"CB6", "revision-7"};
    data.target_fields = {{"FIRE_CO", "kg"}};
    data.target_mass_kg = {5};
    data.species.clear();
    for (auto& [species, availability] : data.species_availability) {
        (void)species;
        availability = cece::FireProductSpeciesAvailability::Unavailable;
    }
    data.quantity_semantics.clear();
    data.source_values.clear();
    data.mass_kg.clear();
    const cece::FireProductEmissionsInterval product(std::move(data));
    const auto target = product.ToTargetSpecificInventory("CB6", "revision-7");
    EXPECT_EQ(target.target_id, "CB6");
    EXPECT_EQ(target.target_version, "revision-7");
    ASSERT_EQ(target.fields.size(), 1);
    EXPECT_EQ(target.fields[0].target_species, "FIRE_CO");
    EXPECT_EQ(target.fields[0].unit, "kg");
    EXPECT_DOUBLE_EQ(target.mass_kg[0], 5);
    EXPECT_THROW(product.ToTargetSpecificInventory("CB6", "wrong-revision"), std::invalid_argument);
}

TEST(test_fire_products, FRPDiagnosticsRemainNonAuthoritativeForProductMass) {
    auto data = ProductInterval("2022-07-01T00:00:00Z", "2022-07-01T01:00:00Z", cece::FireProductQuantityKind::IntervalMass,
                                "kg/cell/interval", {12})
                    .Data();
    data.optional_diagnostics.frp_mw = std::vector<double>{100};
    const cece::FireProductEmissionsInterval product(std::move(data));
    ASSERT_TRUE(product.Data().optional_diagnostics.frp_mw);
    EXPECT_DOUBLE_EQ(product.MassKg()[0], 12);
}

}  // namespace
