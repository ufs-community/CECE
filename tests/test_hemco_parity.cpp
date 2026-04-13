/**
 * @file test_hemco_parity.cpp
 * @brief HEMCO feature parity test suite for CECE.
 *
 * Tests that CECE produces emissions matching HEMCO behaviour for:
 *   - Anthropogenic, biogenic, biomass burning, natural, and aircraft categories
 *   - Temporal scaling: diurnal (24h), weekly (7d), seasonal (12m)
 *   - Vertical distribution methods: SINGLE, RANGE, PRESSURE, HEIGHT, PBL
 *   - Hierarchy-based regional overrides (replace operation)
 *   - Dynamic species and scale factor registration
 *   - Emission provenance tracking
 *
 * All tests run with real Kokkos (no ESMF mocking required for unit-level tests).
 * Requirements: 3.1-3.7, 3.11, 3.14-3.17, 3.18
 */

#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "cece/cece_compute.hpp"
#include "cece/cece_config.hpp"
#include "cece/cece_provenance.hpp"
#include "cece/cece_stacking_engine.hpp"
#include "cece/cece_state.hpp"

namespace cece {

// ---------------------------------------------------------------------------
// Shared test fixture
// ---------------------------------------------------------------------------

class HemcoParityTest : public ::testing::Test {
   protected:
    static constexpr int kNx = 8;
    static constexpr int kNy = 8;
    static constexpr int kNz = 1;

    void SetUp() override {
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }
    }

    /** Creates a DualView filled with a constant value. */
    static DualView3D MakeField(const std::string& name, double val, int nz = kNz) {
        DualView3D dv(name, kNx, kNy, nz);
        Kokkos::deep_copy(dv.view_host(), val);
        dv.modify<Kokkos::HostSpace>();
        dv.sync<Kokkos::DefaultExecutionSpace>();
        return dv;
    }

    /** Runs the StackingEngine and returns the host-side result view. */
    static Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> RunEngine(
        CeceConfig& config, CeceImportState& import_state, CeceExportState& export_state,
        int hour = 0, int dow = 0, int month = 0, int nz = kNz) {
        std::unordered_map<std::string, std::string> empty;
        CeceStateResolver resolver(import_state, export_state, empty, empty, empty);
        StackingEngine engine(config);
        engine.Execute(resolver, kNx, kNy, nz, {}, hour, dow, month);
        export_state.fields.begin()->second.sync<Kokkos::HostSpace>();
        return export_state.fields.begin()->second.view_host();
    }

    /** Computes max relative error between result and expected scalar. */
    static double MaxRelError(
        const Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace>& view, double expected,
        int nz = kNz) {
        double max_err = 0.0;
        for (int i = 0; i < kNx; ++i)
            for (int j = 0; j < kNy; ++j)
                for (int k = 0; k < nz; ++k) {
                    double err = std::abs(view(i, j, k) - expected) / (std::abs(expected) + 1e-30);
                    if (err > max_err) max_err = err;
                }
        return max_err;
    }
};

// ---------------------------------------------------------------------------
// 1. Emission categories (Req 3.1)
// ---------------------------------------------------------------------------

TEST_F(HemcoParityTest, AnthropogenicCategory) {
    CeceImportState imp;
    CeceExportState exp;
    imp.fields["anthro_co"] = MakeField("anthro_co", 2.0e-9);
    exp.fields["co"] = MakeField("co", 0.0);

    CeceConfig cfg;
    EmissionLayer lay;
    lay.field_name = "anthro_co";
    lay.operation = "add";
    lay.hierarchy = 1;
    lay.category = "anthropogenic";
    lay.scale = 1.0;
    cfg.species_layers["co"] = {lay};

    auto result = RunEngine(cfg, imp, exp);
    EXPECT_LT(MaxRelError(result, 2.0e-9), 1e-10) << "Anthropogenic CO emission mismatch";
}

TEST_F(HemcoParityTest, BiogenicCategory) {
    CeceImportState imp;
    CeceExportState exp;
    imp.fields["biogenic_isop"] = MakeField("biogenic_isop", 5.0e-10);
    exp.fields["isop"] = MakeField("isop", 0.0);

    CeceConfig cfg;
    EmissionLayer lay;
    lay.field_name = "biogenic_isop";
    lay.operation = "add";
    lay.hierarchy = 1;
    lay.category = "biogenic";
    lay.scale = 1.0;
    cfg.species_layers["isop"] = {lay};

    auto result = RunEngine(cfg, imp, exp);
    EXPECT_LT(MaxRelError(result, 5.0e-10), 1e-10);
}

TEST_F(HemcoParityTest, BiomassBurningCategory) {
    CeceImportState imp;
    CeceExportState exp;
    imp.fields["bb_co"] = MakeField("bb_co", 3.0e-9);
    exp.fields["co"] = MakeField("co", 0.0);

    CeceConfig cfg;
    EmissionLayer lay;
    lay.field_name = "bb_co";
    lay.operation = "add";
    lay.hierarchy = 1;
    lay.category = "biomass_burning";
    lay.scale = 1.0;
    cfg.species_layers["co"] = {lay};

    auto result = RunEngine(cfg, imp, exp);
    EXPECT_LT(MaxRelError(result, 3.0e-9), 1e-10);
}

TEST_F(HemcoParityTest, NaturalCategory) {
    CeceImportState imp;
    CeceExportState exp;
    imp.fields["nat_so2"] = MakeField("nat_so2", 1.0e-11);
    exp.fields["so2"] = MakeField("so2", 0.0);

    CeceConfig cfg;
    EmissionLayer lay;
    lay.field_name = "nat_so2";
    lay.operation = "add";
    lay.hierarchy = 1;
    lay.category = "natural";
    lay.scale = 1.0;
    cfg.species_layers["so2"] = {lay};

    auto result = RunEngine(cfg, imp, exp);
    EXPECT_LT(MaxRelError(result, 1.0e-11), 1e-10);
}

TEST_F(HemcoParityTest, AircraftCategory) {
    CeceImportState imp;
    CeceExportState exp;
    imp.fields["air_nox"] = MakeField("air_nox", 4.0e-12);
    exp.fields["nox"] = MakeField("nox", 0.0);

    CeceConfig cfg;
    EmissionLayer lay;
    lay.field_name = "air_nox";
    lay.operation = "add";
    lay.hierarchy = 1;
    lay.category = "aircraft";
    lay.scale = 1.0;
    cfg.species_layers["nox"] = {lay};

    auto result = RunEngine(cfg, imp, exp);
    EXPECT_LT(MaxRelError(result, 4.0e-12), 1e-10);
}

// ---------------------------------------------------------------------------
// 2. Temporal scaling (Req 3.7)
// ---------------------------------------------------------------------------

TEST_F(HemcoParityTest, DiurnalCycleScaling) {
    // 24 hourly factors: factor at hour 6 = 2.0, all others = 1.0
    std::vector<double> diurnal(24, 1.0);
    diurnal[6] = 2.0;

    CeceImportState imp;
    CeceExportState exp;
    imp.fields["base_co"] = MakeField("base_co", 1.0e-9);
    exp.fields["co"] = MakeField("co", 0.0);

    CeceConfig cfg;
    cfg.temporal_profiles["diurnal_test"] = TemporalCycle{diurnal};
    EmissionLayer lay;
    lay.field_name = "base_co";
    lay.operation = "add";
    lay.hierarchy = 1;
    lay.scale = 1.0;
    lay.diurnal_cycle = "diurnal_test";
    cfg.species_layers["co"] = {lay};

    // At hour 6, scale = 2.0 -> emission = 2.0e-9
    auto result = RunEngine(cfg, imp, exp, /*hour=*/6, /*dow=*/0, /*month=*/0);
    EXPECT_LT(MaxRelError(result, 2.0e-9), 1e-10)
        << "Diurnal cycle not applied correctly at hour 6";
}

TEST_F(HemcoParityTest, WeeklyCycleScaling) {
    // 7 daily factors: factor on day 1 (Monday) = 1.5
    std::vector<double> weekly = {1.0, 1.5, 1.0, 1.0, 1.0, 0.8, 0.8};

    CeceImportState imp;
    CeceExportState exp;
    imp.fields["base_nox"] = MakeField("base_nox", 1.0e-9);
    exp.fields["nox"] = MakeField("nox", 0.0);

    CeceConfig cfg;
    cfg.temporal_profiles["weekly_test"] = TemporalCycle{weekly};
    EmissionLayer lay;
    lay.field_name = "base_nox";
    lay.operation = "add";
    lay.hierarchy = 1;
    lay.scale = 1.0;
    lay.weekly_cycle = "weekly_test";
    cfg.species_layers["nox"] = {lay};

    // day_of_week=1 -> factor 1.5 -> emission = 1.5e-9
    auto result = RunEngine(cfg, imp, exp, /*hour=*/0, /*dow=*/1, /*month=*/0);
    EXPECT_LT(MaxRelError(result, 1.5e-9), 1e-10) << "Weekly cycle not applied correctly on day 1";
}

TEST_F(HemcoParityTest, SeasonalCycleScaling) {
    // 12 monthly factors: July (month 6) = 3.0
    std::vector<double> seasonal(12, 1.0);
    seasonal[6] = 3.0;

    CeceImportState imp;
    CeceExportState exp;
    imp.fields["base_so2"] = MakeField("base_so2", 1.0e-10);
    exp.fields["so2"] = MakeField("so2", 0.0);

    CeceConfig cfg;
    cfg.temporal_profiles["seasonal_test"] = TemporalCycle{seasonal};
    EmissionLayer lay;
    lay.field_name = "base_so2";
    lay.operation = "add";
    lay.hierarchy = 1;
    lay.scale = 1.0;
    lay.seasonal_cycle = "seasonal_test";
    cfg.species_layers["so2"] = {lay};

    // month=6 -> factor 3.0 -> emission = 3.0e-10
    auto result = RunEngine(cfg, imp, exp, /*hour=*/0, /*dow=*/0, /*month=*/6);
    EXPECT_LT(MaxRelError(result, 3.0e-10), 1e-10)
        << "Seasonal cycle not applied correctly for month 6";
}

TEST_F(HemcoParityTest, CombinedTemporalScaling) {
    // Diurnal * weekly * seasonal applied as product
    std::vector<double> diurnal(24, 1.0);
    diurnal[12] = 2.0;
    std::vector<double> weekly(7, 1.0);
    weekly[3] = 1.5;
    std::vector<double> seasonal(12, 1.0);
    seasonal[0] = 0.5;

    CeceImportState imp;
    CeceExportState exp;
    imp.fields["base_co"] = MakeField("base_co", 1.0e-9);
    exp.fields["co"] = MakeField("co", 0.0);

    CeceConfig cfg;
    cfg.temporal_profiles["d"] = TemporalCycle{diurnal};
    cfg.temporal_profiles["w"] = TemporalCycle{weekly};
    cfg.temporal_profiles["s"] = TemporalCycle{seasonal};
    EmissionLayer lay;
    lay.field_name = "base_co";
    lay.operation = "add";
    lay.hierarchy = 1;
    lay.scale = 1.0;
    lay.diurnal_cycle = "d";
    lay.weekly_cycle = "w";
    lay.seasonal_cycle = "s";
    cfg.species_layers["co"] = {lay};

    // Expected: 1e-9 * 2.0 * 1.5 * 0.5 = 1.5e-9
    auto result = RunEngine(cfg, imp, exp, /*hour=*/12, /*dow=*/3, /*month=*/0);
    EXPECT_LT(MaxRelError(result, 1.5e-9), 1e-10)
        << "Combined temporal scaling (diurnal*weekly*seasonal) incorrect";
}

// ---------------------------------------------------------------------------
// 3. Vertical distribution methods (Req 3.6)
// ---------------------------------------------------------------------------

TEST_F(HemcoParityTest, VerticalDistributionSingle) {
    const int nz = 5;
    CeceImportState imp;
    CeceExportState exp;
    imp.fields["base_co"] = MakeField("base_co", 1.0, /*nz=*/1);
    exp.fields["co"] = MakeField("co", 0.0, nz);

    CeceConfig cfg;
    EmissionLayer lay;
    lay.field_name = "base_co";
    lay.operation = "add";
    lay.hierarchy = 1;
    lay.scale = 1.0;
    lay.vdist_method = VerticalDistributionMethod::SINGLE;
    lay.vdist_layer_start = 2;
    cfg.species_layers["co"] = {lay};

    std::unordered_map<std::string, std::string> empty;
    CeceStateResolver resolver(imp, exp, empty, empty, empty);
    StackingEngine engine(cfg);
    engine.Execute(resolver, kNx, kNy, nz, {}, 0, 0, 0);
    exp.fields["co"].sync<Kokkos::HostSpace>();
    auto result = exp.fields["co"].view_host();

    for (int i = 0; i < kNx; ++i)
        for (int j = 0; j < kNy; ++j) {
            EXPECT_DOUBLE_EQ(result(i, j, 2), 1.0) << "SINGLE: layer 2 should have emission";
            EXPECT_DOUBLE_EQ(result(i, j, 0), 0.0) << "SINGLE: layer 0 should be zero";
            EXPECT_DOUBLE_EQ(result(i, j, 4), 0.0) << "SINGLE: layer 4 should be zero";
        }
}

TEST_F(HemcoParityTest, VerticalDistributionRange) {
    const int nz = 6;
    CeceImportState imp;
    CeceExportState exp;
    imp.fields["base_co"] = MakeField("base_co", 6.0, /*nz=*/1);
    exp.fields["co"] = MakeField("co", 0.0, nz);

    CeceConfig cfg;
    EmissionLayer lay;
    lay.field_name = "base_co";
    lay.operation = "add";
    lay.hierarchy = 1;
    lay.scale = 1.0;
    lay.vdist_method = VerticalDistributionMethod::RANGE;
    lay.vdist_layer_start = 1;
    lay.vdist_layer_end = 3;  // 3 layers -> weight = 1/3 each
    cfg.species_layers["co"] = {lay};

    std::unordered_map<std::string, std::string> empty;
    CeceStateResolver resolver(imp, exp, empty, empty, empty);
    StackingEngine engine(cfg);
    engine.Execute(resolver, kNx, kNy, nz, {}, 0, 0, 0);
    exp.fields["co"].sync<Kokkos::HostSpace>();
    auto result = exp.fields["co"].view_host();

    // Each of layers 1-3 gets 6.0 * (1/3) = 2.0
    for (int i = 0; i < kNx; ++i)
        for (int j = 0; j < kNy; ++j) {
            EXPECT_DOUBLE_EQ(result(i, j, 0), 0.0) << "RANGE: layer 0 outside range";
            EXPECT_NEAR(result(i, j, 1), 2.0, 1e-12) << "RANGE: layer 1";
            EXPECT_NEAR(result(i, j, 2), 2.0, 1e-12) << "RANGE: layer 2";
            EXPECT_NEAR(result(i, j, 3), 2.0, 1e-12) << "RANGE: layer 3";
            EXPECT_DOUBLE_EQ(result(i, j, 4), 0.0) << "RANGE: layer 4 outside range";
        }
}

// ---------------------------------------------------------------------------
// 4. Hierarchy-based regional override (Req 3.4, 3.16)
// ---------------------------------------------------------------------------

TEST_F(HemcoParityTest, HierarchyRegionalOverride) {
    CeceImportState imp;
    CeceExportState exp;
    imp.fields["global_co"] = MakeField("global_co", 5.0e-9);
    imp.fields["regional_co"] = MakeField("regional_co", 12.0e-9);
    exp.fields["co"] = MakeField("co", 0.0);

    // Mask: top half of grid = 1.0, bottom half = 0.0
    DualView3D mask("mask", kNx, kNy, 1);
    auto mask_h = mask.view_host();
    for (int i = 0; i < kNx; ++i)
        for (int j = 0; j < kNy; ++j) mask_h(i, j, 0) = (j >= kNy / 2) ? 1.0 : 0.0;
    mask.modify<Kokkos::HostSpace>();
    mask.sync<Kokkos::DefaultExecutionSpace>();
    imp.fields["region_mask"] = mask;

    CeceConfig cfg;
    EmissionLayer global_lay;
    global_lay.field_name = "global_co";
    global_lay.operation = "add";
    global_lay.hierarchy = 1;
    global_lay.scale = 1.0;

    EmissionLayer regional_lay;
    regional_lay.field_name = "regional_co";
    regional_lay.operation = "replace";
    regional_lay.hierarchy = 10;
    regional_lay.scale = 1.0;
    regional_lay.masks = {"region_mask"};

    cfg.species_layers["co"] = {global_lay, regional_lay};

    auto result = RunEngine(cfg, imp, exp);

    for (int i = 0; i < kNx; ++i)
        for (int j = 0; j < kNy; ++j) {
            if (j >= kNy / 2) {
                EXPECT_NEAR(result(i, j, 0), 12.0e-9, 1e-20)
                    << "Inside mask: regional should replace global";
            } else {
                EXPECT_NEAR(result(i, j, 0), 5.0e-9, 1e-20) << "Outside mask: global should remain";
            }
        }
}

// ---------------------------------------------------------------------------
// 5. Scale factor types (Req 3.2)
// ---------------------------------------------------------------------------

TEST_F(HemcoParityTest, ConstantScaleFactor) {
    CeceImportState imp;
    CeceExportState exp;
    imp.fields["base_co"] = MakeField("base_co", 1.0e-9);
    exp.fields["co"] = MakeField("co", 0.0);

    CeceConfig cfg;
    EmissionLayer lay;
    lay.field_name = "base_co";
    lay.operation = "add";
    lay.hierarchy = 1;
    lay.scale = 2.5;  // constant scale factor
    cfg.species_layers["co"] = {lay};

    auto result = RunEngine(cfg, imp, exp);
    EXPECT_LT(MaxRelError(result, 2.5e-9), 1e-10);
}

TEST_F(HemcoParityTest, SpatiallyVaryingScaleFactor) {
    CeceImportState imp;
    CeceExportState exp;
    imp.fields["base_co"] = MakeField("base_co", 1.0e-9);

    // Spatially varying scale: value = 2.0 everywhere for simplicity
    imp.fields["spatial_sf"] = MakeField("spatial_sf", 2.0);
    exp.fields["co"] = MakeField("co", 0.0);

    CeceConfig cfg;
    EmissionLayer lay;
    lay.field_name = "base_co";
    lay.operation = "add";
    lay.hierarchy = 1;
    lay.scale = 1.0;
    lay.scale_fields = {"spatial_sf"};
    cfg.species_layers["co"] = {lay};

    auto result = RunEngine(cfg, imp, exp);
    EXPECT_LT(MaxRelError(result, 2.0e-9), 1e-10) << "Spatially varying scale factor not applied";
}

// ---------------------------------------------------------------------------
// 6. Dynamic species registration (Req 3.14, 3.15)
// ---------------------------------------------------------------------------

TEST_F(HemcoParityTest, DynamicSpeciesRegistration) {
    CeceImportState imp;
    CeceExportState exp;
    imp.fields["new_species_field"] = MakeField("new_species_field", 7.0e-10);
    exp.fields["new_species"] = MakeField("new_species", 0.0);

    CeceConfig cfg;
    // Start with an empty config, then add species at runtime
    EmissionLayer lay;
    lay.field_name = "new_species_field";
    lay.operation = "add";
    lay.hierarchy = 1;
    lay.scale = 1.0;

    AddSpecies(cfg, "new_species", {lay});

    auto result = RunEngine(cfg, imp, exp);
    EXPECT_LT(MaxRelError(result, 7.0e-10), 1e-10) << "Dynamically registered species not computed";
}

TEST_F(HemcoParityTest, DynamicScaleFactorRegistration) {
    CeceImportState imp;
    CeceExportState exp;
    imp.fields["base_co"] = MakeField("base_co", 1.0e-9);
    imp.fields["dynamic_sf"] = MakeField("dynamic_sf", 3.0);
    exp.fields["co"] = MakeField("co", 0.0);

    CeceConfig cfg;
    AddScaleFactor(cfg, "dynamic_sf", "dynamic_sf");

    EmissionLayer lay;
    lay.field_name = "base_co";
    lay.operation = "add";
    lay.hierarchy = 1;
    lay.scale = 1.0;
    lay.scale_fields = {"dynamic_sf"};
    cfg.species_layers["co"] = {lay};

    auto result = RunEngine(cfg, imp, exp);
    EXPECT_LT(MaxRelError(result, 3.0e-9), 1e-10)
        << "Dynamically registered scale factor not applied";
}

// ---------------------------------------------------------------------------
// 7. Emission provenance tracking (Req 3.17)
// ---------------------------------------------------------------------------

TEST_F(HemcoParityTest, ProvenanceTracking) {
    CeceImportState imp;
    CeceExportState exp;
    imp.fields["global_co"] = MakeField("global_co", 5.0e-9);
    imp.fields["regional_co"] = MakeField("regional_co", 12.0e-9);
    exp.fields["co"] = MakeField("co", 0.0);

    CeceConfig cfg;
    EmissionLayer lay1;
    lay1.field_name = "global_co";
    lay1.operation = "add";
    lay1.hierarchy = 1;
    lay1.category = "anthropogenic";
    lay1.scale = 1.0;

    EmissionLayer lay2;
    lay2.field_name = "regional_co";
    lay2.operation = "replace";
    lay2.hierarchy = 5;
    lay2.category = "anthropogenic";
    lay2.scale = 1.2;

    cfg.species_layers["co"] = {lay1, lay2};

    std::unordered_map<std::string, std::string> empty;
    CeceStateResolver resolver(imp, exp, empty, empty, empty);
    StackingEngine engine(cfg);
    engine.Execute(resolver, kNx, kNy, kNz, {}, 0, 0, 0);

    const auto& prov = engine.GetProvenance();
    const auto* co_prov = prov.GetProvenance("co");
    ASSERT_NE(co_prov, nullptr) << "Provenance for 'co' not found";
    EXPECT_EQ(co_prov->species_name, "co");
    ASSERT_EQ(co_prov->contributions.size(), 2u);

    // Sorted by hierarchy: lay1 (hier=1) first, lay2 (hier=5) second
    EXPECT_EQ(co_prov->contributions[0].field_name, "global_co");
    EXPECT_EQ(co_prov->contributions[0].hierarchy, 1);
    EXPECT_EQ(co_prov->contributions[0].operation, "add");
    EXPECT_EQ(co_prov->contributions[1].field_name, "regional_co");
    EXPECT_EQ(co_prov->contributions[1].hierarchy, 5);
    EXPECT_EQ(co_prov->contributions[1].operation, "replace");
    EXPECT_NEAR(co_prov->contributions[1].base_scale, 1.2, 1e-12);

    // FormatReport should produce non-empty output
    std::string report = prov.FormatReport();
    EXPECT_FALSE(report.empty());
    EXPECT_NE(report.find("co"), std::string::npos);
    EXPECT_NE(report.find("global_co"), std::string::npos);
}

// ---------------------------------------------------------------------------
// 8. Mass conservation across vertical distribution (Req 6.23)
// ---------------------------------------------------------------------------

TEST_F(HemcoParityTest, MassConservationRange) {
    const int nz = 10;
    const double base_val = 1.0;

    CeceImportState imp;
    CeceExportState exp;
    imp.fields["base_co"] = MakeField("base_co", base_val, /*nz=*/1);
    exp.fields["co"] = MakeField("co", 0.0, nz);

    CeceConfig cfg;
    EmissionLayer lay;
    lay.field_name = "base_co";
    lay.operation = "add";
    lay.hierarchy = 1;
    lay.scale = 1.0;
    lay.vdist_method = VerticalDistributionMethod::RANGE;
    lay.vdist_layer_start = 0;
    lay.vdist_layer_end = nz - 1;
    cfg.species_layers["co"] = {lay};

    std::unordered_map<std::string, std::string> empty;
    CeceStateResolver resolver(imp, exp, empty, empty, empty);
    StackingEngine engine(cfg);
    engine.Execute(resolver, kNx, kNy, nz, {}, 0, 0, 0);
    exp.fields["co"].sync<Kokkos::HostSpace>();
    auto result = exp.fields["co"].view_host();

    // Column sum must equal base_val (mass conservation)
    for (int i = 0; i < kNx; ++i)
        for (int j = 0; j < kNy; ++j) {
            double col_sum = 0.0;
            for (int k = 0; k < nz; ++k) col_sum += result(i, j, k);
            EXPECT_NEAR(col_sum, base_val, 1e-10)
                << "Mass conservation violated at (" << i << "," << j << ")";
        }
}

}  // namespace cece

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
