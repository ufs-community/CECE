#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <cmath>
#include <map>

#include "cece/cece_compute.hpp"
#include "cece/cece_config.hpp"
#include "cece/cece_stacking_engine.hpp"

namespace cece {

/**
 * @brief FieldResolver implementation for vertical distribution testing.
 */
class VerticalDistributionFieldResolver : public FieldResolver {
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

    void SetValue(const std::string& name, int i, int j, int k, double val) {
        auto host = fields[name].view_host();
        host(i, j, k) = val;
        fields[name].modify<Kokkos::HostSpace>();
        fields[name].sync<Kokkos::DefaultExecutionSpace::memory_space>();
    }

    double GetValue(const std::string& name, int i, int j, int k) {
        fields[name].sync<Kokkos::HostSpace>();
        return fields[name].view_host()(i, j, k);
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
 * @brief Test suite for SINGLE vertical distribution method.
 * Execution Space: Default (OpenMP/Serial/CUDA)
 */
class VerticalDistributionSingleTest : public ::testing::Test {
   protected:
    void SetUp() override {
        if (!Kokkos::is_initialized()) Kokkos::initialize();
    }
};

/**
 * @test SINGLE: All emissions placed in specified layer.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * For a 2D emission field distributed to a single layer, the total column mass
 * after distribution SHALL equal the original 2D field value.
 */
TEST_F(VerticalDistributionSingleTest, SingleLayerPlacement) {
    int nx = 3, ny = 2, nz = 5;
    CeceConfig config;

    // Configure vertical distribution: SINGLE method, layer 2
    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.vdist_method = VerticalDistributionMethod::SINGLE;
    layer.vdist_layer_start = 2;  // Place in layer 2 (0-indexed)
    layer.vdist_layer_end = 2;

    config.species_layers["CO"] = {layer};

    VerticalDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);  // 2D field (nz=1)
    resolver.AddField("CO", nx, ny, nz);

    // Set 2D emissions to specific values
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            resolver.SetValue("emissions_2d", i, j, 0, 10.0 + i + j);
        }
    }

    StackingEngine engine(config);
    engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

    // Verify: emissions only in layer 2, all other layers are 0
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                double expected = (k == 2) ? (10.0 + i + j) : 0.0;
                EXPECT_NEAR(resolver.GetValue("CO", i, j, k), expected, 1e-9)
                    << "Mismatch at (" << i << "," << j << "," << k << ")";
            }
        }
    }
}

/**
 * @test SINGLE: Mass conservation with different layer indices.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * For any layer index, the sum of 3D emissions in that layer SHALL equal
 * the sum of the original 2D field.
 */
TEST_F(VerticalDistributionSingleTest, MassConservationDifferentLayers) {
    int nx = 4, ny = 3, nz = 10;
    CeceConfig config;

    // Test with layer 5
    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.vdist_method = VerticalDistributionMethod::SINGLE;
    layer.vdist_layer_start = 5;
    layer.vdist_layer_end = 5;

    config.species_layers["NOx"] = {layer};

    VerticalDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("NOx", nx, ny, nz);

    // Set 2D emissions to random values
    double total_2d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double val = 5.0 * (i + 1) * (j + 1);
            resolver.SetValue("emissions_2d", i, j, 0, val);
            total_2d += val;
        }
    }

    StackingEngine engine(config);
    engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

    // Verify mass conservation: sum of layer 5 equals total_2d
    double total_3d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            total_3d += resolver.GetValue("NOx", i, j, 5);
        }
    }

    EXPECT_NEAR(total_3d, total_2d, 1e-10 * total_2d);
}

/**
 * @test SINGLE: Boundary layer (layer 0).
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * SINGLE distribution to layer 0 (surface) SHALL conserve mass.
 */
TEST_F(VerticalDistributionSingleTest, SingleLayerBoundaryZero) {
    int nx = 2, ny = 2, nz = 8;
    CeceConfig config;

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.vdist_method = VerticalDistributionMethod::SINGLE;
    layer.vdist_layer_start = 0;
    layer.vdist_layer_end = 0;

    config.species_layers["ISOP"] = {layer};

    VerticalDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("ISOP", nx, ny, nz);

    double total_2d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double val = 2.5 * (i + j + 1);
            resolver.SetValue("emissions_2d", i, j, 0, val);
            total_2d += val;
        }
    }

    StackingEngine engine(config);
    engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

    // Verify all emissions in layer 0
    double total_3d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            total_3d += resolver.GetValue("ISOP", i, j, 0);
            // All other layers should be 0
            for (int k = 1; k < nz; ++k) {
                EXPECT_NEAR(resolver.GetValue("ISOP", i, j, k), 0.0, 1e-15);
            }
        }
    }

    EXPECT_NEAR(total_3d, total_2d, 1e-10 * total_2d);
}

/**
 * @test SINGLE: Top layer (layer nz-1).
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * SINGLE distribution to the top layer SHALL conserve mass.
 */
TEST_F(VerticalDistributionSingleTest, SingleLayerBoundaryTop) {
    int nx = 2, ny = 2, nz = 8;
    CeceConfig config;

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.vdist_method = VerticalDistributionMethod::SINGLE;
    layer.vdist_layer_start = nz - 1;
    layer.vdist_layer_end = nz - 1;

    config.species_layers["BC"] = {layer};

    VerticalDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("BC", nx, ny, nz);

    double total_2d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double val = 1.5 * (i + j + 1);
            resolver.SetValue("emissions_2d", i, j, 0, val);
            total_2d += val;
        }
    }

    StackingEngine engine(config);
    engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

    // Verify all emissions in top layer
    double total_3d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            total_3d += resolver.GetValue("BC", i, j, nz - 1);
            // All other layers should be 0
            for (int k = 0; k < nz - 1; ++k) {
                EXPECT_NEAR(resolver.GetValue("BC", i, j, k), 0.0, 1e-15);
            }
        }
    }

    EXPECT_NEAR(total_3d, total_2d, 1e-10 * total_2d);
}

/**
 * @test SINGLE: With scale factor.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * SINGLE distribution with a scale factor SHALL conserve mass after scaling.
 */
TEST_F(VerticalDistributionSingleTest, SingleWithScaleFactor) {
    int nx = 2, ny = 2, nz = 5;
    CeceConfig config;

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 2.0;  // Scale factor
    layer.vdist_method = VerticalDistributionMethod::SINGLE;
    layer.vdist_layer_start = 2;
    layer.vdist_layer_end = 2;

    config.species_layers["SO2"] = {layer};

    VerticalDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("SO2", nx, ny, nz);

    double total_2d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double val = 3.0 * (i + j + 1);
            resolver.SetValue("emissions_2d", i, j, 0, val);
            total_2d += val;
        }
    }

    StackingEngine engine(config);
    engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

    // Verify: emissions in layer 2, scaled by 2.0
    double total_3d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double expected = 2.0 * (3.0 * (i + j + 1));
            EXPECT_NEAR(resolver.GetValue("SO2", i, j, 2), expected, 1e-9);
            total_3d += resolver.GetValue("SO2", i, j, 2);
        }
    }

    EXPECT_NEAR(total_3d, 2.0 * total_2d, 1e-10 * total_2d);
}

/**
 * @test SINGLE: With spatial mask.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * SINGLE distribution with a spatial mask SHALL conserve mass in masked regions.
 */
TEST_F(VerticalDistributionSingleTest, SingleWithMask) {
    int nx = 3, ny = 3, nz = 4;
    CeceConfig config;

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.masks = {"region_mask"};
    layer.vdist_method = VerticalDistributionMethod::SINGLE;
    layer.vdist_layer_start = 1;
    layer.vdist_layer_end = 1;

    config.species_layers["OC"] = {layer};

    VerticalDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("region_mask", nx, ny, 1);
    resolver.AddField("OC", nx, ny, nz);

    // Set emissions and mask
    double total_masked = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double emis = 5.0 * (i + j + 1);
            resolver.SetValue("emissions_2d", i, j, 0, emis);

            // Mask: 1.0 for i+j < 3, 0.0 otherwise
            double mask = (i + j < 3) ? 1.0 : 0.0;
            resolver.SetValue("region_mask", i, j, 0, mask);

            if (mask > 0.5) {
                total_masked += emis;
            }
        }
    }

    StackingEngine engine(config);
    engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

    // Verify: emissions only where mask=1.0
    double total_3d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double mask = (i + j < 3) ? 1.0 : 0.0;
            double expected = mask * 5.0 * (i + j + 1);
            EXPECT_NEAR(resolver.GetValue("OC", i, j, 1), expected, 1e-9);
            total_3d += resolver.GetValue("OC", i, j, 1);
        }
    }

    EXPECT_NEAR(total_3d, total_masked, 1e-10 * total_masked);
}

/**
 * @test SINGLE: Multiple layers with hierarchy (replace operation).
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * When multiple SINGLE layers are stacked with replace operation, the final
 * column mass SHALL equal the highest-hierarchy layer's mass.
 */
TEST_F(VerticalDistributionSingleTest, SingleMultipleLayersReplace) {
    int nx = 2, ny = 2, nz = 5;
    CeceConfig config;

    // Layer 1: Add 10.0 to layer 2
    EmissionLayer layer1;
    layer1.operation = "add";
    layer1.field_name = "base_emissions";
    layer1.hierarchy = 0;
    layer1.scale = 1.0;
    layer1.vdist_method = VerticalDistributionMethod::SINGLE;
    layer1.vdist_layer_start = 2;
    layer1.vdist_layer_end = 2;

    // Layer 2: Replace with 5.0 in layer 2 (higher hierarchy)
    EmissionLayer layer2;
    layer2.operation = "replace";
    layer2.field_name = "override_emissions";
    layer2.hierarchy = 10;
    layer2.scale = 1.0;
    layer2.vdist_method = VerticalDistributionMethod::SINGLE;
    layer2.vdist_layer_start = 2;
    layer2.vdist_layer_end = 2;

    config.species_layers["PM25"] = {layer1, layer2};

    VerticalDistributionFieldResolver resolver;
    resolver.AddField("base_emissions", nx, ny, 1);
    resolver.SetValue("base_emissions", 10.0);
    resolver.AddField("override_emissions", nx, ny, 1);
    resolver.SetValue("override_emissions", 5.0);
    resolver.AddField("PM25", nx, ny, nz);

    StackingEngine engine(config);
    engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

    // Verify: layer 2 has 5.0 (replacement), all others are 0
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            EXPECT_NEAR(resolver.GetValue("PM25", i, j, 2), 5.0, 1e-9);
            for (int k = 0; k < nz; ++k) {
                if (k != 2) {
                    EXPECT_NEAR(resolver.GetValue("PM25", i, j, k), 0.0, 1e-15);
                }
            }
        }
    }
}

/**
 * @test SINGLE: Large grid mass conservation.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * For a large grid (360x180x72), SINGLE distribution SHALL conserve mass
 * within 1e-10 relative error.
 */
TEST_F(VerticalDistributionSingleTest, LargeGridMassConservation) {
    int nx = 360, ny = 180, nz = 72;
    CeceConfig config;

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.vdist_method = VerticalDistributionMethod::SINGLE;
    layer.vdist_layer_start = 35;
    layer.vdist_layer_end = 35;

    config.species_layers["CO2"] = {layer};

    VerticalDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("CO2", nx, ny, nz);

    // Set 2D emissions to random values
    double total_2d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double val = 0.1 * (i + j + 1);
            resolver.SetValue("emissions_2d", i, j, 0, val);
            total_2d += val;
        }
    }

    StackingEngine engine(config);
    engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

    // Verify mass conservation
    double total_3d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            total_3d += resolver.GetValue("CO2", i, j, 35);
        }
    }

    double rel_error = std::abs(total_3d - total_2d) / total_2d;
    EXPECT_LT(rel_error, 1e-10);
}

}  // namespace cece
