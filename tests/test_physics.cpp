#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>

#include "aces/aces_physics_factory.hpp"
#include "aces/aces_state.hpp"
#include "aces/physics/aces_fortran_bridge.hpp"
#include "aces/physics/aces_native_example.hpp"

using namespace aces;

class PhysicsTest : public ::testing::Test {
   protected:
    static void SetUpTestSuite() {
        if (!Kokkos::is_initialized()) Kokkos::initialize();
    }

    static void TearDownTestSuite() {
        // Kokkos::finalize(); // Handled by GTest main if necessary
    }

    int nx = 10, ny = 10, nz = 5;
    AcesImportState import_state;
    AcesExportState export_state;

    void SetUp() override {
        import_state.fields["temperature"] = create_dv("temp");
        import_state.fields["wind_speed_10m"] = create_dv("wind");
        import_state.fields["base_anthropogenic_nox"] = create_dv("base_nox");
        export_state.fields["total_nox_emissions"] = create_dv("total_nox");

        Kokkos::deep_copy(import_state.fields["temperature"].view_host(), 300.0);
        Kokkos::deep_copy(import_state.fields["wind_speed_10m"].view_host(), 5.0);
        Kokkos::deep_copy(import_state.fields["base_anthropogenic_nox"].view_host(), 1.0);
        Kokkos::deep_copy(export_state.fields["total_nox_emissions"].view_host(), 0.0);

        import_state.fields["temperature"].modify<Kokkos::HostSpace>();
        import_state.fields["wind_speed_10m"].modify<Kokkos::HostSpace>();
        import_state.fields["base_anthropogenic_nox"].modify<Kokkos::HostSpace>();
        export_state.fields["total_nox_emissions"].modify<Kokkos::HostSpace>();

        import_state.fields["temperature"].sync<Kokkos::DefaultExecutionSpace>();
        import_state.fields["wind_speed_10m"].sync<Kokkos::DefaultExecutionSpace>();
        import_state.fields["base_anthropogenic_nox"].sync<Kokkos::DefaultExecutionSpace>();
        export_state.fields["total_nox_emissions"].sync<Kokkos::DefaultExecutionSpace>();
    }

    DualView3D create_dv(std::string name) {
        return DualView3D(name, nx, ny, nz);
    }
};

TEST_F(PhysicsTest, NativeSchemeTest) {
    PhysicsSchemeConfig config;
    config.name = "native_example";
    auto scheme = PhysicsFactory::CreateScheme(config);
    ASSERT_NE(scheme, nullptr);

    scheme->Run(import_state, export_state);

    auto& dv = export_state.fields["total_nox_emissions"];
    dv.sync<Kokkos::HostSpace>();
    auto hv = dv.view_host();
    EXPECT_DOUBLE_EQ(hv(0, 0, 0), 2.0);  // 0.0 + 1.0 * 2.0
}

TEST_F(PhysicsTest, FortranSchemeTest) {
    PhysicsSchemeConfig config;
    config.name = "fortran_bridge_example";
    auto scheme = PhysicsFactory::CreateScheme(config);
    ASSERT_NE(scheme, nullptr);

    scheme->Run(import_state, export_state);

    auto& dv = export_state.fields["total_nox_emissions"];
    dv.sync<Kokkos::HostSpace>();
    auto hv = dv.view_host();
    EXPECT_DOUBLE_EQ(hv(0, 0, 0), 1.0);  // 0.0 + 1.0
}

TEST_F(PhysicsTest, CombinedSchemesTest) {
    PhysicsSchemeConfig config1;
    config1.name = "native_example";
    PhysicsSchemeConfig config2;
    config2.name = "fortran_bridge_example";

    auto scheme1 = PhysicsFactory::CreateScheme(config1);
    auto scheme2 = PhysicsFactory::CreateScheme(config2);

    scheme1->Run(import_state, export_state);
    scheme2->Run(import_state, export_state);

    auto& dv = export_state.fields["total_nox_emissions"];
    dv.sync<Kokkos::HostSpace>();
    auto hv = dv.view_host();
    // (0.0 + 1.0 * 2.0) + 1.0 = 3.0
    EXPECT_DOUBLE_EQ(hv(0, 0, 0), 3.0);
}
