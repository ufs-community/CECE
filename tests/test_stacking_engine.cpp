#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <map>

#include "cece/cece_compute.hpp"
#include "cece/cece_config.hpp"
#include "cece/cece_stacking_engine.hpp"

namespace cece {

/**
 * @brief FieldResolver implementation that works with actual Kokkos DualViews.
 */
class ActualFieldResolver : public FieldResolver {
    std::map<std::string, DualView3D> fields;

   public:
    void AddField(const std::string& name, int nx, int ny, int nz) {
        fields[name] = DualView3D("test_" + name, nx, ny, nz);
    }

    void SetValue(const std::string& name, double val) {
        auto host = fields[name].view_host();
        Kokkos::deep_copy(host, val);
        fields[name].modify<Kokkos::HostSpace>();
        fields[name].sync<Kokkos::DefaultExecutionSpace::memory_space>();
    }

    double GetValue(const std::string& name) {
        fields[name].sync<Kokkos::HostSpace>();
        return fields[name].view_host()(0, 0, 0);
    }

    UnmanagedHostView3D ResolveImport(const std::string& name, int, int, int) override {
        return fields[name].view_host();
    }
    UnmanagedHostView3D ResolveExport(const std::string& name, int, int, int) override {
        return fields[name].view_host();
    }
    Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>
    ResolveImportDevice(const std::string& name, int, int, int) override {
        return fields[name].view_device();
    }
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> ResolveExportDevice(
        const std::string& name, int, int, int) override {
        return fields[name].view_device();
    }
};

/**
 * @brief Proof: Validation of the Fused StackingEngine.
 * Execution Space: Default (OpenMP/Serial/CUDA)
 */
class StackingEngineTest : public ::testing::Test {
   protected:
    void SetUp() override {
        if (!Kokkos::is_initialized()) Kokkos::initialize();
    }
};

/**
 * @test Verify that hierarchy-based replacement works correctly in the Fused StackingEngine.
 */
TEST_F(StackingEngineTest, HierarchyReplacement) {
    int nx = 1, ny = 1, nz = 1;
    CeceConfig config;

    // Layer 1: Add 10.0 (Hierarchy 1)
    EmissionLayer l1;
    l1.operation = "add";
    l1.field_name = "f1";
    l1.hierarchy = 1;
    l1.scale = 1.0;

    // Layer 2: Replace with 5.0 (Hierarchy 10)
    EmissionLayer l2;
    l2.operation = "replace";
    l2.field_name = "f2";
    l2.hierarchy = 10;
    l2.scale = 1.0;

    config.species_layers["test_species"] = {l2, l1};  // Intentionally out of order

    ActualFieldResolver resolver;
    resolver.AddField("f1", nx, ny, nz);
    resolver.SetValue("f1", 10.0);
    resolver.AddField("f2", nx, ny, nz);
    resolver.SetValue("f2", 5.0);
    resolver.AddField("test_species", nx, ny, nz);
    resolver.SetValue("test_species", 0.0);

    StackingEngine engine(config);
    engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

    EXPECT_NEAR(resolver.GetValue("test_species"), 5.0, 1e-9);
}

/**
 * @test Verify that default_mask is correctly applied when no specific masks are provided.
 */
TEST_F(StackingEngineTest, DefaultMaskApplication) {
    int nx = 1, ny = 1, nz = 1;
    CeceConfig config;

    EmissionLayer l1;
    l1.operation = "add";
    l1.field_name = "f1";
    l1.scale = 1.0;

    config.species_layers["test_species"] = {l1};

    ActualFieldResolver resolver;
    resolver.AddField("f1", nx, ny, nz);
    resolver.SetValue("f1", 10.0);
    resolver.AddField("test_species", nx, ny, nz);
    resolver.SetValue("test_species", 0.0);

    // Provide a default mask of 0.5
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> dmask("dmask", nx,
                                                                                     ny, nz);
    Kokkos::deep_copy(dmask, 0.5);

    StackingEngine engine(config);
    engine.Execute(resolver, nx, ny, nz, dmask, 0, 0);

    // Should be 10.0 * 0.5 = 5.0
    EXPECT_NEAR(resolver.GetValue("test_species"), 5.0, 1e-9);
}

/**
 * @test Proof of complex fusion logic: Multiple layers, scales, and masks.
 */
TEST_F(StackingEngineTest, ComplexFusionLogic) {
    int nx = 1, ny = 1, nz = 1;
    CeceConfig config;

    // Layer 1: Base (Add 10)
    EmissionLayer l1;
    l1.operation = "add";
    l1.field_name = "base";
    l1.hierarchy = 0;
    l1.scale = 1.0;

    // Layer 2: Scaled (Add 2 * 3 = 6)
    EmissionLayer l2;
    l2.operation = "add";
    l2.field_name = "scaled_field";
    l2.scale_fields = {"multiplier"};
    l2.hierarchy = 1;
    l1.scale = 1.0;

    // Layer 3: Conditional Replacement (Replace with 5 if mask=1.0)
    EmissionLayer l3;
    l3.operation = "replace";
    l3.field_name = "replacement";
    l3.masks = {"region_mask"};
    l3.hierarchy = 10;
    l3.scale = 1.0;

    config.species_layers["complex_species"] = {l1, l2, l3};

    ActualFieldResolver resolver;
    resolver.AddField("base", nx, ny, nz);
    resolver.SetValue("base", 10.0);
    resolver.AddField("scaled_field", nx, ny, nz);
    resolver.SetValue("scaled_field", 2.0);
    resolver.AddField("multiplier", nx, ny, nz);
    resolver.SetValue("multiplier", 3.0);
    resolver.AddField("replacement", nx, ny, nz);
    resolver.SetValue("replacement", 5.0);
    resolver.AddField("region_mask", nx, ny, nz);
    resolver.SetValue("region_mask", 1.0);
    resolver.AddField("complex_species", nx, ny, nz);
    resolver.SetValue("complex_species", 0.0);

    StackingEngine engine(config);
    engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

    // Expected: 5.0 (Full replacement)
    EXPECT_NEAR(resolver.GetValue("complex_species"), 5.0, 1e-9);

    // Partial/No replacement test
    resolver.SetValue("region_mask", 0.0);
    engine.Execute(resolver, nx, ny, nz, {}, 0, 0);
    // Expected: 10 + 2*3 = 16.0
    EXPECT_NEAR(resolver.GetValue("complex_species"), 16.0, 1e-9);
}

}  // namespace cece
