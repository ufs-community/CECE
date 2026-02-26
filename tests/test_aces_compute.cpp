#include <gtest/gtest.h>
#include "aces/aces_compute.hpp"
#include "aces/aces_config.hpp"
#include "aces/aces_utils.hpp"
#include <Kokkos_Core.hpp>
#include <fstream>
#include <map>

namespace aces {

/**
 * @brief Mock implementation of FieldResolver for unit testing.
 */
class MockFieldResolver : public FieldResolver {
    std::map<std::string, UnmanagedHostView3D> fields;

public:
    void AddField(const std::string& name, UnmanagedHostView3D view) {
        fields[name] = view;
    }

    UnmanagedHostView3D ResolveImport(const std::string& name, int nx, int ny, int nz) override {
        if (fields.count(name)) return fields[name];
        return UnmanagedHostView3D();
    }

    UnmanagedHostView3D ResolveExport(const std::string& name, int nx, int ny, int nz) override {
        if (fields.count(name)) return fields[name];
        return UnmanagedHostView3D();
    }
};

class AcesComputeTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }
    }
};

TEST_F(AcesComputeTest, BranchlessReplaceLogic) {
    int nx = 10, ny = 10, nz = 1;

    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> background_data("background", nx, ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> regional_data("regional", nx, ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> mask_data("mask", nx, ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> export_data("export", nx, ny, nz);

    Kokkos::deep_copy(background_data, 5.0);
    Kokkos::deep_copy(regional_data, 10.0);
    Kokkos::deep_copy(export_data, 0.0);

    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            mask_data(i, j, 0) = (i < nx / 2) ? 1.0 : 0.0;
        }
    }

    MockFieldResolver resolver;
    resolver.AddField("background_field", background_data);
    resolver.AddField("regional_field", regional_data);
    resolver.AddField("half_mask", mask_data);
    resolver.AddField("total_nox_emissions", export_data);

    AcesConfig config;

    EmissionLayer layer1;
    layer1.operation = "add";
    layer1.field_name = "background_field";
    layer1.scale = 1.0;
    layer1.mask_name = "";

    EmissionLayer layer2;
    layer2.operation = "replace";
    layer2.field_name = "regional_field";
    layer2.mask_name = "half_mask";
    layer2.scale = 1.0;

    config.species_layers["nox"] = {layer1, layer2};

    ComputeEmissions(config, resolver, nx, ny, nz);

    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            if (i < nx / 2) {
                EXPECT_DOUBLE_EQ(export_data(i, j, 0), 10.0) << "Failed at i=" << i << ", j=" << j;
            } else {
                EXPECT_DOUBLE_EQ(export_data(i, j, 0), 5.0) << "Failed at i=" << i << ", j=" << j;
            }
        }
    }
}

TEST_F(AcesComputeTest, YamlParsing) {
    std::ofstream out("test_config.yaml");
    out << "species:\n"
        << "  nox:\n"
        << "    - operation: add\n"
        << "      field: background_nox\n"
        << "      scale: 1.0\n"
        << "    - operation: replace\n"
        << "      field: regional_nox\n"
        << "      mask: europe_mask\n"
        << "      scale: 1.5\n";
    out.close();

    AcesConfig config = ParseConfig("test_config.yaml");

    ASSERT_EQ(config.species_layers.count("nox"), 1);
    auto layers = config.species_layers["nox"];
    ASSERT_EQ(layers.size(), 2);

    EXPECT_EQ(layers[0].operation, "add");
    EXPECT_EQ(layers[0].field_name, "background_nox");
    EXPECT_EQ(layers[0].scale, 1.0);
    EXPECT_EQ(layers[0].mask_name, "");

    EXPECT_EQ(layers[1].operation, "replace");
    EXPECT_EQ(layers[1].field_name, "regional_nox");
    EXPECT_EQ(layers[1].mask_name, "europe_mask");
    EXPECT_EQ(layers[1].scale, 1.5);

    std::remove("test_config.yaml");
}

} // namespace aces
