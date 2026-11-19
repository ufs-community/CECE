// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <functional>

#include "cece/physics/cece_fire.hpp"

namespace {

std::shared_ptr<const cece::FireGrid> ProductGrid(double area_m2 = 100.0) {
    return std::make_shared<const cece::FireGrid>(cece::FireGridDefinition{
        "synthetic-product-grid", std::string(64, 'b'), 1, 1, {0}, {0}, {{{-1, 1, 1, -1}}}, {{{-1, -1, 1, 1}}}, {area_m2}, "EPSG:4326",
        cece::FireLongitudeConvention::Minus180To180});
}

cece::FireProductEmissionsInterval ProductInterval(const std::string& start, const std::string& end, cece::FireProductQuantityKind kind,
                                                   std::string units, std::vector<double> values) {
    const cece::FireUtcTime interval_start(start), interval_end(end);
    auto grid = ProductGrid();
    cece::FireIdentityMetadata metadata{1,
                                        "synthetic-source",
                                        "product-v1",
                                        "decoder-v1",
                                        "delivery-a",
                                        std::string(64, 'a'),
                                        interval_start,
                                        interval_end,
                                        interval_start,
                                        interval_start,
                                        cece::FireUtcTime("2022-07-03T00:00:00Z"),
                                        cece::FireCutoffMode::Fixed,
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
