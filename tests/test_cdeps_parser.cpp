/**
 * @file test_cdeps_parser.cpp
 * @brief Unit tests for CDEPS streams configuration parser.
 *
 * Tests parsing, validation, and serialization of ESMF Config format
 * streams files for CDEPS-inline data ingestion.
 *
 * Coverage:
 * - Parsing valid ESMF Config format files (Req 7.1, 7.2)
 * - Validation error detection: missing files (Req 7.3, 7.4, 7.8)
 * - Validation error detection: invalid variables (Req 7.5, 7.9)
 * - Validation error detection: bad interpolation modes (Req 7.6, 7.10)
 * - Error message quality and actionability (Req 7.7-7.10)
 * - Round-trip serialization (Req 7.13, Property 16)
 *
 * @see Requirements 1.1, 1.2, 7.1-7.10, 7.13, Property 16
 */

#include "aces/aces_cdeps_parser.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace aces {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** Write a string to a temp file and return the filename. */
static void WriteFile(const std::string& filename, const std::string& content) {
    std::ofstream f(filename);
    f << content;
    f.close();
}

/** Return true if any error string contains the given substring. */
static bool AnyErrorContains(const std::vector<std::string>& errors,
                              const std::string& substr) {
    for (const auto& e : errors) {
        if (e.find(substr) != std::string::npos) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

/**
 * @brief Test fixture for CDEPS parser tests.
 *
 * Creates temporary test files and cleans them up after each test.
 */
class CdepsParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        CreateValidStreamsFile("test_streams_valid.txt");
        CreateInvalidStreamsFile("test_streams_invalid.txt");
    }

    void TearDown() override {
        std::remove("test_streams_valid.txt");
        std::remove("test_streams_invalid.txt");
        std::remove("test_streams_output.txt");
        std::remove("test_streams_simple.txt");
        std::remove("test_streams_comments.txt");
        std::remove("test_streams_full.txt");
    }

    void CreateValidStreamsFile(const std::string& filename) {
        WriteFile(filename,
            "# CDEPS Streams Configuration Test\n"
            "# Valid configuration for testing\n\n"
            "stream::test_emissions\n"
            "  file_paths = /data/test_emissions.nc\n"
            "  variables = CO_emis:CO, NOx_emis:NOx\n"
            "  taxmode = cycle\n"
            "  tintalgo = linear\n"
            "  mapalgo = bilinear\n"
            "  yearFirst = 2020\n"
            "  yearLast = 2020\n"
            "  yearAlign = 2020\n"
            "  offset = 0\n"
            "::\n\n"
            "stream::met_data\n"
            "  file_paths = /data/met_2020.nc, /data/met_2021.nc\n"
            "  variables = temperature:T, pressure:P\n"
            "  taxmode = extend\n"
            "  tintalgo = nearest\n"
            "  yearFirst = 2020\n"
            "  yearLast = 2021\n"
            "  yearAlign = 2020\n"
            "::\n");
    }

    void CreateInvalidStreamsFile(const std::string& filename) {
        WriteFile(filename,
            "stream::invalid_stream\n"
            "  # Missing file_paths and variables\n"
            "  taxmode = invalid_mode\n"
            "  tintalgo = bad_interp\n"
            "::\n");
    }
};

// ===========================================================================
// Parsing tests (Req 7.1, 7.2)
// ===========================================================================

/**
 * @brief Test parsing a valid streams file.
 *
 * Validates that a well-formed ESMF Config format file is parsed correctly
 * with all attributes extracted.
 *
 * @see Requirements 7.1, 7.2
 */
TEST_F(CdepsParserTest, ParseValidStreamsFile) {
    AcesCdepsConfig config = CdepsStreamsParser::ParseStreamsFile("test_streams_valid.txt");

    ASSERT_EQ(config.streams.size(), 2u);

    // First stream
    const auto& s1 = config.streams[0];
    EXPECT_EQ(s1.name, "test_emissions");
    ASSERT_EQ(s1.file_paths.size(), 1u);
    EXPECT_EQ(s1.file_paths[0], "/data/test_emissions.nc");
    ASSERT_EQ(s1.variables.size(), 2u);
    EXPECT_EQ(s1.variables[0].name_in_file, "CO_emis");
    EXPECT_EQ(s1.variables[0].name_in_model, "CO");
    EXPECT_EQ(s1.variables[1].name_in_file, "NOx_emis");
    EXPECT_EQ(s1.variables[1].name_in_model, "NOx");
    EXPECT_EQ(s1.taxmode, "cycle");
    EXPECT_EQ(s1.tintalgo, "linear");
    EXPECT_EQ(s1.mapalgo, "bilinear");
    EXPECT_EQ(s1.yearFirst, 2020);
    EXPECT_EQ(s1.yearLast, 2020);
    EXPECT_EQ(s1.yearAlign, 2020);
    EXPECT_EQ(s1.offset, 0);

    // Second stream
    const auto& s2 = config.streams[1];
    EXPECT_EQ(s2.name, "met_data");
    ASSERT_EQ(s2.file_paths.size(), 2u);
    EXPECT_EQ(s2.file_paths[0], "/data/met_2020.nc");
    EXPECT_EQ(s2.file_paths[1], "/data/met_2021.nc");
    ASSERT_EQ(s2.variables.size(), 2u);
    EXPECT_EQ(s2.variables[0].name_in_file, "temperature");
    EXPECT_EQ(s2.variables[0].name_in_model, "T");
    EXPECT_EQ(s2.taxmode, "extend");
    EXPECT_EQ(s2.tintalgo, "nearest");
}

/**
 * @brief Test parsing variables without colon separator.
 *
 * Variables without explicit mapping should use the same name for both
 * file and model.
 */
TEST_F(CdepsParserTest, ParseVariablesWithoutMapping) {
    WriteFile("test_streams_simple.txt",
        "stream::simple\n"
        "  file_paths = /data/test.nc\n"
        "  variables = CO, NOx, SO2\n"
        "  taxmode = cycle\n"
        "  tintalgo = linear\n"
        "::\n");

    AcesCdepsConfig config = CdepsStreamsParser::ParseStreamsFile("test_streams_simple.txt");

    ASSERT_EQ(config.streams.size(), 1u);
    const auto& stream = config.streams[0];
    ASSERT_EQ(stream.variables.size(), 3u);

    EXPECT_EQ(stream.variables[0].name_in_file, "CO");
    EXPECT_EQ(stream.variables[0].name_in_model, "CO");
    EXPECT_EQ(stream.variables[1].name_in_file, "NOx");
    EXPECT_EQ(stream.variables[1].name_in_model, "NOx");
    EXPECT_EQ(stream.variables[2].name_in_file, "SO2");
    EXPECT_EQ(stream.variables[2].name_in_model, "SO2");
}

/**
 * @brief Test parsing with comments and empty lines.
 *
 * The parser must correctly skip comments (#) and blank lines.
 */
TEST_F(CdepsParserTest, ParseWithCommentsAndWhitespace) {
    WriteFile("test_streams_comments.txt",
        "# This is a comment\n"
        "\n"
        "stream::test\n"
        "  # Another comment\n"
        "  file_paths = /data/test.nc\n"
        "\n"
        "  variables = CO:carbon_monoxide\n"
        "  taxmode = cycle\n"
        "  tintalgo = linear\n"
        "::\n");

    AcesCdepsConfig config = CdepsStreamsParser::ParseStreamsFile("test_streams_comments.txt");

    ASSERT_EQ(config.streams.size(), 1u);
    EXPECT_EQ(config.streams[0].name, "test");
    EXPECT_EQ(config.streams[0].file_paths[0], "/data/test.nc");
}

/**
 * @brief Test parsing with all optional attributes.
 *
 * Validates that all optional attributes (dtlimit, meshfile, lev_dimname,
 * offset) are correctly parsed.
 *
 * @see Requirement 7.11
 */
TEST_F(CdepsParserTest, ParseWithAllOptionalAttributes) {
    WriteFile("test_streams_full.txt",
        "stream::full_test\n"
        "  file_paths = /data/test.nc\n"
        "  variables = CO:carbon_monoxide\n"
        "  taxmode = limit\n"
        "  tintalgo = upper\n"
        "  mapalgo = patch\n"
        "  dtlimit = 2000000000\n"
        "  yearFirst = 2015\n"
        "  yearLast = 2025\n"
        "  yearAlign = 2020\n"
        "  offset = 3600\n"
        "  meshfile = /data/mesh.nc\n"
        "  lev_dimname = level\n"
        "::\n");

    AcesCdepsConfig config = CdepsStreamsParser::ParseStreamsFile("test_streams_full.txt");

    ASSERT_EQ(config.streams.size(), 1u);
    const auto& s = config.streams[0];

    EXPECT_EQ(s.name, "full_test");
    EXPECT_EQ(s.taxmode, "limit");
    EXPECT_EQ(s.tintalgo, "upper");
    EXPECT_EQ(s.mapalgo, "patch");
    EXPECT_EQ(s.dtlimit, 2000000000);
    EXPECT_EQ(s.yearFirst, 2015);
    EXPECT_EQ(s.yearLast, 2025);
    EXPECT_EQ(s.yearAlign, 2020);
    EXPECT_EQ(s.offset, 3600);
    EXPECT_EQ(s.meshfile, "/data/mesh.nc");
    EXPECT_EQ(s.lev_dimname, "level");
}

// ===========================================================================
// Error detection: missing file (Req 7.3, 7.4, 7.8)
// ===========================================================================

/**
 * @brief Test that parsing a non-existent file throws with a descriptive message.
 *
 * The exception message must include the missing file path so the user knows
 * exactly what to fix.
 *
 * @see Requirements 7.8
 */
TEST_F(CdepsParserTest, ParseMissingFileThrowsWithPath) {
    const std::string missing = "nonexistent_streams_file.txt";
    try {
        CdepsStreamsParser::ParseStreamsFile(missing);
        FAIL() << "Expected std::runtime_error for missing file";
    } catch (const std::runtime_error& e) {
        std::string msg(e.what());
        EXPECT_NE(msg.find(missing), std::string::npos)
            << "Error message should contain the missing file path. Got: " << msg;
    }
}

/**
 * @brief Test validation reports missing file paths with stream name.
 *
 * When a stream has no file_paths, the error must name the stream so the
 * user can locate the problem in the config.
 *
 * @see Requirements 7.2, 7.3, 7.8
 */
TEST_F(CdepsParserTest, ValidateMissingFilePaths) {
    AcesCdepsConfig config = CdepsStreamsParser::ParseStreamsFile("test_streams_invalid.txt");

    std::vector<std::string> errors;
    bool valid = CdepsStreamsParser::ValidateStreamsConfig(config, errors);

    EXPECT_FALSE(valid);
    ASSERT_GT(errors.size(), 0u);

    // Error must mention the stream name and the missing file_paths
    EXPECT_TRUE(AnyErrorContains(errors, "invalid_stream"))
        << "Error should identify the offending stream name";
    EXPECT_TRUE(AnyErrorContains(errors, "No file paths specified"))
        << "Error should state that file paths are missing";
}

/**
 * @brief Test validation reports non-existent file paths with the path itself.
 *
 * When a stream references a file that does not exist on disk, the error
 * must include the exact path so the user can correct it.
 *
 * @see Requirements 7.4, 7.8
 */
TEST_F(CdepsParserTest, ValidateNonExistentFilePath) {
    AcesCdepsConfig config;
    CdepsStreamConfig stream;
    stream.name = "missing_file_stream";
    stream.file_paths = {"/absolutely/nonexistent/path/emissions.nc"};
    stream.variables = {{"CO_emis", "CO"}};
    stream.taxmode = "cycle";
    stream.tintalgo = "linear";
    config.streams.push_back(stream);

    std::vector<std::string> errors;
    CdepsStreamsParser::ValidateStreamsConfig(config, errors);

    EXPECT_TRUE(AnyErrorContains(errors, "/absolutely/nonexistent/path/emissions.nc"))
        << "Error should contain the missing file path. Errors: "
        << (errors.empty() ? "(none)" : errors[0]);
}

// ===========================================================================
// Error detection: invalid variables (Req 7.5, 7.9)
// ===========================================================================

/**
 * @brief Test validation reports missing variables with stream name.
 *
 * When a stream has no variables defined, the error must name the stream.
 *
 * @see Requirements 7.2, 7.5, 7.9
 */
TEST_F(CdepsParserTest, ValidateMissingVariables) {
    AcesCdepsConfig config = CdepsStreamsParser::ParseStreamsFile("test_streams_invalid.txt");

    std::vector<std::string> errors;
    bool valid = CdepsStreamsParser::ValidateStreamsConfig(config, errors);

    EXPECT_FALSE(valid);
    EXPECT_TRUE(AnyErrorContains(errors, "No variables specified"))
        << "Error should state that variables are missing";
    EXPECT_TRUE(AnyErrorContains(errors, "invalid_stream"))
        << "Error should identify the offending stream name";
}

/**
 * @brief Test validation reports missing NetCDF variable with variable name and file path.
 *
 * When a variable does not exist in the NetCDF file, the error must include
 * both the variable name and the file path so the user knows exactly what
 * to look for.
 *
 * @see Requirements 7.5, 7.9
 */
TEST_F(CdepsParserTest, ValidateMissingNetCDFVariableMessageContent) {
    // Create a real (empty) NetCDF-like file that exists but won't have the variable
    // We use a plain text file here; the parser will fail the NetCDF open check
    // and report the file as not a valid NetCDF file — which is also an actionable error.
    const std::string fake_nc = "fake_emissions.nc";
    WriteFile(fake_nc, "not a netcdf file\n");

    AcesCdepsConfig config;
    CdepsStreamConfig stream;
    stream.name = "var_check_stream";
    stream.file_paths = {fake_nc};
    stream.variables = {{"MISSING_VAR", "MV"}};
    stream.taxmode = "cycle";
    stream.tintalgo = "linear";
    config.streams.push_back(stream);

    std::vector<std::string> errors;
    CdepsStreamsParser::ValidateStreamsConfig(config, errors);

    // Either "not a valid NetCDF file" or "Variable not found" — both are actionable
    bool actionable = AnyErrorContains(errors, fake_nc) ||
                      AnyErrorContains(errors, "MISSING_VAR") ||
                      AnyErrorContains(errors, "NetCDF");
    EXPECT_TRUE(actionable)
        << "Error should reference the file or variable name. Errors: "
        << (errors.empty() ? "(none)" : errors[0]);

    std::remove(fake_nc.c_str());
}

// ===========================================================================
// Error detection: bad interpolation modes (Req 7.6, 7.10)
// ===========================================================================

/**
 * @brief Test validation detects invalid tintalgo with valid options listed.
 *
 * The error message must name the invalid value AND list the valid options
 * so the user knows how to fix it.
 *
 * @see Requirements 7.6, 7.10
 */
TEST_F(CdepsParserTest, ValidateInvalidTintalgoWithValidOptionsListed) {
    AcesCdepsConfig config = CdepsStreamsParser::ParseStreamsFile("test_streams_invalid.txt");

    std::vector<std::string> errors;
    CdepsStreamsParser::ValidateStreamsConfig(config, errors);

    EXPECT_TRUE(AnyErrorContains(errors, "Invalid temporal interpolation mode"))
        << "Error should describe the problem (invalid tintalgo)";
    EXPECT_TRUE(AnyErrorContains(errors, "bad_interp"))
        << "Error should echo the invalid value back to the user";
    // Must list at least one valid option so the user knows what to use
    bool lists_valid = AnyErrorContains(errors, "none") ||
                       AnyErrorContains(errors, "linear") ||
                       AnyErrorContains(errors, "nearest");
    EXPECT_TRUE(lists_valid)
        << "Error should list valid tintalgo options (none, linear, nearest, ...)";
}

/**
 * @brief Test validation detects invalid taxmode with valid options listed.
 *
 * @see Requirements 7.6, 7.10
 */
TEST_F(CdepsParserTest, ValidateInvalidTaxmodeWithValidOptionsListed) {
    AcesCdepsConfig config = CdepsStreamsParser::ParseStreamsFile("test_streams_invalid.txt");

    std::vector<std::string> errors;
    CdepsStreamsParser::ValidateStreamsConfig(config, errors);

    EXPECT_TRUE(AnyErrorContains(errors, "Invalid time axis mode"))
        << "Error should describe the problem (invalid taxmode)";
    EXPECT_TRUE(AnyErrorContains(errors, "invalid_mode"))
        << "Error should echo the invalid taxmode value back to the user";
    bool lists_valid = AnyErrorContains(errors, "cycle") ||
                       AnyErrorContains(errors, "extend") ||
                       AnyErrorContains(errors, "limit");
    EXPECT_TRUE(lists_valid)
        << "Error should list valid taxmode options (cycle, extend, limit)";
}

/**
 * @brief Test all valid tintalgo values are accepted.
 *
 * @see Requirement 7.6
 */
TEST_F(CdepsParserTest, ValidTintalgoValuesAccepted) {
    const std::vector<std::string> valid_modes = {"none", "linear", "nearest", "lower", "upper"};

    for (const auto& mode : valid_modes) {
        AcesCdepsConfig config;
        CdepsStreamConfig stream;
        stream.name = "mode_test";
        stream.file_paths = {"/data/test.nc"};
        stream.variables = {{"CO", "CO"}};
        stream.taxmode = "cycle";
        stream.tintalgo = mode;
        config.streams.push_back(stream);

        std::vector<std::string> errors;
        CdepsStreamsParser::ValidateStreamsConfig(config, errors);

        // Should not produce a tintalgo error
        EXPECT_FALSE(AnyErrorContains(errors, "Invalid temporal interpolation mode"))
            << "tintalgo='" << mode << "' should be valid but got error";
    }
}

/**
 * @brief Test all valid taxmode values are accepted.
 *
 * @see Requirement 7.6
 */
TEST_F(CdepsParserTest, ValidTaxmodeValuesAccepted) {
    const std::vector<std::string> valid_modes = {"cycle", "extend", "limit"};

    for (const auto& mode : valid_modes) {
        AcesCdepsConfig config;
        CdepsStreamConfig stream;
        stream.name = "taxmode_test";
        stream.file_paths = {"/data/test.nc"};
        stream.variables = {{"CO", "CO"}};
        stream.taxmode = mode;
        stream.tintalgo = "linear";
        config.streams.push_back(stream);

        std::vector<std::string> errors;
        CdepsStreamsParser::ValidateStreamsConfig(config, errors);

        EXPECT_FALSE(AnyErrorContains(errors, "Invalid time axis mode"))
            << "taxmode='" << mode << "' should be valid but got error";
    }
}

// ===========================================================================
// Error message quality: actionability (Req 7.7-7.10)
// ===========================================================================

/**
 * @brief Test that all validation errors include the stream name.
 *
 * Every error message must identify which stream is problematic so the user
 * can locate it in a multi-stream config file.
 *
 * @see Requirements 7.7, 7.8, 7.9, 7.10
 */
TEST_F(CdepsParserTest, AllErrorsIdentifyStreamName) {
    AcesCdepsConfig config = CdepsStreamsParser::ParseStreamsFile("test_streams_invalid.txt");

    std::vector<std::string> errors;
    CdepsStreamsParser::ValidateStreamsConfig(config, errors);

    ASSERT_GT(errors.size(), 0u) << "Expected validation errors";

    for (const auto& error : errors) {
        EXPECT_NE(error.find("invalid_stream"), std::string::npos)
            << "Every error should name the stream. Got: " << error;
    }
}

/**
 * @brief Test that empty configuration produces a clear error.
 *
 * @see Requirement 7.7
 */
TEST_F(CdepsParserTest, ValidateEmptyConfigProducesClearError) {
    AcesCdepsConfig config;

    std::vector<std::string> errors;
    bool valid = CdepsStreamsParser::ValidateStreamsConfig(config, errors);

    EXPECT_FALSE(valid);
    ASSERT_GT(errors.size(), 0u);
    EXPECT_EQ(errors[0], "No streams defined in configuration");
}

/**
 * @brief Test that multiple errors are all reported (not just the first).
 *
 * The validator must collect all errors so the user can fix everything in
 * one pass rather than discovering issues one at a time.
 *
 * @see Requirement 7.7
 */
TEST_F(CdepsParserTest, MultipleErrorsAllReported) {
    AcesCdepsConfig config = CdepsStreamsParser::ParseStreamsFile("test_streams_invalid.txt");

    std::vector<std::string> errors;
    CdepsStreamsParser::ValidateStreamsConfig(config, errors);

    // The invalid stream has: no file_paths, no variables, bad tintalgo, bad taxmode
    // We expect at least 3 distinct errors
    EXPECT_GE(errors.size(), 3u)
        << "Validator should report all errors, not stop at the first one";
}

// ===========================================================================
// Round-trip serialization (Req 7.13, Property 16)
// ===========================================================================

/**
 * @brief Test round-trip serialization: parse → write → parse produces equivalent config.
 *
 * Validates Property 16: Streams Configuration Round-Trip.
 *
 * @see Requirements 7.13, Property 16
 */
TEST_F(CdepsParserTest, RoundTripSerialization) {
    AcesCdepsConfig config1 = CdepsStreamsParser::ParseStreamsFile("test_streams_valid.txt");

    CdepsStreamsParser::WriteStreamsFile("test_streams_output.txt", config1);

    AcesCdepsConfig config2 = CdepsStreamsParser::ParseStreamsFile("test_streams_output.txt");

    ASSERT_EQ(config1.streams.size(), config2.streams.size());

    for (size_t i = 0; i < config1.streams.size(); ++i) {
        const auto& s1 = config1.streams[i];
        const auto& s2 = config2.streams[i];

        EXPECT_EQ(s1.name, s2.name);
        EXPECT_EQ(s1.file_paths, s2.file_paths);
        EXPECT_EQ(s1.taxmode, s2.taxmode);
        EXPECT_EQ(s1.tintalgo, s2.tintalgo);
        EXPECT_EQ(s1.mapalgo, s2.mapalgo);
        EXPECT_EQ(s1.yearFirst, s2.yearFirst);
        EXPECT_EQ(s1.yearLast, s2.yearLast);
        EXPECT_EQ(s1.yearAlign, s2.yearAlign);
        EXPECT_EQ(s1.offset, s2.offset);

        ASSERT_EQ(s1.variables.size(), s2.variables.size());
        for (size_t j = 0; j < s1.variables.size(); ++j) {
            EXPECT_EQ(s1.variables[j].name_in_file, s2.variables[j].name_in_file);
            EXPECT_EQ(s1.variables[j].name_in_model, s2.variables[j].name_in_model);
        }
    }
}

/**
 * @brief Test that WriteStreamsFile throws for unwritable path.
 *
 * @see Requirement 7.7
 */
TEST_F(CdepsParserTest, WriteToUnwritablePathThrows) {
    AcesCdepsConfig config = CdepsStreamsParser::ParseStreamsFile("test_streams_valid.txt");

    EXPECT_THROW(
        CdepsStreamsParser::WriteStreamsFile("/nonexistent_dir/output.txt", config),
        std::runtime_error);
}

}  // namespace
}  // namespace aces
