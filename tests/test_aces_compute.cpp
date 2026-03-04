#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <fstream>
#include <map>

#include "aces/aces_compute.hpp"
#include "aces/aces_config.hpp"
#include "aces/aces_utils.hpp"

namespace aces {

/**
 * @brief Mock implementation of FieldResolver for unit testing.
 */
class MockFieldResolver : public FieldResolver {
    std::map<std::string, DualView3D> fields;

   public:
    void AddField(const std::string& name, int nx, int ny, int nz) {
        fields[name] = DualView3D("mock_" + name, nx, ny, nz);
    }

    void SetFieldData(const std::string& name, const UnmanagedHostView3D& host_view) {
        Kokkos::deep_copy(fields[name].view_host(), host_view);
        fields[name].modify<Kokkos::HostSpace>();
        fields[name].sync<Kokkos::DefaultExecutionSpace::memory_space>();
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

class AcesComputeTest : public ::testing::Test {
   protected:
    void SetUp() override {
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }
    }
};

TEST_F(AcesComputeTest, BranchlessReplaceLogic) {
    int nx = 10;
    int ny = 10;
    int nz = 1;

    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> background_data("background", nx,
                                                                                  ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> regional_data("regional", nx, ny,
                                                                                nz);
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
    resolver.AddField("background_field", nx, ny, nz);
    resolver.SetFieldData("background_field", background_data);
    resolver.AddField("regional_field", nx, ny, nz);
    resolver.SetFieldData("regional_field", regional_data);
    resolver.AddField("half_mask", nx, ny, nz);
    resolver.SetFieldData("half_mask", mask_data);
    resolver.AddField("nox", nx, ny, nz);
    resolver.SetFieldData("nox", export_data);

    AcesConfig config;

    EmissionLayer layer1;
    layer1.operation = "add";
    layer1.field_name = "background_field";
    layer1.scale = 1.0;

    EmissionLayer layer2;
    layer2.operation = "replace";
    layer2.field_name = "regional_field";
    layer2.masks = {"half_mask"};
    layer2.scale = 1.0;

    config.species_layers["nox"] = {layer1, layer2};

    ComputeEmissions(config, resolver, nx, ny, nz);

    auto result = resolver.GetFieldData("nox");
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            if (i < nx / 2) {
                EXPECT_DOUBLE_EQ(result(i, j, 0), 10.0);
            } else {
                EXPECT_DOUBLE_EQ(result(i, j, 0), 5.0);
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

    ASSERT_EQ(config.species_layers.find("nox") != config.species_layers.end(), 1);
    auto layers = config.species_layers["nox"];
    ASSERT_EQ(layers.size(), 2);

    EXPECT_EQ(layers[0].operation, "add");
    EXPECT_EQ(layers[0].field_name, "background_nox");
    EXPECT_EQ(layers[0].scale, 1.0);
    EXPECT_TRUE(layers[0].masks.empty());

    EXPECT_EQ(layers[1].operation, "replace");
    EXPECT_EQ(layers[1].field_name, "regional_nox");
    ASSERT_EQ(layers[1].masks.size(), 1);
    EXPECT_EQ(layers[1].masks[0], "europe_mask");
    EXPECT_EQ(layers[1].scale, 1.5);

    std::remove("test_config.yaml");
}

TEST_F(AcesComputeTest, YamlParsingExtended) {
    std::ofstream out("test_config_ext.yaml");
    out << "meteorology:\n"
        << "  temperature: air_temperature\n"
        << "scale_factors:\n"
        << "  sf1: SF_EXT_1\n"
        << "masks:\n"
        << "  m1: MASK_EXT_1\n"
        << "temporal_profiles:\n"
        << "  diurnal: [1.0, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9, 2.0, "
           "2.1, 2.2, 2.3, 2.4, "
           "2.5, 2.6, 2.7, 2.8, 2.9, 3.0, 3.1, 3.2, 3.3]\n"
        << "species:\n"
        << "  nox:\n"
        << "    - operation: add\n"
        << "      field: background_nox\n"
        << "      mask: [mask1, mask2]\n"
        << "      diurnal_cycle: diurnal\n";
    out.close();

    AcesConfig config = ParseConfig("test_config_ext.yaml");

    EXPECT_EQ(config.met_mapping["temperature"], "air_temperature");
    EXPECT_EQ(config.scale_factor_mapping["sf1"], "SF_EXT_1");
    EXPECT_EQ(config.mask_mapping["m1"], "MASK_EXT_1");
    ASSERT_EQ(config.temporal_profiles.find("diurnal") != config.temporal_profiles.end(), 1);
    EXPECT_EQ(config.temporal_profiles["diurnal"].factors.size(), 24);
    EXPECT_DOUBLE_EQ(config.temporal_profiles["diurnal"].factors[23], 3.3);

    auto layers = config.species_layers["nox"];
    ASSERT_EQ(layers.size(), 1);
    EXPECT_EQ(layers[0].masks.size(), 2);
    EXPECT_EQ(layers[0].masks[0], "mask1");
    EXPECT_EQ(layers[0].masks[1], "mask2");
    EXPECT_EQ(layers[0].diurnal_cycle, "diurnal");

    std::remove("test_config_ext.yaml");
}

TEST_F(AcesComputeTest, HierarchyAndCategory) {
    int nx = 4;
    int ny = 4;
    int nz = 1;

    // Background (Cat 1, Hier 1)
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> bg_data("bg", nx, ny, nz);
    Kokkos::deep_copy(bg_data, 1.0);

    // Overlay (Cat 1, Hier 10, Replace)
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> overlay_data("overlay", nx, ny,
                                                                               nz);
    Kokkos::deep_copy(overlay_data, 2.0);

    // Another category (Cat 2, Hier 1)
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> cat2_data("cat2", nx, ny, nz);
    Kokkos::deep_copy(cat2_data, 10.0);

    // Scale field
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> sf_data("sf", nx, ny, nz);
    Kokkos::deep_copy(sf_data, 1.5);

    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> export_data("export", nx, ny, nz);
    Kokkos::deep_copy(export_data, 0.0);

    MockFieldResolver resolver;
    resolver.AddField("bg", nx, ny, nz);
    resolver.SetFieldData("bg", bg_data);
    resolver.AddField("overlay", nx, ny, nz);
    resolver.SetFieldData("overlay", overlay_data);
    resolver.AddField("cat2", nx, ny, nz);
    resolver.SetFieldData("cat2", cat2_data);
    resolver.AddField("sf", nx, ny, nz);
    resolver.SetFieldData("sf", sf_data);
    resolver.AddField("nox", nx, ny, nz);
    resolver.SetFieldData("nox", export_data);

    AcesConfig config;

    EmissionLayer l1;
    l1.operation = "add";
    l1.field_name = "bg";
    l1.category = "1";
    l1.hierarchy = 1;

    EmissionLayer l2;
    l2.operation = "replace";
    l2.field_name = "overlay";
    l2.category = "1";
    l2.hierarchy = 10;
    l2.scale_fields = {"sf"};

    EmissionLayer l3;
    l3.operation = "add";
    l3.field_name = "cat2";
    l3.category = "2";
    l3.hierarchy = 1;

    // Out of order in vector to test sorting
    config.species_layers["nox"] = {l2, l1, l3};

    ComputeEmissions(config, resolver, nx, ny, nz);

    auto result = resolver.GetFieldData("nox");
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            // GLOBAL Hierarchy check:
            // 1. BG (Hier 1, Add) -> Total = 1.0
            // 2. Cat2 (Hier 1, Add) -> Total = 11.0 (since they have same hierarchy,
            // order in list matters, but both are add)
            // 3. Overlay (Hier 10, Replace) -> Total = (11.0 * (1-1)) + (2.0 * 1.5)
            // = 3.0 Wait, if Overlay has Hier 10 and Replace, it should replace the
            // SUM of everything before it.
            EXPECT_DOUBLE_EQ(result(i, j, 0), 3.0);
        }
    }
}

TEST_F(AcesComputeTest, TemporalCycles) {
    int nx = 1;
    int ny = 1;
    int nz = 1;

    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> field_data("field", nx, ny, nz);
    Kokkos::deep_copy(field_data, 1.0);

    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> export_data("export", nx, ny, nz);
    Kokkos::deep_copy(export_data, 0.0);

    MockFieldResolver resolver;
    resolver.AddField("base_field", nx, ny, nz);
    resolver.SetFieldData("base_field", field_data);
    resolver.AddField("nox", nx, ny, nz);
    resolver.SetFieldData("nox", export_data);

    AcesConfig config;

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "base_field";
    layer.scale = 1.0;
    layer.diurnal_cycle = "diurnal";
    layer.weekly_cycle = "weekly";

    config.species_layers["nox"] = {layer};

    // 24 factors for diurnal
    TemporalCycle diurnal;
    diurnal.factors = std::vector<double>(24, 1.0);
    diurnal.factors[10] = 2.5;  // Peak at 10 AM
    config.temporal_cycles["diurnal"] = diurnal;

    // 7 factors for weekly
    TemporalCycle weekly;
    weekly.factors = {1.0, 1.0, 1.0, 1.0, 1.0, 0.5, 0.5};  // Weekend reduction
    config.temporal_cycles["weekly"] = weekly;

    // Test Hour 10, Day 0 (Monday) -> scale should be 2.5 * 1.0 = 2.5
    ComputeEmissions(config, resolver, nx, ny, nz, {}, 10, 0);

    auto result = resolver.GetFieldData("nox");
    EXPECT_DOUBLE_EQ(result(0, 0, 0), 2.5);

    // Test Hour 10, Day 5 (Saturday) -> scale should be 2.5 * 0.5 = 1.25
    Kokkos::deep_copy(resolver.GetFieldData("nox"), 0.0);
    ComputeEmissions(config, resolver, nx, ny, nz, {}, 10, 5);
    result = resolver.GetFieldData("nox");
    EXPECT_DOUBLE_EQ(result(0, 0, 0), 1.25);
}

TEST_F(AcesComputeTest, MultipleMasks) {
    int nx = 1;
    int ny = 1;
    int nz = 1;

    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> field_data("field", nx, ny, nz);
    Kokkos::deep_copy(field_data, 10.0);

    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> mask1("m1", nx, ny, nz);
    Kokkos::deep_copy(mask1, 0.5);

    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> mask2("m2", nx, ny, nz);
    Kokkos::deep_copy(mask2, 0.2);

    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> export_data("export", nx, ny, nz);
    Kokkos::deep_copy(export_data, 0.0);

    MockFieldResolver resolver;
    resolver.AddField("base_field", nx, ny, nz);
    resolver.SetFieldData("base_field", field_data);
    resolver.AddField("m1", nx, ny, nz);
    resolver.SetFieldData("m1", mask1);
    resolver.AddField("m2", nx, ny, nz);
    resolver.SetFieldData("m2", mask2);
    resolver.AddField("nox", nx, ny, nz);
    resolver.SetFieldData("nox", export_data);

    AcesConfig config;
    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "base_field";
    layer.masks = {"m1", "m2"};

    config.species_layers["nox"] = {layer};

    ComputeEmissions(config, resolver, nx, ny, nz);

    auto result = resolver.GetFieldData("nox");
    // Result should be 10.0 * (0.5 * 0.2) = 1.0
    EXPECT_DOUBLE_EQ(result(0, 0, 0), 1.0);
}

TEST_F(AcesComputeTest, MeteorologyMappingAndScaling) {
    int nx = 1;
    int ny = 1;
    int nz = 1;

    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> emissions_data("emi", nx, ny, nz);
    Kokkos::deep_copy(emissions_data, 100.0);

    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> temp_data("temp", nx, ny, nz);
    Kokkos::deep_copy(temp_data, 1.2);  // Scaling factor from meteorology

    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> export_data("export", nx, ny, nz);
    Kokkos::deep_copy(export_data, 0.0);

    MockFieldResolver resolver;
    resolver.AddField("base_emi", nx, ny, nz);
    resolver.SetFieldData("base_emi", emissions_data);
    resolver.AddField("air_temperature", nx, ny, nz);
    resolver.SetFieldData("air_temperature", temp_data);
    resolver.AddField("nox", nx, ny, nz);
    resolver.SetFieldData("nox", export_data);

    AcesConfig config;
    config.met_mapping["temperature"] = "air_temperature";

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "base_emi";
    layer.scale_fields = {"temperature"};

    config.species_layers["nox"] = {layer};

    // resolver should use the mapping
    AcesImportState imp;
    AcesExportState exp;
    // We manually populate the state as AcesStateResolver would expect
    imp.fields["base_emi"] = DualView3D("base_emi", nx, ny, nz);
    Kokkos::deep_copy(imp.fields["base_emi"].view_host(), emissions_data);
    imp.fields["base_emi"].modify<Kokkos::HostSpace>();
    imp.fields["base_emi"].sync<Kokkos::DefaultExecutionSpace::memory_space>();

    imp.fields["air_temperature"] = DualView3D("air_temperature", nx, ny, nz);
    Kokkos::deep_copy(imp.fields["air_temperature"].view_host(), temp_data);
    imp.fields["air_temperature"].modify<Kokkos::HostSpace>();
    imp.fields["air_temperature"].sync<Kokkos::DefaultExecutionSpace::memory_space>();

    exp.fields["nox"] = DualView3D("nox", nx, ny, nz);
    Kokkos::deep_copy(exp.fields["nox"].view_host(), export_data);
    exp.fields["nox"].modify<Kokkos::HostSpace>();
    exp.fields["nox"].sync<Kokkos::DefaultExecutionSpace::memory_space>();

    AcesStateResolver state_resolver(imp, exp, config.met_mapping, config.scale_factor_mapping,
                                     config.mask_mapping);

    ComputeEmissions(config, state_resolver, nx, ny, nz);

    exp.fields["nox"].sync<Kokkos::HostSpace>();
    auto result = exp.fields["nox"].view_host();
    EXPECT_DOUBLE_EQ(result(0, 0, 0), 120.0);
}

}  // namespace aces
