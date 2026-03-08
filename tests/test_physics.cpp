#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>

#include "aces/aces_physics_factory.hpp"
#include "aces/aces_state.hpp"

namespace aces {

class PhysicsTest : public ::testing::Test {
   public:
    static void SetUpTestSuite() {
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }
    }

    int nx = 4;
    int ny = 4;
    int nz = 2;
    AcesImportState import_state;
    AcesExportState export_state;

    void SetUp() override {
        // Common fields
        import_state.fields["temperature"] = create_dv("temp", 300.0);
        import_state.fields["wind_speed_10m"] = create_dv("wind", 15.0);  // High enough for dust
        import_state.fields["tskin"] = create_dv("tskin", 300.0);
        import_state.fields["lai"] = create_dv("lai", 3.0);
        import_state.fields["pardr"] = create_dv("pardr", 100.0);
        import_state.fields["pardf"] = create_dv("pardf", 50.0);
        import_state.fields["suncos"] = create_dv("suncos", 1.0);
        import_state.fields["DMS_seawater"] = create_dv("DMS_seawater", 1.0e-6);
        import_state.fields["convective_cloud_top_height"] = create_dv("conv_h", 5000.0);
        import_state.fields["gwettop"] = create_dv("gwettop", 0.1);  // Dry for dust
        import_state.fields["land_mask"] = create_dv("land_mask", 1.0);
        import_state.fields["GINOUX_SAND"] = create_dv("sand", 0.1);
        import_state.fields["zsfc"] = create_dv("zsfc", 100.0);
        import_state.fields["bxheight_m"] = create_dv("bxh", 1000.0);

        // Export fields
        export_state.fields["SALA"] = create_dv("sala", 0.0);
        export_state.fields["SALC"] = create_dv("salc", 0.0);
        export_state.fields["isoprene"] = create_dv("isop", 0.0);
        export_state.fields["dms"] = create_dv("dms", 0.0);
        export_state.fields["lightning_nox"] = create_dv("light", 0.0);
        export_state.fields["soil_nox"] = create_dv("soil", 0.0);
        export_state.fields["dust"] = create_dv("dust", 0.0);
        export_state.fields["so2"] = create_dv("so2", 0.0);
        export_state.fields["nox"] = create_dv("nox", 0.0);
        import_state.fields["base_anthropogenic_nox"] = create_dv("base_nox", 1.0);
    }

    [[nodiscard]] DualView3D create_dv(const std::string& name, double val) const {
        DualView3D dv(name, nx, ny, nz);
        Kokkos::deep_copy(dv.view_host(), val);
        dv.modify<Kokkos::HostSpace>();
        dv.sync<Kokkos::DefaultExecutionSpace>();
        return dv;
    }

    void SetFieldValue(const std::string& name, double val, bool is_import = true) {
        auto& fields = is_import ? import_state.fields : export_state.fields;
        Kokkos::deep_copy(fields[name].view_host(), val);
        fields[name].modify<Kokkos::HostSpace>();
        fields[name].sync<Kokkos::DefaultExecutionSpace>();
    }
};

void TestParity(PhysicsTest* test, const std::string& cpp_name, const std::string& fortran_name,
                const std::string& field_name) {
    PhysicsSchemeConfig cfg_cpp;
    PhysicsSchemeConfig cfg_fort;
    cfg_cpp.name = cpp_name;
    cfg_fort.name = fortran_name;

    auto scheme_cpp = PhysicsFactory::CreateScheme(cfg_cpp);
    auto scheme_fort = PhysicsFactory::CreateScheme(cfg_fort);

    if (scheme_cpp == nullptr || scheme_fort == nullptr ||
        fortran_name.find("fortran") != std::string_view::npos) {
#ifndef ACES_HAS_FORTRAN
        std::cout << "Skipping parity test for " << cpp_name << " (Fortran disabled).\n";
        return;
#endif
    }

    // Initialize schemes (needed for optimized versions)
    scheme_cpp->Initialize(cfg_cpp.options, nullptr);
    scheme_fort->Initialize(cfg_fort.options, nullptr);

    // Run C++
    scheme_cpp->Run(test->import_state, test->export_state);
    auto& dv = test->export_state.fields[field_name];
    dv.sync<Kokkos::HostSpace>();
    double val_cpp = dv.view_host()(0, 0, 0);

    // Reset and Run Fortran
    Kokkos::deep_copy(dv.view_host(), 0.0);
    dv.modify<Kokkos::HostSpace>();
    dv.sync<Kokkos::DefaultExecutionSpace>();

    scheme_fort->Run(test->import_state, test->export_state);
    dv.sync<Kokkos::HostSpace>();
    double val_fort = dv.view_host()(0, 0, 0);

    EXPECT_NEAR(val_cpp, val_fort, std::abs(val_cpp) * 1e-6) << "Parity failed for " << cpp_name;
}

TEST_F(PhysicsTest, SeaSaltParity) {
    TestParity(this, "sea_salt", "sea_salt_fortran", "SALA");
}

TEST_F(PhysicsTest, MeganParity) {
    TestParity(this, "megan", "megan_fortran", "isoprene");
}

TEST_F(PhysicsTest, DMSParity) {
    TestParity(this, "dms", "dms_fortran", "dms");
}

TEST_F(PhysicsTest, LightningParity) {
    TestParity(this, "lightning", "lightning_fortran", "lightning_nox");
}

TEST_F(PhysicsTest, SoilNoxParity) {
    TestParity(this, "soil_nox", "soil_nox_fortran", "soil_nox");
}

TEST_F(PhysicsTest, DustParity) {
    TestParity(this, "dust", "dust_fortran", "dust");
}

TEST_F(PhysicsTest, VolcanoParity) {
    TestParity(this, "volcano", "volcano_fortran", "so2");
}

// Vertical Distribution Verification
TEST_F(PhysicsTest, SurfaceEmissionVerticalDistribution) {
    std::vector<std::string> schemes = {"sea_salt", "megan", "dms", "dust", "soil_nox"};
    std::vector<std::string> fields = {"SALA", "isoprene", "dms", "dust", "soil_nox"};

    for (size_t i = 0; i < schemes.size(); ++i) {
        PhysicsSchemeConfig cfg;
        cfg.name = schemes[i];
        auto scheme = PhysicsFactory::CreateScheme(cfg);
        scheme->Initialize(cfg.options, nullptr);

        SetFieldValue(fields[i], 0.0, false);
        scheme->Run(import_state, export_state);

        auto& dv = export_state.fields[fields[i]];
        dv.sync<Kokkos::HostSpace>();
        auto hv = dv.view_host();

        EXPECT_GT(hv(0, 0, 0), 0.0) << "Surface emission missing for " << schemes[i];
        EXPECT_DOUBLE_EQ(hv(0, 0, 1), 0.0) << "Emission leaked to upper layer for " << schemes[i];
    }
}

// Comprehensive Scientific Sensitivity Tests
TEST_F(PhysicsTest, SeaSaltSensitivity) {
    PhysicsSchemeConfig cfg;
    cfg.name = "sea_salt";
    auto scheme = PhysicsFactory::CreateScheme(cfg);
    scheme->Initialize(cfg.options, nullptr);

    // Test Wind Speed Sensitivity
    SetFieldValue("wind_speed_10m", 5.0);
    SetFieldValue("SALA", 0.0, false);
    scheme->Run(import_state, export_state);
    double em_low = export_state.fields["SALA"].view_host()(0, 0, 0);

    SetFieldValue("wind_speed_10m", 10.0);
    SetFieldValue("SALA", 0.0, false);
    scheme->Run(import_state, export_state);
    double em_high = export_state.fields["SALA"].view_host()(0, 0, 0);

    EXPECT_GT(em_high, em_low * 10.0);  // U^3.41 dependency: 2^3.41 approx 10.6

    // Test SST Sensitivity
    SetFieldValue("wind_speed_10m", 10.0);
    SetFieldValue("tskin", 273.15 + 0.0);  // 0C
    SetFieldValue("SALA", 0.0, false);
    scheme->Run(import_state, export_state);
    double em_0c = export_state.fields["SALA"].view_host()(0, 0, 0);

    SetFieldValue("tskin", 273.15 + 20.0);  // 20C
    SetFieldValue("SALA", 0.0, false);
    scheme->Run(import_state, export_state);
    double em_20c = export_state.fields["SALA"].view_host()(0, 0, 0);

    EXPECT_GT(em_20c, em_0c);
}

TEST_F(PhysicsTest, MeganSensitivity) {
    PhysicsSchemeConfig cfg;
    cfg.name = "megan";
    auto scheme = PhysicsFactory::CreateScheme(cfg);
    scheme->Initialize(cfg.options, nullptr);

    // Test Light Sensitivity
    SetFieldValue("lai", 3.0);
    SetFieldValue("suncos", 0.0);  // Night
    SetFieldValue("isoprene", 0.0, false);
    scheme->Run(import_state, export_state);
    EXPECT_DOUBLE_EQ(export_state.fields["isoprene"].view_host()(0, 0, 0), 0.0);

    SetFieldValue("suncos", 1.0);  // Day
    SetFieldValue("isoprene", 0.0, false);
    scheme->Run(import_state, export_state);
    EXPECT_GT(export_state.fields["isoprene"].view_host()(0, 0, 0), 0.0);
}

TEST_F(PhysicsTest, SoilNoxSensitivity) {
    PhysicsSchemeConfig cfg;
    cfg.name = "soil_nox";
    auto scheme = PhysicsFactory::CreateScheme(cfg);
    scheme->Initialize(cfg.options, nullptr);

    // Test Moisture Sensitivity (Poisson-like)
    SetFieldValue("gwettop", 0.01);
    SetFieldValue("soil_nox", 0.0, false);
    scheme->Run(import_state, export_state);
    double em_dry = export_state.fields["soil_nox"].view_host()(0, 0, 0);

    SetFieldValue("gwettop", 0.3);  // Optimal
    SetFieldValue("soil_nox", 0.0, false);
    scheme->Run(import_state, export_state);
    double em_opt = export_state.fields["soil_nox"].view_host()(0, 0, 0);

    EXPECT_GT(em_opt, em_dry);
}

TEST_F(PhysicsTest, NativeExampleMultipleInputs) {
    PhysicsSchemeConfig cfg;
    cfg.name = "native_example";

    // Test case 1: Default (no secondary input)
    {
        auto scheme = PhysicsFactory::CreateScheme(cfg);
        scheme->Initialize(cfg.options, nullptr);

        SetFieldValue("nox", 0.0, false);
        SetFieldValue("base_anthropogenic_nox", 1.0);

        scheme->Run(import_state, export_state);

        auto& dv = export_state.fields["nox"];
        dv.sync<Kokkos::HostSpace>();
        EXPECT_NEAR(dv.view_host()(0, 0, 0), 2.0, 1e-6);  // 1.0 * 2.0 (default multiplier)
    }

    // Test case 2: Secondary input from Import State with mapping
    {
        PhysicsSchemeConfig cfg_mapped = cfg;
        cfg_mapped.options = YAML::Load("input_mapping: {secondary_input: custom_import}");

        auto scheme = PhysicsFactory::CreateScheme(cfg_mapped);
        scheme->Initialize(cfg_mapped.options, nullptr);

        import_state.fields["custom_import"] = create_dv("custom_import", 5.0);
        SetFieldValue("nox", 0.0, false);
        SetFieldValue("base_anthropogenic_nox", 1.0);

        scheme->Run(import_state, export_state);

        auto& dv = export_state.fields["nox"];
        dv.sync<Kokkos::HostSpace>();
        EXPECT_NEAR(dv.view_host()(0, 0, 0), 5.0, 1e-6);  // 1.0 * 5.0
    }

    // Test case 3: Secondary input from Export State (chaining schemes)
    {
        PhysicsSchemeConfig cfg_chained = cfg;
        cfg_chained.options = YAML::Load("input_mapping: {secondary_input: SALA}");

        auto scheme = PhysicsFactory::CreateScheme(cfg_chained);
        scheme->Initialize(cfg_chained.options, nullptr);

        SetFieldValue("SALA", 10.0, false);
        SetFieldValue("nox", 0.0, false);
        SetFieldValue("base_anthropogenic_nox", 1.0);

        scheme->Run(import_state, export_state);

        auto& dv = export_state.fields["nox"];
        dv.sync<Kokkos::HostSpace>();
        EXPECT_NEAR(dv.view_host()(0, 0, 0), 10.0, 1e-6);  // 1.0 * 10.0
    }

    // Test case 4: Output mapping verification
    {
        PhysicsSchemeConfig cfg_out_mapped = cfg;
        cfg_out_mapped.options = YAML::Load("output_mapping: {nox: custom_nox}");

        auto scheme = PhysicsFactory::CreateScheme(cfg_out_mapped);
        scheme->Initialize(cfg_out_mapped.options, nullptr);

        export_state.fields["custom_nox"] = create_dv("custom_nox", 0.0);
        SetFieldValue("base_anthropogenic_nox", 1.0);

        scheme->Run(import_state, export_state);

        auto& dv = export_state.fields["custom_nox"];
        dv.sync<Kokkos::HostSpace>();
        EXPECT_NEAR(dv.view_host()(0, 0, 0), 2.0, 1e-6);  // 1.0 * 2.0
    }
}

}  // namespace aces

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
