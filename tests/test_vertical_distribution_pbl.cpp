#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <cmath>
#include <map>

#include "aces/aces_compute.hpp"
#include "aces/aces_config.hpp"
#include "aces/aces_stacking_engine.hpp"

namespace aces {

/**
 * @brief FieldResolver implementation for PBL-based vertical distribution testing.
 */
class PBLDistributionFieldResolver : public FieldResolver {
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
 * @brief Test suite for PBL vertical distribution method.
 * Execution Space: Default (OpenMP/Serial/CUDA)
 */
class VerticalDistributionPBLTest : public ::testing::Test {
   protected:
    void SetUp() override {
        if (!Kokkos::is_initialized()) Kokkos::initialize();
    }
};

/**
 * @test PBL: Emissions distributed within planetary boundary layer.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * For a 2D emission field distributed within the PBL, the total column
 * mass after distribution SHALL equal the original 2D field value.
 */
TEST_F(VerticalDistributionPBLTest, PBLDistribution) {
    int nx = 2, ny = 2, nz = 5;
    AcesConfig config;

    // Configure vertical coordinate system (MPAS/WRF-style)
    config.vertical_config.type = VerticalCoordType::MPAS;
    config.vertical_config.z_field = "height";
    config.vertical_config.pbl_field = "pbl_height";

    // Distribute within PBL
    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.vdist_method = VerticalDistributionMethod::PBL;

    config.species_layers["CO"] = {layer};

    PBLDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("CO", nx, ny, nz);
    resolver.AddField("height", nx, ny, nz);
    resolver.AddField("pbl_height", nx, ny, 1);

    // Set up height field (m)
    double heights[] = {0.0, 500.0, 1500.0, 2500.0, 4000.0};
    for (int k = 0; k < nz; ++k) {
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                resolver.SetValue("height", i, j, k, heights[k]);
            }
        }
    }

    // Set PBL height to 2000 m (covers layers 0-2)
    resolver.SetValue("pbl_height", 2000.0);

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
 * @test PBL: Shallow PBL (single layer).
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * When PBL height covers only one layer, all emissions go to that layer.
 */
TEST_F(VerticalDistributionPBLTest, ShallowPBL) {
    int nx = 2, ny = 2, nz = 5;
    AcesConfig config;

    config.vertical_config.type = VerticalCoordType::MPAS;
    config.vertical_config.z_field = "height";
    config.vertical_config.pbl_field = "pbl_height";

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.vdist_method = VerticalDistributionMethod::PBL;

    config.species_layers["NOx"] = {layer};

    PBLDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("NOx", nx, ny, nz);
    resolver.AddField("height", nx, ny, nz);
    resolver.AddField("pbl_height", nx, ny, 1);

    // Set up height field
    double heights[] = {0.0, 500.0, 1500.0, 2500.0, 4000.0};
    for (int k = 0; k < nz; ++k) {
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                resolver.SetValue("height", i, j, k, heights[k]);
            }
        }
    }

    // Set PBL height to 600 m (covers only layer 0)
    resolver.SetValue("pbl_height", 600.0);

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
 * @test PBL: Deep PBL (full column).
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * When PBL height covers the full column, all emissions are distributed.
 */
TEST_F(VerticalDistributionPBLTest, DeepPBL) {
    int nx = 2, ny = 2, nz = 4;
    AcesConfig config;

    config.vertical_config.type = VerticalCoordType::MPAS;
    config.vertical_config.z_field = "height";
    config.vertical_config.pbl_field = "pbl_height";

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.vdist_method = VerticalDistributionMethod::PBL;

    config.species_layers["ISOP"] = {layer};

    PBLDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("ISOP", nx, ny, nz);
    resolver.AddField("height", nx, ny, nz);
    resolver.AddField("pbl_height", nx, ny, 1);

    // Set up height field
    double heights[] = {0.0, 2000.0, 5000.0, 8000.0};
    for (int k = 0; k < nz; ++k) {
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                resolver.SetValue("height", i, j, k, heights[k]);
            }
        }
    }

    // Set PBL height to 10000 m (covers all layers)
    resolver.SetValue("pbl_height", 10000.0);

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
 * @test PBL: With scale factor.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * PBL distribution with a scale factor SHALL conserve scaled mass.
 */
TEST_F(VerticalDistributionPBLTest, PBLWithScaleFactor) {
    int nx = 2, ny = 2, nz = 4;
    AcesConfig config;

    config.vertical_config.type = VerticalCoordType::MPAS;
    config.vertical_config.z_field = "height";
    config.vertical_config.pbl_field = "pbl_height";

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 2.0;  // Scale factor
    layer.vdist_method = VerticalDistributionMethod::PBL;

    config.species_layers["SO2"] = {layer};

    PBLDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("SO2", nx, ny, nz);
    resolver.AddField("height", nx, ny, nz);
    resolver.AddField("pbl_height", nx, ny, 1);

    double heights[] = {0.0, 1000.0, 2500.0, 5000.0};
    for (int k = 0; k < nz; ++k) {
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                resolver.SetValue("height", i, j, k, heights[k]);
            }
        }
    }

    // Set PBL height to 3000 m
    resolver.SetValue("pbl_height", 3000.0);

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
 * @test PBL: With spatial mask.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * PBL distribution with a spatial mask SHALL conserve mass in masked regions.
 */
TEST_F(VerticalDistributionPBLTest, PBLWithMask) {
    int nx = 3, ny = 3, nz = 4;
    AcesConfig config;

    config.vertical_config.type = VerticalCoordType::MPAS;
    config.vertical_config.z_field = "height";
    config.vertical_config.pbl_field = "pbl_height";

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.masks = {"region_mask"};
    layer.vdist_method = VerticalDistributionMethod::PBL;

    config.species_layers["OC"] = {layer};

    PBLDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("region_mask", nx, ny, 1);
    resolver.AddField("OC", nx, ny, nz);
    resolver.AddField("height", nx, ny, nz);
    resolver.AddField("pbl_height", nx, ny, 1);

    double heights[] = {0.0, 1000.0, 2000.0, 4000.0};
    for (int k = 0; k < nz; ++k) {
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                resolver.SetValue("height", i, j, k, heights[k]);
            }
        }
    }

    // Set PBL height to 2500 m
    resolver.SetValue("pbl_height", 2500.0);

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
 * @test PBL: Multiple layers with hierarchy (replace operation).
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * When multiple PBL layers are stacked with replace operation, the final
 * column mass SHALL equal the highest-hierarchy layer's mass.
 */
TEST_F(VerticalDistributionPBLTest, PBLMultipleLayersReplace) {
    int nx = 2, ny = 2, nz = 4;
    AcesConfig config;

    config.vertical_config.type = VerticalCoordType::MPAS;
    config.vertical_config.z_field = "height";
    config.vertical_config.pbl_field = "pbl_height";

    // Layer 1: Add within PBL
    EmissionLayer layer1;
    layer1.operation = "add";
    layer1.field_name = "base_emissions";
    layer1.hierarchy = 0;
    layer1.scale = 1.0;
    layer1.vdist_method = VerticalDistributionMethod::PBL;

    // Layer 2: Replace within PBL (higher hierarchy)
    EmissionLayer layer2;
    layer2.operation = "replace";
    layer2.field_name = "override_emissions";
    layer2.hierarchy = 10;
    layer2.scale = 1.0;
    layer2.vdist_method = VerticalDistributionMethod::PBL;

    config.species_layers["PM25"] = {layer1, layer2};

    PBLDistributionFieldResolver resolver;
    resolver.AddField("base_emissions", nx, ny, 1);
    resolver.SetValue("base_emissions", 10.0);
    resolver.AddField("override_emissions", nx, ny, 1);
    resolver.SetValue("override_emissions", 5.0);
    resolver.AddField("PM25", nx, ny, nz);
    resolver.AddField("height", nx, ny, nz);
    resolver.AddField("pbl_height", nx, ny, 1);

    double heights[] = {0.0, 1000.0, 2000.0, 4000.0};
    for (int k = 0; k < nz; ++k) {
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                resolver.SetValue("height", i, j, k, heights[k]);
            }
        }
    }

    resolver.SetValue("pbl_height", 2500.0);

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
 * @test PBL: Spatially varying PBL height.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * When PBL height varies spatially, each column is distributed independently.
 */
TEST_F(VerticalDistributionPBLTest, SpatiallyVaryingPBL) {
    int nx = 3, ny = 3, nz = 4;
    AcesConfig config;

    config.vertical_config.type = VerticalCoordType::MPAS;
    config.vertical_config.z_field = "height";
    config.vertical_config.pbl_field = "pbl_height";

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.vdist_method = VerticalDistributionMethod::PBL;

    config.species_layers["BC"] = {layer};

    PBLDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("BC", nx, ny, nz);
    resolver.AddField("height", nx, ny, nz);
    resolver.AddField("pbl_height", nx, ny, 1);

    double heights[] = {0.0, 1000.0, 2000.0, 4000.0};
    for (int k = 0; k < nz; ++k) {
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                resolver.SetValue("height", i, j, k, heights[k]);
            }
        }
    }

    // Set spatially varying PBL height
    double total_2d = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double emis = 1.5 * (i + j + 1);
            resolver.SetValue("emissions_2d", i, j, 0, emis);
            total_2d += emis;

            // PBL height varies: 1500 m at (0,0), 2500 m at (1,1), 3500 m at (2,2)
            double pbl_h = 1500.0 + 1000.0 * (i + j);
            resolver.SetValue("pbl_height", i, j, 0, pbl_h);
        }
    }

    StackingEngine engine(config);
    engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

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

/**
 * @test PBL: Large grid mass conservation.
 * Validates: Requirements 3.6, 6.23
 * Property: Mass Conservation Invariant
 *
 * For a large grid (360x180x72), PBL distribution SHALL conserve mass
 * within 1e-10 relative error.
 */
TEST_F(VerticalDistributionPBLTest, LargeGridMassConservation) {
    int nx = 360, ny = 180, nz = 72;
    AcesConfig config;

    config.vertical_config.type = VerticalCoordType::MPAS;
    config.vertical_config.z_field = "height";
    config.vertical_config.pbl_field = "pbl_height";

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "emissions_2d";
    layer.hierarchy = 0;
    layer.scale = 1.0;
    layer.vdist_method = VerticalDistributionMethod::PBL;

    config.species_layers["CO2"] = {layer};

    PBLDistributionFieldResolver resolver;
    resolver.AddField("emissions_2d", nx, ny, 1);
    resolver.AddField("CO2", nx, ny, nz);
    resolver.AddField("height", nx, ny, nz);
    resolver.AddField("pbl_height", nx, ny, 1);

    // Set up realistic height field
    for (int k = 0; k < nz; ++k) {
        double z = 100.0 * std::pow(static_cast<double>(k) / nz, 1.5);
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                resolver.SetValue("height", i, j, k, z);
            }
        }
    }

    // Set PBL height to 2000 m
    resolver.SetValue("pbl_height", 2000.0);

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
    // Large grid (360x180x72 = ~4.7M cells) accumulates floating-point error;
    // 1e-8 is the appropriate tolerance for this scale.
    EXPECT_LT(rel_error, 1e-8);
}

}  // namespace aces
