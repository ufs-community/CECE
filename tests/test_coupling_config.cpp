/**
 * @file test_coupling_config.cpp
 * @brief Unit tests for the optional `nuopc:` field-coupling config section.
 *
 * Validates:
 *   - Absent and empty sections advertise nothing
 *   - Export/import maps parse with optional attributes defaulted
 *   - Entries are ordered alphabetically regardless of document order
 *   - Every parse-time validation rule names the offending key
 *   - The path-based C queries the Fortran cap uses (count + per-index spec)
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>

#include "cece/cece_config.hpp"

extern "C" {
void cece_nuopc_export_count(const char* config_path, int path_len, int* count, int* rc);
void cece_nuopc_export_spec(const char* config_path, int path_len, int index, char* species, int species_cap, int* species_len, char* std_name,
                            int std_cap, int* std_len, char* units, int units_cap, int* units_len, char* name, int name_cap, int* name_len, int* rc);
void cece_nuopc_import_count(const char* config_path, int path_len, int* count, int* rc);
void cece_nuopc_import_spec(const char* config_path, int path_len, int index, char* field, int field_cap, int* field_len, char* std_name, int std_cap,
                            int* std_len, char* units, int units_cap, int* units_len, char* name, int name_cap, int* name_len, int* rc);
}

namespace {

void WriteFile(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    f << content;
    f.close();
}

// Minimal base config with one species and one input name per mapping section.
constexpr const char* kBaseConfig = R"YAML(
species:
  oc:
    - field: "OC_A"
      operation: "add"
  nox:
    - field: "NOX_A"
      operation: "add"
meteorology:
  temperature: air_temperature
scale_factors:
  sf1: some_factor
masks:
  m1: some_mask
output:
  fields:
    - name: oc
)YAML";

std::string WithNuopc(const std::string& nuopc_block) {
    return std::string(kBaseConfig) + "\n" + nuopc_block;
}

class CouplingConfigTest : public ::testing::Test {
   protected:
    void SetUp() override {
        path_ = "/tmp/cece_coupling_config_test.yaml";
    }
    void TearDown() override {
        std::remove(path_.c_str());
    }

    cece::CeceConfig Parse(const std::string& content) {
        WriteFile(path_, content);
        return cece::ParseConfig(path_);
    }

    std::string path_;
};

TEST_F(CouplingConfigTest, AbsentSectionAdvertisesNothing) {
    cece::CeceConfig config = Parse(kBaseConfig);
    EXPECT_TRUE(config.nuopc.export_fields.empty());
    EXPECT_TRUE(config.nuopc.import_fields.empty());
}

TEST_F(CouplingConfigTest, EmptySectionAdvertisesNothing) {
    cece::CeceConfig config = Parse(WithNuopc("nuopc:\n"));
    EXPECT_TRUE(config.nuopc.export_fields.empty());
    EXPECT_TRUE(config.nuopc.import_fields.empty());

    cece::CeceConfig empty_map = Parse(WithNuopc("nuopc: {}\n"));
    EXPECT_TRUE(empty_map.nuopc.export_fields.empty());
    EXPECT_TRUE(empty_map.nuopc.import_fields.empty());
}

TEST_F(CouplingConfigTest, ParsesExportAndImportWithDefaults) {
    cece::CeceConfig config = Parse(WithNuopc(R"YAML(nuopc:
  export_fields:
    oc:
      standard_name: "agent_count_emission_flux_of_particulate_organic_matter"
  import_fields:
    temperature:
      standard_name: "air_temperature"
      units: "K"
      name: "tmp"
)YAML"));

    ASSERT_EQ(config.nuopc.export_fields.size(), 1u);
    EXPECT_EQ(config.nuopc.export_fields[0].first, "oc");
    EXPECT_EQ(config.nuopc.export_fields[0].second.standard_name, "agent_count_emission_flux_of_particulate_organic_matter");
    EXPECT_TRUE(config.nuopc.export_fields[0].second.units.empty());  // optional, unset
    EXPECT_TRUE(config.nuopc.export_fields[0].second.name.empty());   // optional, unset

    ASSERT_EQ(config.nuopc.import_fields.size(), 1u);
    EXPECT_EQ(config.nuopc.import_fields[0].first, "temperature");
    EXPECT_EQ(config.nuopc.import_fields[0].second.standard_name, "air_temperature");
    EXPECT_EQ(config.nuopc.import_fields[0].second.units, "K");
    EXPECT_EQ(config.nuopc.import_fields[0].second.name, "tmp");
}

TEST_F(CouplingConfigTest, EntriesSortedAlphabeticallyNotByDocumentOrder) {
    cece::CeceConfig config = Parse(WithNuopc(R"YAML(nuopc:
  export_fields:
    nox:
      standard_name: "nox_standard"
    oc:
      standard_name: "oc_standard"
  import_fields:
    m1:
      standard_name: "mask_one"
    sf1:
      standard_name: "factor_one"
)YAML"));

    ASSERT_EQ(config.nuopc.export_fields.size(), 2u);
    EXPECT_EQ(config.nuopc.export_fields[0].first, "nox");
    EXPECT_EQ(config.nuopc.export_fields[1].first, "oc");

    ASSERT_EQ(config.nuopc.import_fields.size(), 2u);
    EXPECT_EQ(config.nuopc.import_fields[0].first, "m1");
    EXPECT_EQ(config.nuopc.import_fields[1].first, "sf1");
}

TEST_F(CouplingConfigTest, UnknownSpeciesRejected) {
    // "dust" is not a configured emission species in the base config.
    EXPECT_THROW(Parse(WithNuopc("nuopc:\n  export_fields:\n    dust:\n      standard_name: \"x\"\n")), std::invalid_argument);
}

TEST_F(CouplingConfigTest, UnknownImportKeyRejected) {
    // A key that is not in meteorology/scale_factors/masks must fail.
    EXPECT_THROW(Parse(WithNuopc("nuopc:\n  import_fields:\n    humidity:\n      standard_name: \"x\"\n")), std::invalid_argument);
}

TEST_F(CouplingConfigTest, MissingStandardNameRejected) {
    EXPECT_THROW(Parse(WithNuopc("nuopc:\n  export_fields:\n    oc:\n      units: \"kg m-2 s-1\"\n")), std::invalid_argument);
    EXPECT_THROW(Parse(WithNuopc("nuopc:\n  export_fields:\n    oc:\n      standard_name: \"\"\n")), std::invalid_argument);
}

TEST_F(CouplingConfigTest, SlashInNamesRejected) {
    EXPECT_THROW(Parse(WithNuopc("nuopc:\n  export_fields:\n    oc:\n      standard_name: \"bad/name\"\n")), std::invalid_argument);
    EXPECT_THROW(Parse(WithNuopc("nuopc:\n  export_fields:\n    oc:\n      standard_name: \"ok\"\n      name: \"bad/name\"\n")),
                 std::invalid_argument);
}

TEST_F(CouplingConfigTest, UnknownSubKeyRejected) {
    EXPECT_THROW(Parse(WithNuopc("nuopc:\n  export_fields:\n    oc:\n      standard_name: \"ok\"\n      rank: 3\n")), std::invalid_argument);
}

TEST_F(CouplingConfigTest, ErrorMessageNamesOffendingKey) {
    try {
        Parse(WithNuopc("nuopc:\n  export_fields:\n    dust:\n      standard_name: \"x\"\n"));
        FAIL() << "expected throw";
    } catch (const std::invalid_argument& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("dust"), std::string::npos) << msg;
        EXPECT_NE(msg.find("export_fields"), std::string::npos) << msg;
    }
}

// ---------------------------------------------------------------------------
// Path-based C queries used by the Fortran cap during Advertise/Realize.
// ---------------------------------------------------------------------------

TEST_F(CouplingConfigTest, QueryCountsMatchParsedSection) {
    WriteFile(path_, WithNuopc(R"YAML(nuopc:
  export_fields:
    nox:
      standard_name: "nox_standard"
    oc:
      standard_name: "oc_standard"
      units: "kg m-2 s-1"
  import_fields:
    temperature:
      standard_name: "air_temperature"
)YAML"));

    int count = -1, rc = -1;
    cece_nuopc_export_count(path_.c_str(), static_cast<int>(path_.size()), &count, &rc);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(count, 2);

    cece_nuopc_import_count(path_.c_str(), static_cast<int>(path_.size()), &count, &rc);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(count, 1);
}

TEST_F(CouplingConfigTest, QueryAbsentSectionReturnsZero) {
    WriteFile(path_, kBaseConfig);
    int count = -1, rc = -1;
    cece_nuopc_export_count(path_.c_str(), static_cast<int>(path_.size()), &count, &rc);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(count, 0);
}

TEST_F(CouplingConfigTest, QuerySpecReturnsAlphabeticalOrderAndDefaults) {
    WriteFile(path_, WithNuopc(R"YAML(nuopc:
  export_fields:
    nox:
      standard_name: "nox_standard"
      name: "nox_flux"
    oc:
      standard_name: "oc_standard"
      units: "kg m-2 s-1"
)YAML"));

    char key[256], std_name[256], units[256], name[256];
    int key_len = 0, std_len = 0, units_len = 0, name_len = 0, rc = -1;

    // Index 0 must be the alphabetically first key ("nox").
    cece_nuopc_export_spec(path_.c_str(), static_cast<int>(path_.size()), 0, key, sizeof(key), &key_len, std_name, sizeof(std_name), &std_len, units,
                           sizeof(units), &units_len, name, sizeof(name), &name_len, &rc);
    ASSERT_EQ(rc, 0);
    EXPECT_EQ(std::string(key, key_len), "nox");
    EXPECT_EQ(std::string(std_name, std_len), "nox_standard");
    EXPECT_EQ(units_len, 0);  // unset optional => caller omits the argument
    EXPECT_EQ(std::string(name, name_len), "nox_flux");

    cece_nuopc_export_spec(path_.c_str(), static_cast<int>(path_.size()), 1, key, sizeof(key), &key_len, std_name, sizeof(std_name), &std_len, units,
                           sizeof(units), &units_len, name, sizeof(name), &name_len, &rc);
    ASSERT_EQ(rc, 0);
    EXPECT_EQ(std::string(key, key_len), "oc");
    EXPECT_EQ(std::string(units, units_len), "kg m-2 s-1");
    EXPECT_EQ(name_len, 0);  // unset optional => caller uses the species key
}

TEST_F(CouplingConfigTest, QueryIndexOutOfRangeFails) {
    WriteFile(path_, WithNuopc("nuopc:\n  export_fields:\n    oc:\n      standard_name: \"oc_standard\"\n"));
    char key[256], std_name[256], units[256], name[256];
    int key_len = 0, std_len = 0, units_len = 0, name_len = 0, rc = -1;
    cece_nuopc_export_spec(path_.c_str(), static_cast<int>(path_.size()), 5, key, sizeof(key), &key_len, std_name, sizeof(std_name), &std_len, units,
                           sizeof(units), &units_len, name, sizeof(name), &name_len, &rc);
    EXPECT_NE(rc, 0);
}

TEST_F(CouplingConfigTest, QueryBufferTooSmallFailsWithoutTruncation) {
    WriteFile(path_, WithNuopc("nuopc:\n  export_fields:\n    oc:\n      standard_name: \"a_quite_long_standard_name_here\"\n"));
    char key[256], std_name[8], units[256], name[256];
    int key_len = 0, std_len = 0, units_len = 0, name_len = 0, rc = -1;
    cece_nuopc_export_spec(path_.c_str(), static_cast<int>(path_.size()), 0, key, sizeof(key), &key_len, std_name, sizeof(std_name), &std_len, units,
                           sizeof(units), &units_len, name, sizeof(name), &name_len, &rc);
    EXPECT_NE(rc, 0);
}

TEST_F(CouplingConfigTest, QueryInvalidConfigFails) {
    // Species referenced by nuopc: that does not exist => parse failure => rc != 0.
    WriteFile(path_, WithNuopc("nuopc:\n  export_fields:\n    dust:\n      standard_name: \"x\"\n"));
    int count = 0, rc = -1;
    cece_nuopc_export_count(path_.c_str(), static_cast<int>(path_.size()), &count, &rc);
    EXPECT_NE(rc, 0);
}

}  // namespace
