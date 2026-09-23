/**
 * @file test_bdsnp_runtime.cpp
 * @brief Production runtime acceptance tests for the canonical BDSNP scheme.
 *
 * Expected values are fixed golden results. The tests intentionally exercise
 * BdsnpScheme rather than computing expectations with a second implementation
 * of the production equations.
 */

#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <conf/conf.hpp>
#include <stdexcept>
#include <string>

#include "cece/cece_state.hpp"
#include "cece/physics/cece_bdsnp.hpp"

namespace cece {
namespace {

struct GoldenCase {
    const char* name;
    bool use_soil_temperature;
    int biome;
    double temperature_c;
    double soil_moisture;
    double arid_fraction;
    double nonarid_fraction;
    double leaf_area_index;
    double canopy_nox;
    double wind_speed_squared;
    double solar_zenith_cosine;
    double soil_fertilizer;
    double deposited_nitrogen;
    double pulse_factor;
    double expected_total;
    double expected_added_n;
};

void ExpectGoldenNear(double actual, double expected) {
    ASSERT_TRUE(std::isfinite(actual));
    ASSERT_TRUE(std::isfinite(expected));
    if (expected == 0.0) {
        EXPECT_DOUBLE_EQ(actual, 0.0);
        return;
    }
    const double tolerance = std::max(2.0e-12 * std::abs(expected), 1.0e-27);
    EXPECT_NEAR(actual, expected, tolerance);
}

class BdsnpRuntimeTest : public ::testing::Test {
   protected:
    static constexpr int kNx = 2;
    static constexpr int kNy = 2;

    CeceImportState import_state;
    CeceExportState export_state;

    static conf::Config CanonicalConfig(bool use_soil_temperature) {
        return conf::Config::from_string(std::string("soil_no_method: bdsnp\nuse_soil_temperature: ") + (use_soil_temperature ? "true" : "false"));
    }

    DualView3D MakeField(const std::string& name, int levels, double value, int nx = kNx, int ny = kNy) {
        DualView3D field(name, nx, ny, levels);
        Kokkos::deep_copy(field.view_host(), value);
        field.modify<Kokkos::HostSpace>();
        field.sync<Kokkos::DefaultExecutionSpace>();
        return field;
    }

    void AddImport(const std::string& name, int levels, double value) {
        import_state.fields[name] = MakeField(name, levels, value);
    }

    void SetImportLayer(const std::string& name, int layer, double value) {
        auto& field = import_state.fields.at(name);
        field.sync<Kokkos::HostSpace>();
        auto host = field.view_host();
        for (int i = 0; i < kNx; ++i) {
            for (int j = 0; j < kNy; ++j) {
                host(i, j, layer) = value;
            }
        }
        field.modify<Kokkos::HostSpace>();
        field.sync<Kokkos::DefaultExecutionSpace>();
    }

    void AddOutputs() {
        export_state.fields["soil_nox_emissions"] = MakeField("soil_nox_emissions", 1, -1.0);
        export_state.fields["soil_nox_fertilizer_emissions"] = MakeField("soil_nox_fertilizer_emissions", 1, -1.0);
    }

    void BuildInputs(const GoldenCase& test_case) {
        import_state.fields.clear();
        export_state.fields.clear();

        const std::string temperature_name = test_case.use_soil_temperature ? "soil_temperature" : "surface_temperature";
        AddImport(temperature_name, 1, test_case.temperature_c + 273.15);
        AddImport("soil_moisture", 1, test_case.soil_moisture);
        AddImport("soilnox_land_fractions", 24, 0.0);
        AddImport("soilnox_arid_fraction", 1, test_case.arid_fraction);
        AddImport("soilnox_nonarid_fraction", 1, test_case.nonarid_fraction);
        AddImport("leaf_area_index", 1, test_case.leaf_area_index);
        AddImport("soilnox_canopy_nox", 24, 0.0);
        AddImport("wind_speed_squared", 1, test_case.wind_speed_squared);
        AddImport("solar_zenith_cosine", 1, test_case.solar_zenith_cosine);
        AddImport("soil_fertilizer", 1, test_case.soil_fertilizer);
        AddImport("deposited_nitrogen", 1, test_case.deposited_nitrogen);
        AddImport("soilnox_pulse_factor", 1, test_case.pulse_factor);
        SetImportLayer("soilnox_land_fractions", test_case.biome - 1, 1.0);
        SetImportLayer("soilnox_canopy_nox", test_case.biome - 1, test_case.canopy_nox);
        AddOutputs();
    }

    void RunCanonical(bool use_soil_temperature) {
        BdsnpScheme scheme;
        scheme.Initialize(CanonicalConfig(use_soil_temperature).root(), nullptr);
        scheme.Run(import_state, export_state);
    }

    void ExpectOutputs(double expected_total, double expected_added_n) {
        auto& total = export_state.fields.at("soil_nox_emissions");
        auto& added_n = export_state.fields.at("soil_nox_fertilizer_emissions");
        total.sync<Kokkos::HostSpace>();
        added_n.sync<Kokkos::HostSpace>();
        const auto total_host = total.view_host();
        const auto added_n_host = added_n.view_host();
        for (int i = 0; i < kNx; ++i) {
            for (int j = 0; j < kNy; ++j) {
                ExpectGoldenNear(total_host(i, j, 0), expected_total);
                ExpectGoldenNear(added_n_host(i, j, 0), expected_added_n);
            }
        }
    }

    void RunAndCheck(const GoldenCase& test_case) {
        SCOPED_TRACE(test_case.name);
        BuildInputs(test_case);
        RunCanonical(test_case.use_soil_temperature);
        ExpectOutputs(test_case.expected_total, test_case.expected_added_n);
    }
};

TEST_F(BdsnpRuntimeTest, AllTwentyFourOneHotBiomesMatchGoldenValues) {
    constexpr double expected_total[] = {
        0.00000000000000000E+000, 0.00000000000000000E+000, 0.00000000000000000E+000, 0.00000000000000000E+000, 0.00000000000000000E+000,
        1.01005483916639623E-012, 1.51508225874959454E-012, 1.51508225874959454E-012, 1.68342473194399405E-013, 1.41407677483295504E-011,
        1.41407677483295504E-011, 4.04021935666558491E-012, 7.07038387416477520E-012, 1.04372333380527629E-011, 5.05027419583198113E-013,
        6.06032903499837817E-012, 6.06032903499837817E-012, 5.89198656180397866E-012, 2.79448505502703002E-011, 1.34673978555519524E-012,
        7.40706882055357502E-012, 9.59552097208076779E-012, 9.59552097208076779E-012, 9.59552097208076779E-012,
    };

    for (int biome = 1; biome <= 24; ++biome) {
        SCOPED_TRACE(::testing::Message() << "biome=" << biome);
        const GoldenCase test_case = {"one_hot_biome",           true, biome, 20.0, 0.3, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0,
                                      expected_total[biome - 1], 0.0};
        RunAndCheck(test_case);
    }
}

TEST_F(BdsnpRuntimeTest, TemperatureWetnessCanopyAndAddedNitrogenBranchesMatchGoldenValues) {
    constexpr GoldenCase cases[] = {
        {"air_dry_freeze", false, 6, -5.0, 0.2, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0},
        {"air_dry_at_zero", false, 6, 0.0, 0.2, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.89576908932480631E-013, 0.0},
        {"air_dry_cap", false, 6, 25.0, 0.2, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 2.48939451711858147E-012, 0.0},
        {"air_wet_biome_2", false, 2, 10.0, 0.5, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0},
        {"air_wet_biome_2_added_n", false, 2, 10.0, 0.5, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 1.0E-4, 0.0, 1.0, 6.00912331250967E-014,
         6.00912331250967E-014},
        {"air_wet_biome_6", false, 6, 10.0, 0.5, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 4.31292073768097301E-013, 0.0},
        {"air_wet_biome_15", false, 15, 10.0, 0.5, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.51932804187535274E-013, 0.0},
        {"air_wet_biome_22", false, 22, 10.0, 0.5, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 3.26650970241171153E-012, 0.0},
        {"soil_negative", true, 6, -1.0, 0.2, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0},
        {"soil_10", true, 6, 10.0, 0.2, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 3.17283214219394639E-013, 0.0},
        {"soil_20", true, 6, 20.0, 0.2, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 8.88731172332905519E-013, 0.0},
        {"soil_above_20", true, 6, 20.000001, 0.2, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 9.52507100972130979E-013, 0.0},
        {"soil_30", true, 6, 30.0, 0.2, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 3.47055171619283452E-012, 0.0},
        {"soil_40", true, 6, 40.0, 0.2, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 6.60026792493154164E-012, 0.0},
        {"soil_45_cap", true, 6, 45.0, 0.2, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 6.60026792493154164E-012, 0.0},
        {"wet_arid_zero", true, 6, 20.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0},
        {"wet_arid_02", true, 6, 20.0, 0.2, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.00832624108810336E-012, 0.0},
        {"wet_arid_03", true, 6, 20.0, 0.3, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 8.09577216327002880E-013, 0.0},
        {"wet_arid_1", true, 6, 20.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 3.09768527333926151E-017, 0.0},
        {"wet_nonarid_02", true, 6, 20.0, 0.2, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 8.88731172332905519E-013, 0.0},
        {"wet_nonarid_03", true, 6, 20.0, 0.3, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.01005483916639623E-012, 0.0},
        {"wet_nonarid_1", true, 6, 20.0, 1.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 2.15684739439270443E-014, 0.0},
        {"wet_tie_is_arid", true, 6, 20.0, 0.2, 0.5, 0.5, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.00832624108810336E-012, 0.0},
        {"wet_zero_is_nonarid", true, 6, 20.0, 0.3, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.01005483916639623E-012, 0.0},
        {"canopy_day", true, 21, 20.0, 0.3, 0.0, 1.0, 7.0, 0.02, 9.0, 1.0, 0.0, 0.0, 1.0, 2.46902294018452460E-012, 0.0},
        {"canopy_night", true, 21, 20.0, 0.3, 0.0, 1.0, 7.0, 0.02, 9.0, 0.0, 0.0, 0.0, 1.0, 6.73369892777597114E-013, 0.0},
        {"canopy_exc_0_1", true, 6, 20.0, 0.3, 0.0, 1.0, 7.0, 0.02, 9.0, 1.0, 1.0E-4, 0.0, 1.0, 1.123286379405761E-012, 1.6132938972347878E-013},
        {"canopy_exc_0_5", true, 2, 20.0, 0.3, 0.0, 1.0, 7.0, 0.02, 9.0, 1.0, 1.0E-4, 0.0, 1.0, 1.3551668736772218E-013, 1.3551668736772218E-013},
        {"canopy_exc_1", true, 10, 20.0, 0.3, 0.0, 1.0, 7.0, 0.02, 9.0, 1.0, 1.0E-4, 0.0, 1.0, 9.540109071692802E-012, 1.1293057280643515E-013},
        {"canopy_exc_2", true, 14, 20.0, 0.3, 0.0, 1.0, 7.0, 0.02, 9.0, 1.0, 1.0E-4, 0.0, 1.0, 5.3033145986312085E-012, 8.469792960482637E-014},
        {"canopy_exc_4", true, 15, 20.0, 0.3, 0.0, 1.0, 7.0, 0.02, 9.0, 1.0, 1.0E-4, 0.0, 1.0, 2.248077595976169E-013, 5.646528640321756E-014},
        {"canopy_lai_zero", true, 21, 20.0, 0.3, 0.0, 1.0, 0.0, 0.02, 9.0, 1.0, 0.0, 0.0, 1.0, 7.40706882055357502E-012, 0.0},
        {"canopy_nox_zero", true, 21, 20.0, 0.3, 0.0, 1.0, 7.0, 0.0, 9.0, 1.0, 0.0, 0.0, 1.0, 7.40706882055357502E-012, 0.0},
        {"added_n_zero", true, 6, 20.0, 0.3, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.01005483916639623E-012, 0.0},
        {"soil_fertilizer", true, 6, 20.0, 0.3, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 1.0E-4, 0.0, 1.0, 1.17945069837604896E-012, 1.6939585920965273E-013},
        {"deposited_nitrogen", true, 6, 20.0, 0.3, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0E-4, 1.0, 1.17945069837604896E-012, 1.6939585920965273E-013},
        {"combined_added_n", true, 6, 20.0, 0.3, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 1.0E-4, 2.0E-4, 1.0, 1.51824241679535422E-012, 5.081875776289581E-013},
    };

    for (const auto& test_case : cases) {
        RunAndCheck(test_case);
    }
}

TEST_F(BdsnpRuntimeTest, PulseFactorScalesNaturalAndAddedNitrogenTerms) {
    const GoldenCase pulse_case = {
        "pulse_factor", true, 6, 20.0, 0.3, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 1.0E-4, 2.0E-4, 0.37, 5.617496942142811E-013, 1.880294037227145E-013};
    RunAndCheck(pulse_case);
}

TEST_F(BdsnpRuntimeTest, MixedBiomesUseTheirExactFractionalWeights) {
    const GoldenCase mixed = {"mixed_biomes",        false, 6, 20.0, 0.2, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 1.0E-4, 2.0E-4, 1.0, 1.6738069096054774E-011,
                              7.483610257141477E-013};
    BuildInputs(mixed);
    SetImportLayer("soilnox_land_fractions", 5, 0.25);
    SetImportLayer("soilnox_land_fractions", 9, 0.75);
    RunCanonical(false);
    ExpectOutputs(mixed.expected_total, mixed.expected_added_n);
}

TEST_F(BdsnpRuntimeTest, ExactNoSoilClassClearsBothOutputs) {
    const GoldenCase no_soil = {"no_soil", true, 6, 20.0, 0.3, 0.0, 1.0, 7.0, 0.02, 9.0, 1.0, 1.0, 2.0, 4.0, 0.0, 0.0};
    BuildInputs(no_soil);
    SetImportLayer("soilnox_land_fractions", 5, 0.0);
    SetImportLayer("soilnox_land_fractions", 0, 1.0);
    RunCanonical(true);
    ExpectOutputs(0.0, 0.0);
}

TEST_F(BdsnpRuntimeTest, DefaultConfigurationSelectsCanonicalBdsnp) {
    const GoldenCase default_case = {"default_bdsnp", false, 6, 0.0, 0.2, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.89576908932480631E-013, 0.0};
    BuildInputs(default_case);

    BdsnpScheme scheme;
    scheme.Initialize(conf::Config::from_string("").root(), nullptr);
    scheme.Run(import_state, export_state);
    ExpectOutputs(default_case.expected_total, default_case.expected_added_n);
}

TEST_F(BdsnpRuntimeTest, RemovedVersionPinnedSelectorIsRejected) {
    conf::Config config = conf::Config::from_string("soil_no_method: hemco_3_12_1");
    BdsnpScheme scheme;
    EXPECT_THROW(scheme.Initialize(config.root(), nullptr), std::invalid_argument);
}

TEST_F(BdsnpRuntimeTest, MissingAndMalformedProductionContractFailsLoudly) {
    BdsnpScheme missing_scheme;
    missing_scheme.Initialize(CanonicalConfig(false).root(), nullptr);
    try {
        missing_scheme.Run(import_state, export_state);
        FAIL() << "Expected missing canonical BDSNP fields to fail";
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        for (const char* name :
             {"surface_temperature", "soil_moisture", "soilnox_land_fractions", "soilnox_arid_fraction", "soilnox_nonarid_fraction",
              "leaf_area_index", "soilnox_canopy_nox", "wind_speed_squared", "solar_zenith_cosine", "soil_fertilizer", "deposited_nitrogen",
              "soilnox_pulse_factor", "soil_nox_emissions", "soil_nox_fertilizer_emissions"}) {
            EXPECT_NE(message.find(name), std::string::npos) << "missing field name: " << name;
        }
    }

    const GoldenCase valid = {"shape_base", false, 6, 20.0, 0.2, 0.0, 1.0, 0.0, 0.0, 9.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    auto expect_shape_failure = [this, &valid](const auto& mutate) {
        BuildInputs(valid);
        mutate();
        BdsnpScheme scheme;
        scheme.Initialize(CanonicalConfig(false).root(), nullptr);
        EXPECT_THROW(scheme.Run(import_state, export_state), std::runtime_error);
    };

    expect_shape_failure([this]() { import_state.fields["soil_moisture"] = MakeField("bad_soil_moisture", 2, 0.2); });
    expect_shape_failure([this]() { import_state.fields["surface_temperature"] = MakeField("bad_surface_temperature", 1, 293.15, kNx + 1, kNy); });
    expect_shape_failure([this]() { import_state.fields["soilnox_land_fractions"] = MakeField("bad_land_fractions", 23, 0.0); });
    expect_shape_failure([this]() { import_state.fields["soilnox_canopy_nox"] = MakeField("bad_canopy_nox", 23, 0.0); });
    expect_shape_failure([this]() { export_state.fields["soil_nox_emissions"] = MakeField("bad_soil_nox_emissions", 2, 0.0); });
    expect_shape_failure([this]() { export_state.fields["soil_nox_fertilizer_emissions"] = MakeField("bad_fertilizer_emissions", 2, 0.0); });
}

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
