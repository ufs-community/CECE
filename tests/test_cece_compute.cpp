#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <fstream>
#include <map>

#include "cece/cece_compute.hpp"
#include "cece/cece_config.hpp"
#include "cece/cece_utils.hpp"

namespace cece {

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

    UnmanagedHostView3D ResolveImport(const std::string& name, int /*nx*/, int /*ny*/, int /*nz*/) override {
        if (fields.find(name) != fields.end()) {
            return fields[name].view_host();
        }
        return {};
    }

    UnmanagedHostView3D ResolveExport(const std::string& name, int /*nx*/, int /*ny*/, int /*nz*/) override {
        if (fields.find(name) != fields.end()) {
            return fields[name].view_host();
        }
        return {};
    }

    Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> ResolveImportDevice(const std::string& name, int /*nx*/,
                                                                                                         int /*ny*/, int /*nz*/) override {
        if (fields.find(name) != fields.end()) {
            return fields[name].view_device();
        }
        return {};
    }

    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> ResolveExportDevice(const std::string& name, int /*nx*/, int /*ny*/,
                                                                                                   int /*nz*/) override {
        if (fields.find(name) != fields.end()) {
            return fields[name].view_device();
        }
        return {};
    }
};

class CeceComputeTest : public ::testing::Test {
   protected:
    void SetUp() override {
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }
    }
};

TEST_F(CeceComputeTest, BranchlessReplaceLogic) {
    int nx = 10;
    int ny = 10;
    int nz = 1;

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
    resolver.AddField("background_field", nx, ny, nz);
    resolver.SetFieldData("background_field", background_data);
    resolver.AddField("regional_field", nx, ny, nz);
    resolver.SetFieldData("regional_field", regional_data);
    resolver.AddField("half_mask", nx, ny, nz);
    resolver.SetFieldData("half_mask", mask_data);
    resolver.AddField("nox", nx, ny, nz);
    resolver.SetFieldData("nox", export_data);

    CeceConfig config;

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

TEST_F(CeceComputeTest, YamlParsing) {
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

    CeceConfig config = ParseConfig("test_config.yaml");

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

TEST_F(CeceComputeTest, YamlParsingExtended) {
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

    CeceConfig config = ParseConfig("test_config_ext.yaml");

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

TEST_F(CeceComputeTest, HierarchyAndCategory) {
    int nx = 4;
    int ny = 4;
    int nz = 1;

    // We will test 2 independent Categories (Cat1 and Cat2).
    // Cat1 checks replacement with scaling and masks, as well as ignoring the replace operation of the lowest hierarchy in the category.
    // Cat2 checks independent accumulation and masked replacement in a different sector.
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> c1_mid_hier("c1_mid_hier", nx, ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> c1_low_hier("c1_low_hier", nx, ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> c1_high_hier("c1_high_hier", nx, ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> c1_highest_hier("c1_highest_hier", nx, ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> c2_base("c2_base", nx, ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> c2_high_hier("c2_high_hier", nx, ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> c3_base("c3_base", nx, ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> c3_high_hier("c3_high_hier", nx, ny, nz);

    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> left_mask("left_mask", nx, ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> bottom_mask("bottom_mask", nx, ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> very_bottom_mask("bottom_mask", nx, ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> sf_data("sf_data", nx, ny, nz);

    // Set field values
    Kokkos::deep_copy(c1_mid_hier, 10.0);
    Kokkos::deep_copy(c1_low_hier, 100.0);
    Kokkos::deep_copy(c1_high_hier, 50.0);
    Kokkos::deep_copy(c1_highest_hier, 150.0);

    Kokkos::deep_copy(c2_base, 1000.0);
    Kokkos::deep_copy(c2_high_hier, 5000.0);

    Kokkos::deep_copy(c3_base, 1.0);
    Kokkos::deep_copy(c3_high_hier, 5.0);

    // Scale factor multiplier
    Kokkos::deep_copy(sf_data, 2.0);

    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            left_mask(i, j, 0) = (i < nx / 2) ? 1.0 : 0.0;
            bottom_mask(i, j, 0) = (j < ny / 2) ? 1.0 : 0.0;
            very_bottom_mask(i, j, 0) = (j < 1) ? 1.0 : 0.0;
        }
    }

    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> export_data("export", nx, ny, nz);
    Kokkos::deep_copy(export_data, 0.0);

    MockFieldResolver resolver;
    resolver.AddField("c1_mid_hier", nx, ny, nz);
    resolver.SetFieldData("c1_mid_hier", c1_mid_hier);
    resolver.AddField("c1_low_hier", nx, ny, nz);
    resolver.SetFieldData("c1_low_hier", c1_low_hier);
    resolver.AddField("c1_high_hier", nx, ny, nz);
    resolver.SetFieldData("c1_high_hier", c1_high_hier);
    resolver.AddField("c1_highest_hier", nx, ny, nz);
    resolver.SetFieldData("c1_highest_hier", c1_highest_hier);
    resolver.AddField("c2_base", nx, ny, nz);
    resolver.SetFieldData("c2_base", c2_base);
    resolver.AddField("c2_high_hier", nx, ny, nz);
    resolver.SetFieldData("c2_high_hier", c2_high_hier);
    resolver.AddField("c3_base", nx, ny, nz);
    resolver.SetFieldData("c3_base", c3_base);
    resolver.AddField("c3_high_hier", nx, ny, nz);
    resolver.SetFieldData("c3_high_hier", c3_high_hier);
    resolver.AddField("left_mask", nx, ny, nz);
    resolver.SetFieldData("left_mask", left_mask);
    resolver.AddField("bottom_mask", nx, ny, nz);
    resolver.SetFieldData("bottom_mask", bottom_mask);
    resolver.AddField("very_bottom_mask", nx, ny, nz);
    resolver.SetFieldData("very_bottom_mask", very_bottom_mask);
    resolver.AddField("sf_data", nx, ny, nz);
    resolver.SetFieldData("sf_data", sf_data);
    resolver.AddField("nox", nx, ny, nz);
    resolver.SetFieldData("nox", export_data);

    CeceConfig config;

    // Cat 1, Mid Hierarchy (Hier 10, Add)
    EmissionLayer l1;
    l1.operation = "add";
    l1.field_name = "c1_mid_hier";
    l1.category = "Cat1";
    l1.hierarchy = 10;

    // Cat 1, Lower Hierarchy (Hier 5, Replace)
    // -> This should be added to the base and not replace anything because it is the lowest hierarchy in the category.
    EmissionLayer l2;
    l2.operation = "replace";
    l2.field_name = "c1_low_hier";
    l2.category = "Cat1";
    l2.hierarchy = 5;

    // Cat 1, Higher Hierarchy (Hier 20, Replace), Left Mask, Scaled
    // -> Replaces base on the left half, scaled by 2.0.
    EmissionLayer l3;
    l3.operation = "replace";
    l3.field_name = "c1_high_hier";
    l3.category = "Cat1";
    l3.hierarchy = 20;
    l3.masks = {"left_mask"};
    l3.scale_fields = {"sf_data"};

    // Cat 1, Higher Hierarchy (Hier 30)
    // -> Adds to lower hierarchies of Cat 1
    EmissionLayer l4;
    l4.operation = "add";
    l4.field_name = "c1_highest_hier";
    l4.category = "Cat1";
    l4.hierarchy = 30;

    // Cat 2, Base (Hier 10, Add)
    EmissionLayer l5;
    l5.operation = "add";
    l5.field_name = "c2_base";
    l5.category = "Cat2";
    l5.hierarchy = 10;

    // Cat 2, Higher Hierarchy (Hier 30, Replace), Bottom Mask
    // -> Replaces base on the bottom half.
    EmissionLayer l6;
    l6.operation = "replace";
    l6.field_name = "c2_high_hier";
    l6.category = "Cat2";
    l6.hierarchy = 30;
    l6.masks = {"bottom_mask"};

    // Cat 3, Base (Hier 4, add)
    EmissionLayer l7;
    l7.operation = "add";
    l7.field_name = "c3_base";
    l7.category = "Cat3";
    l7.hierarchy = 5;

    // Cat 3, Higher Hierarchy (Hier 10, Replace), very bottom mask
    EmissionLayer l8;
    l8.operation = "replace";
    l8.field_name = "c3_high_hier";
    l8.category = "Cat3";
    l8.hierarchy = 10;
    l8.masks = {"very_bottom_mask"};

    // Input layers out of logical order to ensure internal sorting works correctly
    config.species_layers["nox"] = {l7, l3, l1, l5, l8, l4, l2, l6};

    ComputeEmissions(config, resolver, nx, ny, nz);

    auto result = resolver.GetFieldData("nox");
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double expected = 0.0;

            // --- Cat 1 Evaluation ---
            if (i < 2) {
                // Left half: masked high-hierarchy replace applies.
                // Values = 50.0 (base) * 2.0 (scale field) = 100.0
                // Add 150 for highest hierarchy.
                expected += 100.0 + 150.0;
            } else {
                // Right half: mask doesn't apply, so fallback uses the accumulated value
                // for cat 1 mid hier and cat 1 low hier. Add 150.0 for highest hierarchy
                expected += 110.0 + 150.0;
            }

            // --- Cat 2 Evaluation ---
            if (j < 2) {
                // Bottom half: masked high-hierarchy replace applies.
                expected += 5000.0;
            } else {
                // Top half: mask doesn't apply, fall back to base layer (1000.0)
                expected += 1000.0;
            }

            // --- Cat 3 Evaluation ---
            if (j < 1) {
                // Bottom row: masked high-hierarchy replace applies.
                expected += 5;
            } else {
                // top 3 rows: mask doesn't apply, fall back to base layer
                expected += 1;
            }

            // Final result, shown visually with cartesian indexing (rows are j=0..3, counting up from the bottom; columns are i=0..3,
            // counting up from the left):
            //      1251.0, 1251.0, 1261.0, 1261.0
            //      1251.0, 1251.0, 1261.0, 1261.0
            //      5251.0, 5251.0, 5261.0, 5261.0
            //      5255.0, 5255.0, 5265.0, 5265.0

            // Verify final summed output of both categories
            EXPECT_DOUBLE_EQ(result(i, j, 0), expected);
        }
    }
}

TEST_F(CeceComputeTest, TemporalCycles) {
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

    CeceConfig config;

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

TEST_F(CeceComputeTest, MultipleMasks) {
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

    CeceConfig config;
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

TEST_F(CeceComputeTest, MeteorologyMappingAndScaling) {
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

    CeceConfig config;
    config.met_mapping["temperature"] = "air_temperature";

    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "base_emi";
    layer.scale_fields = {"temperature"};

    config.species_layers["nox"] = {layer};

    // resolver should use the mapping
    CeceImportState imp;
    CeceExportState exp;
    // We manually populate the state as CeceStateResolver would expect
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

    CeceStateResolver state_resolver(imp, exp, config.met_mapping, config.scale_factor_mapping, config.mask_mapping);

    ComputeEmissions(config, state_resolver, nx, ny, nz);

    exp.fields["nox"].sync<Kokkos::HostSpace>();
    auto result = exp.fields["nox"].view_host();
    EXPECT_DOUBLE_EQ(result(0, 0, 0), 120.0);
}

}  // namespace cece
