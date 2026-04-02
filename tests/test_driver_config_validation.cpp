/**
 * @file test_driver_config_validation.cpp
 * @brief Unit tests for driver configuration validation (Task 1.6).
 *
 * Tests validation logic for driver configuration including:
 * - Valid/invalid ISO8601 formats
 * - Default value fallback
 * - Validation error cases (start >= end, non-positive timestep, invalid grid dimensions)
 *
 * Requirements: 1.1, 1.4, 2.1, 2.4, 3.1, 3.3, 14.7
 */

#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include "aces/aces_config.hpp"

// ESMF C API for ISO8601 validation
extern "C" {
#include "ESMC.h"
}

// Forward declarations for Fortran ISO8601 functions
extern "C" {
void init_iso8601_utils_c_wrapper();
void parse_iso8601_to_esmf_time_c_wrapper(const char* iso_str, int* yy, int* mm, int* dd,
                                           int* hh, int* mn, int* ss, int* rc);
}

namespace {

// Helper function to write a test config file
void WriteTestConfig(const std::string& filename, const std::string& content) {
    std::ofstream file(filename);
    file << content;
    file.close();
}

// Helper function to clean up test files
void CleanupTestFile(const std::string& filename) {
    std::remove(filename.c_str());
}

// Helper function to validate ISO8601 format using Fortran parser
bool IsValidISO8601(const std::string& iso_str) {
    int yy, mm, dd, hh, mn, ss, rc;
    parse_iso8601_to_esmf_time_c_wrapper(iso_str.c_str(), &yy, &mm, &dd, &hh, &mn, &ss, &rc);
    return (rc == 0);  // ESMF_SUCCESS is typically 0
}

// Helper function to compare two ISO8601 times (returns -1 if t1 < t2, 0 if equal, 1 if t1 > t2)
int CompareISO8601Times(const std::string& t1, const std::string& t2) {
    int yy1, mm1, dd1, hh1, mn1, ss1, rc1;
    int yy2, mm2, dd2, hh2, mn2, ss2, rc2;

    parse_iso8601_to_esmf_time_c_wrapper(t1.c_str(), &yy1, &mm1, &dd1, &hh1, &mn1, &ss1, &rc1);
    parse_iso8601_to_esmf_time_c_wrapper(t2.c_str(), &yy2, &mm2, &dd2, &hh2, &mn2, &ss2, &rc2);

    if (rc1 != 0 || rc2 != 0) {
        return -999;  // Invalid comparison
    }

    // Compare year, month, day, hour, minute, second in order
    if (yy1 != yy2) return (yy1 < yy2) ? -1 : 1;
    if (mm1 != mm2) return (mm1 < mm2) ? -1 : 1;
    if (dd1 != dd2) return (dd1 < dd2) ? -1 : 1;
    if (hh1 != hh2) return (hh1 < hh2) ? -1 : 1;
    if (mn1 != mn2) return (mn1 < mn2) ? -1 : 1;
    if (ss1 != ss2) return (ss1 < ss2) ? -1 : 1;

    return 0;  // Equal
}

class DriverConfigValidationTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_config_file = "test_driver_config_validation.yaml";

        // Initialize ESMF for ISO8601 utilities
        int rc = ESMC_Initialize(nullptr, ESMC_ArgLast);
        if (rc != ESMF_SUCCESS) {
            FAIL() << "Failed to initialize ESMF, rc=" << rc;
        }

        // Initialize ISO8601 utilities
        init_iso8601_utils_c_wrapper();
    }

    void TearDown() override {
        CleanupTestFile(test_config_file);
    }

    std::string test_config_file;
};

// ============================================================================
// ISO8601 Format Validation Tests
// ============================================================================

// Test 1: Valid ISO8601 formats are accepted
// Requirements: 1.1, 2.1
TEST_F(DriverConfigValidationTest, ValidISO8601FormatsAccepted) {
    std::vector<std::string> valid_formats = {
        "2020-01-01T00:00:00",  // Default start time
        "2020-01-02T00:00:00",  // Default end time
        "2021-06-15T12:30:45",  // Mid-year, mid-day
        "1900-01-01T00:00:00",  // Early date
        "2100-12-31T23:59:59",  // Late date
        "2000-02-29T12:00:00",  // Leap year
        "2020-12-31T23:59:59",  // End of year
    };

    for (const auto& iso_str : valid_formats) {
        EXPECT_TRUE(IsValidISO8601(iso_str))
            << "Valid ISO8601 format rejected: " << iso_str;
    }
}

// Test 2: Invalid ISO8601 formats are rejected - wrong separators
// Requirements: 1.3, 2.3
TEST_F(DriverConfigValidationTest, InvalidISO8601FormatWrongSeparators) {
    std::vector<std::string> invalid_formats = {
        "2020/01/01T00:00:00",  // Wrong date separator (/)
        "2020-01-01 00:00:00",  // Wrong time separator (space instead of T)
        "2020-01-01t00:00:00",  // Lowercase 't'
        "2020-01-01T00-00-00",  // Wrong time separator (-)
        "2020.01.01T00:00:00",  // Wrong date separator (.)
    };

    for (const auto& iso_str : invalid_formats) {
        EXPECT_FALSE(IsValidISO8601(iso_str))
            << "Invalid ISO8601 format accepted: " << iso_str;
    }
}

// Test 3: Invalid ISO8601 formats are rejected - wrong length
// Requirements: 1.3, 2.3
TEST_F(DriverConfigValidationTest, InvalidISO8601FormatWrongLength) {
    std::vector<std::string> invalid_formats = {
        "2020-01-01",           // Too short (date only)
        "00:00:00",             // Too short (time only)
        "2020-1-1T00:00:00",    // Missing leading zeros
        "20-01-01T00:00:00",    // Year too short
        "",                     // Empty string
        "2020-01-01T00:00",     // Missing seconds
    };

    for (const auto& iso_str : invalid_formats) {
        EXPECT_FALSE(IsValidISO8601(iso_str))
            << "Invalid ISO8601 format accepted: " << iso_str;
    }
}

// Test 4: Invalid ISO8601 formats are rejected - out of range values
// Requirements: 1.3, 2.3
TEST_F(DriverConfigValidationTest, InvalidISO8601FormatOutOfRange) {
    std::vector<std::string> invalid_formats = {
        "2020-13-01T00:00:00",  // Month > 12
        "2020-00-01T00:00:00",  // Month < 1
        "2020-01-32T00:00:00",  // Day > 31
        "2020-01-00T00:00:00",  // Day < 1
        "2020-01-01T24:00:00",  // Hour > 23
        "2020-01-01T00:60:00",  // Minute > 59
        "2020-01-01T00:00:60",  // Second > 59
        "2020-02-30T00:00:00",  // Invalid day for February
        "2021-02-29T00:00:00",  // Feb 29 in non-leap year
    };

    for (const auto& iso_str : invalid_formats) {
        EXPECT_FALSE(IsValidISO8601(iso_str))
            << "Invalid ISO8601 format accepted: " << iso_str;
    }
}

// ============================================================================
// Time Ordering Validation Tests
// ============================================================================

// Test 5: Start time before end time is valid
// Requirements: 1.4, 2.4
TEST_F(DriverConfigValidationTest, StartTimeBeforeEndTimeValid) {
    WriteTestConfig(test_config_file, R"(
driver:
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-02T00:00:00"
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    // Parse config (should succeed)
    aces::AcesConfig config = aces::ParseConfig(test_config_file);

    // Verify times are parsed correctly
    EXPECT_EQ(config.driver_config.start_time, "2020-01-01T00:00:00");
    EXPECT_EQ(config.driver_config.end_time, "2020-01-02T00:00:00");

    // Verify start < end
    int cmp = CompareISO8601Times(config.driver_config.start_time,
                                   config.driver_config.end_time);
    EXPECT_EQ(cmp, -1) << "Start time should be before end time";
}

// Test 6: Start time equal to end time should be detected
// Requirements: 1.4, 2.4
TEST_F(DriverConfigValidationTest, StartTimeEqualToEndTimeDetected) {
    WriteTestConfig(test_config_file, R"(
driver:
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-01T00:00:00"
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    // Parse config
    aces::AcesConfig config = aces::ParseConfig(test_config_file);

    // Verify times are equal
    int cmp = CompareISO8601Times(config.driver_config.start_time,
                                   config.driver_config.end_time);
    EXPECT_EQ(cmp, 0) << "Start time equals end time (zero-length simulation)";
}

// Test 7: Start time after end time should be detected
// Requirements: 1.4, 2.4
TEST_F(DriverConfigValidationTest, StartTimeAfterEndTimeDetected) {
    WriteTestConfig(test_config_file, R"(
driver:
  start_time: "2020-01-02T00:00:00"
  end_time: "2020-01-01T00:00:00"
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    // Parse config
    aces::AcesConfig config = aces::ParseConfig(test_config_file);

    // Verify start > end (invalid)
    int cmp = CompareISO8601Times(config.driver_config.start_time,
                                   config.driver_config.end_time);
    EXPECT_EQ(cmp, 1) << "Start time is after end time (invalid configuration)";
}

// ============================================================================
// Timestep Validation Tests
// ============================================================================

// Test 8: Positive timestep is valid
// Requirements: 3.1, 3.3
TEST_F(DriverConfigValidationTest, PositiveTimestepValid) {
    std::vector<int> valid_timesteps = {1, 60, 300, 600, 900, 1800, 3600, 7200, 86400};

    for (int timestep : valid_timesteps) {
        WriteTestConfig(test_config_file,
            "driver:\n"
            "  timestep_seconds: " + std::to_string(timestep) + "\n"
            "species:\n"
            "  co:\n"
            "    - field: \"TEST_CO\"\n"
            "      operation: \"add\"\n");

        aces::AcesConfig config = aces::ParseConfig(test_config_file);

        EXPECT_EQ(config.driver_config.timestep_seconds, timestep);
        EXPECT_GT(config.driver_config.timestep_seconds, 0)
            << "Timestep must be positive";
    }
}

// Test 9: Zero timestep should be detected as invalid
// Requirements: 3.3
TEST_F(DriverConfigValidationTest, ZeroTimestepInvalid) {
    WriteTestConfig(test_config_file, R"(
driver:
  timestep_seconds: 0
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    aces::AcesConfig config = aces::ParseConfig(test_config_file);

    // Zero timestep is invalid
    EXPECT_EQ(config.driver_config.timestep_seconds, 0);
    EXPECT_LE(config.driver_config.timestep_seconds, 0)
        << "Zero timestep is invalid";
}

// Test 10: Negative timestep should be detected as invalid
// Requirements: 3.3
TEST_F(DriverConfigValidationTest, NegativeTimestepInvalid) {
    WriteTestConfig(test_config_file, R"(
driver:
  timestep_seconds: -3600
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    aces::AcesConfig config = aces::ParseConfig(test_config_file);

    // Negative timestep is invalid
    EXPECT_EQ(config.driver_config.timestep_seconds, -3600);
    EXPECT_LT(config.driver_config.timestep_seconds, 0)
        << "Negative timestep is invalid";
}

// ============================================================================
// Grid Dimension Validation Tests
// ============================================================================

// Test 11: Positive grid dimensions are valid
// Requirements: 14.7
TEST_F(DriverConfigValidationTest, PositiveGridDimensionsValid) {
    std::vector<std::pair<int, int>> valid_dimensions = {
        {1, 1}, {4, 4}, {8, 8}, {16, 16}, {32, 32},
        {64, 64}, {128, 128}, {360, 180}, {720, 360}
    };

    for (const auto& [nx, ny] : valid_dimensions) {
        WriteTestConfig(test_config_file,
            "driver:\n"
            "  grid:\n"
            "    nx: " + std::to_string(nx) + "\n"
            "    ny: " + std::to_string(ny) + "\n"
            "species:\n"
            "  co:\n"
            "    - field: \"TEST_CO\"\n"
            "      operation: \"add\"\n");

        aces::AcesConfig config = aces::ParseConfig(test_config_file);

        EXPECT_EQ(config.driver_config.grid.nx, nx);
        EXPECT_EQ(config.driver_config.grid.ny, ny);
        EXPECT_GT(config.driver_config.grid.nx, 0) << "nx must be positive";
        EXPECT_GT(config.driver_config.grid.ny, 0) << "ny must be positive";
    }
}

// Test 12: Zero grid dimensions should be detected as invalid
// Requirements: 14.7
TEST_F(DriverConfigValidationTest, ZeroGridDimensionsInvalid) {
    // Test nx = 0
    WriteTestConfig(test_config_file, R"(
driver:
  grid:
    nx: 0
    ny: 4
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    aces::AcesConfig config = aces::ParseConfig(test_config_file);
    EXPECT_EQ(config.driver_config.grid.nx, 0);
    EXPECT_LE(config.driver_config.grid.nx, 0) << "nx = 0 is invalid";

    // Test ny = 0
    WriteTestConfig(test_config_file, R"(
driver:
  grid:
    nx: 4
    ny: 0
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    config = aces::ParseConfig(test_config_file);
    EXPECT_EQ(config.driver_config.grid.ny, 0);
    EXPECT_LE(config.driver_config.grid.ny, 0) << "ny = 0 is invalid";
}

// Test 13: Negative grid dimensions should be detected as invalid
// Requirements: 14.7
TEST_F(DriverConfigValidationTest, NegativeGridDimensionsInvalid) {
    // Test nx < 0
    WriteTestConfig(test_config_file, R"(
driver:
  grid:
    nx: -4
    ny: 4
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    aces::AcesConfig config = aces::ParseConfig(test_config_file);
    EXPECT_EQ(config.driver_config.grid.nx, -4);
    EXPECT_LT(config.driver_config.grid.nx, 0) << "nx < 0 is invalid";

    // Test ny < 0
    WriteTestConfig(test_config_file, R"(
driver:
  grid:
    nx: 4
    ny: -4
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    config = aces::ParseConfig(test_config_file);
    EXPECT_EQ(config.driver_config.grid.ny, -4);
    EXPECT_LT(config.driver_config.grid.ny, 0) << "ny < 0 is invalid";
}

// ============================================================================
// Default Value Tests
// ============================================================================

// Test 14: Default values are used when driver section is absent
// Requirements: 1.2, 2.2, 3.2, 14.5
TEST_F(DriverConfigValidationTest, DefaultValuesWhenDriverSectionAbsent) {
    WriteTestConfig(test_config_file, R"(
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    aces::AcesConfig config = aces::ParseConfig(test_config_file);

    // Verify default values
    EXPECT_EQ(config.driver_config.start_time, "2020-01-01T00:00:00");
    EXPECT_EQ(config.driver_config.end_time, "2020-01-02T00:00:00");
    EXPECT_EQ(config.driver_config.timestep_seconds, 3600);
    EXPECT_TRUE(config.driver_config.mesh_file.empty());
    EXPECT_EQ(config.driver_config.grid.nx, 4);
    EXPECT_EQ(config.driver_config.grid.ny, 4);

    // Verify defaults are valid
    EXPECT_TRUE(IsValidISO8601(config.driver_config.start_time));
    EXPECT_TRUE(IsValidISO8601(config.driver_config.end_time));
    EXPECT_GT(config.driver_config.timestep_seconds, 0);
    EXPECT_GT(config.driver_config.grid.nx, 0);
    EXPECT_GT(config.driver_config.grid.ny, 0);

    // Verify default start < end
    int cmp = CompareISO8601Times(config.driver_config.start_time,
                                   config.driver_config.end_time);
    EXPECT_EQ(cmp, -1) << "Default start time should be before end time";
}

// Test 15: Partial configuration uses defaults for missing fields
// Requirements: 1.2, 2.2, 3.2, 14.5
TEST_F(DriverConfigValidationTest, PartialConfigurationUsesDefaults) {
    WriteTestConfig(test_config_file, R"(
driver:
  start_time: "2021-06-15T12:00:00"
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    aces::AcesConfig config = aces::ParseConfig(test_config_file);

    // Verify custom value
    EXPECT_EQ(config.driver_config.start_time, "2021-06-15T12:00:00");

    // Verify defaults for other fields
    EXPECT_EQ(config.driver_config.end_time, "2020-01-02T00:00:00");
    EXPECT_EQ(config.driver_config.timestep_seconds, 3600);
    EXPECT_TRUE(config.driver_config.mesh_file.empty());
    EXPECT_EQ(config.driver_config.grid.nx, 4);
    EXPECT_EQ(config.driver_config.grid.ny, 4);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

// Test 16: Very large timestep is accepted (but may not divide evenly)
// Requirements: 3.1, 14.7
TEST_F(DriverConfigValidationTest, VeryLargeTimestepAccepted) {
    WriteTestConfig(test_config_file, R"(
driver:
  timestep_seconds: 86400
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    aces::AcesConfig config = aces::ParseConfig(test_config_file);

    EXPECT_EQ(config.driver_config.timestep_seconds, 86400);  // 1 day
    EXPECT_GT(config.driver_config.timestep_seconds, 0);
}

// Test 17: Very small timestep is accepted
// Requirements: 3.1
TEST_F(DriverConfigValidationTest, VerySmallTimestepAccepted) {
    WriteTestConfig(test_config_file, R"(
driver:
  timestep_seconds: 1
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    aces::AcesConfig config = aces::ParseConfig(test_config_file);

    EXPECT_EQ(config.driver_config.timestep_seconds, 1);  // 1 second
    EXPECT_GT(config.driver_config.timestep_seconds, 0);
}

// Test 18: Very large grid dimensions are accepted
// Requirements: 14.7
TEST_F(DriverConfigValidationTest, VeryLargeGridDimensionsAccepted) {
    WriteTestConfig(test_config_file, R"(
driver:
  grid:
    nx: 1440
    ny: 720
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    aces::AcesConfig config = aces::ParseConfig(test_config_file);

    EXPECT_EQ(config.driver_config.grid.nx, 1440);
    EXPECT_EQ(config.driver_config.grid.ny, 720);
    EXPECT_GT(config.driver_config.grid.nx, 0);
    EXPECT_GT(config.driver_config.grid.ny, 0);
}

// Test 19: Single grid cell is accepted
// Requirements: 14.7
TEST_F(DriverConfigValidationTest, SingleGridCellAccepted) {
    WriteTestConfig(test_config_file, R"(
driver:
  grid:
    nx: 1
    ny: 1
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    aces::AcesConfig config = aces::ParseConfig(test_config_file);

    EXPECT_EQ(config.driver_config.grid.nx, 1);
    EXPECT_EQ(config.driver_config.grid.ny, 1);
    EXPECT_GT(config.driver_config.grid.nx, 0);
    EXPECT_GT(config.driver_config.grid.ny, 0);
}

// Test 20: Leap year February 29 is valid
// Requirements: 1.1, 2.1
TEST_F(DriverConfigValidationTest, LeapYearFebruary29Valid) {
    WriteTestConfig(test_config_file, R"(
driver:
  start_time: "2020-02-29T00:00:00"
  end_time: "2020-03-01T00:00:00"
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    aces::AcesConfig config = aces::ParseConfig(test_config_file);

    EXPECT_EQ(config.driver_config.start_time, "2020-02-29T00:00:00");
    EXPECT_TRUE(IsValidISO8601(config.driver_config.start_time));
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
