/**
 * @file test_diagnostics_config.cpp
 * @brief Table-driven tests for the diagnostics configuration surface (feature
 * 003, user story 2).
 *
 * Covers the full validation matrix and precedence table of
 * contracts/diagnostics-config.md:
 *   - output.include_diagnostics parsed (bool, default false)
 *   - diagnostics:variables non-empty -> selection; empty/absent -> write-all
 *   - output_interval_seconds honored, with the deprecated output_interval
 *     alias (seconds key wins when both are present)
 *   - V1 negative interval throws; V2 interval that is not a multiple of
 *     driver.timestep_seconds throws naming both values; V3/V4 selection names
 *     colliding with a coordinate or species name throw naming the offender
 *   - parse-order guarantee: the diagnostics block is validated with
 *     driver.timestep_seconds known regardless of document order
 *   - sequence-style diagnostics: list still accepted
 *
 * Framework-free (core parsing only, no ESMF): mirrors the pattern of
 * tests/test_driver_configuration.cpp.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cece/cece_config.hpp"

using cece::CeceConfig;
using cece::ParseConfig;

namespace {

void WriteConfigFile(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    f << content;
    f.close();
}

class DiagnosticsConfigTest : public ::testing::Test {
   protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        std::string name = info->name();
        // Parameterized tests append "/N" to the case name; sanitize it so
        // the per-test temp file is a valid path.
        std::replace(name.begin(), name.end(), '/', '_');
        path_ = std::string("test_diagcfg_") + name + ".yaml";
    }

    void TearDown() override {
        std::remove(path_.c_str());
    }

    CeceConfig Parse(const std::string& content) {
        WriteConfigFile(path_, content);
        return ParseConfig(path_);
    }

    // Asserts ParseConfig rejects the content with std::invalid_argument whose
    // message contains every expected fragment (contract: "message must
    // contain").
    void ExpectRejected(const std::string& content, const std::vector<std::string>& message_fragments) {
        WriteConfigFile(path_, content);
        try {
            (void)ParseConfig(path_);
            FAIL() << "Expected ParseConfig to reject:\n" << content;
        } catch (const std::invalid_argument& error) {
            const std::string message = error.what();
            for (const auto& fragment : message_fragments) {
                EXPECT_NE(message.find(fragment), std::string::npos) << "message \"" << message << "\" missing fragment \"" << fragment << "\"";
            }
        }
    }

    std::string path_;
};

// Minimal config carrying only the keys under test; the rest is defaulted.
// Validation fixtures default to an ENABLED diagnostics channel
// (include_diagnostics: true) — dormant blocks stay inert per the backward
// compatibility contract, so they must not be validated.
std::string WithDriverAndDiagnostics(const std::string& diagnostics_block, const std::string& driver_block = "driver:\n  timestep_seconds: 3600\n",
                                     const std::string& output_block = "output:\n  enabled: true\n  include_diagnostics: true\n") {
    return driver_block + diagnostics_block + output_block;
}

// ---------------------------------------------------------------------------
// output.include_diagnostics
// ---------------------------------------------------------------------------

TEST_F(DiagnosticsConfigTest, IncludeDiagnosticsDefaultsFalse) {
    const CeceConfig config = Parse("output:\n  enabled: true\n  directory: ./out\n");
    EXPECT_FALSE(config.output_config.include_diagnostics);
}

TEST_F(DiagnosticsConfigTest, IncludeDiagnosticsParsedTrue) {
    const CeceConfig config = Parse("output:\n  enabled: true\n  include_diagnostics: true\n");
    EXPECT_TRUE(config.output_config.include_diagnostics);
}

TEST_F(DiagnosticsConfigTest, NoOutputBlockKeepsDefaults) {
    const CeceConfig config = Parse("driver:\n  timestep_seconds: 3600\n");
    EXPECT_FALSE(config.output_config.include_diagnostics);
    EXPECT_FALSE(config.output_config.enabled);
}

// ---------------------------------------------------------------------------
// diagnostics:variables — selection vs write-all (Q2=A precedence)
// ---------------------------------------------------------------------------

TEST_F(DiagnosticsConfigTest, NonEmptyVariablesIsSelection) {
    const CeceConfig config = Parse(WithDriverAndDiagnostics("diagnostics:\n  variables: [temperature_anomaly, emission_efficiency]\n"));
    EXPECT_EQ(config.diagnostics.variables, (std::vector<std::string>{"temperature_anomaly", "emission_efficiency"}));
}

TEST_F(DiagnosticsConfigTest, AbsentVariablesMeansWriteAll) {
    const CeceConfig config = Parse(WithDriverAndDiagnostics("diagnostics:\n  output_interval_seconds: 7200\n"));
    EXPECT_TRUE(config.diagnostics.variables.empty());
}

TEST_F(DiagnosticsConfigTest, EmptyVariablesListMeansWriteAll) {
    const CeceConfig config = Parse(WithDriverAndDiagnostics("diagnostics:\n  variables: []\n"));
    EXPECT_TRUE(config.diagnostics.variables.empty());
}

TEST_F(DiagnosticsConfigTest, SequenceStyleDiagnosticsStillAccepted) {
    const CeceConfig config = Parse(WithDriverAndDiagnostics("diagnostics: [temperature_anomaly]\n"));
    EXPECT_EQ(config.diagnostics.variables, (std::vector<std::string>{"temperature_anomaly"}));
}

// ---------------------------------------------------------------------------
// Interval keys: output_interval_seconds primary, output_interval deprecated
// alias, seconds wins when both present.
// ---------------------------------------------------------------------------

TEST_F(DiagnosticsConfigTest, OutputIntervalSecondsHonored) {
    const CeceConfig config = Parse(WithDriverAndDiagnostics("diagnostics:\n  output_interval_seconds: 7200\n"));
    EXPECT_EQ(config.diagnostics.output_interval_seconds, 7200);
}

TEST_F(DiagnosticsConfigTest, DeprecatedOutputIntervalAliasHonored) {
    const CeceConfig config = Parse(WithDriverAndDiagnostics("diagnostics:\n  output_interval: 3600\n"));
    EXPECT_EQ(config.diagnostics.output_interval_seconds, 3600);
}

TEST_F(DiagnosticsConfigTest, SecondsKeyWinsWhenBothPresent) {
    const CeceConfig config = Parse(WithDriverAndDiagnostics("diagnostics:\n  output_interval: 3600\n  output_interval_seconds: 7200\n"));
    EXPECT_EQ(config.diagnostics.output_interval_seconds, 7200);
}

TEST_F(DiagnosticsConfigTest, ZeroIntervalIsValidMeaningEveryFileWrite) {
    const CeceConfig config = Parse(WithDriverAndDiagnostics("diagnostics:\n  output_interval_seconds: 0\n"));
    EXPECT_EQ(config.diagnostics.output_interval_seconds, 0);
}

// ---------------------------------------------------------------------------
// Validation matrix V1-V5
// ---------------------------------------------------------------------------

TEST_F(DiagnosticsConfigTest, V1NegativeIntervalThrowsNamingKeyAndValue) {
    ExpectRejected(WithDriverAndDiagnostics("diagnostics:\n  output_interval_seconds: -100\n"), {"output_interval_seconds", "-100"});
}

TEST_F(DiagnosticsConfigTest, V1NegativeDeprecatedAliasThrows) {
    ExpectRejected(WithDriverAndDiagnostics("diagnostics:\n  output_interval: -1\n"), {"output_interval", "-1"});
}

TEST_F(DiagnosticsConfigTest, V2NonMultipleIntervalThrowsNamingBothValues) {
    ExpectRejected(WithDriverAndDiagnostics("diagnostics:\n  output_interval_seconds: 5400\n", "driver:\n  timestep_seconds: 3600\n"),
                   {"5400", "3600"});
}

TEST_F(DiagnosticsConfigTest, V2UsesParsedDefaultTimestepWhenDriverBlockAbsent) {
    // No driver: block at all — the parsed default timestep (3600) applies.
    ExpectRejected(WithDriverAndDiagnostics("diagnostics:\n  output_interval_seconds: 5400\n", ""), {"5400", "3600"});
}

TEST_F(DiagnosticsConfigTest, V2ValidMultipleOfCustomTimestepAccepted) {
    const CeceConfig config =
        Parse(WithDriverAndDiagnostics("diagnostics:\n  output_interval_seconds: 10800\n", "driver:\n  timestep_seconds: 3600\n"));
    EXPECT_EQ(config.diagnostics.output_interval_seconds, 10800);
}

TEST_F(DiagnosticsConfigTest, V3CoordinateNameInSelectionThrowsNamingOffender) {
    // Every writer-managed coordinate/bounds/mesh name is reserved.
    for (const std::string& name : {"lon", "lat", "lev", "time", "lon_bnds", "lat_bnds", "mesh"}) {
        ExpectRejected(WithDriverAndDiagnostics("diagnostics:\n  variables: [" + name + "]\n"), {name});
    }
}

TEST_F(DiagnosticsConfigTest, V4SpeciesNameInSelectionThrowsNamingOffender) {
    const std::string species = "species:\n  co: []\n  nox: []\n";
    ExpectRejected(species + WithDriverAndDiagnostics("diagnostics:\n  variables: [co]\n"), {"co"});
    ExpectRejected(species + WithDriverAndDiagnostics("diagnostics:\n  variables: [temperature_anomaly, nox]\n"), {"nox"});
}

// Backward compatibility: a diagnostics block that would be invalid when
// live stays inert (and parseable) while include_diagnostics is false —
// existing example configs keep running unchanged (SC-005).
TEST_F(DiagnosticsConfigTest, DormantBlockIsNotValidated) {
    const std::string content =
        "species:\n  co: []\ndriver:\n  timestep_seconds: 3600\ndiagnostics:\n  output_interval_seconds: 5400\n"
        "  variables: [co, lon]\noutput:\n  enabled: true\n  include_diagnostics: false\n";
    const CeceConfig config = Parse(content);
    EXPECT_EQ(config.diagnostics.output_interval_seconds, 5400);
    EXPECT_EQ(config.diagnostics.variables, (std::vector<std::string>{"co", "lon"}));
}

TEST_F(DiagnosticsConfigTest, V5IncludeDiagnosticsWithoutBlockIsValidNoOp) {
    // include_diagnostics with no diagnostics: block at all is valid:
    // write-all, every file write.
    const CeceConfig config = Parse("output:\n  enabled: true\n  include_diagnostics: true\n");
    EXPECT_TRUE(config.output_config.include_diagnostics);
    EXPECT_EQ(config.diagnostics.output_interval_seconds, 0);
    EXPECT_TRUE(config.diagnostics.variables.empty());
}

// ---------------------------------------------------------------------------
// Parse-order guarantee: the diagnostics block is validated with
// driver.timestep_seconds known even when the block appears BEFORE driver: in
// the document (the historical parser order).
// ---------------------------------------------------------------------------

TEST_F(DiagnosticsConfigTest, DiagnosticsBeforeDriverStillSeesTimestep) {
    // Valid pair: 7200 is a multiple of 3600 even though driver: comes last.
    const CeceConfig config = Parse(
        "diagnostics:\n  output_interval_seconds: 7200\n  variables: [temperature_anomaly]\noutput:\n  enabled: true\n  include_diagnostics: "
        "true\ndriver:\n  timestep_seconds: 3600\n");
    EXPECT_EQ(config.diagnostics.output_interval_seconds, 7200);
    EXPECT_EQ(config.driver_config.timestep_seconds, 3600);
}

TEST_F(DiagnosticsConfigTest, DiagnosticsBeforeDriverRejectsNonMultiple) {
    ExpectRejected(
        "diagnostics:\n  output_interval_seconds: 5400\noutput:\n  enabled: true\n  include_diagnostics: true\ndriver:\n  timestep_seconds: 3600\n",
        {"5400", "3600"});
}

// ---------------------------------------------------------------------------
// Grid-hint keys: parsed but ignored in v1 (native grid only).
// ---------------------------------------------------------------------------

TEST_F(DiagnosticsConfigTest, GridHintKeysParsedNotRejected) {
    const CeceConfig config = Parse(WithDriverAndDiagnostics(
        "diagnostics:\n  grid_type: gaussian\n  grid_file: /tmp/mesh.nc\n  nx: 10\n  ny: 20\n  variables: [temperature_anomaly]\n"));
    EXPECT_EQ(config.diagnostics.grid_type, "gaussian");
    EXPECT_EQ(config.diagnostics.grid_file, "/tmp/mesh.nc");
    EXPECT_EQ(config.diagnostics.nx, 10);
    EXPECT_EQ(config.diagnostics.ny, 20);
    EXPECT_EQ(config.diagnostics.variables, (std::vector<std::string>{"temperature_anomaly"}));
}

// ---------------------------------------------------------------------------
// Precedence table rows (§2): the parsed fields must be able to express every
// row — the channel gating itself is the writer's job.
// ---------------------------------------------------------------------------

struct PrecedenceRow {
    const char* name;
    std::string output_block;
    bool expected_enabled;
    bool expected_include;
};

class PrecedenceTableTest : public DiagnosticsConfigTest, public ::testing::WithParamInterface<PrecedenceRow> {};

TEST_P(PrecedenceTableTest, RowParsesToExpectedFlags) {
    const CeceConfig config = Parse(WithDriverAndDiagnostics("diagnostics:\n  variables: [temperature_anomaly]\n",
                                                             "driver:\n  timestep_seconds: "
                                                             "3600\n",
                                                             GetParam().output_block));
    EXPECT_EQ(config.output_config.enabled, GetParam().expected_enabled) << GetParam().name;
    EXPECT_EQ(config.output_config.include_diagnostics, GetParam().expected_include) << GetParam().name;
}

INSTANTIATE_TEST_SUITE_P(DiagnosticsConfig, PrecedenceTableTest,
                         ::testing::Values(PrecedenceRow{"off/off", "output:\n  enabled: false\n  include_diagnostics: false\n", false, false},
                                           PrecedenceRow{"species only", "output:\n  enabled: true\n  include_diagnostics: false\n", true, false},
                                           PrecedenceRow{"coupled only", "output:\n  enabled: false\n  include_diagnostics: true\n", false, true},
                                           PrecedenceRow{"file + diagnostics", "output:\n  enabled: true\n  include_diagnostics: true\n", true, true},
                                           PrecedenceRow{"bare output defaults", "output:\n  enabled: true\n", true, false}));

}  // namespace
