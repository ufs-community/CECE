#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <map>

#include "aces/aces_compute.hpp"
#include "aces/aces_config.hpp"
#include "aces/aces_state.hpp"

namespace aces {

/**
 * @brief Mock implementation of FieldResolver for unit testing.
 */
class MockFieldResolver : public FieldResolver {
    std::map<std::string, DualView3D> fields;

   public:
    void AddField(const std::string& name, int nx, int ny, int nz, double initial_val = 0.0) {
        fields[name] = DualView3D("mock_" + name, nx, ny, nz);
        Kokkos::deep_copy(fields[name].view_host(), initial_val);
        fields[name].modify<Kokkos::HostSpace>();
        fields[name].sync<Kokkos::DefaultExecutionSpace>();
    }

    void SetFieldData(const std::string& name, UnmanagedHostView3D host_view) {
        Kokkos::deep_copy(fields[name].view_host(), host_view);
        fields[name].modify<Kokkos::HostSpace>();
        fields[name].sync<Kokkos::DefaultExecutionSpace>();
    }

    UnmanagedHostView3D GetFieldData(const std::string& name) {
        fields[name].sync<Kokkos::HostSpace>();
        return fields[name].view_host();
    }

    UnmanagedHostView3D ResolveImport(const std::string& name, int /*nx*/, int /*ny*/,
                                      int /*nz*/) override {
        if (fields.find(name) != fields.end()) {
            return fields[name].view_host();
        }
        return {};
    }

    UnmanagedHostView3D ResolveExport(const std::string& name, int /*nx*/, int /*ny*/,
                                      int /*nz*/) override {
        if (fields.find(name) != fields.end()) {
            return fields[name].view_host();
        }
        return {};
    }

    Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>
    ResolveImportDevice(const std::string& name, int /*nx*/, int /*ny*/, int /*nz*/) override {
        if (fields.find(name) != fields.end()) {
            return fields[name].view_device();
        }
        return {};
    }

    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> ResolveExportDevice(
        const std::string& name, int /*nx*/, int /*ny*/, int /*nz*/) override {
        if (fields.find(name) != fields.end()) {
            return fields[name].view_device();
        }
        return {};
    }
};

class HemcoMimicTest : public ::testing::Test {
   protected:
    void SetUp() override {
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }
    }
};

/**
 * @brief Mimics a HEMCO regional override case.
 *
 * Case description:
 * - TEST_1: Global emission, hierarchy 1, value 5.0.
 * - TEST_2: Regional emission, hierarchy 10, value 12.0, applied with a mask.
 * - Expected: Within the mask, total emission should be 12.0. Outside the mask, 5.0.
 */
TEST_F(HemcoMimicTest, RegionalOverrideMimic) {
    int nx = 10;
    int ny = 10;
    int nz = 1;
    MockFieldResolver resolver;

    // 1. Setup Fields
    resolver.AddField("test_1_global", nx, ny, nz, 5.0);
    resolver.AddField("test_2_regional", nx, ny, nz, 12.0);
    resolver.AddField("nox", nx, ny, nz, 0.0);

    // Define a mask (top half of the grid)
    resolver.AddField("regional_mask", nx, ny, nz, 0.0);
    auto mask_hv = resolver.GetFieldData("regional_mask");
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            if (j >= ny / 2) {
                mask_hv(i, j, 0) = 1.0;
            }
        }
    }
    // Sync mask to device
    resolver.SetFieldData("regional_mask", mask_hv);

    // 2. Setup Configuration
    AcesConfig config;

    EmissionLayer global_layer;
    global_layer.field_name = "test_1_global";
    global_layer.operation = "add";
    global_layer.hierarchy = 1;
    global_layer.category = "anthropogenic";

    EmissionLayer regional_layer;
    regional_layer.field_name = "test_2_regional";
    regional_layer.operation = "replace";
    regional_layer.hierarchy = 10;
    regional_layer.category = "anthropogenic";
    regional_layer.masks = {"regional_mask"};

    config.species_layers["nox"] = {global_layer, regional_layer};

    // 3. Run Computation
    ComputeEmissions(config, resolver, nx, ny, nz);

    // 4. Verification
    auto result = resolver.GetFieldData("nox");
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            if (j >= ny / 2) {
                // Inside mask, regional (12.0) should replace global (5.0)
                EXPECT_DOUBLE_EQ(result(i, j, 0), 12.0) << "Failed at i=" << i << ", j=" << j;
            } else {
                // Outside mask, only global (5.0) should be present
                EXPECT_DOUBLE_EQ(result(i, j, 0), 5.0) << "Failed at i=" << i << ", j=" << j;
            }
        }
    }
}

/**
 * @brief Mimics HEMCO's "add into specific levels" case.
 *
 * Case description:
 * - EMIT_L1: Level 1 emission, value 10.0.
 * - EMIT_L2: Level 2 emission, value 3.0.
 */
TEST_F(HemcoMimicTest, VerticalDistributionMimic) {
    int nx = 4;
    int ny = 4;
    int nz = 5;
    MockFieldResolver resolver;

    resolver.AddField("emit_l1", nx, ny, nz, 10.0);
    resolver.AddField("emit_l2", nx, ny, nz, 3.0);
    resolver.AddField("nox", nx, ny, nz, 0.0);

    // In ACES, vertical distribution for base emissions is usually handled
    // by specific fields having data only in certain levels, or by masks.
    // For this test, we'll assume the input fields already have the values
    // but we apply them selectively.
    // Note: ComputeEmissions applies 2D fields to 3D by default if not masked vertically.
    // However, if the source field is 3D and has zeros in other levels, it works.

    auto l1_hv = resolver.GetFieldData("emit_l1");
    auto l2_hv = resolver.GetFieldData("emit_l2");
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                if (k != 0) {
                    l1_hv(i, j, k) = 0.0;
                }
                if (k != 1) {
                    l2_hv(i, j, k) = 0.0;
                }
            }
        }
    }
    resolver.SetFieldData("emit_l1", l1_hv);
    resolver.SetFieldData("emit_l2", l2_hv);

    AcesConfig config;
    EmissionLayer lay1;
    lay1.field_name = "emit_l1";
    lay1.operation = "add";
    lay1.hierarchy = 1;
    lay1.vdist_method = VerticalDistributionMethod::SINGLE;
    lay1.vdist_layer_start = 0;

    EmissionLayer lay2;
    lay2.field_name = "emit_l2";
    lay2.operation = "add";
    lay2.hierarchy = 1;
    lay2.vdist_method = VerticalDistributionMethod::SINGLE;
    lay2.vdist_layer_start = 1;

    config.species_layers["nox"] = {lay1, lay2};

    ComputeEmissions(config, resolver, nx, ny, nz);

    auto result = resolver.GetFieldData("nox");
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            EXPECT_DOUBLE_EQ(result(i, j, 0), 10.0);
            EXPECT_DOUBLE_EQ(result(i, j, 1), 3.0);
            EXPECT_DOUBLE_EQ(result(i, j, 2), 0.0);
        }
    }
}

}  // namespace aces

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
