#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <cmath>
#include <map>

#include "aces/aces_compute.hpp"
#include "aces/aces_config.hpp"
#include "aces/aces_stacking_engine.hpp"

namespace aces {

/**
 * @brief FieldResolver implementation for vertical distribution testing.
 */
class RangeDistributionFieldResolver : public FieldResolver {
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
 * @brief Test suite for RANGE vertical distribution method.
 * Execution Space: Default (OpenMP/Serial/CUDA)
 */
class VerticalDistributionRangeTest : public ::testing::Test {
   protected:
    void SetUp() override {
        if (!Kokkos::is_initialized()) Kokkos::initialize();
    }
};

/**
 * @test RANGE: Emissions distributed evenly over layer range.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * For a 2D emission field distributed over a range of layers, each layer
 * receives an equal fraction of the total emission, and the sum equals the
 * original 2D field.
 */
TEST_F(VerticalDistributionRangeTest, EvenDistributionOverRange) {
    int nx = 2, ny = 2, nz = 10;
    AcesConfig config;

    // Distribute over layers 2-4 (3 layers)
    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.vdist_method = VerticalDistributionMethod::RANGE;
    layer.vdist_layer_start = 2;
    layer.vdist_layer_end = 4;

    config.species_layers["CO"] = {layer};

    RangeDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("CO", nx, ny, nz);

    // Set 2D emissions
    double total_2d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double val = 10.0 + i + j;
            resolver.SetValue("emissions_2d", i, j, 0, val);
            total_2d += val;
        }
    }

    StackingEngine engine(config);
    engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

    // Verify: each layer gets 1/3 of the emission
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double expected_per_layer = (10.0 + i + j) / 3.0;
            for (int k = 2; k <= 4; ++k) {
                EXPECT_NEAR(resolver.GetValue("CO", i, j, k), expected_per_layer, 1e-9)
                    << "Mismatch at (" << i << "," << j << "," << k << ")";
            }
            // Other layers should be 0
            for (int k = 0; k < 2; ++k) {
                EXPECT_NEAR(resolver.GetValue("CO", i, j, k), 0.0, 1e-15);
            }
            for (int k = 5; k < nz; ++k) {
                EXPECT_NEAR(resolver.GetValue("CO", i, j, k), 0.0, 1e-15);
            }
        }
    }

    // Verify mass conservation
    double total_3d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                total_3d += resolver.GetValue("CO", i, j, k);
            }
        }
    }

    EXPECT_NEAR(total_3d, total_2d, 1e-10 * total_2d);
}

/**
 * @test RANGE: Single layer range (start == end).
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * When start == end, RANGE should behave like SINGLE.
 */
TEST_F(VerticalDistributionRangeTest, RangeSingleLayer) {
    int nx = 2, ny = 2, nz = 8;
    AcesConfig config;

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.vdist_method = VerticalDistributionMethod::RANGE;
    layer.vdist_layer_start = 3;
    layer.vdist_layer_end = 3;  // Same as start

    config.species_layers["NOx"] = {layer};

    RangeDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("NOx", nx, ny, nz);

    double total_2d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double val = 5.0 * (i + j + 1);
            resolver.SetValue("emissions_2d", i, j, 0, val);
            total_2d += val;
        }
    }

    StackingEngine engine(config);
    engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

    // Verify all emissions in layer 3
    double total_3d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double expected = 5.0 * (i + j + 1);
            EXPECT_NEAR(resolver.GetValue("NOx", i, j, 3), expected, 1e-9);
            total_3d += resolver.GetValue("NOx", i, j, 3);
        }
    }

    EXPECT_NEAR(total_3d, total_2d, 1e-10 * total_2d);
}

/**
 * @test RANGE: Full vertical column.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * When range covers all layers, each layer gets 1/nz of the emission.
 */
TEST_F(VerticalDistributionRangeTest, RangeFullColumn) {
    int nx = 2, ny = 2, nz = 5;
    AcesConfig config;

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.vdist_method = VerticalDistributionMethod::RANGE;
    layer.vdist_layer_start = 0;
    layer.vdist_layer_end = nz - 1;

    config.species_layers["ISOP"] = {layer};

    RangeDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("ISOP", nx, ny, nz);

    double total_2d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double val = 2.0 * (i + j + 1);
            resolver.SetValue("emissions_2d", i, j, 0, val);
            total_2d += val;
        }
    }

    StackingEngine engine(config);
    engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

    // Verify: each layer gets 1/5 of the emission
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double expected = (2.0 * (i + j + 1)) / nz;
            for (int k = 0; k < nz; ++k) {
                EXPECT_NEAR(resolver.GetValue("ISOP", i, j, k), expected, 1e-9);
            }
        }
    }

    // Verify mass conservation
    double total_3d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                total_3d += resolver.GetValue("ISOP", i, j, k);
            }
        }
    }

    EXPECT_NEAR(total_3d, total_2d, 1e-10 * total_2d);
}

/**
 * @test RANGE: With scale factor.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * RANGE distribution with a scale factor SHALL conserve scaled mass.
 */
TEST_F(VerticalDistributionRangeTest, RangeWithScaleFactor) {
    int nx = 2, ny = 2, nz = 6;
    AcesConfig config;

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 0.5;  // Scale factor
    layer.vdist_method = VerticalDistributionMethod::RANGE;
    layer.vdist_layer_start = 1;
    layer.vdist_layer_end = 3;

    config.species_layers["SO2"] = {layer};

    RangeDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("SO2", nx, ny, nz);

    double total_2d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double val = 4.0 * (i + j + 1);
            resolver.SetValue("emissions_2d", i, j, 0, val);
            total_2d += val;
        }
    }

    StackingEngine engine(config);
    engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

    // Verify: each layer gets 0.5 * (1/3) of the emission
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double expected = 0.5 * (4.0 * (i + j + 1)) / 3.0;
            for (int k = 1; k <= 3; ++k) {
                EXPECT_NEAR(resolver.GetValue("SO2", i, j, k), expected, 1e-9);
            }
        }
    }

    // Verify mass conservation
    double total_3d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                total_3d += resolver.GetValue("SO2", i, j, k);
            }
        }
    }

    EXPECT_NEAR(total_3d, 0.5 * total_2d, 1e-10 * total_2d);
}

/**
 * @test RANGE: With spatial mask.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * RANGE distribution with a spatial mask SHALL conserve mass in masked regions.
 */
TEST_F(VerticalDistributionRangeTest, RangeWithMask) {
    int nx = 3, ny = 3, nz = 4;
    AcesConfig config;

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.masks = {"region_mask"};
    layer.vdist_method = VerticalDistributionMethod::RANGE;
    layer.vdist_layer_start = 1;
    layer.vdist_layer_end = 2;

    config.species_layers["OC"] = {layer};

    RangeDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("region_mask", nx, ny, 1);
    resolver.AddField("OC", nx, ny, nz);

    // Set emissions and mask
    double total_masked = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double emis = 3.0 * (i + j + 1);
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

    // Verify: emissions distributed over layers 1-2 where mask=1.0
    double total_3d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double mask = (i + j < 3) ? 1.0 : 0.0;
            double expected = mask * (3.0 * (i + j + 1)) / 2.0;
            for (int k = 1; k <= 2; ++k) {
                EXPECT_NEAR(resolver.GetValue("OC", i, j, k), expected, 1e-9);
                total_3d += resolver.GetValue("OC", i, j, k);
            }
        }
    }

    EXPECT_NEAR(total_3d, total_masked, 1e-10 * total_masked);
}

/**
 * @test RANGE: Multiple layers with hierarchy (replace operation).
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * When multiple RANGE layers are stacked with replace operation, the final
 * column mass SHALL equal the highest-hierarchy layer's mass.
 */
TEST_F(VerticalDistributionRangeTest, RangeMultipleLayersReplace) {
    int nx = 2, ny = 2, nz = 5;
    AcesConfig config;

    // Layer 1: Add to layers 1-3
    EmissionLayer layer1;
    layer1.operation = "add";
    layer1.field_name = "base_emissions";
    layer1.hierarchy = 0;
    layer1.scale = 1.0;
    layer1.vdist_method = VerticalDistributionMethod::RANGE;
    layer1.vdist_layer_start = 1;
    layer1.vdist_layer_end = 3;

    // Layer 2: Replace layers 1-3 (higher hierarchy)
    EmissionLayer layer2;
    layer2.operation = "replace";
    layer2.field_name = "override_emissions";
    layer2.hierarchy = 10;
    layer2.scale = 1.0;
    layer2.vdist_method = VerticalDistributionMethod::RANGE;
    layer2.vdist_layer_start = 1;
    layer2.vdist_layer_end = 3;

    config.species_layers["PM25"] = {layer1, layer2};

    RangeDistributionFieldResolver resolver;
    resolver.AddField("base_emissions", nx, ny, 1);
    resolver.SetValue("base_emissions", 10.0);
    resolver.AddField("override_emissions", nx, ny, 1);
    resolver.SetValue("override_emissions", 5.0);
    resolver.AddField("PM25", nx, ny, nz);

    StackingEngine engine(config);
    engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

    // Verify: layers 1-3 have 5.0/3 each (replacement), all others are 0
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double expected = 5.0 / 3.0;
            for (int k = 1; k <= 3; ++k) {
                EXPECT_NEAR(resolver.GetValue("PM25", i, j, k), expected, 1e-9);
            }
            // Other layers should be 0
            EXPECT_NEAR(resolver.GetValue("PM25", i, j, 0), 0.0, 1e-15);
            EXPECT_NEAR(resolver.GetValue("PM25", i, j, 4), 0.0, 1e-15);
        }
    }
}

/**
 * @test RANGE: Large grid mass conservation.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * For a large grid (360x180x72), RANGE distribution SHALL conserve mass
 * within 1e-10 relative error.
 */
TEST_F(VerticalDistributionRangeTest, LargeGridMassConservation) {
    int nx = 360, ny = 180, nz = 72;
    AcesConfig config;

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.vdist_method = VerticalDistributionMethod::RANGE;
    layer.vdist_layer_start = 10;
    layer.vdist_layer_end = 40;

    config.species_layers["CO2"] = {layer};

    RangeDistributionFieldResolver resolver;
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
            for (int k = 10; k <= 40; ++k) {
                total_3d += resolver.GetValue("CO2", i, j, k);
            }
        }
    }

    double rel_error = std::abs(total_3d - total_2d) / total_2d;
    EXPECT_LT(rel_error, 1e-10);
}

/**
 * @test RANGE: Boundary layers (0 to nz-1).
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * RANGE distribution from layer 0 to nz-1 SHALL conserve mass.
 */
TEST_F(VerticalDistributionRangeTest, RangeBoundaryLayers) {
    int nx = 2, ny = 2, nz = 8;
    AcesConfig config;

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.vdist_method = VerticalDistributionMethod::RANGE;
    layer.vdist_layer_start = 0;
    layer.vdist_layer_end = nz - 1;

    config.species_layers["BC"] = {layer};

    RangeDistributionFieldResolver resolver;
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

    // Verify: each layer gets 1/nz of the emission
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double expected = (1.5 * (i + j + 1)) / nz;
            for (int k = 0; k < nz; ++k) {
                EXPECT_NEAR(resolver.GetValue("BC", i, j, k), expected, 1e-9);
            }
        }
    }

    // Verify mass conservation
    double total_3d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                total_3d += resolver.GetValue("BC", i, j, k);
            }
        }
    }

    EXPECT_NEAR(total_3d, total_2d, 1e-10 * total_2d);
}

}  // namespace aces
