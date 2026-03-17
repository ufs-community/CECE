#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <cmath>
#include <map>

#include "aces/aces_compute.hpp"
#include "aces/aces_config.hpp"
#include "aces/aces_stacking_engine.hpp"

namespace aces {

/**
 * @brief FieldResolver implementation for height-based vertical distribution testing.
 */
class HeightDistributionFieldResolver : public FieldResolver {
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
 * @brief Test suite for HEIGHT vertical distribution method.
 * Execution Space: Default (OpenMP/Serial/CUDA)
 */
class VerticalDistributionHeightTest : public ::testing::Test {
   protected:
    void SetUp() override {
        if (!Kokkos::is_initialized()) Kokkos::initialize();
    }
};

/**
 * @test HEIGHT: Emissions distributed based on height overlap.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * For a 2D emission field distributed over a height range, the total column
 * mass after distribution SHALL equal the original 2D field value.
 */
TEST_F(VerticalDistributionHeightTest, HeightOverlapDistribution) {
    int nx = 2, ny = 2, nz = 5;
    AcesConfig config;

    // Configure vertical coordinate system (MPAS/WRF-style)
    config.vertical_config.type = VerticalCoordType::MPAS;
    config.vertical_config.z_field = "height";

    // Distribute over height range 1000-3000 m
    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.vdist_method = VerticalDistributionMethod::HEIGHT;
    layer.vdist_h_start = 1000.0;
    layer.vdist_h_end = 3000.0;

    config.species_layers["CO"] = {layer};

    HeightDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("CO", nx, ny, nz);
    resolver.AddField("height", nx, ny, nz);

    // Set up height field (m)
    double heights[] = {0.0, 500.0, 1500.0, 2500.0, 4000.0};
    for (int k = 0; k < nz; ++k) {
        resolver.SetValue("height", 0, 0, k, heights[k]);
        resolver.SetValue("height", 0, 1, k, heights[k]);
        resolver.SetValue("height", 1, 0, k, heights[k]);
        resolver.SetValue("height", 1, 1, k, heights[k]);
    }

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
 * @test HEIGHT: Full height column.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * When height range covers the full column, all emissions are distributed.
 */
TEST_F(VerticalDistributionHeightTest, HeightFullColumn) {
    int nx = 2, ny = 2, nz = 4;
    AcesConfig config;

    config.vertical_config.type = VerticalCoordType::MPAS;
    config.vertical_config.z_field = "height";

    // Distribute over full height range
    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.vdist_method = VerticalDistributionMethod::HEIGHT;
    layer.vdist_h_start = 0.0;
    layer.vdist_h_end = 10000.0;

    config.species_layers["NOx"] = {layer};

    HeightDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("NOx", nx, ny, nz);
    resolver.AddField("height", nx, ny, nz);

    // Set up height field
    double heights[] = {0.0, 2000.0, 5000.0, 8000.0};
    for (int k = 0; k < nz; ++k) {
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                resolver.SetValue("height", i, j, k, heights[k]);
            }
        }
    }

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

    // Verify mass conservation
    double total_3d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                total_3d += resolver.GetValue("NOx", i, j, k);
            }
        }
    }

    EXPECT_NEAR(total_3d, total_2d, 1e-10 * total_2d);
}

/**
 * @test HEIGHT: Single layer height range.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * When height range covers a single layer, all emissions go to that layer.
 */
TEST_F(VerticalDistributionHeightTest, HeightSingleLayer) {
    int nx = 2, ny = 2, nz = 5;
    AcesConfig config;

    config.vertical_config.type = VerticalCoordType::MPAS;
    config.vertical_config.z_field = "height";

    // Distribute over layer 2 height range (1500-2500 m)
    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.vdist_method = VerticalDistributionMethod::HEIGHT;
    layer.vdist_h_start = 1500.0;
    layer.vdist_h_end = 2500.0;

    config.species_layers["ISOP"] = {layer};

    HeightDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("ISOP", nx, ny, nz);
    resolver.AddField("height", nx, ny, nz);

    // Set up height field
    double heights[] = {0.0, 500.0, 1500.0, 2500.0, 4000.0};
    for (int k = 0; k < nz; ++k) {
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                resolver.SetValue("height", i, j, k, heights[k]);
            }
        }
    }

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
 * @test HEIGHT: With scale factor.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * HEIGHT distribution with a scale factor SHALL conserve scaled mass.
 */
TEST_F(VerticalDistributionHeightTest, HeightWithScaleFactor) {
    int nx = 2, ny = 2, nz = 4;
    AcesConfig config;

    config.vertical_config.type = VerticalCoordType::MPAS;
    config.vertical_config.z_field = "height";

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 2.0;  // Scale factor
    layer.vdist_method = VerticalDistributionMethod::HEIGHT;
    layer.vdist_h_start = 500.0;
    layer.vdist_h_end = 5000.0;

    config.species_layers["SO2"] = {layer};

    HeightDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("SO2", nx, ny, nz);
    resolver.AddField("height", nx, ny, nz);

    double heights[] = {0.0, 1000.0, 2500.0, 5000.0};
    for (int k = 0; k < nz; ++k) {
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                resolver.SetValue("height", i, j, k, heights[k]);
            }
        }
    }

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

    // Verify mass conservation with scale factor
    double total_3d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                total_3d += resolver.GetValue("SO2", i, j, k);
            }
        }
    }

    EXPECT_NEAR(total_3d, 2.0 * total_2d, 1e-10 * total_2d);
}

/**
 * @test HEIGHT: With spatial mask.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * HEIGHT distribution with a spatial mask SHALL conserve mass in masked regions.
 */
TEST_F(VerticalDistributionHeightTest, HeightWithMask) {
    int nx = 3, ny = 3, nz = 4;
    AcesConfig config;

    config.vertical_config.type = VerticalCoordType::MPAS;
    config.vertical_config.z_field = "height";

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.masks = {"region_mask"};
    layer.vdist_method = VerticalDistributionMethod::HEIGHT;
    layer.vdist_h_start = 500.0;
    layer.vdist_h_end = 3000.0;

    config.species_layers["OC"] = {layer};

    HeightDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("region_mask", nx, ny, 1);
    resolver.AddField("OC", nx, ny, nz);
    resolver.AddField("height", nx, ny, nz);

    double heights[] = {0.0, 1000.0, 2000.0, 4000.0};
    for (int k = 0; k < nz; ++k) {
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                resolver.SetValue("height", i, j, k, heights[k]);
            }
        }
    }

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

    // Verify mass conservation in masked regions
    double total_3d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                total_3d += resolver.GetValue("OC", i, j, k);
            }
        }
    }

    EXPECT_NEAR(total_3d, total_masked, 1e-10 * total_masked);
}

/**
 * @test HEIGHT: Multiple layers with hierarchy (replace operation).
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * When multiple HEIGHT layers are stacked with replace operation, the final
 * column mass SHALL equal the highest-hierarchy layer's mass.
 */
TEST_F(VerticalDistributionHeightTest, HeightMultipleLayersReplace) {
    int nx = 2, ny = 2, nz = 4;
    AcesConfig config;

    config.vertical_config.type = VerticalCoordType::MPAS;
    config.vertical_config.z_field = "height";

    // Layer 1: Add to height range 500-3000 m
    EmissionLayer layer1;
    layer1.operation = "add";
    layer1.field_name = "base_emissions";
    layer1.hierarchy = 0;
    layer1.scale = 1.0;
    layer1.vdist_method = VerticalDistributionMethod::HEIGHT;
    layer1.vdist_h_start = 500.0;
    layer1.vdist_h_end = 3000.0;

    // Layer 2: Replace in same height range (higher hierarchy)
    EmissionLayer layer2;
    layer2.operation = "replace";
    layer2.field_name = "override_emissions";
    layer2.hierarchy = 10;
    layer2.scale = 1.0;
    layer2.vdist_method = VerticalDistributionMethod::HEIGHT;
    layer2.vdist_h_start = 500.0;
    layer2.vdist_h_end = 3000.0;

    config.species_layers["PM25"] = {layer1, layer2};

    HeightDistributionFieldResolver resolver;
    resolver.AddField("base_emissions", nx, ny, 1);
    resolver.SetValue("base_emissions", 10.0);
    resolver.AddField("override_emissions", nx, ny, 1);
    resolver.SetValue("override_emissions", 5.0);
    resolver.AddField("PM25", nx, ny, nz);
    resolver.AddField("height", nx, ny, nz);

    double heights[] = {0.0, 1000.0, 2000.0, 4000.0};
    for (int k = 0; k < nz; ++k) {
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                resolver.SetValue("height", i, j, k, heights[k]);
            }
        }
    }

    StackingEngine engine(config);
    engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

    // Verify: replacement occurred (5.0 total, not 10.0)
    // nx=2, ny=2 -> 4 grid points. Each should have 5.0 total column emissions.
    // Total 3D should be 4 * 5.0 = 20.0.
    double expected_total = static_cast<double>(nx * ny) * 5.0;
    double total_3d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                total_3d += resolver.GetValue("PM25", i, j, k);
            }
        }
    }

    EXPECT_NEAR(total_3d, expected_total, 1e-10 * expected_total);
}

/**
 * @test HEIGHT: Large grid mass conservation.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * For a large grid (360x180x72), HEIGHT distribution SHALL conserve mass
 * within 1e-10 relative error.
 */
TEST_F(VerticalDistributionHeightTest, LargeGridMassConservation) {
    int nx = 360, ny = 180, nz = 72;
    AcesConfig config;

    config.vertical_config.type = VerticalCoordType::MPAS;
    config.vertical_config.z_field = "height";

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.vdist_method = VerticalDistributionMethod::HEIGHT;
    layer.vdist_h_start = 1000.0;
    layer.vdist_h_end = 5000.0;

    config.species_layers["CO2"] = {layer};

    HeightDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("CO2", nx, ny, nz);
    resolver.AddField("height", nx, ny, nz + 1);

    // Set up realistic height field (0 at bottom, increasing upwards)
    // k=0 is top, k=nz is bottom.
    // k=0: 10000m, k=nz: 0m.
    for (int k = 0; k <= nz; ++k) {
        double z = 10000.0 * (1.0 - static_cast<double>(k) / nz);
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                resolver.SetValue("height", i, j, k, z);
            }
        }
    }

    // Set 2D emissions
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
            for (int k = 0; k < nz; ++k) {
                total_3d += resolver.GetValue("CO2", i, j, k);
            }
        }
    }

    double rel_error = std::abs(total_3d - total_2d) / total_2d;
    if (rel_error >= 1e-10) {
        printf("DEBUG: total_2d = %e, total_3d = %e, rel_error = %e\n", total_2d, total_3d,
               rel_error);
    }
    EXPECT_LT(rel_error, 1e-10);
}

}  // namespace aces
