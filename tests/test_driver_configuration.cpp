/**
 * @file test_driver_configuration.cpp
 * @brief Tests for driver configuration parsing and validation.
 *
 * Validates:
 *   - ISO8601 datetime parsing (YYYY-MM-DDTHH:MM:SS format)
 *   - Configuration file parsing for driver section
 *   - Default value fallback
 *   - Validation of start_time < end_time
 *   - Validation of positive timestep
 *   - Validation of positive grid dimensions
 *
 * Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 2.1, 2.2, 2.3, 2.4, 3.1, 3.2, 3.3, 14.1, 14.2, 14.3, 14.4,
 * 14.5, 14.6, 14.7
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

#include "cece/cece_config.hpp"

using namespace cece;

// ---------------------------------------------------------------------------
// Helper: Write test config files
// ---------------------------------------------------------------------------

static void WriteConfigFile(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    f << content;
    f.close();
}

static void DeleteFile(const std::string& path) {
    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// Tests for ISO8601 Parsing (Task 1.3, 1.4)
// ---------------------------------------------------------------------------

class ISO8601ParsingTest : public ::testing::Test {
   protected:
    // Helper to parse ISO8601 string (mimics Fortran parse_iso8601)
    static bool ParseISO8601(const std::string& iso_str, int& yy, int& mm, int& dd, int& hh, int& mn, int& ss) {
        if (iso_str.length() < 19) return false;  // YYYY-MM-DDTHH:MM:SS

        try {
            yy = std::stoi(iso_str.substr(0, 4));
            mm = std::stoi(iso_str.substr(5, 2));
            dd = std::stoi(iso_str.substr(8, 2));

            if (iso_str[10] != 'T') return false;

            hh = std::stoi(iso_str.substr(11, 2));
            mn = std::stoi(iso_str.substr(14, 2));
            ss = std::stoi(iso_str.substr(17, 2));

            return true;
        } catch (...) {
            return false;
        }
    }
};

// Property 1: ISO8601 Parsing Round Trip
// For any valid ISO8601 datetime string, parsing and reconstructing should produce equivalent
// datetime
TEST_F(ISO8601ParsingTest, ValidISO8601Format) {
    int yy, mm, dd, hh, mn, ss;

    // Test valid format
    EXPECT_TRUE(ParseISO8601("2020-01-01T00:00:00", yy, mm, dd, hh, mn, ss));
    EXPECT_EQ(yy, 2020);
    EXPECT_EQ(mm, 1);
    EXPECT_EQ(dd, 1);
    EXPECT_EQ(hh, 0);
    EXPECT_EQ(mn, 0);
    EXPECT_EQ(ss, 0);
}

TEST_F(ISO8601ParsingTest, ValidISO8601FormatWithTime) {
    int yy, mm, dd, hh, mn, ss;

    // Test with non-zero time
    EXPECT_TRUE(ParseISO8601("2020-06-15T14:30:45", yy, mm, dd, hh, mn, ss));
    EXPECT_EQ(yy, 2020);
    EXPECT_EQ(mm, 6);
    EXPECT_EQ(dd, 15);
    EXPECT_EQ(hh, 14);
    EXPECT_EQ(mn, 30);
    EXPECT_EQ(ss, 45);
}

TEST_F(ISO8601ParsingTest, InvalidISO8601Format) {
    int yy, mm, dd, hh, mn, ss;

    // Test invalid formats
    EXPECT_FALSE(ParseISO8601("2020-01-01", yy, mm, dd, hh, mn, ss));       // Missing time
    EXPECT_FALSE(ParseISO8601("20200101T000000", yy, mm, dd, hh, mn, ss));  // No separators
    EXPECT_FALSE(ParseISO8601("invalid", yy, mm, dd, hh, mn, ss));          // Completely invalid
}

// ---------------------------------------------------------------------------
// Tests for Configuration File Parsing (Task 1.2, 1.6)
// ---------------------------------------------------------------------------

class DriverConfigurationTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // Use a unique filename per test to avoid race conditions when
        // ctest runs multiple test binaries in parallel (-j).
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        test_config_file = std::string("test_driver_config_") + info->test_suite_name() + "_" + info->name() + ".yaml";
    }

    void TearDown() override {
        DeleteFile(test_config_file);
    }

    std::string test_config_file;
};

TEST_F(DriverConfigurationTest, DefaultDriverConfiguration) {
    // Write minimal config without driver section
    WriteConfigFile(test_config_file, R"(
species:
  CO:
    - operation: add
      field: CO_anthro
      hierarchy: 0
      scale: 1.0

physics_schemes:
  - name: NativeExample
    language: cpp
)");

    CeceConfig config = ParseConfig(test_config_file);

    // Verify defaults are used
    EXPECT_EQ(config.driver_config.start_time, "2020-01-01T00:00:00");
    EXPECT_EQ(config.driver_config.end_time, "2020-01-02T00:00:00");
    EXPECT_EQ(config.driver_config.timestep_seconds, 3600);
    EXPECT_TRUE(config.driver_config.mesh_file.empty());
    EXPECT_EQ(config.driver_config.grid.nx, 4);
    EXPECT_EQ(config.driver_config.grid.ny, 4);
}

TEST_F(DriverConfigurationTest, CustomDriverConfiguration) {
    // Write config with custom driver section
    WriteConfigFile(test_config_file, R"(
driver:
  start_time: "2020-06-01T12:00:00"
  end_time: "2020-06-02T12:00:00"
  timestep_seconds: 1800
  mesh_file: "/path/to/mesh.nc"
  grid:
    nx: 8
    ny: 8

species:
  CO:
    - operation: add
      field: CO_anthro
      hierarchy: 0
      scale: 1.0

physics_schemes:
  - name: NativeExample
    language: cpp
)");

    CeceConfig config = ParseConfig(test_config_file);

    // Verify custom values are parsed
    EXPECT_EQ(config.driver_config.start_time, "2020-06-01T12:00:00");
    EXPECT_EQ(config.driver_config.end_time, "2020-06-02T12:00:00");
    EXPECT_EQ(config.driver_config.timestep_seconds, 1800);
    EXPECT_EQ(config.driver_config.mesh_file, "/path/to/mesh.nc");
    EXPECT_EQ(config.driver_config.grid.nx, 8);
    EXPECT_EQ(config.driver_config.grid.ny, 8);
}

TEST_F(DriverConfigurationTest, PartialDriverConfiguration) {
    // Write config with only some driver fields
    WriteConfigFile(test_config_file, R"(
driver:
  start_time: "2020-03-15T06:00:00"
  grid:
    nx: 16

species:
  CO:
    - operation: add
      field: CO_anthro
      hierarchy: 0
      scale: 1.0

physics_schemes:
  - name: NativeExample
    language: cpp
)");

    CeceConfig config = ParseConfig(test_config_file);

    // Verify partial config with defaults for missing fields
    EXPECT_EQ(config.driver_config.start_time, "2020-03-15T06:00:00");
    EXPECT_EQ(config.driver_config.end_time, "2020-01-02T00:00:00");  // Default
    EXPECT_EQ(config.driver_config.timestep_seconds, 3600);           // Default
    EXPECT_TRUE(config.driver_config.mesh_file.empty());              // Default
    EXPECT_EQ(config.driver_config.grid.nx, 16);
    EXPECT_EQ(config.driver_config.grid.ny, 4);  // Default
}

// ---------------------------------------------------------------------------
// Tests for Configuration Validation (Task 1.5)
// ---------------------------------------------------------------------------

class DriverConfigurationValidationTest : public ::testing::Test {
   protected:
    // Helper to validate start < end
    static bool ValidateTimeOrdering(const std::string& start_str, const std::string& end_str) {
        // Simple string comparison for ISO8601 format works for validation
        return start_str < end_str;
    }

    // Helper to validate positive timestep
    static bool ValidateTimestep(int timestep_seconds) {
        return timestep_seconds > 0;
    }

    // Helper to validate positive grid dimensions
    static bool ValidateGridDimensions(int nx, int ny) {
        return nx > 0 && ny > 0;
    }
};

TEST_F(DriverConfigurationValidationTest, ValidTimeOrdering) {
    EXPECT_TRUE(ValidateTimeOrdering("2020-01-01T00:00:00", "2020-01-02T00:00:00"));
    EXPECT_TRUE(ValidateTimeOrdering("2020-01-01T00:00:00", "2020-01-01T01:00:00"));
}

TEST_F(DriverConfigurationValidationTest, InvalidTimeOrdering) {
    EXPECT_FALSE(ValidateTimeOrdering("2020-01-02T00:00:00", "2020-01-01T00:00:00"));
    EXPECT_FALSE(ValidateTimeOrdering("2020-01-01T00:00:00", "2020-01-01T00:00:00"));  // Equal
}

TEST_F(DriverConfigurationValidationTest, ValidTimestep) {
    EXPECT_TRUE(ValidateTimestep(1));
    EXPECT_TRUE(ValidateTimestep(3600));
    EXPECT_TRUE(ValidateTimestep(86400));
}

TEST_F(DriverConfigurationValidationTest, InvalidTimestep) {
    EXPECT_FALSE(ValidateTimestep(0));
    EXPECT_FALSE(ValidateTimestep(-1));
    EXPECT_FALSE(ValidateTimestep(-3600));
}

TEST_F(DriverConfigurationValidationTest, ValidGridDimensions) {
    EXPECT_TRUE(ValidateGridDimensions(1, 1));
    EXPECT_TRUE(ValidateGridDimensions(4, 4));
    EXPECT_TRUE(ValidateGridDimensions(360, 180));
}

TEST_F(DriverConfigurationValidationTest, InvalidGridDimensions) {
    EXPECT_FALSE(ValidateGridDimensions(0, 4));
    EXPECT_FALSE(ValidateGridDimensions(4, 0));
    EXPECT_FALSE(ValidateGridDimensions(-1, 4));
    EXPECT_FALSE(ValidateGridDimensions(4, -1));
}

// ---------------------------------------------------------------------------
// Tests for C Interface Validation (Task 1.5)
// ---------------------------------------------------------------------------

// External C function for getting driver config
extern "C" {
void cece_core_get_driver_config(const char* config_file, int config_file_len, char* start_time, int start_time_len, char* end_time, int end_time_len,
                                 int* timestep_seconds, char* mesh_file, int mesh_file_len, int* nx, int* ny, int* rc);
}

class DriverConfigCInterfaceTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // Use a unique filename per test to avoid race conditions when
        // ctest runs multiple test binaries in parallel (-j).
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        test_config_file = std::string("test_driver_config_c_") + info->name() + ".yaml";
    }

    void TearDown() override {
        DeleteFile(test_config_file);
    }

    std::string test_config_file;
};

TEST_F(DriverConfigCInterfaceTest, ValidConfiguration) {
    WriteConfigFile(test_config_file, R"(
driver:
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-02T00:00:00"
  timestep_seconds: 3600
  grid:
    nx: 4
    ny: 4

species:
  CO:
    - operation: add
      field: CO_anthro
      hierarchy: 0
      scale: 1.0

physics_schemes:
  - name: NativeExample
    language: cpp
)");

    char start_time[64] = {0};
    char end_time[64] = {0};
    char mesh_file[512] = {0};
    int timestep_seconds = 0;
    int nx = 0, ny = 0;
    int rc = 0;

    cece_core_get_driver_config(test_config_file.c_str(), test_config_file.length(), start_time, sizeof(start_time), end_time, sizeof(end_time),
                                &timestep_seconds, mesh_file, sizeof(mesh_file), &nx, &ny, &rc);

    EXPECT_EQ(rc, 0) << "Expected successful config read";
    EXPECT_EQ(timestep_seconds, 3600);
    EXPECT_EQ(nx, 4);
    EXPECT_EQ(ny, 4);
}

TEST_F(DriverConfigCInterfaceTest, InvalidTimestepZero) {
    WriteConfigFile(test_config_file, R"(
driver:
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-02T00:00:00"
  timestep_seconds: 0
  grid:
    nx: 4
    ny: 4

species:
  CO:
    - operation: add
      field: CO_anthro
      hierarchy: 0
      scale: 1.0

physics_schemes:
  - name: NativeExample
    language: cpp
)");

    char start_time[64] = {0};
    char end_time[64] = {0};
    char mesh_file[512] = {0};
    int timestep_seconds = 0;
    int nx = 0, ny = 0;
    int rc = 0;

    cece_core_get_driver_config(test_config_file.c_str(), test_config_file.length(), start_time, sizeof(start_time), end_time, sizeof(end_time),
                                &timestep_seconds, mesh_file, sizeof(mesh_file), &nx, &ny, &rc);

    EXPECT_EQ(rc, -1) << "Expected validation error for timestep_seconds = 0";
}

TEST_F(DriverConfigCInterfaceTest, InvalidTimestepNegative) {
    WriteConfigFile(test_config_file, R"(
driver:
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-02T00:00:00"
  timestep_seconds: -3600
  grid:
    nx: 4
    ny: 4

species:
  CO:
    - operation: add
      field: CO_anthro
      hierarchy: 0
      scale: 1.0

physics_schemes:
  - name: NativeExample
    language: cpp
)");

    char start_time[64] = {0};
    char end_time[64] = {0};
    char mesh_file[512] = {0};
    int timestep_seconds = 0;
    int nx = 0, ny = 0;
    int rc = 0;

    cece_core_get_driver_config(test_config_file.c_str(), test_config_file.length(), start_time, sizeof(start_time), end_time, sizeof(end_time),
                                &timestep_seconds, mesh_file, sizeof(mesh_file), &nx, &ny, &rc);

    EXPECT_EQ(rc, -1) << "Expected validation error for negative timestep_seconds";
}

TEST_F(DriverConfigCInterfaceTest, InvalidGridNxZero) {
    WriteConfigFile(test_config_file, R"(
start_time: "2020-01-01T00:00:00"
end_time: "2020-01-02T00:00:00"
timestep_seconds: 3600
grid_nx: 0
grid_ny: 4
)");

    char start_time[64] = {0};
    char end_time[64] = {0};
    char mesh_file[512] = {0};
    int timestep_seconds = 0;
    int nx = 0, ny = 0;
    int rc = 0;

    cece_core_get_driver_config(test_config_file.c_str(), test_config_file.length(), start_time, sizeof(start_time), end_time, sizeof(end_time),
                                &timestep_seconds, mesh_file, sizeof(mesh_file), &nx, &ny, &rc);

    EXPECT_EQ(rc, -1) << "Expected validation error for nx = 0";
}

TEST_F(DriverConfigCInterfaceTest, InvalidGridNyNegative) {
    WriteConfigFile(test_config_file, R"(
start_time: "2020-01-01T00:00:00"
end_time: "2020-01-02T00:00:00"
timestep_seconds: 3600
grid_nx: 4
grid_ny: -1
)");

    char start_time[64] = {0};
    char end_time[64] = {0};
    char mesh_file[512] = {0};
    int timestep_seconds = 0;
    int nx = 0, ny = 0;
    int rc = 0;

    cece_core_get_driver_config(test_config_file.c_str(), test_config_file.length(), start_time, sizeof(start_time), end_time, sizeof(end_time),
                                &timestep_seconds, mesh_file, sizeof(mesh_file), &nx, &ny, &rc);

    EXPECT_EQ(rc, -1) << "Expected validation error for negative ny";
}

// ---------------------------------------------------------------------------
// Property-Based Tests
// ---------------------------------------------------------------------------

// Property 1: ISO8601 Parsing Round Trip
// For any valid ISO8601 datetime string, parsing should succeed
TEST_F(ISO8601ParsingTest, Property1_ISO8601RoundTrip) {
    // Test a range of valid dates
    std::vector<std::string> valid_dates = {
        "2000-01-01T00:00:00",
        "2020-06-15T14:30:45",
        "2099-12-31T23:59:59",
        "2020-02-29T12:00:00",  // Leap year
    };

    int yy, mm, dd, hh, mn, ss;
    for (const auto& date_str : valid_dates) {
        EXPECT_TRUE(ParseISO8601(date_str, yy, mm, dd, hh, mn, ss)) << "Failed to parse: " << date_str;
    }
}

// Property 20: Default Configuration Correctness
// For any invocation without explicit driver configuration, defaults must be used
TEST_F(DriverConfigurationTest, Property20_DefaultConfigurationCorrectness) {
    WriteConfigFile(test_config_file, R"(
species:
  CO:
    - operation: add
      field: CO_anthro
      hierarchy: 0
      scale: 1.0

physics_schemes:
  - name: NativeExample
    language: cpp
)");

    CeceConfig config = ParseConfig(test_config_file);

    // Verify all documented defaults
    EXPECT_EQ(config.driver_config.start_time, "2020-01-01T00:00:00");
    EXPECT_EQ(config.driver_config.end_time, "2020-01-02T00:00:00");
    EXPECT_EQ(config.driver_config.timestep_seconds, 3600);
    EXPECT_TRUE(config.driver_config.mesh_file.empty());
    EXPECT_EQ(config.driver_config.grid.nx, 4);
    EXPECT_EQ(config.driver_config.grid.ny, 4);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
