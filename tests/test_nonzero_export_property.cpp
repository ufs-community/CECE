/**
 * @file test_nonzero_export_property.cpp
 * @brief Property 4: Nonzero export from nonzero import.
 *
 * For any AcesImportState where the base emission field contains at least one
 * nonzero value, after StackingEngine::Execute the export field shall contain
 * at least one nonzero value.
 *
 * Uses real ESMF objects (no mocking) per the ACES no-mock policy.
 *
 * Validates: Requirements 4.1
 */

#include <ESMC.h>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include <Kokkos_Core.hpp>
#include <cmath>

#include "aces/aces_compute.hpp"
#include "aces/aces_config.hpp"
#include "aces/aces_stacking_engine.hpp"
#include "aces/aces_state.hpp"

namespace aces {
namespace {

// ---------------------------------------------------------------------------
// Global ESMF + Kokkos environment
// ---------------------------------------------------------------------------
class ESMFEnvironment : public ::testing::Environment {
   public:
    void SetUp() override {
        if (!Kokkos::is_initialized()) Kokkos::initialize();
        int rc = ESMC_Initialize(nullptr, ESMC_ArgLast);
        ASSERT_EQ(rc, ESMF_SUCCESS) << "ESMF init failed";
    }
    void TearDown() override { ESMC_Finalize(); }
};

// ---------------------------------------------------------------------------
// Helper: build a minimal AcesConfig with one species / one "add" layer
// ---------------------------------------------------------------------------
AcesConfig MakeSingleSpeciesConfig(const std::string& species,
                                   const std::string& field_name) {
    AcesConfig cfg;
    EmissionLayer layer;
    layer.operation  = "add";
    layer.field_name = field_name;
    layer.scale      = 1.0;
    layer.hierarchy  = 0;
    cfg.species_layers[species] = {layer};
    return cfg;
}

// ---------------------------------------------------------------------------
// Helper: allocate a DualView3D filled with a constant value
// ---------------------------------------------------------------------------
DualView3D MakeField(int nx, int ny, int nz, double fill) {
    DualView3D dv("field", nx, ny, nz);
    auto h = dv.view_host();
    for (int i = 0; i < nx; ++i)
        for (int j = 0; j < ny; ++j)
            for (int k = 0; k < nz; ++k)
                h(i, j, k) = fill;
    dv.sync_device();
    return dv;
}

// ---------------------------------------------------------------------------
// Property 4a: single nonzero import cell → export has at least one nonzero
// ---------------------------------------------------------------------------
TEST(NonzeroExportProperty, SingleNonzeroImportProducesNonzeroExport) {
    const int nx = 4, ny = 4, nz = 2;
    const std::string species   = "CO";
    const std::string field_name = "CO_base";

    auto cfg = MakeSingleSpeciesConfig(species, field_name);
    StackingEngine engine(cfg);

    // Import: all zeros except one cell
    AcesImportState import_state;
    import_state.fields[field_name] = MakeField(nx, ny, nz, 0.0);
    {
        auto h = import_state.fields[field_name].view_host();
        h(1, 1, 0) = 42.0;
        import_state.fields[field_name].sync_device();
    }

    // Export: pre-allocate
    AcesExportState export_state;
    export_state.fields[species] = MakeField(nx, ny, nz, 0.0);

    // Default mask (all 1.0)
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>
        mask("mask", nx, ny, nz);
    Kokkos::deep_copy(mask, 1.0);

    AcesStateResolver resolver(import_state, export_state,
                               cfg.met_mapping,
                               cfg.scale_factor_mapping,
                               cfg.mask_mapping);

    engine.Execute(resolver, nx, ny, nz, mask, /*hour=*/0, /*dow=*/0);

    // Sync export to host and check for nonzero
    export_state.fields[species].sync_host();
    auto h = export_state.fields[species].view_host();

    double max_val = 0.0;
    for (int i = 0; i < nx; ++i)
        for (int j = 0; j < ny; ++j)
            for (int k = 0; k < nz; ++k)
                max_val = std::max(max_val, std::abs(h(i, j, k)));

    EXPECT_GT(max_val, 0.0)
        << "Export field must contain at least one nonzero value when import is nonzero";
}

// ---------------------------------------------------------------------------
// Property 4b (PBT): for any uniform nonzero fill value, export is nonzero
// ---------------------------------------------------------------------------
RC_GTEST_PROP(NonzeroExportProperty, UniformNonzeroImportProducesNonzeroExport, ()) {
    const int nx = *rc::gen::inRange(1, 5);
    const int ny = *rc::gen::inRange(1, 5);
    const int nz = *rc::gen::inRange(1, 4);
    // fill ∈ (0, 1000], never zero
    const double fill = *rc::gen::map(rc::gen::inRange(1, 1001),
                                      [](int v) { return static_cast<double>(v); });

    const std::string species    = "CO";
    const std::string field_name = "CO_base";

    auto cfg = MakeSingleSpeciesConfig(species, field_name);
    StackingEngine engine(cfg);

    AcesImportState import_state;
    import_state.fields[field_name] = MakeField(nx, ny, nz, fill);

    AcesExportState export_state;
    export_state.fields[species] = MakeField(nx, ny, nz, 0.0);

    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>
        mask("mask", nx, ny, nz);
    Kokkos::deep_copy(mask, 1.0);

    AcesStateResolver resolver(import_state, export_state,
                               cfg.met_mapping,
                               cfg.scale_factor_mapping,
                               cfg.mask_mapping);

    engine.Execute(resolver, nx, ny, nz, mask, 0, 0);

    export_state.fields[species].sync_host();
    auto h = export_state.fields[species].view_host();

    double max_val = 0.0;
    for (int i = 0; i < nx; ++i)
        for (int j = 0; j < ny; ++j)
            for (int k = 0; k < nz; ++k)
                max_val = std::max(max_val, std::abs(h(i, j, k)));

    RC_ASSERT(max_val > 0.0);
}

// ---------------------------------------------------------------------------
// Property 4c: all-zero import → export remains zero
// ---------------------------------------------------------------------------
TEST(NonzeroExportProperty, AllZeroImportProducesZeroExport) {
    const int nx = 4, ny = 4, nz = 2;
    const std::string species    = "CO";
    const std::string field_name = "CO_base";

    auto cfg = MakeSingleSpeciesConfig(species, field_name);
    StackingEngine engine(cfg);

    AcesImportState import_state;
    import_state.fields[field_name] = MakeField(nx, ny, nz, 0.0);

    AcesExportState export_state;
    export_state.fields[species] = MakeField(nx, ny, nz, 0.0);

    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>
        mask("mask", nx, ny, nz);
    Kokkos::deep_copy(mask, 1.0);

    AcesStateResolver resolver(import_state, export_state,
                               cfg.met_mapping,
                               cfg.scale_factor_mapping,
                               cfg.mask_mapping);

    engine.Execute(resolver, nx, ny, nz, mask, 0, 0);

    export_state.fields[species].sync_host();
    auto h = export_state.fields[species].view_host();

    double max_val = 0.0;
    for (int i = 0; i < nx; ++i)
        for (int j = 0; j < ny; ++j)
            for (int k = 0; k < nz; ++k)
                max_val = std::max(max_val, std::abs(h(i, j, k)));

    EXPECT_DOUBLE_EQ(max_val, 0.0)
        << "All-zero import must produce all-zero export";
}

}  // namespace
}  // namespace aces

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new aces::ESMFEnvironment);
    return RUN_ALL_TESTS();
}
