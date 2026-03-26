/**
 * @file test_null_safe_layer_skip_property.cpp
 * @brief Property-based test for Property 5: Null-safe layer skip in StackingEngine.
 *
 * Feature: aces-example-runner
 * Property 5: For any StackingEngine config where one or more import field views
 * have data() == nullptr, Execute shall complete without exception and the export
 * field shall be zero for missing layers.
 *
 * Validates: Requirements 5.3
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <Kokkos_Core.hpp>

#include "aces/aces_compute.hpp"
#include "aces/aces_config.hpp"
#include "aces/aces_stacking_engine.hpp"
#include "aces/aces_state.hpp"

namespace aces {
namespace {

/**
 * @brief Helper to create a DualView3D initialized to a given value.
 */
DualView3D MakeDualView(const std::string& label, int nx, int ny, int nz, double val) {
    DualView3D dv(label, nx, ny, nz);
    Kokkos::deep_copy(dv.view_host(), val);
    dv.modify<Kokkos::HostSpace>();
    dv.sync<Kokkos::DefaultExecutionSpace::memory_space>();
    return dv;
}

/**
 * @brief Build a minimal AcesConfig with one species "co" referencing field "MACCITY".
 */
AcesConfig MakeNullLayerConfig() {
    AcesConfig config;
    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "MACCITY";
    layer.scale = 1.0;
    layer.hierarchy = 0;
    config.species_layers["co"] = {layer};
    return config;
}

class NullSafeLayerSkipTest : public ::testing::Test {
   protected:
    void SetUp() override {
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }
    }
};

/**
 * @test Property 5 (core): When the import field "MACCITY" is absent from the
 * import state (so ResolveImportDevice returns a null view), Execute must not
 * crash and the export field "co" must be zero everywhere.
 *
 * Validates: Requirements 5.3
 */
TEST_F(NullSafeLayerSkipTest, MissingImportFieldYieldsZeroExport) {
    const int nx = 4, ny = 4, nz = 1;

    AcesConfig config = MakeNullLayerConfig();
    StackingEngine engine(config);

    // Empty import state — "MACCITY" is not present, so its view will be null
    AcesImportState import_state;

    // Export state with "co" initialized to 1.0 (non-zero, to confirm it gets zeroed)
    AcesExportState export_state;
    export_state.fields["co"] = MakeDualView("co", nx, ny, nz, 1.0);

    AcesStateResolver resolver(import_state, export_state, config.met_mapping,
                               config.scale_factor_mapping, config.mask_mapping);

    // Must not throw or crash
    ASSERT_NO_THROW(engine.Execute(resolver, nx, ny, nz, {}, 0, 0));

    // Sync export back to host and verify all values are zero
    export_state.fields["co"].sync<Kokkos::HostSpace>();
    auto host_view = export_state.fields["co"].view_host();

    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                EXPECT_DOUBLE_EQ(host_view(i, j, k), 0.0)
                    << "Expected zero at (" << i << "," << j << "," << k
                    << ") when import field is missing";
            }
        }
    }
}

/**
 * @test Property 5 (multi-layer): When one layer's import field is present and
 * another is absent, only the present layer contributes to the export.
 *
 * Validates: Requirements 5.3
 */
TEST_F(NullSafeLayerSkipTest, PresentLayerContributesNullLayerSkipped) {
    const int nx = 2, ny = 2, nz = 1;

    AcesConfig config;
    // Layer 1: present field with value 5.0
    EmissionLayer l1;
    l1.operation = "add";
    l1.field_name = "PRESENT_FIELD";
    l1.scale = 1.0;
    l1.hierarchy = 0;
    // Layer 2: missing field (will be null)
    EmissionLayer l2;
    l2.operation = "add";
    l2.field_name = "MISSING_FIELD";
    l2.scale = 1.0;
    l2.hierarchy = 1;
    config.species_layers["co"] = {l1, l2};

    StackingEngine engine(config);

    AcesImportState import_state;
    // Only PRESENT_FIELD is in the import state
    import_state.fields["PRESENT_FIELD"] = MakeDualView("PRESENT_FIELD", nx, ny, nz, 5.0);

    AcesExportState export_state;
    export_state.fields["co"] = MakeDualView("co", nx, ny, nz, 0.0);

    AcesStateResolver resolver(import_state, export_state, config.met_mapping,
                               config.scale_factor_mapping, config.mask_mapping);

    ASSERT_NO_THROW(engine.Execute(resolver, nx, ny, nz, {}, 0, 0));

    export_state.fields["co"].sync<Kokkos::HostSpace>();
    auto host_view = export_state.fields["co"].view_host();

    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                EXPECT_DOUBLE_EQ(host_view(i, j, k), 5.0)
                    << "Expected 5.0 (only present layer) at (" << i << "," << j << "," << k << ")";
            }
        }
    }
}

/**
 * @test Property 5 (RapidCheck): For any grid size in [1,8]^3, Execute with a
 * missing import field must complete without exception and produce a zero export.
 *
 * Validates: Requirements 5.3
 */
RC_GTEST_FIXTURE_PROP(NullSafeLayerSkipTest, NullFieldAlwaysProducesZeroExport, ()) {
    // Generate small grid dimensions to keep the test fast
    const int nx = *rc::gen::inRange(1, 5);
    const int ny = *rc::gen::inRange(1, 5);
    const int nz = *rc::gen::inRange(1, 3);

    AcesConfig config = MakeNullLayerConfig();
    StackingEngine engine(config);

    // Empty import state — MACCITY is absent
    AcesImportState import_state;

    AcesExportState export_state;
    export_state.fields["co"] = MakeDualView("co_pbt", nx, ny, nz, 99.0);

    AcesStateResolver resolver(import_state, export_state, config.met_mapping,
                               config.scale_factor_mapping, config.mask_mapping);

    // Must not throw
    engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

    export_state.fields["co"].sync<Kokkos::HostSpace>();
    auto host_view = export_state.fields["co"].view_host();

    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                RC_ASSERT(host_view(i, j, k) == 0.0);
            }
        }
    }
}

}  // namespace
}  // namespace aces
