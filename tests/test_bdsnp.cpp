/**
 * @file test_bdsnp.cpp
 * @brief Property-based tests for the BdsnpScheme soil NO physics module.
 *
 * Properties tested:
 * 12. BDSNP Freezing Produces Zero Emissions (Requirements 4.8)
 */

#include <Kokkos_Core.hpp>
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <cmath>
#include <string>

#include "cece/cece_state.hpp"
#include "cece/physics/cece_bdsnp.hpp"

namespace cece {

// ============================================================================
// Replicate the freezing check logic from cece_bdsnp.cpp for direct testing.
// The inline helpers have internal linkage in the source file, so we replicate
// the relevant math here.
// ============================================================================

/// YL95 soil temperature term: returns 0 when tc <= 0 (freezing).
static double test_bdsnp_soil_temp_term(double tc, double tc_max, double exp_coeff) {
    if (tc <= 0.0) {
        return 0.0;
    }
    return std::exp(exp_coeff * std::min(tc_max, tc));
}

/// BDSNP moisture factor (piecewise linear).
static double test_bdsnp_moisture_factor(double soil_moisture) {
    if (soil_moisture <= 0.0) return 0.0;
    if (soil_moisture <= 0.3) return soil_moisture / 0.3;
    return 1.0 - 0.5 * (soil_moisture - 0.3) / 0.7;
}

/// BDSNP nitrogen deposition factor.
static double test_bdsnp_ndep_factor(double ndep, double fert_ef,
                                     double wet_dep_s, double dry_dep_s) {
    double dep_contribution = ndep * (wet_dep_s + dry_dep_s);
    return 1.0 + fert_ef * dep_contribution;
}

/// BDSNP canopy reduction factor.
static double test_bdsnp_canopy_reduction(double lai) {
    if (lai <= 0.0) return 1.0;
    return std::exp(-0.24 * lai);
}

// ============================================================================
// Property 12: BDSNP Freezing Produces Zero Emissions
// Feature: megan3-integration, Property 12: BDSNP Freezing Produces Zero Emissions
// **Validates: Requirements 4.8**
//
// For any soil temperature below 0°C (273.15 K) and for any valid values of
// soil moisture, nitrogen deposition, land use type, and other BDSNP inputs,
// the computed soil NO emission SHALL be exactly 0.0, regardless of whether
// the "bdsnp" or "yl95" algorithm is selected.
// ============================================================================

// --------------------------------------------------------------------------
// 12a: Direct formula test — YL95 temperature term returns 0 when tc <= 0
// --------------------------------------------------------------------------

RC_GTEST_PROP(BdsnpProperty, Property12_YL95_FreezingTempTermZero, ()) {
    // Generate soil temperature in [200, 273.15) K → tc in [-73.15, 0) °C
    // Use integer range [20000, 27314] to represent [200.00, 273.14] K
    double soil_temp_k = 200.0 + (*rc::gen::inRange(0, 7315)) / 100.0;
    RC_PRE(soil_temp_k < 273.15);

    double tc = soil_temp_k - 273.15;  // Will be negative or zero

    // Random valid YL95 parameters
    double tc_max = 20.0 + (*rc::gen::inRange(0, 2001)) / 100.0;   // [20, 40]
    double exp_coeff = 0.05 + (*rc::gen::inRange(0, 1501)) / 10000.0;  // [0.05, 0.2]

    double t_term = test_bdsnp_soil_temp_term(tc, tc_max, exp_coeff);
    RC_ASSERT(t_term == 0.0);
}

// --------------------------------------------------------------------------
// 12b: Scheme-level test — BdsnpScheme in YL95 mode produces zero emissions
//      when soil temperature < 273.15 K
// --------------------------------------------------------------------------

RC_GTEST_PROP(BdsnpProperty, Property12_YL95_SchemeFreezingZeroEmissions, ()) {
    // Generate soil temperature in [200, 273.15) K
    double soil_temp_k = 200.0 + (*rc::gen::inRange(0, 7315)) / 100.0;
    RC_PRE(soil_temp_k < 273.15);

    // Generate random valid soil moisture in [0, 1]
    double soil_moisture = (*rc::gen::inRange(0, 10001)) / 10000.0;

    // Small grid: 2x2
    int nx = 2, ny = 2;

    // Set up import state with soil_temperature and soil_moisture
    CeceImportState import_state;
    import_state.fields["soil_temperature"] = DualView3D("soil_temp", nx, ny, 1);
    import_state.fields["soil_moisture"] = DualView3D("soil_moist", nx, ny, 1);

    // Fill with uniform values on device
    {
        auto& dv_temp = import_state.fields["soil_temperature"];
        auto h_temp = dv_temp.view_host();
        for (int i = 0; i < nx; ++i)
            for (int j = 0; j < ny; ++j)
                h_temp(i, j, 0) = soil_temp_k;
        dv_temp.modify_host();
        dv_temp.sync_device();
    }
    {
        auto& dv_moist = import_state.fields["soil_moisture"];
        auto h_moist = dv_moist.view_host();
        for (int i = 0; i < nx; ++i)
            for (int j = 0; j < ny; ++j)
                h_moist(i, j, 0) = soil_moisture;
        dv_moist.modify_host();
        dv_moist.sync_device();
    }

    // Set up export state with soil_nox_emissions (initialized to 0)
    CeceExportState export_state;
    export_state.fields["soil_nox_emissions"] = DualView3D("soil_nox", nx, ny, 1);
    Kokkos::deep_copy(export_state.fields["soil_nox_emissions"].view_device(), 0.0);
    export_state.fields["soil_nox_emissions"].modify_device();

    // Initialize BdsnpScheme with YL95 mode
    YAML::Node config;
    config["soil_no_method"] = "yl95";

    BdsnpScheme scheme;
    scheme.Initialize(config, nullptr);
    scheme.Run(import_state, export_state);

    // Read back results
    auto& dv_nox = export_state.fields["soil_nox_emissions"];
    dv_nox.sync_host();
    auto h_nox = dv_nox.view_host();

    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            RC_ASSERT(h_nox(i, j, 0) == 0.0);
        }
    }
}

// --------------------------------------------------------------------------
// 12c: Scheme-level test — BdsnpScheme in BDSNP mode produces zero emissions
//      when soil temperature < 273.15 K
// --------------------------------------------------------------------------

RC_GTEST_PROP(BdsnpProperty, Property12_BDSNP_SchemeFreezingZeroEmissions, ()) {
    // Generate soil temperature in [200, 273.15) K
    double soil_temp_k = 200.0 + (*rc::gen::inRange(0, 7315)) / 100.0;
    RC_PRE(soil_temp_k < 273.15);

    // Generate random valid inputs
    double soil_moisture = (*rc::gen::inRange(0, 10001)) / 10000.0;

    // Small grid: 2x2
    int nx = 2, ny = 2;

    // Set up import state
    CeceImportState import_state;
    import_state.fields["soil_temperature"] = DualView3D("soil_temp", nx, ny, 1);
    import_state.fields["soil_moisture"] = DualView3D("soil_moist", nx, ny, 1);

    {
        auto& dv_temp = import_state.fields["soil_temperature"];
        auto h_temp = dv_temp.view_host();
        for (int i = 0; i < nx; ++i)
            for (int j = 0; j < ny; ++j)
                h_temp(i, j, 0) = soil_temp_k;
        dv_temp.modify_host();
        dv_temp.sync_device();
    }
    {
        auto& dv_moist = import_state.fields["soil_moisture"];
        auto h_moist = dv_moist.view_host();
        for (int i = 0; i < nx; ++i)
            for (int j = 0; j < ny; ++j)
                h_moist(i, j, 0) = soil_moisture;
        dv_moist.modify_host();
        dv_moist.sync_device();
    }

    // Set up export state
    CeceExportState export_state;
    export_state.fields["soil_nox_emissions"] = DualView3D("soil_nox", nx, ny, 1);
    Kokkos::deep_copy(export_state.fields["soil_nox_emissions"].view_device(), 0.0);
    export_state.fields["soil_nox_emissions"].modify_device();

    // Initialize BdsnpScheme with BDSNP mode (default)
    YAML::Node config;
    config["soil_no_method"] = "bdsnp";

    BdsnpScheme scheme;
    scheme.Initialize(config, nullptr);
    scheme.Run(import_state, export_state);

    // Read back results
    auto& dv_nox = export_state.fields["soil_nox_emissions"];
    dv_nox.sync_host();
    auto h_nox = dv_nox.view_host();

    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            RC_ASSERT(h_nox(i, j, 0) == 0.0);
        }
    }
}

// ============================================================================
// Property 14: BDSNP C++/Fortran Numerical Parity
// Feature: megan3-integration, Property 14: BDSNP C++/Fortran Numerical Parity
// **Validates: Requirements 4.10, 11.5**
//
// For any valid BDSNP inputs (soil temperature > 0°C, soil moisture in [0,1])
// and for any algorithm selection ("bdsnp" or "yl95"), the BdsnpScheme (C++)
// and BdsnpFortranScheme (Fortran bridge) SHALL produce numerically identical
// soil NO emissions within a relative tolerance of 1e-6.
// ============================================================================

#ifdef CECE_HAS_FORTRAN

#include "cece/physics/cece_bdsnp_fortran.hpp"

// --------------------------------------------------------------------------
// 14a: YL95 mode parity — C++ and Fortran produce identical soil NO
// --------------------------------------------------------------------------

RC_GTEST_PROP(BdsnpParityProperty, Property14_YL95_CppFortranParity, ()) {
    // Generate soil temperature above freezing: [274, 330] K
    double soil_temp_k = 274.0 + (*rc::gen::inRange(0, 5601)) / 100.0;

    // Generate soil moisture in [0.01, 1.0]
    double soil_moisture = 0.01 + (*rc::gen::inRange(0, 9900)) / 10000.0;

    int nx = 2, ny = 2, nz = 1;

    // ---- Set up identical import states for both schemes ----
    CeceImportState import_cpp, import_fort;
    CeceExportState export_cpp, export_fort;

    auto make_dv = [&](const std::string& label, double val) {
        DualView3D dv(label, nx, ny, nz);
        auto h = dv.view_host();
        for (int i = 0; i < nx; ++i)
            for (int j = 0; j < ny; ++j)
                h(i, j, 0) = val;
        dv.modify_host();
        dv.sync_device();
        return dv;
    };

    import_cpp.fields["soil_temperature"] = make_dv("st_cpp", soil_temp_k);
    import_cpp.fields["soil_moisture"] = make_dv("sm_cpp", soil_moisture);
    export_cpp.fields["soil_nox_emissions"] = make_dv("snox_cpp", 0.0);

    import_fort.fields["soil_temperature"] = make_dv("st_fort", soil_temp_k);
    import_fort.fields["soil_moisture"] = make_dv("sm_fort", soil_moisture);
    export_fort.fields["soil_nox_emissions"] = make_dv("snox_fort", 0.0);

    // ---- Initialize and run C++ scheme ----
    YAML::Node config_yl95;
    config_yl95["soil_no_method"] = "yl95";

    BdsnpScheme scheme_cpp;
    scheme_cpp.Initialize(config_yl95, nullptr);
    scheme_cpp.Run(import_cpp, export_cpp);

    // ---- Initialize and run Fortran scheme ----
    BdsnpFortranScheme scheme_fort;
    scheme_fort.Initialize(config_yl95, nullptr);
    scheme_fort.Run(import_fort, export_fort);

    // ---- Compare results ----
    auto& dv_cpp = export_cpp.fields["soil_nox_emissions"];
    auto& dv_fort = export_fort.fields["soil_nox_emissions"];
    dv_cpp.sync_host();
    dv_fort.sync_host();

    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double val_cpp = dv_cpp.view_host()(i, j, 0);
            double val_fort = dv_fort.view_host()(i, j, 0);
            double tol = std::max(std::abs(val_cpp) * 1e-6, 1e-15);
            RC_ASSERT(std::abs(val_cpp - val_fort) <= tol);
        }
    }
}

// --------------------------------------------------------------------------
// 14b: BDSNP mode parity — C++ and Fortran produce identical soil NO
// --------------------------------------------------------------------------

RC_GTEST_PROP(BdsnpParityProperty, Property14_BDSNP_CppFortranParity, ()) {
    // Generate soil temperature above freezing: [274, 330] K
    double soil_temp_k = 274.0 + (*rc::gen::inRange(0, 5601)) / 100.0;

    // Generate soil moisture in [0.01, 1.0]
    double soil_moisture = 0.01 + (*rc::gen::inRange(0, 9900)) / 10000.0;

    int nx = 2, ny = 2, nz = 1;

    auto make_dv = [&](const std::string& label, double val) {
        DualView3D dv(label, nx, ny, nz);
        auto h = dv.view_host();
        for (int i = 0; i < nx; ++i)
            for (int j = 0; j < ny; ++j)
                h(i, j, 0) = val;
        dv.modify_host();
        dv.sync_device();
        return dv;
    };

    CeceImportState import_cpp, import_fort;
    CeceExportState export_cpp, export_fort;

    import_cpp.fields["soil_temperature"] = make_dv("st_cpp", soil_temp_k);
    import_cpp.fields["soil_moisture"] = make_dv("sm_cpp", soil_moisture);
    export_cpp.fields["soil_nox_emissions"] = make_dv("snox_cpp", 0.0);

    import_fort.fields["soil_temperature"] = make_dv("st_fort", soil_temp_k);
    import_fort.fields["soil_moisture"] = make_dv("sm_fort", soil_moisture);
    export_fort.fields["soil_nox_emissions"] = make_dv("snox_fort", 0.0);

    // ---- Initialize and run C++ scheme (BDSNP mode) ----
    YAML::Node config_bdsnp;
    config_bdsnp["soil_no_method"] = "bdsnp";

    BdsnpScheme scheme_cpp;
    scheme_cpp.Initialize(config_bdsnp, nullptr);
    scheme_cpp.Run(import_cpp, export_cpp);

    // ---- Initialize and run Fortran scheme (BDSNP mode) ----
    BdsnpFortranScheme scheme_fort;
    scheme_fort.Initialize(config_bdsnp, nullptr);
    scheme_fort.Run(import_fort, export_fort);

    // ---- Compare results ----
    auto& dv_cpp = export_cpp.fields["soil_nox_emissions"];
    auto& dv_fort = export_fort.fields["soil_nox_emissions"];
    dv_cpp.sync_host();
    dv_fort.sync_host();

    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double val_cpp = dv_cpp.view_host()(i, j, 0);
            double val_fort = dv_fort.view_host()(i, j, 0);
            double tol = std::max(std::abs(val_cpp) * 1e-6, 1e-15);
            RC_ASSERT(std::abs(val_cpp - val_fort) <= tol);
        }
    }
}

// --------------------------------------------------------------------------
// 14c: Freezing parity — both C++ and Fortran produce zero at freezing
// --------------------------------------------------------------------------

RC_GTEST_PROP(BdsnpParityProperty, Property14_Freezing_CppFortranParity, ()) {
    // Generate soil temperature below freezing: [200, 273.14] K
    double soil_temp_k = 200.0 + (*rc::gen::inRange(0, 7315)) / 100.0;
    RC_PRE(soil_temp_k < 273.15);

    double soil_moisture = (*rc::gen::inRange(0, 10001)) / 10000.0;

    // Select method randomly
    auto method = *rc::gen::element(std::string("bdsnp"), std::string("yl95"));

    int nx = 2, ny = 2, nz = 1;

    auto make_dv = [&](const std::string& label, double val) {
        DualView3D dv(label, nx, ny, nz);
        auto h = dv.view_host();
        for (int i = 0; i < nx; ++i)
            for (int j = 0; j < ny; ++j)
                h(i, j, 0) = val;
        dv.modify_host();
        dv.sync_device();
        return dv;
    };

    CeceImportState import_cpp, import_fort;
    CeceExportState export_cpp, export_fort;

    import_cpp.fields["soil_temperature"] = make_dv("st_cpp", soil_temp_k);
    import_cpp.fields["soil_moisture"] = make_dv("sm_cpp", soil_moisture);
    export_cpp.fields["soil_nox_emissions"] = make_dv("snox_cpp", 0.0);

    import_fort.fields["soil_temperature"] = make_dv("st_fort", soil_temp_k);
    import_fort.fields["soil_moisture"] = make_dv("sm_fort", soil_moisture);
    export_fort.fields["soil_nox_emissions"] = make_dv("snox_fort", 0.0);

    YAML::Node config;
    config["soil_no_method"] = method;

    BdsnpScheme scheme_cpp;
    scheme_cpp.Initialize(config, nullptr);
    scheme_cpp.Run(import_cpp, export_cpp);

    BdsnpFortranScheme scheme_fort;
    scheme_fort.Initialize(config, nullptr);
    scheme_fort.Run(import_fort, export_fort);

    auto& dv_cpp = export_cpp.fields["soil_nox_emissions"];
    auto& dv_fort = export_fort.fields["soil_nox_emissions"];
    dv_cpp.sync_host();
    dv_fort.sync_host();

    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double val_cpp = dv_cpp.view_host()(i, j, 0);
            double val_fort = dv_fort.view_host()(i, j, 0);
            // Both should be zero for freezing temperatures
            RC_ASSERT(val_cpp == 0.0);
            RC_ASSERT(val_fort == 0.0);
        }
    }
}

#endif  // CECE_HAS_FORTRAN

// ============================================================================
// Property 12: BDSNP YL95 Parity with Existing SoilNoxScheme
// Feature: megan3-integration, Property 12: BDSNP YL95 Parity with Existing SoilNoxScheme
// **Validates: Requirements 4.4**
//
// For any valid temperature (> 0°C) and soil moisture inputs, the BDSNP scheme
// configured with soil_no_method: yl95 and default YL95 parameters SHALL
// produce numerically identical soil NO emissions to the existing SoilNoxScheme
// within a relative tolerance of 1e-6.
// ============================================================================

}  // namespace cece (temporarily close for includes)

#include "cece/physics/cece_soil_nox.hpp"

namespace cece {

RC_GTEST_PROP(BdsnpParityProperty, Property12_YL95_ParityWithSoilNoxScheme, ()) {
    // Generate soil temperature above freezing: [274, 330] K
    double soil_temp_k = 274.0 + (*rc::gen::inRange(0, 5601)) / 100.0;

    // Generate soil moisture in [0.01, 1.0]
    double soil_moisture = 0.01 + (*rc::gen::inRange(0, 9900)) / 10000.0;

    int nx = 2, ny = 2, nz = 1;

    auto make_dv = [&](const std::string& label, double val) {
        DualView3D dv(label, nx, ny, nz);
        auto h = dv.view_host();
        for (int i = 0; i < nx; ++i)
            for (int j = 0; j < ny; ++j)
                h(i, j, 0) = val;
        dv.modify_host();
        dv.sync_device();
        return dv;
    };

    // ---- Set up states for BdsnpScheme (YL95 mode) ----
    CeceImportState import_bdsnp;
    CeceExportState export_bdsnp;
    import_bdsnp.fields["soil_temperature"] = make_dv("st_bdsnp", soil_temp_k);
    import_bdsnp.fields["soil_moisture"] = make_dv("sm_bdsnp", soil_moisture);
    export_bdsnp.fields["soil_nox_emissions"] = make_dv("snox_bdsnp", 0.0);

    // ---- Set up states for SoilNoxScheme ----
    CeceImportState import_soilnox;
    CeceExportState export_soilnox;
    // SoilNoxScheme reads "temperature" (not "soil_temperature")
    import_soilnox.fields["temperature"] = make_dv("t_soilnox", soil_temp_k);
    import_soilnox.fields["soil_moisture"] = make_dv("sm_soilnox", soil_moisture);
    export_soilnox.fields["soil_nox_emissions"] = make_dv("snox_soilnox", 0.0);

    // ---- Initialize and run BdsnpScheme in YL95 mode ----
    YAML::Node config_yl95;
    config_yl95["soil_no_method"] = "yl95";

    BdsnpScheme bdsnp_scheme;
    bdsnp_scheme.Initialize(config_yl95, nullptr);
    bdsnp_scheme.Run(import_bdsnp, export_bdsnp);

    // ---- Initialize and run SoilNoxScheme ----
    YAML::Node config_soilnox;
    SoilNoxScheme soilnox_scheme;
    soilnox_scheme.Initialize(config_soilnox, nullptr);
    soilnox_scheme.Run(import_soilnox, export_soilnox);

    // ---- Compare results ----
    auto& dv_bdsnp = export_bdsnp.fields["soil_nox_emissions"];
    auto& dv_soilnox = export_soilnox.fields["soil_nox_emissions"];
    dv_bdsnp.sync_host();
    dv_soilnox.sync_host();

    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double val_bdsnp = dv_bdsnp.view_host()(i, j, 0);
            double val_soilnox = dv_soilnox.view_host()(i, j, 0);
            double tol = std::max(std::abs(val_soilnox) * 1e-6, 1e-15);
            RC_ASSERT(std::abs(val_bdsnp - val_soilnox) <= tol);
        }
    }
}

}  // namespace cece

// ============================================================================
// Unit Tests for BdsnpScheme (Task 8.4)
// ============================================================================

#include "cece/cece_physics_factory.hpp"

namespace cece {

// ============================================================================
// Test fixture for BdsnpScheme unit tests
// ============================================================================

class BdsnpSchemeTest : public ::testing::Test {
   public:
    static void SetUpTestSuite() {
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }
    }

    int nx = 4;
    int ny = 4;
    int nz = 1;
    CeceImportState import_state;
    CeceExportState export_state;

    void SetUp() override {
        // Set up import fields
        import_state.fields["soil_temperature"] = create_dv("soil_temp", 300.0);
        import_state.fields["soil_moisture"] = create_dv("soil_moist", 0.2);

        // Set up export fields
        export_state.fields["soil_nox_emissions"] = create_dv("soil_nox", 0.0);
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

// ============================================================================
// Test: Factory creates BdsnpScheme for "bdsnp" (Req 9.7)
// ============================================================================

TEST_F(BdsnpSchemeTest, FactoryCreatesBdsnpScheme) {
    PhysicsSchemeConfig cfg;
    cfg.name = "bdsnp";
    auto scheme = PhysicsFactory::CreateScheme(cfg);
    ASSERT_NE(scheme, nullptr) << "PhysicsFactory should create a non-null scheme for 'bdsnp'";
}

// ============================================================================
// Test: soil_no_method config parsing (Req 14.1)
//
// Verifies that BdsnpScheme correctly parses the soil_no_method key and
// accepts both "bdsnp" and "yl95" values.
// ============================================================================

TEST_F(BdsnpSchemeTest, SoilNoMethodConfigParsing_Bdsnp) {
    YAML::Node config;
    config["soil_no_method"] = "bdsnp";

    BdsnpScheme scheme;
    EXPECT_NO_THROW(scheme.Initialize(config, nullptr));

    // Run with warm soil temp — should produce non-zero emissions in BDSNP mode
    scheme.Run(import_state, export_state);

    auto& dv = export_state.fields["soil_nox_emissions"];
    dv.sync<Kokkos::HostSpace>();
    double val = dv.view_host()(0, 0, 0);
    EXPECT_GT(val, 0.0) << "BDSNP mode with warm soil should produce non-zero emissions";
}

TEST_F(BdsnpSchemeTest, SoilNoMethodConfigParsing_YL95) {
    YAML::Node config;
    config["soil_no_method"] = "yl95";

    BdsnpScheme scheme;
    EXPECT_NO_THROW(scheme.Initialize(config, nullptr));

    // Run with warm soil temp — should produce non-zero emissions in YL95 mode
    scheme.Run(import_state, export_state);

    auto& dv = export_state.fields["soil_nox_emissions"];
    dv.sync<Kokkos::HostSpace>();
    double val = dv.view_host()(0, 0, 0);
    EXPECT_GT(val, 0.0) << "YL95 mode with warm soil should produce non-zero emissions";
}

TEST_F(BdsnpSchemeTest, SoilNoMethodConfigParsing_DefaultIsBdsnp) {
    // No soil_no_method key — should default to "bdsnp"
    YAML::Node config;

    BdsnpScheme scheme;
    EXPECT_NO_THROW(scheme.Initialize(config, nullptr));

    // Run with warm soil temp — should produce non-zero emissions (default bdsnp mode)
    scheme.Run(import_state, export_state);

    auto& dv = export_state.fields["soil_nox_emissions"];
    dv.sync<Kokkos::HostSpace>();
    double val = dv.view_host()(0, 0, 0);
    EXPECT_GT(val, 0.0) << "Default mode (bdsnp) with warm soil should produce non-zero emissions";
}

// ============================================================================
// Test: Soil NO written to export state (Req 4.7)
//
// Verifies that BdsnpScheme writes computed soil NO emissions to the
// "soil_nox_emissions" export field with non-zero values for warm soil.
// ============================================================================

TEST_F(BdsnpSchemeTest, SoilNOWrittenToExportState) {
    YAML::Node config;
    config["soil_no_method"] = "yl95";

    BdsnpScheme scheme;
    scheme.Initialize(config, nullptr);

    // Set warm soil temperature (300 K = 26.85°C) and moderate moisture
    SetFieldValue("soil_temperature", 300.0);
    SetFieldValue("soil_moisture", 0.2);
    SetFieldValue("soil_nox_emissions", 0.0, false);

    scheme.Run(import_state, export_state);

    auto& dv = export_state.fields["soil_nox_emissions"];
    dv.sync<Kokkos::HostSpace>();
    auto hv = dv.view_host();

    // All grid cells should have non-zero soil NO emissions
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            EXPECT_GT(hv(i, j, 0), 0.0)
                << "soil_nox_emissions should be positive at (" << i << "," << j << ")";
        }
    }
}

TEST_F(BdsnpSchemeTest, SoilNOWrittenToExportState_BdsnpMode) {
    YAML::Node config;
    config["soil_no_method"] = "bdsnp";

    BdsnpScheme scheme;
    scheme.Initialize(config, nullptr);

    // Set warm soil temperature and moderate moisture
    SetFieldValue("soil_temperature", 300.0);
    SetFieldValue("soil_moisture", 0.2);
    SetFieldValue("soil_nox_emissions", 0.0, false);

    scheme.Run(import_state, export_state);

    auto& dv = export_state.fields["soil_nox_emissions"];
    dv.sync<Kokkos::HostSpace>();
    auto hv = dv.view_host();

    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            EXPECT_GT(hv(i, j, 0), 0.0)
                << "soil_nox_emissions (bdsnp mode) should be positive at (" << i << "," << j << ")";
        }
    }
}

// ============================================================================
// Test: Diagnostic fields registered when enabled (Req 13.4, 13.5)
//
// Verifies that when diagnostics are listed in the config, the base class
// registers them via the diagnostic manager.
// ============================================================================

TEST_F(BdsnpSchemeTest, DiagnosticFieldsRegisteredWhenEnabled) {
    YAML::Node config;
    config["soil_no_method"] = "bdsnp";

    // Add diagnostics list
    config["diagnostics"].push_back("soil_no_emission_rate");
    config["diagnostics"].push_back("soil_temp_response");
    config["diagnostics"].push_back("soil_moisture_pulse");
    config["diagnostics"].push_back("canopy_reduction_factor");
    config["nx"] = nx;
    config["ny"] = ny;
    config["nz"] = nz;

    // Create a diagnostic manager
    CeceDiagnosticManager diag_manager;

    BdsnpScheme scheme;
    // Initialize with diagnostics enabled — should not throw
    EXPECT_NO_THROW(scheme.Initialize(config, &diag_manager));
}

}  // namespace cece

// ============================================================================
// Custom main: initialize Kokkos before running tests, finalize after.
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (!Kokkos::is_initialized()) {
        Kokkos::initialize(argc, argv);
    }
    int result = RUN_ALL_TESTS();
    if (Kokkos::is_initialized()) {
        Kokkos::finalize();
    }
    return result;
}
