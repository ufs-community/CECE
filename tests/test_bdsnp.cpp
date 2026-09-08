/**
 * @file test_bdsnp.cpp
 * @brief Generic registration and YL95 compatibility checks for BdsnpScheme.
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <conf/conf.hpp>
#include <stdexcept>
#include <string>

#include "cece/cece_physics_factory.hpp"
#include "cece/cece_state.hpp"
#include "cece/physics/cece_bdsnp.hpp"
#include "cece/physics/cece_soil_nox.hpp"

#ifdef CECE_HAS_FORTRAN
#include "cece/physics/cece_bdsnp_fortran.hpp"
#endif

namespace cece {
namespace {

DualView3D MakeField(const std::string& name, double value) {
    DualView3D field(name, 1, 1, 1);
    Kokkos::deep_copy(field.view_host(), value);
    field.modify_host();
    field.sync_device();
    return field;
}

double RunBdsnpYl95(double temperature_k, double soil_moisture) {
    CeceImportState import_state;
    CeceExportState export_state;
    import_state.fields["soil_temperature"] = MakeField("soil_temperature", temperature_k);
    import_state.fields["soil_moisture"] = MakeField("soil_moisture", soil_moisture);
    export_state.fields["soil_nox_emissions"] = MakeField("soil_nox_emissions", -1.0);

    conf::Config config = conf::Config::from_string("soil_no_method: yl95");
    BdsnpScheme scheme;
    scheme.Initialize(config.root(), nullptr);
    scheme.Run(import_state, export_state);

    auto& output = export_state.fields.at("soil_nox_emissions");
    output.sync_host();
    return output.view_host()(0, 0, 0);
}

double RunSoilNoxYl95(double temperature_k, double soil_moisture) {
    CeceImportState import_state;
    CeceExportState export_state;
    import_state.fields["temperature"] = MakeField("temperature", temperature_k);
    import_state.fields["soil_moisture"] = MakeField("soil_moisture", soil_moisture);
    // SoilNoxScheme accumulates into its export; zero isolates its one-run YL95 flux.
    export_state.fields["soil_nox_emissions"] = MakeField("soil_nox_emissions", 0.0);

    SoilNoxScheme scheme;
    conf::Config config = conf::Config::from_string("");
    scheme.Initialize(config.root(), nullptr);
    scheme.Run(import_state, export_state);

    auto& output = export_state.fields.at("soil_nox_emissions");
    output.sync_host();
    return output.view_host()(0, 0, 0);
}

TEST(BdsnpSchemeTest, FactoryCreatesBdsnpScheme) {
    PhysicsSchemeConfig config;
    config.name = "bdsnp";
    auto scheme = PhysicsFactory::CreateScheme(config);
    EXPECT_NE(scheme, nullptr);
}

TEST(BdsnpSchemeTest, SupportedMethodsInitializeAndUnknownMethodsFail) {
    BdsnpScheme canonical;
    conf::Config bdsnp = conf::Config::from_string("soil_no_method: bdsnp");
    EXPECT_NO_THROW(canonical.Initialize(bdsnp.root(), nullptr));

    BdsnpScheme fallback;
    conf::Config yl95 = conf::Config::from_string("soil_no_method: yl95");
    EXPECT_NO_THROW(fallback.Initialize(yl95.root(), nullptr));

    BdsnpScheme removed_selector;
    conf::Config old_name = conf::Config::from_string("soil_no_method: hemco_3_12_1");
    EXPECT_THROW(removed_selector.Initialize(old_name.root(), nullptr), std::invalid_argument);

    BdsnpScheme unknown;
    conf::Config typo = conf::Config::from_string("soil_no_method: not-a-method");
    EXPECT_THROW(unknown.Initialize(typo.root(), nullptr), std::invalid_argument);

    BdsnpScheme obsolete;
    conf::Config removed_option = conf::Config::from_string("fert_emission_factor: 1.0");
    EXPECT_THROW(obsolete.Initialize(removed_option.root(), nullptr), std::invalid_argument);
}

TEST(BdsnpSchemeTest, CanonicalMethodRejectsEveryYl95OnlyCoefficientAlias) {
    for (const char* option : {"biome_coefficient_wet", "a_biome_wet", "temp_limit", "tc_max", "temp_exp_coeff", "exp_coeff", "wet_coeff_1", "wet_c1",
                               "wet_coeff_2", "wet_c2"}) {
        conf::Config config = conf::Config::from_string(std::string("soil_no_method: bdsnp\n") + option + ": 1.0");

        BdsnpScheme scheme;
        try {
            scheme.Initialize(config.root(), nullptr);
            FAIL() << "Expected canonical BDSNP to reject YL95-only option " << option;
        } catch (const std::invalid_argument& error) {
            EXPECT_NE(std::string(error.what()).find(option), std::string::npos);
        }
    }
}

TEST(BdsnpSchemeTest, CanonicalMethodDoesNotLogYl95Defaults) {
    conf::Config config = conf::Config::from_string("soil_no_method: bdsnp");

    testing::internal::CaptureStdout();
    BdsnpScheme scheme;
    scheme.Initialize(config.root(), nullptr);
    const std::string output = testing::internal::GetCapturedStdout();

    EXPECT_EQ(output.find("Using default a_biome_wet"), std::string::npos);
    EXPECT_EQ(output.find("Using default tc_max"), std::string::npos);
    EXPECT_EQ(output.find("Using default exp_coeff"), std::string::npos);
    EXPECT_EQ(output.find("Using default wet_c1"), std::string::npos);
    EXPECT_EQ(output.find("Using default wet_c2"), std::string::npos);
}

TEST(BdsnpSchemeTest, Yl95MethodAcceptsCoefficientAliases) {
    for (const char* option : {"biome_coefficient_wet", "a_biome_wet", "temp_limit", "tc_max", "temp_exp_coeff", "exp_coeff", "wet_coeff_1", "wet_c1",
                               "wet_coeff_2", "wet_c2"}) {
        conf::Config config = conf::Config::from_string(std::string("soil_no_method: yl95\n") + option + ": 1.0");

        BdsnpScheme scheme;
        EXPECT_NO_THROW(scheme.Initialize(config.root(), nullptr)) << "YL95 option: " << option;
    }
}

TEST(BdsnpSchemeTest, DiagnosticFieldsRegisterWhenEnabled) {
    conf::Config config = conf::Config::from_string("soil_no_method: bdsnp\ndiagnostics:\n  - soil_no_emission_rate\nnx: 1\nny: 1\nnz: 1\n");

    CeceDiagnosticManager diagnostics;
    BdsnpScheme scheme;
    EXPECT_NO_THROW(scheme.Initialize(config.root(), &diagnostics));
}

TEST(BdsnpSchemeTest, Yl95MissingFieldsReportEveryRequiredName) {
    CeceImportState import_state;
    CeceExportState export_state;
    conf::Config config = conf::Config::from_string("soil_no_method: yl95");

    BdsnpScheme scheme;
    scheme.Initialize(config.root(), nullptr);
    try {
        scheme.Run(import_state, export_state);
        FAIL() << "Expected missing YL95 fields to fail";
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        EXPECT_NE(message.find("soil_temperature"), std::string::npos);
        EXPECT_NE(message.find("soil_moisture"), std::string::npos);
        EXPECT_NE(message.find("soil_nox_emissions"), std::string::npos);
    }
}

RC_GTEST_PROP(BdsnpProperty, Yl95FreezingProducesExactlyZero, ()) {
    const double temperature_k = 200.0 + (*rc::gen::inRange(0, 7315)) / 100.0;
    RC_PRE(temperature_k < 273.15);
    const double soil_moisture = (*rc::gen::inRange(0, 10001)) / 10000.0;
    RC_ASSERT(RunBdsnpYl95(temperature_k, soil_moisture) == 0.0);
}

RC_GTEST_PROP(BdsnpProperty, Yl95MatchesExistingSoilNoxScheme, ()) {
    const double temperature_k = 274.0 + (*rc::gen::inRange(0, 5601)) / 100.0;
    const double soil_moisture = 0.01 + (*rc::gen::inRange(0, 9900)) / 10000.0;
    const double candidate = RunBdsnpYl95(temperature_k, soil_moisture);
    const double reference = RunSoilNoxYl95(temperature_k, soil_moisture);
    const double tolerance = std::max(std::abs(reference) * 1.0e-6, 1.0e-15);
    RC_ASSERT(std::abs(candidate - reference) <= tolerance);
}

#ifdef CECE_HAS_FORTRAN
RC_GTEST_PROP(BdsnpProperty, Yl95MatchesLegacyFortranInterface, ()) {
    const double temperature_k = 274.0 + (*rc::gen::inRange(0, 5601)) / 100.0;
    const double soil_moisture = 0.01 + (*rc::gen::inRange(0, 9900)) / 10000.0;

    CeceImportState import_state;
    CeceExportState export_state;
    import_state.fields["soil_temperature"] = MakeField("soil_temperature", temperature_k);
    import_state.fields["soil_moisture"] = MakeField("soil_moisture", soil_moisture);
    export_state.fields["soil_nox_emissions"] = MakeField("soil_nox_emissions", -1.0);

    conf::Config config = conf::Config::from_string("soil_no_method: yl95");
    BdsnpFortranScheme scheme;
    scheme.Initialize(config.root(), nullptr);
    scheme.Run(import_state, export_state);

    auto& output = export_state.fields.at("soil_nox_emissions");
    output.sync_host();
    const double reference = output.view_host()(0, 0, 0);
    const double candidate = RunBdsnpYl95(temperature_k, soil_moisture);
    const double tolerance = std::max(std::abs(reference) * 1.0e-6, 1.0e-15);
    RC_ASSERT(std::abs(candidate - reference) <= tolerance);
}
#endif

}  // namespace
}  // namespace cece

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (!Kokkos::is_initialized()) {
        Kokkos::initialize(argc, argv);
    }
    const int result = RUN_ALL_TESTS();
    if (Kokkos::is_initialized()) {
        Kokkos::finalize();
    }
    return result;
}
