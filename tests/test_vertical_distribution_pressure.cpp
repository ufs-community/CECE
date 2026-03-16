#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <cmath>
#include <map>

#include "aces/aces_compute.hpp"
#include "aces/aces_config.hpp"
#include "aces/aces_stacking_engine.hpp"

namespace aces {

/**
 * @brief FieldResolver implementation for pressure-based vertical distribution testing.
 */
class PressureDistributionFieldResolver : public FieldResolver {
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
 * @brief Test suite for PRESSURE vertical distribution method.
 * Execution Space: Default (OpenMP/Serial/CUDA)
 */
class VerticalDistributionPressureTest : public ::testing::Test {
   protected:
    void SetUp() override {
        if (!Kokkos::is_initialized()) Kokkos::initialize();
    }
};

/**
 * @test PRESSURE: Emissions distributed based on pressure overlap.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * For a 2D emission field distributed over a pressure range, the total column
 * mass after distribution SHALL equal the original 2D field value.
 */
TEST_F(VerticalDistributionPressureTest, PressureOverlapDistribution) {
    int nx = 2, ny = 2, nz = 5;
    AcesConfig config;

    // Configure vertical coordinate system (FV3-style)
    config.vertical_config.type = VerticalCoordType::FV3;
    config.vertical_config.ak_field = "ak";
    config.vertical_config.bk_field = "bk";
    config.vertical_config.p_surf_field = "ps";

    // Distribute over pressure range 50000-80000 Pa
    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.vdist_method = VerticalDistributionMethod::PRESSURE;
    layer.vdist_p_start = 50000.0;
    layer.vdist_p_end = 80000.0;

    config.species_layers["CO"] = {layer};

    PressureDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("CO", nx, ny, nz);
    resolver.AddField("ak", 1, 1, nz + 1);
    resolver.AddField("bk", 1, 1, nz + 1);
    resolver.AddField("ps", nx, ny, 1);

    // Set up FV3-style pressure coefficients
    // ak: pressure at top of layer (Pa)
    // bk: sigma coefficient
    // p(k) = ak(k) + bk(k) * ps
    double ak_vals[] = {0.0, 10000.0, 20000.0, 35000.0, 50000.0, 70000.0};
    double bk_vals[] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

    for (int k = 0; k <= nz; ++k) {
        resolver.SetValue("ak", 0, 0, k, ak_vals[k]);
        resolver.SetValue("bk", 0, 0, k, bk_vals[k]);
    }

    // Set surface pressure to 100000 Pa
    resolver.SetValue("ps", 100000.0);

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
 * @test PRESSURE: Full pressure column.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * When pressure range covers the full column, all emissions are distributed.
 */
TEST_F(VerticalDistributionPressureTest, PressureFullColumn) {
    int nx = 2, ny = 2, nz = 4;
    AcesConfig config;

    config.vertical_config.type = VerticalCoordType::FV3;
    config.vertical_config.ak_field = "ak";
    config.vertical_config.bk_field = "bk";
    config.vertical_config.p_surf_field = "ps";

    // Distribute over full pressure range
    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.vdist_method = VerticalDistributionMethod::PRESSURE;
    layer.vdist_p_start = 0.0;
    layer.vdist_p_end = 100000.0;

    config.species_layers["NOx"] = {layer};

    PressureDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("NOx", nx, ny, nz);
    resolver.AddField("ak", 1, 1, nz + 1);
    resolver.AddField("bk", 1, 1, nz + 1);
    resolver.AddField("ps", nx, ny, 1);

    // Set up pressure coefficients
    double ak_vals[] = {0.0, 10000.0, 30000.0, 60000.0, 100000.0};
    double bk_vals[] = {0.0, 0.0, 0.0, 0.0, 0.0};

    for (int k = 0; k <= nz; ++k) {
        resolver.SetValue("ak", 0, 0, k, ak_vals[k]);
        resolver.SetValue("bk", 0, 0, k, bk_vals[k]);
    }

    resolver.SetValue("ps", 100000.0);

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
 * @test PRESSURE: Single layer pressure range.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * When pressure range covers a single layer, all emissions go to that layer.
 */
TEST_F(VerticalDistributionPressureTest, PressureSingleLayer) {
    int nx = 2, ny = 2, nz = 5;
    AcesConfig config;

    config.vertical_config.type = VerticalCoordType::FV3;
    config.vertical_config.ak_field = "ak";
    config.vertical_config.bk_field = "bk";
    config.vertical_config.p_surf_field = "ps";

    // Distribute over layer 2 pressure range (30000-50000 Pa)
    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.vdist_method = VerticalDistributionMethod::PRESSURE;
    layer.vdist_p_start = 30000.0;
    layer.vdist_p_end = 50000.0;

    config.species_layers["ISOP"] = {layer};

    PressureDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("ISOP", nx, ny, nz);
    resolver.AddField("ak", 1, 1, nz + 1);
    resolver.AddField("bk", 1, 1, nz + 1);
    resolver.AddField("ps", nx, ny, 1);

    // Set up pressure coefficients
    double ak_vals[] = {0.0, 10000.0, 20000.0, 30000.0, 50000.0, 80000.0};
    double bk_vals[] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

    for (int k = 0; k <= nz; ++k) {
        resolver.SetValue("ak", 0, 0, k, ak_vals[k]);
        resolver.SetValue("bk", 0, 0, k, bk_vals[k]);
    }

    resolver.SetValue("ps", 100000.0);

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
 * @test PRESSURE: With scale factor.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * PRESSURE distribution with a scale factor SHALL conserve scaled mass.
 */
TEST_F(VerticalDistributionPressureTest, PressureWithScaleFactor) {
    int nx = 2, ny = 2, nz = 4;
    AcesConfig config;

    config.vertical_config.type = VerticalCoordType::FV3;
    config.vertical_config.ak_field = "ak";
    config.vertical_config.bk_field = "bk";
    config.vertical_config.p_surf_field = "ps";

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 2.0;  // Scale factor
    layer.vdist_method = VerticalDistributionMethod::PRESSURE;
    layer.vdist_p_start = 20000.0;
    layer.vdist_p_end = 80000.0;

    config.species_layers["SO2"] = {layer};

    PressureDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("SO2", nx, ny, nz);
    resolver.AddField("ak", 1, 1, nz + 1);
    resolver.AddField("bk", 1, 1, nz + 1);
    resolver.AddField("ps", nx, ny, 1);

    double ak_vals[] = {0.0, 10000.0, 20000.0, 50000.0, 100000.0};
    double bk_vals[] = {0.0, 0.0, 0.0, 0.0, 0.0};

    for (int k = 0; k <= nz; ++k) {
        resolver.SetValue("ak", 0, 0, k, ak_vals[k]);
        resolver.SetValue("bk", 0, 0, k, bk_vals[k]);
    }

    resolver.SetValue("ps", 100000.0);

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
 * @test PRESSURE: With spatial mask.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * PRESSURE distribution with a spatial mask SHALL conserve mass in masked regions.
 */
TEST_F(VerticalDistributionPressureTest, PressureWithMask) {
    int nx = 3, ny = 3, nz = 4;
    AcesConfig config;

    config.vertical_config.type = VerticalCoordType::FV3;
    config.vertical_config.ak_field = "ak";
    config.vertical_config.bk_field = "bk";
    config.vertical_config.p_surf_field = "ps";

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.masks = {"region_mask"};
    layer.vdist_method = VerticalDistributionMethod::PRESSURE;
    layer.vdist_p_start = 20000.0;
    layer.vdist_p_end = 80000.0;

    config.species_layers["OC"] = {layer};

    PressureDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("region_mask", nx, ny, 1);
    resolver.AddField("OC", nx, ny, nz);
    resolver.AddField("ak", 1, 1, nz + 1);
    resolver.AddField("bk", 1, 1, nz + 1);
    resolver.AddField("ps", nx, ny, 1);

    double ak_vals[] = {0.0, 10000.0, 20000.0, 50000.0, 100000.0};
    double bk_vals[] = {0.0, 0.0, 0.0, 0.0, 0.0};

    for (int k = 0; k <= nz; ++k) {
        resolver.SetValue("ak", 0, 0, k, ak_vals[k]);
        resolver.SetValue("bk", 0, 0, k, bk_vals[k]);
    }

    resolver.SetValue("ps", 100000.0);

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
 * @test PRESSURE: Multiple layers with hierarchy (replace operation).
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * When multiple PRESSURE layers are stacked with replace operation, the final
 * column mass SHALL equal the highest-hierarchy layer's mass.
 */
TEST_F(VerticalDistributionPressureTest, PressureMultipleLayersReplace) {
    int nx = 2, ny = 2, nz = 4;
    AcesConfig config;

    config.vertical_config.type = VerticalCoordType::FV3;
    config.vertical_config.ak_field = "ak";
    config.vertical_config.bk_field = "bk";
    config.vertical_config.p_surf_field = "ps";

    // Layer 1: Add to pressure range 20000-80000 Pa
    EmissionLayer layer1;
    layer1.operation = "add";
    layer1.field_name = "base_emissions";
    layer1.hierarchy = 0;
    layer1.scale = 1.0;
    layer1.vdist_method = VerticalDistributionMethod::PRESSURE;
    layer1.vdist_p_start = 20000.0;
    layer1.vdist_p_end = 80000.0;

    // Layer 2: Replace in same pressure range (higher hierarchy)
    EmissionLayer layer2;
    layer2.operation = "replace";
    layer2.field_name = "override_emissions";
    layer2.hierarchy = 10;
    layer2.scale = 1.0;
    layer2.vdist_method = VerticalDistributionMethod::PRESSURE;
    layer2.vdist_p_start = 20000.0;
    layer2.vdist_p_end = 80000.0;

    config.species_layers["PM25"] = {layer1, layer2};

    PressureDistributionFieldResolver resolver;
    resolver.AddField("base_emissions", nx, ny, 1);
    resolver.SetValue("base_emissions", 10.0);
    resolver.AddField("override_emissions", nx, ny, 1);
    resolver.SetValue("override_emissions", 5.0);
    resolver.AddField("PM25", nx, ny, nz);
    resolver.AddField("ak", 1, 1, nz + 1);
    resolver.AddField("bk", 1, 1, nz + 1);
    resolver.AddField("ps", nx, ny, 1);

    double ak_vals[] = {0.0, 10000.0, 20000.0, 50000.0, 100000.0};
    double bk_vals[] = {0.0, 0.0, 0.0, 0.0, 0.0};

    for (int k = 0; k <= nz; ++k) {
        resolver.SetValue("ak", 0, 0, k, ak_vals[k]);
        resolver.SetValue("bk", 0, 0, k, bk_vals[k]);
    }

    resolver.SetValue("ps", 100000.0);

    StackingEngine engine(config);
    engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

    // Verify: replacement occurred (5.0 total, not 10.0)
    // Note: The total 3D field value should be the sum of the override_emissions over all grid
    // points. nx=2, ny=2 -> 4 grid points. Each should have 5.0 total column emissions. Total 3D
    // should be 4 * 5.0 = 20.0.
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
 * @test PRESSURE: Large grid mass conservation.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * For a large grid (360x180x72), PRESSURE distribution SHALL conserve mass
 * within 1e-10 relative error.
 */
TEST_F(VerticalDistributionPressureTest, LargeGridMassConservation) {
    int nx = 360, ny = 180, nz = 72;
    AcesConfig config;

    config.vertical_config.type = VerticalCoordType::FV3;
    config.vertical_config.ak_field = "ak";
    config.vertical_config.bk_field = "bk";
    config.vertical_config.p_surf_field = "ps";

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.vdist_method = VerticalDistributionMethod::PRESSURE;
    layer.vdist_p_start = 30000.0;
    layer.vdist_p_end = 70000.0;

    config.species_layers["CO2"] = {layer};

    PressureDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("CO2", nx, ny, nz);
    resolver.AddField("ak", 1, 1, nz + 1);
    resolver.AddField("bk", 1, 1, nz + 1);
    resolver.AddField("ps", nx, ny, 1);

    // Set up realistic pressure coefficients
    // ak values must be non-increasing for pressure from top (0) to bottom (nz)
    // Here we use ak for pressure, so ak[0] should be top (e.g., 0) and ak[nz] should be bottom
    // (e.g., 100000)
    for (int k = 0; k <= nz; ++k) {
        double ak = 100000.0 * std::pow(static_cast<double>(k) / nz, 2.0);
        resolver.SetValue("ak", 0, 0, k, ak);
        resolver.SetValue("bk", 0, 0, k, 0.0);
    }

    resolver.SetValue("ps", 100000.0);

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
    EXPECT_LT(rel_error, 1e-10);
}

}  // namespace aces
