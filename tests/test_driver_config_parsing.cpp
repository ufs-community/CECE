/**
 * @file test_driver_config_parsing.cpp
 * @brief Unit tests for driver configuration parsing (Task 1.2).
 *
 * Tests the YAML parsing of the optional driver section including:
 * - Default values when driver section is absent
 * - Custom values when driver section is present
 * - All driver configuration fields (start_time, end_time, timestep_seconds, mesh_file, grid.nx, grid.ny)
 *
 * Requirements: 1.1, 1.2, 2.1, 2.2, 3.1, 3.2, 14.1, 14.5, 15.1, 15.3
 */

#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include "aces/aces_config.hpp"

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

class DriverConfigParsingTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_config_file = "test_driver_config_parsing.yaml";
    }

    void TearDown() override {
        CleanupTestFile(test_config_file);
    }

    std::string test_config_file;
};

// Test 1: Default values when driver section is absent
// Requirements: 1.2, 2.2, 3.2, 14.5, 15.3
TEST_F(DriverConfigParsingTest, DefaultValuesWhenDriverSectionAbsent) {
    // Write minimal config without driver section
    WriteTestConfig(test_config_file, R"(
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    // Parse config
    aces::AcesConfig config = aces::ParseConfig(test_config_file);

    // Verify default values are used
    EXPECT_EQ(config.driver_config.start_time, "2020-01-01T00:00:00");
    EXPECT_EQ(config.driver_config.end_time, "2020-01-02T00:00:00");
    EXPECT_EQ(config.driver_config.timestep_seconds, 3600);
    EXPECT_TRUE(config.driver_config.mesh_file.empty());
    EXPECT_EQ(config.driver_config.grid.nx, 4);
    EXPECT_EQ(config.driver_config.grid.ny, 4);
}

// Test 2: Custom start_time is parsed correctly
// Requirements: 1.1
TEST_F(DriverConfigParsingTest, CustomStartTimeParsed) {
    WriteTestConfig(test_config_file, R"(
driver:
  start_time: "2021-06-15T12:30:00"
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    aces::AcesConfig config = aces::ParseConfig(test_config_file);

    EXPECT_EQ(config.driver_config.start_time, "2021-06-15T12:30:00");
    // Other fields should have defaults
    EXPECT_EQ(config.driver_config.end_time, "2020-01-02T00:00:00");
    EXPECT_EQ(config.driver_config.timestep_seconds, 3600);
}

// Test 3: Custom end_time is parsed correctly
// Requirements: 2.1
TEST_F(DriverConfigParsingTest, CustomEndTimeParsed) {
    WriteTestConfig(test_config_file, R"(
driver:
  end_time: "2021-12-31T23:59:59"
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    aces::AcesConfig config = aces::ParseConfig(test_config_file);

    EXPECT_EQ(config.driver_config.end_time, "2021-12-31T23:59:59");
    // Other fields should have defaults
    EXPECT_EQ(config.driver_config.start_time, "2020-01-01T00:00:00");
    EXPECT_EQ(config.driver_config.timestep_seconds, 3600);
}

// Test 4: Custom timestep_seconds is parsed correctly
// Requirements: 3.1
TEST_F(DriverConfigParsingTest, CustomTimestepParsed) {
    WriteTestConfig(test_config_file, R"(
driver:
  timestep_seconds: 1800
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    aces::AcesConfig config = aces::ParseConfig(test_config_file);

    EXPECT_EQ(config.driver_config.timestep_seconds, 1800);
    // Other fields should have defaults
    EXPECT_EQ(config.driver_config.start_time, "2020-01-01T00:00:00");
    EXPECT_EQ(config.driver_config.end_time, "2020-01-02T00:00:00");
}

// Test 5: Custom mesh_file is parsed correctly
// Requirements: 14.1
TEST_F(DriverConfigParsingTest, CustomMeshFileParsed) {
    WriteTestConfig(test_config_file, R"(
driver:
  mesh_file: "/path/to/custom_mesh.nc"
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    aces::AcesConfig config = aces::ParseConfig(test_config_file);

    EXPECT_EQ(config.driver_config.mesh_file, "/path/to/custom_mesh.nc");
    // Other fields should have defaults
    EXPECT_EQ(config.driver_config.grid.nx, 4);
    EXPECT_EQ(config.driver_config.grid.ny, 4);
}

// Test 6: Custom grid dimensions are parsed correctly
// Requirements: 14.1
TEST_F(DriverConfigParsingTest, CustomGridDimensionsParsed) {
    WriteTestConfig(test_config_file, R"(
driver:
  grid:
    nx: 360
    ny: 180
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    aces::AcesConfig config = aces::ParseConfig(test_config_file);

    EXPECT_EQ(config.driver_config.grid.nx, 360);
    EXPECT_EQ(config.driver_config.grid.ny, 180);
    // Other fields should have defaults
    EXPECT_TRUE(config.driver_config.mesh_file.empty());
}

// Test 7: Full driver configuration with all fields
// Requirements: 1.1, 2.1, 3.1, 14.1
TEST_F(DriverConfigParsingTest, FullDriverConfigurationParsed) {
    WriteTestConfig(test_config_file, R"(
driver:
  start_time: "2020-03-15T06:00:00"
  end_time: "2020-03-16T18:00:00"
  timestep_seconds: 900
  mesh_file: "/data/mesh_files/global_mesh.nc"
  grid:
    nx: 720
    ny: 360
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    aces::AcesConfig config = aces::ParseConfig(test_config_file);

    EXPECT_EQ(config.driver_config.start_time, "2020-03-15T06:00:00");
    EXPECT_EQ(config.driver_config.end_time, "2020-03-16T18:00:00");
    EXPECT_EQ(config.driver_config.timestep_seconds, 900);
    EXPECT_EQ(config.driver_config.mesh_file, "/data/mesh_files/global_mesh.nc");
    EXPECT_EQ(config.driver_config.grid.nx, 720);
    EXPECT_EQ(config.driver_config.grid.ny, 360);
}

// Test 8: Partial grid configuration (only nx specified)
TEST_F(DriverConfigParsingTest, PartialGridConfigurationNxOnly) {
    WriteTestConfig(test_config_file, R"(
driver:
  grid:
    nx: 100
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    aces::AcesConfig config = aces::ParseConfig(test_config_file);

    EXPECT_EQ(config.driver_config.grid.nx, 100);
    EXPECT_EQ(config.driver_config.grid.ny, 4);  // Default value
}

// Test 9: Partial grid configuration (only ny specified)
TEST_F(DriverConfigParsingTest, PartialGridConfigurationNyOnly) {
    WriteTestConfig(test_config_file, R"(
driver:
  grid:
    ny: 200
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    aces::AcesConfig config = aces::ParseConfig(test_config_file);

    EXPECT_EQ(config.driver_config.grid.nx, 4);  // Default value
    EXPECT_EQ(config.driver_config.grid.ny, 200);
}

// Test 10: Empty mesh_file string (should be treated as no mesh file)
TEST_F(DriverConfigParsingTest, EmptyMeshFileString) {
    WriteTestConfig(test_config_file, R"(
driver:
  mesh_file: ""
  grid:
    nx: 8
    ny: 8
species:
  co:
    - field: "TEST_CO"
      operation: "add"
)");

    aces::AcesConfig config = aces::ParseConfig(test_config_file);

    EXPECT_TRUE(config.driver_config.mesh_file.empty());
    EXPECT_EQ(config.driver_config.grid.nx, 8);
    EXPECT_EQ(config.driver_config.grid.ny, 8);
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
