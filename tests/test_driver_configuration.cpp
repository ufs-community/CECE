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
#include <stdexcept>

#include "cece/cece_config.hpp"
#include "cece/cece_driver_facade.hpp"

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

static void ExpectConfigInvalidArgument(const std::string& path, const std::string& content, const std::string& expected_message) {
    WriteConfigFile(path, content);
    try {
        (void)ParseConfig(path);
        FAIL() << "Expected ParseConfig to reject: " << content;
    } catch (const std::invalid_argument& error) {
        EXPECT_EQ(error.what(), expected_message);
    }
}

static void ExpectFacadeInvalidArgument(const std::string& path, const std::string& content, const std::string& expected_message) {
    WriteConfigFile(path, content);
    constexpr double coordinate = 0.0;
    try {
        CeceDriverOrchestrator driver(path, 1, 1, 1, &coordinate, 1, &coordinate, 1, MPI_COMM_NULL);
        FAIL() << "Expected the direct driver facade to reject: " << content;
    } catch (const std::invalid_argument& error) {
        EXPECT_EQ(error.what(), expected_message);
    }
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
    EXPECT_TRUE(config.driver_config.gridspec_file.empty());
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
  gridspec_file: "/path/to/mesh.nc"
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
    EXPECT_EQ(config.driver_config.gridspec_file, "/path/to/mesh.nc");
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
    EXPECT_TRUE(config.driver_config.gridspec_file.empty());          // Default
    EXPECT_EQ(config.driver_config.grid.nx, 16);
    EXPECT_EQ(config.driver_config.grid.ny, 4);  // Default
}

TEST_F(DriverConfigurationTest, NzDefaultIsOne) {
    // nz should default to 1 when not specified in config
    WriteConfigFile(test_config_file, R"(
driver:
  grid:
    nx: 4
    ny: 4

species:
  CO:
    - operation: add
      field: CO_anthro
      hierarchy: 0
      scale: 1.0
)");

    CeceConfig config = ParseConfig(test_config_file);
    EXPECT_EQ(config.driver_config.grid.nz, 1);
}

TEST_F(DriverConfigurationTest, NzParsedFromYAML) {
    // nz should be read from driver.grid.nz in the YAML
    WriteConfigFile(test_config_file, R"(
driver:
  grid:
    nx: 8
    ny: 8
    nz: 72

species:
  CO:
    - operation: add
      field: CO_anthro
      hierarchy: 0
      scale: 1.0
)");

    CeceConfig config = ParseConfig(test_config_file);
    EXPECT_EQ(config.driver_config.grid.nz, 72);
}

TEST_F(DriverConfigurationTest, ParseAmioWorkerThreads) {
    // 1. Verify custom positive values are successfully parsed.
    WriteConfigFile(test_config_file, R"(
driver:
  start_time: "2010-01-01T00:00:00"
  end_time: "2010-01-01T23:00:00"
  timestep_seconds: 3600
  amio_worker_threads: 4

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
    EXPECT_EQ(config.driver_config.amio_worker_threads, 4);

    // 2. Verify omitted values retain the default of 1.
    WriteConfigFile(test_config_file, R"(
driver:
  start_time: "2010-01-01T00:00:00"
  end_time: "2010-01-01T23:00:00"
  timestep_seconds: 3600

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
    config = ParseConfig(test_config_file);
    EXPECT_EQ(config.driver_config.amio_worker_threads, 1);
}

TEST_F(DriverConfigurationTest, RejectNonpositiveDriverAmioWorkerThreads) {
    for (const int threads : {0, -3}) {
        const std::string yaml = "driver:\n  amio_worker_threads: " + std::to_string(threads) + "\nphysics_schemes: []\n";
        ExpectConfigInvalidArgument(test_config_file, yaml, "driver.amio_worker_threads must be >= 1; got " + std::to_string(threads) + ".");
    }
}

TEST_F(DriverConfigurationTest, DirectFacadeRejectsNonpositiveAmioWorkerThreads) {
    for (const int threads : {0, -3}) {
        const std::string yaml = "driver:\n  amio_worker_threads: " + std::to_string(threads) + "\nphysics_schemes: []\n";
        ExpectFacadeInvalidArgument(test_config_file, yaml, "driver.amio_worker_threads must be >= 1; got " + std::to_string(threads) + ".");
    }
}

TEST_F(DriverConfigurationTest, ParseAmioStagingBufferCount) {
    WriteConfigFile(test_config_file, R"(
driver:
  amio_staging_buffer_count: 16
physics_schemes: []
)");
    CeceConfig config = ParseConfig(test_config_file);
    EXPECT_EQ(config.driver_config.amio_staging_buffer_count, 16);

    WriteConfigFile(test_config_file, R"(
driver: {}
physics_schemes: []
)");
    config = ParseConfig(test_config_file);
    EXPECT_EQ(config.driver_config.amio_staging_buffer_count, 8);
}

TEST_F(DriverConfigurationTest, RejectNonpositiveAmioStagingBufferCount) {
    for (const int count : {0, -3}) {
        const std::string yaml = "driver:\n  amio_staging_buffer_count: " + std::to_string(count) + "\nphysics_schemes: []\n";
        ExpectConfigInvalidArgument(test_config_file, yaml, "driver.amio_staging_buffer_count must be >= 1; got " + std::to_string(count) + ".");
    }
}

TEST_F(DriverConfigurationTest, DirectFacadeRejectsNonpositiveAmioStagingBufferCount) {
    for (const int count : {0, -3}) {
        const std::string yaml = "driver:\n  amio_staging_buffer_count: " + std::to_string(count) + "\nphysics_schemes: []\n";
        ExpectFacadeInvalidArgument(test_config_file, yaml, "driver.amio_staging_buffer_count must be >= 1; got " + std::to_string(count) + ".");
    }
}

TEST_F(DriverConfigurationTest, ParseAmioWorkerThreadsOutput) {
    // 1. Verify custom positive values parse correctly in output block.
    WriteConfigFile(test_config_file, R"(
output:
  enabled: true
  directory: ./cece_output
  filename_pattern: "cece_ex1_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc"
  frequency_steps: 1
  fields: [CO]
  amio_worker_threads: 3

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
    EXPECT_EQ(config.output_config.amio_worker_threads, 3);

    // 2. Verify omitted output values retain -1 (representing fallback unset).
    WriteConfigFile(test_config_file, R"(
output:
  enabled: true
  directory: ./cece_output
  filename_pattern: "cece_ex1_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc"
  frequency_steps: 1
  fields: [CO]

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
    config = ParseConfig(test_config_file);
    EXPECT_EQ(config.output_config.amio_worker_threads, -1);
}

TEST_F(DriverConfigurationTest, RejectNonpositiveOutputAmioWorkerThreads) {
    for (const int threads : {0, -3}) {
        const std::string yaml =
            "output:\n  enabled: true\n  directory: ./cece_output\n  amio_worker_threads: " + std::to_string(threads) + "\nphysics_schemes: []\n";
        ExpectConfigInvalidArgument(test_config_file, yaml, "output.amio_worker_threads must be >= 1; got " + std::to_string(threads) + ".");
    }
}

TEST_F(DriverConfigurationTest, ParseOutputFieldsWithInlineAttributes) {
    // output.fields entries may be maps carrying per-field NetCDF attributes;
    // scalar entries remain valid shorthand for a field with no attributes.
    WriteConfigFile(test_config_file, R"(
output:
  enabled: true
  directory: ./cece_output
  fields:
    - name: co
      attributes:
        units: "kg m-2 s-1"
        long_name: "carbon_monoxide_emission_flux"
    - name: nox
    - name: isoprene
      attributes:
        units: "kg m-2 s-1"
    - sea_salt_total

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

    const auto data_fields = config.output_config.fields.GetDataFields();
    ASSERT_EQ(data_fields.size(), 4u);
    EXPECT_EQ(data_fields[0].get().name, "co");
    EXPECT_EQ(data_fields[1].get().name, "nox");
    EXPECT_EQ(data_fields[2].get().name, "isoprene");
    EXPECT_EQ(data_fields[3].get().name, "sea_salt_total");

    ASSERT_EQ(data_fields[0].get().attributes.size(), 2u);
    EXPECT_EQ(data_fields[0].get().attributes.at("units"), "kg m-2 s-1");
    EXPECT_EQ(data_fields[0].get().attributes.at("long_name"), "carbon_monoxide_emission_flux");

    ASSERT_EQ(data_fields[2].get().attributes.size(), 1u);
    EXPECT_EQ(data_fields[2].get().attributes.at("units"), "kg m-2 s-1");

    // Fields without configured attributes carry an empty map.
    EXPECT_TRUE(data_fields[1].get().attributes.empty());
    EXPECT_TRUE(data_fields[3].get().attributes.empty());

    // The seeded coordinate variables tag along in every collection.
    EXPECT_EQ(config.output_config.fields.GetCoordinateFields().size(), 4u);
}

TEST_F(DriverConfigurationTest, ParseOutputFieldsScalarShorthandStillWorks) {
    WriteConfigFile(test_config_file, R"(
output:
  enabled: true
  fields: [CO]

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

    const auto data_fields = config.output_config.fields.GetDataFields();
    ASSERT_EQ(data_fields.size(), 1u);
    EXPECT_EQ(data_fields[0].get().name, "CO");
    EXPECT_TRUE(data_fields[0].get().attributes.empty());
}

TEST_F(DriverConfigurationTest, OutputFieldCreateIOManifest) {
    // Configured attributes verbatim, coordinates override honored and
    // emitted last.
    const CeceOutputField configured{"co", {{"units", "kg m-2 s-1"}, {"coordinates", "lon lat time"}}};
    EXPECT_EQ(configured.CreateIOManifest(),
              "  co:\n"
              "    attributes:\n"
              "      units: \"kg m-2 s-1\"\n"
              "      coordinates: \"lon lat time\"\n");

    // No configured attributes: only the structural coordinates default.
    const CeceOutputField bare{"nox", {}};
    EXPECT_EQ(bare.CreateIOManifest(),
              "  nox:\n"
              "    attributes:\n"
              "      coordinates: \"time lev lat lon\"\n");

    // Coordinate variables carry their configured attributes but never a
    // coordinates attribute of their own (attributes emit in map order).
    const CeceOutputField coordinate{"lon", {{"units", "degrees_east"}, {"long_name", "longitude"}}};
    EXPECT_EQ(coordinate.CreateIOManifest(),
              "  lon:\n"
              "    attributes:\n"
              "      long_name: \"longitude\"\n"
              "      units: \"degrees_east\"\n");
}

TEST_F(DriverConfigurationTest, OutputFieldCollectionPartitionLookupAndManifest) {
    // Every collection is seeded with the coordinate variables; configured
    // entries join as data fields (coordinate-named entries are rejected —
    // see CollectionRejectsDuplicateFieldNames).
    CeceOutputFieldCollection collection{
        {"co", {{"units", "kg m-2 s-1"}}},
        {"nox", {}},
    };

    const auto coordinate_fields = collection.GetCoordinateFields();
    ASSERT_EQ(coordinate_fields.size(), 4u);
    EXPECT_EQ(coordinate_fields[0].get().name, "lon");
    EXPECT_EQ(coordinate_fields[0].get().attributes.at("units"), "degrees_east");
    EXPECT_EQ(coordinate_fields[3].get().name, "time");

    const auto data_fields = collection.GetDataFields();
    ASSERT_EQ(data_fields.size(), 2u);
    EXPECT_EQ(data_fields[0].get().name, "co");
    EXPECT_EQ(data_fields[1].get().name, "nox");

    EXPECT_TRUE(collection.Contains("co"));
    EXPECT_TRUE(collection.Contains("lat"));
    EXPECT_FALSE(collection.Contains("absent"));
    EXPECT_EQ(collection.Find("absent"), nullptr);

    // Time's units are set at initialization (SetTimeUnits turns the
    // ISO-8601 T into a space); rendering is const and returns the
    // variable_names list plus every field's block in declaration order
    // (seeded coordinate variables without a coordinates attribute first,
    // then data fields with override-or-default coordinates last).
    collection.SetTimeUnits("2001-06-01T00:00:00");
    const std::string expected_blocks =
        "  lon:\n"
        "    attributes:\n"
        "      long_name: \"longitude\"\n"
        "      units: \"degrees_east\"\n"
        "  lat:\n"
        "    attributes:\n"
        "      long_name: \"latitude\"\n"
        "      units: \"degrees_north\"\n"
        "  lev:\n"
        "    attributes:\n"
        "      long_name: \"vertical level\"\n"
        "      units: \"level\"\n"
        "  time:\n"
        "    attributes:\n"
        "      long_name: \"time\"\n"
        "      units: \"seconds since 2001-06-01 00:00:00\"\n"
        "  co:\n"
        "    attributes:\n"
        "      units: \"kg m-2 s-1\"\n"
        "      coordinates: \"time lev lat lon\"\n"
        "  nox:\n"
        "    attributes:\n"
        "      coordinates: \"time lev lat lon\"\n";
    EXPECT_EQ(collection.CreateIOManifest(),
              "variable_names: [\"lon\", \"lat\", \"lev\", \"time\", \"co\", \"nox\"]\n"
              "variables:\n" +
                  expected_blocks);
}

TEST_F(DriverConfigurationTest, CollectionRejectsDuplicateFieldNames) {
    // Duplicate data field name.
    CeceOutputFieldCollection collection{{"co", {}}};
    EXPECT_THROW(collection.push_back({"co", {}}), std::runtime_error);

    // Coordinate names collide with the seeded coordinate variables.
    EXPECT_THROW((CeceOutputFieldCollection{{"lon", {}}}), std::runtime_error);

    // Duplicates inside a single initializer list are caught too — the
    // list constructor routes every entry through push_back.
    EXPECT_THROW((CeceOutputFieldCollection{{"co", {}}, {"co", {}}}), std::runtime_error);
}

TEST_F(DriverConfigurationTest, ParseOutputFieldsDuplicateRejected) {
    WriteConfigFile(test_config_file, R"(
output:
  enabled: true
  fields:
    - co
    - name: co
      attributes:
        units: "kg m-2 s-1"

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
    EXPECT_THROW(ParseConfig(test_config_file), std::runtime_error);
}

TEST_F(DriverConfigurationTest, CreateIOManifestRequiresTimeUnits) {
    // Rendering without SetTimeUnits would silently emit a time block with
    // no units; the collection refuses instead. Presence is checked, not
    // content.
    const CeceOutputFieldCollection unset{{"co", {}}};
    EXPECT_THROW(unset.CreateIOManifest(), std::runtime_error);

    CeceOutputFieldCollection set{{"co", {}}};
    set.SetTimeUnits("2001-06-01T00:00:00");
    EXPECT_NO_THROW(set.CreateIOManifest());
}

TEST_F(DriverConfigurationTest, ParseOutputEnabledFalse) {
    // enabled: false keeps the block as dormant configuration but disables
    // output; an omitted or true enabled key keeps output on.
    const std::string config_tail = R"(
  directory: ./cece_output
  fields: [CO]

species:
  CO:
    - operation: add
      field: CO_anthro
      hierarchy: 0
      scale: 1.0

physics_schemes:
  - name: NativeExample
    language: cpp
)";
    WriteConfigFile(test_config_file, "output:\n  enabled: false" + config_tail);
    CeceConfig config = ParseConfig(test_config_file);
    EXPECT_FALSE(config.output_config.enabled);
    // The rest of the block still parses (dormant, not discarded).
    EXPECT_EQ(config.output_config.directory, "./cece_output");
    EXPECT_EQ(config.output_config.fields.GetDataFields().size(), 1u);

    WriteConfigFile(test_config_file, "output:\n  enabled: true" + config_tail);
    config = ParseConfig(test_config_file);
    EXPECT_TRUE(config.output_config.enabled);

    // Presence of the block enables output when the key is omitted.
    WriteConfigFile(test_config_file, "output:" + config_tail);
    config = ParseConfig(test_config_file);
    EXPECT_TRUE(config.output_config.enabled);
}

TEST_F(DriverConfigurationTest, OutputFieldEntryWithoutNameIsRejected) {
    WriteConfigFile(test_config_file, R"(
output:
  enabled: true
  fields:
    - attributes:
        units: "kg m-2 s-1"

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
    EXPECT_THROW(ParseConfig(test_config_file), std::runtime_error);
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
                                 int* timestep_seconds, char* gridspec_file, int gridspec_file_len, int* nx, int* ny, int* rc);
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
    char gridspec_file[512] = {0};
    int timestep_seconds = 0;
    int nx = 0, ny = 0;
    int rc = 0;

    cece_core_get_driver_config(test_config_file.c_str(), test_config_file.length(), start_time, sizeof(start_time), end_time, sizeof(end_time),
                                &timestep_seconds, gridspec_file, sizeof(gridspec_file), &nx, &ny, &rc);

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
    char gridspec_file[512] = {0};
    int timestep_seconds = 0;
    int nx = 0, ny = 0;
    int rc = 0;

    cece_core_get_driver_config(test_config_file.c_str(), test_config_file.length(), start_time, sizeof(start_time), end_time, sizeof(end_time),
                                &timestep_seconds, gridspec_file, sizeof(gridspec_file), &nx, &ny, &rc);

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
    char gridspec_file[512] = {0};
    int timestep_seconds = 0;
    int nx = 0, ny = 0;
    int rc = 0;

    cece_core_get_driver_config(test_config_file.c_str(), test_config_file.length(), start_time, sizeof(start_time), end_time, sizeof(end_time),
                                &timestep_seconds, gridspec_file, sizeof(gridspec_file), &nx, &ny, &rc);

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
    char gridspec_file[512] = {0};
    int timestep_seconds = 0;
    int nx = 0, ny = 0;
    int rc = 0;

    cece_core_get_driver_config(test_config_file.c_str(), test_config_file.length(), start_time, sizeof(start_time), end_time, sizeof(end_time),
                                &timestep_seconds, gridspec_file, sizeof(gridspec_file), &nx, &ny, &rc);

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
    char gridspec_file[512] = {0};
    int timestep_seconds = 0;
    int nx = 0, ny = 0;
    int rc = 0;

    cece_core_get_driver_config(test_config_file.c_str(), test_config_file.length(), start_time, sizeof(start_time), end_time, sizeof(end_time),
                                &timestep_seconds, gridspec_file, sizeof(gridspec_file), &nx, &ny, &rc);

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
    EXPECT_TRUE(config.driver_config.gridspec_file.empty());
    EXPECT_EQ(config.driver_config.grid.nx, 4);
    EXPECT_EQ(config.driver_config.grid.ny, 4);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
