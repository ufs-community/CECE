/**
 * @file test_hemco_megan_runtime.cpp
 * @brief Source-conformance tests for stateless HEMCO 3.12.1 MEGAN isoprene arithmetic.
 *
 * The implementation and oracle are separate-language transcriptions of the
 * pinned HEMCO source identified in tests/data/hemco_megan/README.md. These are
 * scalar/source-conformance tests, not evidence from an executed HEMCO run.
 */

#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <conf/config.hpp>
#include <fstream>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "cece/physics/cece_megan.hpp"

namespace cece::hemco_megan::v3_12_1 {

namespace {

constexpr double kTol = 1.0e-12;

double SourceNormalizationFactor() {
    const double phi = 0.6;
    const double bbb = 1.0 + 0.0005 * (400.0 - 400.0);
    const double aaa = 2.46 * bbb * phi - 0.9 * phi * phi;
    const double gamma_par = 0.866 * aaa;
    const double gamma_lai = 0.49 * 5.0 / std::sqrt(1.0 + 0.2 * 5.0 * 5.0);
    const double gamma_age = 0.1 * 0.6 + 0.8 * 1.0 + 0.1 * 0.9;
    const double e_opt = 2.0 * std::exp(0.08 * (297.0 - 297.0));
    const double t_opt = 313.0 + 0.6 * (297.0 - 297.0);
    const double x = (1.0 / t_opt - 1.0 / 303.0) / 8.3144598e-3;
    const double gamma_t_ld = e_opt * 200.0 * std::exp(95.0 * x) / (200.0 - 95.0 * (1.0 - std::exp(200.0 * x)));
    return 1.0 / (gamma_age * gamma_lai * gamma_par * gamma_t_ld);
}

struct OracleRow {
    std::string case_id;
    double T_K;
    double lai;
    double lai_prev;
    double pardr_Wm2;
    double pardf_Wm2;
    double suncos;
    double gwetroot;
    double co2_ppm;
    double expected;
};

std::vector<OracleRow> LoadOracle(const std::string& path) {
    std::vector<OracleRow> rows;
    std::ifstream stream(path);
    if (!stream.is_open()) return rows;

    std::string line;
    std::getline(stream, line);
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        std::istringstream values(line);
        OracleRow row{};
        char comma;
        std::getline(values, row.case_id, ',');
        if (!(values >> row.T_K >> comma >> row.lai >> comma >> row.lai_prev >> comma >> row.pardr_Wm2 >> comma >> row.pardf_Wm2 >> comma >>
              row.suncos >> comma >> row.gwetroot >> comma >> row.co2_ppm >> comma >> row.expected)) {
            continue;
        }
        rows.push_back(row);
    }
    return rows;
}

std::vector<OracleRow> gOracleRows;

struct OracleLoader {
    OracleLoader() {
        const std::string path = std::string(CECE_SOURCE_DIR) + "/tests/data/hemco_megan/hemco_3_12_1_megan_reference.csv";
        gOracleRows = LoadOracle(path);
    }
} gLoader;

}  // namespace

TEST(HEMCO3121MeganConstants, SourcePinnedIsopreneValues) {
    EXPECT_DOUBLE_EQ(kLdf, 1.0);
    EXPECT_DOUBLE_EQ(kBeta, 0.13);
    EXPECT_DOUBLE_EQ(kCT1, 95.0);
    EXPECT_DOUBLE_EQ(kCEO, 2.0);
    EXPECT_DOUBLE_EQ(kANew, 0.05);
    EXPECT_DOUBLE_EQ(kAGro, 0.60);
    EXPECT_DOUBLE_EQ(kAMat, 1.00);
    EXPECT_DOUBLE_EQ(kAOld, 0.90);
}

TEST(HEMCO3121MeganConstants, SourceComputedNormalization) {
    EXPECT_NEAR(kNormFac, SourceNormalizationFactor(), 2.0e-16);
    EXPECT_NE(kNormFac, 1.0 / 1.0101081) << "1.0101081 is a rounded source comment, not executable arithmetic";
}

TEST(HEMCO3121MeganConstants, PtoaAndParUnitContract) {
    EXPECT_DOUBLE_EQ(kPtoaC1, 3000.0);
    EXPECT_DOUBLE_EQ(kPtoaC2, 99.0);
    EXPECT_DOUBLE_EQ(kPtoaDoyOffset, 10.0);
    EXPECT_DOUBLE_EQ(kWm2ToUmol, 4.766);
    EXPECT_DOUBLE_EQ(kParDirectHistoryWm2, 30.0);
    EXPECT_DOUBLE_EQ(kParDiffuseHistoryWm2, 48.0);
}

TEST(HEMCO3121MeganConstants, ColdStartHistoryAndLaiInterval) {
    EXPECT_DOUBLE_EQ(kTemperatureHistoryK, static_cast<double>(288.15F));
    EXPECT_DOUBLE_EQ(kDaysBetweenLai, 1.0);
    EXPECT_EQ(kReferenceDoy, 171);
}

TEST(HEMCO3121MeganGamma, CO2SwitchIsExplicit) {
    MeganInputs enabled;
    enabled.T_K = 303.0;
    enabled.lai = 4.0;
    enabled.lai_prev = 4.0;
    enabled.pardr_Wm2 = 200.0;
    enabled.pardf_Wm2 = 100.0;
    enabled.suncos = 0.7;
    enabled.co2_ppm = 390.0;
    enabled.apply_co2_inhibition = true;

    MeganInputs disabled = enabled;
    disabled.apply_co2_inhibition = false;

    const double with_inhibition = IsopreneEmissionFactor(enabled);
    const double without_inhibition = IsopreneEmissionFactor(disabled);
    EXPECT_GT(without_inhibition, with_inhibition);
    EXPECT_NEAR(with_inhibition / without_inhibition, GammaCO2(390.0), kTol);
}

TEST(HEMCO3121MeganGamma, CO2InhibitionDecreasesWithConcentration) {
    EXPECT_GT(GammaCO2(280.0), GammaCO2(390.0));
    EXPECT_GT(GammaCO2(390.0), GammaCO2(560.0));
}

TEST(HEMCO3121MeganGamma, TemperatureHistoryControlsLightDependentResponse) {
    EXPECT_NEAR(GammaTLD(313.0, 297.0), kCEO, kTol);
    EXPECT_NE(GammaTLD(303.0, 288.15), GammaTLD(303.0, 297.0));
}

TEST(HEMCO3121MeganGamma, ParHistoryUsesSeparateWattPerSquareMetreFields) {
    const double implicit_cold_start = GammaPAR(200.0, 100.0, 0.7);
    const double explicit_cold_start = GammaPAR(200.0, 100.0, 0.7, 30.0, 48.0, 171);
    EXPECT_DOUBLE_EQ(implicit_cold_start, explicit_cold_start);

    const double cold_start_bbb = 1.0 + 0.0005 * ((30.0 + 48.0) * 4.766 - 400.0);
    EXPECT_NEAR(cold_start_bbb, 0.985874, 1.0e-12);
    EXPECT_NE(implicit_cold_start, GammaPAR(200.0, 100.0, 0.7, 200.0, 200.0, 171));
}

TEST(HEMCO3121MeganGamma, ParNightAndLowSunGuard) {
    EXPECT_EQ(GammaPAR(200.0, 100.0, 0.0), 0.0);
    EXPECT_EQ(GammaPAR(200.0, 100.0, -0.5), 0.0);
    EXPECT_EQ(GammaPAR(200.0, 100.0, 1.01), 0.0);
    EXPECT_EQ(GammaPAR(200.0, 100.0, std::numeric_limits<double>::quiet_NaN()), 0.0);

    const double sin_half_degree = std::sin(0.5 * std::numbers::pi / 180.0);
    const double ptoa = kPtoaC1 + kPtoaC2 * std::cos(2.0 * std::numbers::pi * (171.0 - kPtoaDoyOffset) / 365.0);
    const double high_history_bbb = 1.0 + 0.0005 * ((1000.0 + 1000.0) * kWm2ToUmol - 400.0);
    const double optimum_phi = kGpC3 * high_history_bbb / (2.0 * kGpC4);
    const double instantaneous_wm2 = optimum_phi * sin_half_degree * ptoa / kWm2ToUmol;
    EXPECT_EQ(GammaPAR(instantaneous_wm2, 0.0, sin_half_degree, 1000.0, 1000.0, 171), 0.0);
}

TEST(HEMCO3121MeganGamma, LeafAndNightGates) {
    EXPECT_EQ(GammaLAI(0.0), 0.0);
    EXPECT_EQ(GammaLAI(-1.0), 0.0);

    MeganInputs no_leaf;
    no_leaf.lai = 0.0;
    EXPECT_EQ(IsopreneEmissionFactor(no_leaf), 0.0);

    MeganInputs night;
    night.T_K = 303.0;
    night.lai = 4.0;
    night.lai_prev = 4.0;
    night.suncos = -0.1;
    EXPECT_EQ(IsopreneEmissionFactor(night), 0.0) << "ISOP is fully light-dependent in HEMCO 3.12.1";
}

TEST(HEMCO3121MeganGamma, LeafAgeUsesHistoryTemperatureAndDailyLaiInterval) {
    const double steady = 0.1 * kAGro + 0.8 * kAMat + 0.1 * kAOld;
    EXPECT_NEAR(GammaAge(4.0, 4.0, kTemperatureHistoryK), steady, kTol);
    EXPECT_NE(GammaAge(5.0, 3.0, 288.15, 1.0), GammaAge(5.0, 3.0, 288.15, 30.0));
    EXPECT_GE(GammaAge(3.0, 5.0, 288.15), 0.0);
}

class HEMCO3121MeganRuntime : public ::testing::Test {
   protected:
    struct CellInputs {
        double temperature_k = 307.0;
        double lai = 4.123456789;
        double lai_prev = 2.234567891;
        double par_direct_wm2 = 240.0;
        double par_diffuse_wm2 = 65.0;
        double solar_cosine = 0.78;
    };

    struct OneCellStates {
        CeceImportState import_state;
        CeceExportState export_state;
    };

    static constexpr double kRuntimeAef = 2.5e-9;
    static constexpr double kRuntimeCO2Ppm = 415.0;
    static constexpr double kRuntimeParDirectHistoryWm2 = 31.234567891;
    static constexpr double kRuntimeParDiffuseHistoryWm2 = 47.543210987;
    static constexpr double kRuntimeTemperatureHistoryK = 295.123456789;
    static constexpr int kRuntimeDoy = 200;

    static const std::string& OutputFieldName() {
        static const std::string name = "runtime_isoprene";
        return name;
    }

    static DualView3D MakeField(const std::string& name, double value, int nx = 1, int ny = 1, int nz = 1) {
        DualView3D field(name, nx, ny, nz);
        Kokkos::deep_copy(field.view_host(), value);
        field.modify_host();
        field.sync_device();
        return field;
    }

    static OneCellStates MakeStates(const CellInputs& input, bool include_previous_lai = true) {
        OneCellStates states;
        states.import_state.fields["temperature"] = MakeField("runtime_temperature", input.temperature_k);
        states.import_state.fields["leaf_area_index"] = MakeField("runtime_lai", input.lai);
        states.import_state.fields["par_direct"] = MakeField("runtime_par_direct", input.par_direct_wm2);
        states.import_state.fields["par_diffuse"] = MakeField("runtime_par_diffuse", input.par_diffuse_wm2);
        states.import_state.fields["solar_cosine"] = MakeField("runtime_solar_cosine", input.solar_cosine);
        if (include_previous_lai) {
            states.import_state.fields["leaf_area_index_prev"] = MakeField("runtime_lai_prev", input.lai_prev);
        }
        states.export_state.fields[OutputFieldName()] = MakeField("runtime_isoprene_output", 0.0);
        return states;
    }

    static std::string RuntimeConfig(bool apply_co2_inhibition = true) {
        return std::string("megan_method: hemco_3_12_1\n") +
               "aef: 2.5e-9\n"
               "export_field_name: runtime_isoprene\n"
               "hemco_co2_ppm: 415.0\n"
               "hemco_co2_inhibition: " +
               (apply_co2_inhibition ? "true\n" : "false\n") +
               "hemco_par_direct_history_wm2: 31.234567891\n"
               "hemco_par_diffuse_history_wm2: 47.543210987\n"
               "hemco_temperature_history_k: 295.123456789\n"
               "hemco_day_of_year: 200\n";
    }

    static std::string ReplaceSetting(std::string yaml, const std::string& current, const std::string& replacement) {
        const std::size_t position = yaml.find(current);
        if (position == std::string::npos) {
            throw std::runtime_error("test configuration setting not found: " + current);
        }
        yaml.replace(position, current.size(), replacement);
        return yaml;
    }

    static MeganInputs ScalarInputs(const CellInputs& input, bool apply_co2_inhibition = true) {
        MeganInputs scalar;
        scalar.T_K = input.temperature_k;
        scalar.lai = input.lai;
        scalar.lai_prev = input.lai_prev;
        scalar.pardr_Wm2 = input.par_direct_wm2;
        scalar.pardf_Wm2 = input.par_diffuse_wm2;
        scalar.suncos = input.solar_cosine;
        scalar.co2_ppm = kRuntimeCO2Ppm;
        scalar.apply_co2_inhibition = apply_co2_inhibition;
        scalar.pardr_history_Wm2 = static_cast<double>(static_cast<float>(kRuntimeParDirectHistoryWm2));
        scalar.pardf_history_Wm2 = static_cast<double>(static_cast<float>(kRuntimeParDiffuseHistoryWm2));
        scalar.temperature_history_K = static_cast<double>(static_cast<float>(kRuntimeTemperatureHistoryK));
        scalar.days_between_lai = kDaysBetweenLai;
        scalar.doy = kRuntimeDoy;
        return scalar;
    }

    static double ReadOutput(OneCellStates& states) {
        auto& output = states.export_state.fields.at(OutputFieldName());
        output.sync_host();
        return output.view_host()(0, 0, 0);
    }

    static void ExpectScalarMatch(double runtime_value, double scalar_value) {
        EXPECT_NEAR(runtime_value, scalar_value, std::max(std::abs(scalar_value) * kTol, 1.0e-30));
    }
};

TEST_F(HEMCO3121MeganRuntime, KokkosRunUsesExactEffectivePreviousLai) {
    const CellInputs input;
    auto states = MakeStates(input);
    auto config = conf::Config::from_string(RuntimeConfig());

    MeganScheme scheme;
    scheme.Initialize(config.root(), nullptr);
    scheme.Run(states.import_state, states.export_state);

    const double expected = kRuntimeAef * IsopreneEmissionFactor(ScalarInputs(input));
    ExpectScalarMatch(ReadOutput(states), expected);

    const double projected_previous_lai = static_cast<double>(static_cast<float>(input.lai_prev));
    EXPECT_NE(input.lai_prev, projected_previous_lai);
    MeganInputs projected = ScalarInputs(input);
    projected.lai_prev = projected_previous_lai;
    const double projected_expected = kRuntimeAef * IsopreneEmissionFactor(projected);
    EXPECT_NE(expected, projected_expected);
}

TEST_F(HEMCO3121MeganRuntime, EachRunOverwritesOutputAndZeroLaiClearsStaleFlux) {
    const CellInputs input;
    auto states = MakeStates(input);
    auto& output = states.export_state.fields.at(OutputFieldName());
    Kokkos::deep_copy(output.view_host(), 123.0);
    output.modify_host();
    output.sync_device();
    auto config = conf::Config::from_string(RuntimeConfig());

    MeganScheme scheme;
    scheme.Initialize(config.root(), nullptr);
    scheme.Run(states.import_state, states.export_state);

    const double expected = kRuntimeAef * IsopreneEmissionFactor(ScalarInputs(input));
    ExpectScalarMatch(ReadOutput(states), expected);

    scheme.Run(states.import_state, states.export_state);
    ExpectScalarMatch(ReadOutput(states), expected);

    auto& lai = states.import_state.fields.at("leaf_area_index");
    Kokkos::deep_copy(lai.view_host(), 0.0);
    lai.modify_host();
    lai.sync_device();
    scheme.Run(states.import_state, states.export_state);
    EXPECT_DOUBLE_EQ(ReadOutput(states), 0.0);
}

TEST_F(HEMCO3121MeganRuntime, MissingPreviousLaiFailsClosed) {
    const CellInputs input;
    auto states = MakeStates(input, false);
    auto config = conf::Config::from_string(RuntimeConfig());

    MeganScheme scheme;
    scheme.Initialize(config.root(), nullptr);
    EXPECT_THROW(scheme.Run(states.import_state, states.export_state), std::runtime_error);
}

TEST_F(HEMCO3121MeganRuntime, ConfiguredHistoriesUseFloatProjection) {
    const CellInputs input;
    auto states = MakeStates(input);
    auto config = conf::Config::from_string(RuntimeConfig());

    MeganScheme scheme;
    scheme.Initialize(config.root(), nullptr);
    scheme.Run(states.import_state, states.export_state);

    const double runtime_value = ReadOutput(states);
    const double projected_expected = kRuntimeAef * IsopreneEmissionFactor(ScalarInputs(input));
    ExpectScalarMatch(runtime_value, projected_expected);

    MeganInputs unprojected = ScalarInputs(input);
    unprojected.pardr_history_Wm2 = kRuntimeParDirectHistoryWm2;
    unprojected.pardf_history_Wm2 = kRuntimeParDiffuseHistoryWm2;
    unprojected.temperature_history_K = kRuntimeTemperatureHistoryK;
    const double unprojected_expected = kRuntimeAef * IsopreneEmissionFactor(unprojected);
    EXPECT_NE(projected_expected, unprojected_expected);
}

TEST_F(HEMCO3121MeganRuntime, CO2ToggleReachesKokkosKernel) {
    const CellInputs input;
    auto inhibited_states = MakeStates(input);
    auto uninhibited_states = MakeStates(input);
    auto inhibited_config = conf::Config::from_string(RuntimeConfig(true));
    auto uninhibited_config = conf::Config::from_string(RuntimeConfig(false));

    MeganScheme inhibited_scheme;
    inhibited_scheme.Initialize(inhibited_config.root(), nullptr);
    inhibited_scheme.Run(inhibited_states.import_state, inhibited_states.export_state);

    MeganScheme uninhibited_scheme;
    uninhibited_scheme.Initialize(uninhibited_config.root(), nullptr);
    uninhibited_scheme.Run(uninhibited_states.import_state, uninhibited_states.export_state);

    const double inhibited_expected = kRuntimeAef * IsopreneEmissionFactor(ScalarInputs(input, true));
    const double uninhibited_expected = kRuntimeAef * IsopreneEmissionFactor(ScalarInputs(input, false));
    const double inhibited_runtime = ReadOutput(inhibited_states);
    const double uninhibited_runtime = ReadOutput(uninhibited_states);
    ExpectScalarMatch(inhibited_runtime, inhibited_expected);
    ExpectScalarMatch(uninhibited_runtime, uninhibited_expected);
    EXPECT_GT(uninhibited_runtime, inhibited_runtime);
}

TEST_F(HEMCO3121MeganRuntime, RejectsInvalidConfiguration) {
    const std::vector<std::pair<std::string, std::string>> invalid_configs = {
        {"unknown method", "megan_method: unknown\n"},
        {"CO2 below supported range", ReplaceSetting(RuntimeConfig(), "hemco_co2_ppm: 415.0", "hemco_co2_ppm: 149.0")},
        {"negative direct PAR history",
         ReplaceSetting(RuntimeConfig(), "hemco_par_direct_history_wm2: 31.234567891", "hemco_par_direct_history_wm2: -1.0")},
        {"negative diffuse PAR history",
         ReplaceSetting(RuntimeConfig(), "hemco_par_diffuse_history_wm2: 47.543210987", "hemco_par_diffuse_history_wm2: -1.0")},
        {"nonpositive temperature history",
         ReplaceSetting(RuntimeConfig(), "hemco_temperature_history_k: 295.123456789", "hemco_temperature_history_k: 0.0")},
        {"invalid day of year", ReplaceSetting(RuntimeConfig(), "hemco_day_of_year: 200", "hemco_day_of_year: 367")},
        {"negative AEF", ReplaceSetting(RuntimeConfig(), "aef: 2.5e-9", "aef: -1.0")},
        {"non-isoprene species", RuntimeConfig() + "species_name: monoterpene\n"},
    };

    for (const auto& [description, yaml] : invalid_configs) {
        SCOPED_TRACE(description);
        auto config = conf::Config::from_string(yaml);
        MeganScheme scheme;
        EXPECT_THROW(scheme.Initialize(config.root(), nullptr), std::invalid_argument);
    }
}

TEST_F(HEMCO3121MeganRuntime, RejectsObsoleteHistoryKeys) {
    const std::vector<std::string> obsolete_keys = {"hemco_par_avg_umol", "hemco_t_avg_15_k"};
    for (const std::string& key : obsolete_keys) {
        SCOPED_TRACE(key);
        auto config = conf::Config::from_string("megan_method: hemco_3_12_1\n" + key + ": 400.0\n");
        MeganScheme scheme;
        EXPECT_THROW(scheme.Initialize(config.root(), nullptr), std::invalid_argument);
    }
}

TEST_F(HEMCO3121MeganRuntime, RequiredSourceContractSettingsFailClosed) {
    const std::vector<std::pair<std::string, std::string>> incomplete_configs = {
        {"effective AEF", "megan_method: hemco_3_12_1\nhemco_day_of_year: 171\nhemco_co2_inhibition: false\n"},
        {"day of year", "megan_method: hemco_3_12_1\naef: 1.0e-9\nhemco_co2_inhibition: false\n"},
        {"CO2 switch", "megan_method: hemco_3_12_1\naef: 1.0e-9\nhemco_day_of_year: 171\n"},
        {"CO2 concentration", "megan_method: hemco_3_12_1\naef: 1.0e-9\nhemco_day_of_year: 171\nhemco_co2_inhibition: true\n"},
    };

    for (const auto& [description, yaml] : incomplete_configs) {
        SCOPED_TRACE(description);
        auto config = conf::Config::from_string(yaml);
        MeganScheme scheme;
        EXPECT_THROW(scheme.Initialize(config.root(), nullptr), std::invalid_argument);
    }

    auto disabled_config =
        conf::Config::from_string("megan_method: hemco_3_12_1\naef: 1.0e-9\nhemco_day_of_year: 171\nhemco_co2_inhibition: false\n");
    MeganScheme disabled_scheme;
    EXPECT_NO_THROW(disabled_scheme.Initialize(disabled_config.root(), nullptr));
}

TEST_F(HEMCO3121MeganRuntime, MissingRequiredFieldsFailClosed) {
    struct RequiredField {
        std::string name;
        bool is_export;
    };
    const std::vector<RequiredField> required_fields = {
        {"temperature", false}, {"leaf_area_index", false}, {"leaf_area_index_prev", false}, {"par_direct", false},
        {"par_diffuse", false}, {"solar_cosine", false},    {OutputFieldName(), true},
    };
    auto config = conf::Config::from_string(RuntimeConfig());

    for (const auto& field : required_fields) {
        SCOPED_TRACE(field.name);
        auto states = MakeStates(CellInputs{});
        if (field.is_export) {
            states.export_state.fields.erase(field.name);
        } else {
            states.import_state.fields.erase(field.name);
        }

        MeganScheme scheme;
        scheme.Initialize(config.root(), nullptr);
        EXPECT_THROW(scheme.Run(states.import_state, states.export_state), std::runtime_error);
    }
}

TEST_F(HEMCO3121MeganRuntime, IncompatibleFieldShapesFailClosed) {
    struct RuntimeField {
        std::string name;
        bool is_export;
    };
    const std::vector<RuntimeField> fields = {
        {"temperature", false}, {"leaf_area_index", false}, {"leaf_area_index_prev", false}, {"par_direct", false},
        {"par_diffuse", false}, {"solar_cosine", false},    {OutputFieldName(), true},
    };
    auto config = conf::Config::from_string(RuntimeConfig());

    for (const auto& field : fields) {
        SCOPED_TRACE(field.name);
        auto states = MakeStates(CellInputs{});
        if (field.is_export) {
            states.export_state.fields[field.name] = MakeField("wrong_export_shape", 0.0, 1, 1, 2);
        } else {
            states.import_state.fields[field.name] = MakeField("wrong_import_shape", 1.0, 2, 1, 1);
        }

        MeganScheme scheme;
        scheme.Initialize(config.root(), nullptr);
        EXPECT_THROW(scheme.Run(states.import_state, states.export_state), std::runtime_error);
    }
}

TEST_F(HEMCO3121MeganRuntime, InvalidSolarCosineFailsClosed) {
    for (const double invalid_value : {-1.01, 1.01, std::numeric_limits<double>::quiet_NaN()}) {
        SCOPED_TRACE(invalid_value);
        CellInputs input;
        input.solar_cosine = invalid_value;
        auto states = MakeStates(input);
        auto config = conf::Config::from_string(RuntimeConfig());

        MeganScheme scheme;
        scheme.Initialize(config.root(), nullptr);
        EXPECT_THROW(scheme.Run(states.import_state, states.export_state), std::runtime_error);
    }
}

TEST_F(HEMCO3121MeganRuntime, NativeModeRetainsMissingFieldNoOp) {
    auto config = conf::Config::from_string("megan_method: native\nexport_field_name: runtime_isoprene\n");
    auto states = MakeStates(CellInputs{});
    states.import_state.fields.erase("par_diffuse");

    MeganScheme scheme;
    scheme.Initialize(config.root(), nullptr);
    EXPECT_NO_THROW(scheme.Run(states.import_state, states.export_state));
    EXPECT_DOUBLE_EQ(ReadOutput(states), 0.0);
}

class HEMCO3121MeganOracle : public ::testing::TestWithParam<OracleRow> {};

TEST(HEMCO3121MeganOracleContract, RegressionVectorsLoaded) {
    EXPECT_EQ(gOracleRows.size(), 16U);
}

TEST_P(HEMCO3121MeganOracle, MatchesSourceDerivedReference) {
    const OracleRow& row = GetParam();
    MeganInputs input;
    input.T_K = row.T_K;
    input.lai = row.lai;
    input.lai_prev = row.lai_prev;
    input.pardr_Wm2 = row.pardr_Wm2;
    input.pardf_Wm2 = row.pardf_Wm2;
    input.suncos = row.suncos;
    input.gwetroot = row.gwetroot;
    input.co2_ppm = row.co2_ppm;
    input.apply_co2_inhibition = true;

    const double got = IsopreneEmissionFactor(input);
    const double scale = std::max(std::abs(row.expected), 1.0e-30);
    EXPECT_NEAR(got, row.expected, kTol * scale) << "Case: " << row.case_id;
}

INSTANTIATE_TEST_SUITE_P(Oracle, HEMCO3121MeganOracle, ::testing::ValuesIn(gOracleRows), [](const ::testing::TestParamInfo<OracleRow>& info) {
    std::string name = info.param.case_id;
    std::replace(name.begin(), name.end(), ' ', '_');
    return name;
});

}  // namespace cece::hemco_megan::v3_12_1

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
