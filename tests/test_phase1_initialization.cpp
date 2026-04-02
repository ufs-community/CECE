/**
 * @file test_phase1_initialization.cpp
 * @brief Unit tests for ACES Phase 1 (Advertise + Init) initialization.
 *
 * Tests the IPDv01p1 phase which includes:
 * - Kokkos initialization
 * - YAML configuration parsing
 * - Physics scheme registration
 * - StackingEngine initialization
 * - Field advertisement
 *
 * Requirements: 10.1, 10.3
 */

#include <gtest/gtest.h>

#include <fstream>
#include <iostream>
#include <string>

#include "aces/aces_internal.hpp"

extern "C" {
void aces_core_initialize_p1(void** data_ptr_ptr, int* rc);
void aces_core_advertise(int* rc);
void aces_core_finalize(void* data_ptr, int* rc);
void aces_set_config_file_path(const char* config_path, int path_len);
void aces_core_get_species_count(void* data_ptr, int* count, int* rc);
void aces_core_get_species_name(void* data_ptr, int* index, char* name, int* name_len, int* rc);
}

class Phase1InitializationTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // Create a test configuration file
        CreateTestConfig();
    }

    void TearDown() override {
        // Clean up test config file
        std::remove("test_phase1_config.yaml");
    }

    void CreateTestConfig() {
        std::ofstream f("test_phase1_config.yaml");
        f << R"(
species:
  CO:
    - operation: add
      field: CO_anthro
      hierarchy: 0
      scale: 1.0
  NOx:
    - operation: add
      field: NOx_anthro
      hierarchy: 0
      scale: 1.0
  SO2:
    - operation: add
      field: SO2_anthro
      hierarchy: 0
      scale: 1.0

meteorology:
  temperature: air_temperature

physics_schemes:
  - name: NativeExample
    language: cpp
    options:
      example_param: 1.0

diagnostics:
  output_interval_seconds: 3600
  variables:
    - CO
    - NOx

aces_data:
  streams: []
)";
        f.close();
    }
};

/**
 * @test Phase1_InitializesSuccessfully
 * Validates: Requirements 10.1, 10.3
 *
 * Verifies that Phase 1 initialization completes successfully and returns
 * a valid internal data pointer.
 */
TEST_F(Phase1InitializationTest, Phase1_InitializesSuccessfully) {
    aces_set_config_file_path("test_phase1_config.yaml", 23);

    void* data_ptr = nullptr;
    int rc = -1;

    aces_core_initialize_p1(&data_ptr, &rc);

    EXPECT_EQ(rc, 0) << "Phase 1 should return success (rc=0)";
    ASSERT_NE(data_ptr, nullptr) << "Phase 1 should allocate internal data structure";

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);
    EXPECT_FALSE(internal_data->config.species_layers.empty())
        << "Config must be loaded with at least one species";

    aces_core_finalize(data_ptr, &rc);
}

/**
 * @test Phase1_LoadsKokkosSuccessfully
 * Validates: Requirements 10.1, 10.3
 *
 * Verifies that Kokkos is initialized during Phase 1.
 */
TEST_F(Phase1InitializationTest, Phase1_LoadsKokkosSuccessfully) {
    aces_set_config_file_path("test_phase1_config.yaml", 23);

    void* data_ptr = nullptr;
    int rc = -1;

    aces_core_initialize_p1(&data_ptr, &rc);

    ASSERT_EQ(rc, 0);
    ASSERT_NE(data_ptr, nullptr);

    // Verify Kokkos is initialized
    EXPECT_TRUE(Kokkos::is_initialized()) << "Kokkos should be initialized after Phase 1";

    aces_core_finalize(data_ptr, &rc);
}

/**
 * @test Phase1_ParsesConfigurationSuccessfully
 * Validates: Requirements 10.1, 10.3
 *
 * Verifies that YAML configuration is parsed correctly during Phase 1.
 */
TEST_F(Phase1InitializationTest, Phase1_ParsesConfigurationSuccessfully) {
    aces_set_config_file_path("test_phase1_config.yaml", 23);

    void* data_ptr = nullptr;
    int rc = -1;

    aces_core_initialize_p1(&data_ptr, &rc);

    ASSERT_EQ(rc, 0);
    ASSERT_NE(data_ptr, nullptr);

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);

    // Verify species are loaded
    EXPECT_EQ(internal_data->config.species_layers.size(), 3u)
        << "Should have 3 species (CO, NOx, SO2)";
    EXPECT_TRUE(internal_data->config.species_layers.count("CO") > 0);
    EXPECT_TRUE(internal_data->config.species_layers.count("NOx") > 0);
    EXPECT_TRUE(internal_data->config.species_layers.count("SO2") > 0);

    aces_core_finalize(data_ptr, &rc);
}

/**
 * @test Phase1_RegistersPhysicsSchemesSuccessfully
 * Validates: Requirements 10.1, 10.3
 *
 * Verifies that physics schemes are registered during Phase 1.
 */
TEST_F(Phase1InitializationTest, Phase1_RegistersPhysicsSchemesSuccessfully) {
    aces_set_config_file_path("test_phase1_config.yaml", 23);

    void* data_ptr = nullptr;
    int rc = -1;

    aces_core_initialize_p1(&data_ptr, &rc);

    ASSERT_EQ(rc, 0);
    ASSERT_NE(data_ptr, nullptr);

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);

    // Verify physics schemes are registered
    EXPECT_GT(internal_data->active_schemes.size(), 0u)
        << "Should have at least 1 physics scheme (NativeExample)";

    aces_core_finalize(data_ptr, &rc);
}

/**
 * @test Phase1_InitializesStackingEngineSuccessfully
 * Validates: Requirements 10.1, 10.3
 *
 * Verifies that StackingEngine is initialized during Phase 1.
 */
TEST_F(Phase1InitializationTest, Phase1_InitializesStackingEngineSuccessfully) {
    aces_set_config_file_path("test_phase1_config.yaml", 23);

    void* data_ptr = nullptr;
    int rc = -1;

    aces_core_initialize_p1(&data_ptr, &rc);

    ASSERT_EQ(rc, 0);
    ASSERT_NE(data_ptr, nullptr);

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);

    // Verify StackingEngine is initialized
    EXPECT_NE(internal_data->stacking_engine, nullptr)
        << "StackingEngine must be initialized during Phase 1";

    aces_core_finalize(data_ptr, &rc);
}

/**
 * @test Phase1_InitializesDiagnosticManagerSuccessfully
 * Validates: Requirements 10.1, 10.3
 *
 * Verifies that DiagnosticManager is initialized during Phase 1.
 */
TEST_F(Phase1InitializationTest, Phase1_InitializesDiagnosticManagerSuccessfully) {
    aces_set_config_file_path("test_phase1_config.yaml", 23);

    void* data_ptr = nullptr;
    int rc = -1;

    aces_core_initialize_p1(&data_ptr, &rc);

    ASSERT_EQ(rc, 0);
    ASSERT_NE(data_ptr, nullptr);

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);

    // Verify DiagnosticManager is initialized
    EXPECT_NE(internal_data->diagnostic_manager, nullptr)
        << "DiagnosticManager must be initialized during Phase 1";

    aces_core_finalize(data_ptr, &rc);
}

/**
 * @test Phase1_AdvertisesFieldsSuccessfully
 * Validates: Requirements 10.1, 10.3
 *
 * Verifies that fields are advertised during Phase 1.
 */
TEST_F(Phase1InitializationTest, Phase1_AdvertisesFieldsSuccessfully) {
    aces_set_config_file_path("test_phase1_config.yaml", 23);

    void* data_ptr = nullptr;
    int rc = -1;

    aces_core_initialize_p1(&data_ptr, &rc);

    ASSERT_EQ(rc, 0);
    ASSERT_NE(data_ptr, nullptr);

    // Call advertise phase
    aces_core_advertise(&rc);
    EXPECT_EQ(rc, 0) << "Advertise phase should succeed";

    aces_core_finalize(data_ptr, &rc);
}

/**
 * @test Phase1_RetrievesSpeciesCountSuccessfully
 * Validates: Requirements 10.1, 10.3
 *
 * Verifies that species count can be retrieved after Phase 1.
 */
TEST_F(Phase1InitializationTest, Phase1_RetrievesSpeciesCountSuccessfully) {
    aces_set_config_file_path("test_phase1_config.yaml", 23);

    void* data_ptr = nullptr;
    int rc = -1;

    aces_core_initialize_p1(&data_ptr, &rc);

    ASSERT_EQ(rc, 0);
    ASSERT_NE(data_ptr, nullptr);

    // Get species count
    int species_count = 0;
    aces_core_get_species_count(data_ptr, &species_count, &rc);

    EXPECT_EQ(rc, 0) << "Getting species count should succeed";
    EXPECT_EQ(species_count, 3) << "Should have 3 species (CO, NOx, SO2)";

    aces_core_finalize(data_ptr, &rc);
}

/**
 * @test Phase1_RetrievesSpeciesNamesSuccessfully
 * Validates: Requirements 10.1, 10.3
 *
 * Verifies that species names can be retrieved after Phase 1.
 */
TEST_F(Phase1InitializationTest, Phase1_RetrievesSpeciesNamesSuccessfully) {
    aces_set_config_file_path("test_phase1_config.yaml", 23);

    void* data_ptr = nullptr;
    int rc = -1;

    aces_core_initialize_p1(&data_ptr, &rc);

    ASSERT_EQ(rc, 0);
    ASSERT_NE(data_ptr, nullptr);

    // Get species count
    int species_count = 0;
    aces_core_get_species_count(data_ptr, &species_count, &rc);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(species_count, 3);

    // Get species names
    char name_buffer[256];
    int name_len = 0;

    for (int i = 0; i < species_count; i++) {
        int index = i;
        aces_core_get_species_name(data_ptr, &index, name_buffer, &name_len, &rc);
        EXPECT_EQ(rc, 0) << "Getting species name should succeed for species " << i;
        EXPECT_GT(name_len, 0) << "Species name length should be positive for species " << i;
        std::string species_name(name_buffer, name_len);
        EXPECT_FALSE(species_name.empty()) << "Species name should not be empty for species " << i;
    }

    aces_core_finalize(data_ptr, &rc);
}

/**
 * @test Phase1_HandlesMissingConfigFile
 * Validates: Requirements 10.1, 10.3
 *
 * Verifies that Phase 1 fails gracefully when config file is missing.
 */
TEST_F(Phase1InitializationTest, Phase1_HandlesMissingConfigFile) {
    aces_set_config_file_path("nonexistent_config.yaml", 24);

    void* data_ptr = nullptr;
    int rc = 0;

    aces_core_initialize_p1(&data_ptr, &rc);

    EXPECT_NE(rc, 0) << "Phase 1 should fail with missing config file";
    EXPECT_EQ(data_ptr, nullptr) << "data_ptr should be null on failure";
}

/**
 * @test Phase1_TracksKokkosOwnership
 * Validates: Requirements 10.1, 10.3
 *
 * Verifies that Kokkos ownership is tracked correctly.
 */
TEST_F(Phase1InitializationTest, Phase1_TracksKokkosOwnership) {
    aces_set_config_file_path("test_phase1_config.yaml", 23);

    // Ensure Kokkos is already initialized
    if (!Kokkos::is_initialized()) {
        Kokkos::initialize();
    }

    void* data_ptr = nullptr;
    int rc = -1;

    aces_core_initialize_p1(&data_ptr, &rc);

    ASSERT_EQ(rc, 0);
    ASSERT_NE(data_ptr, nullptr);

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);

    // Kokkos was already initialized, so ACES must NOT claim ownership
    EXPECT_FALSE(internal_data->kokkos_initialized_here)
        << "ACES must not claim Kokkos ownership when it was pre-initialized";

    aces_core_finalize(data_ptr, &rc);
    EXPECT_TRUE(Kokkos::is_initialized())
        << "Kokkos must remain initialized when not owned by ACES";
}

/**
 * @test Phase1_ReturnsValidInternalDataStructure
 * Validates: Requirements 10.1, 10.3
 *
 * Verifies that the returned internal data structure is valid and complete.
 */
TEST_F(Phase1InitializationTest, Phase1_ReturnsValidInternalDataStructure) {
    aces_set_config_file_path("test_phase1_config.yaml", 23);

    void* data_ptr = nullptr;
    int rc = -1;

    aces_core_initialize_p1(&data_ptr, &rc);

    ASSERT_EQ(rc, 0);
    ASSERT_NE(data_ptr, nullptr);

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);

    // Verify all required fields are initialized
    EXPECT_FALSE(internal_data->config.species_layers.empty());
    EXPECT_NE(internal_data->stacking_engine, nullptr);
    EXPECT_NE(internal_data->diagnostic_manager, nullptr);
    EXPECT_GE(internal_data->active_schemes.size(), 0u);

    aces_core_finalize(data_ptr, &rc);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
