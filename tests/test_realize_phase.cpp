/**
 * @file test_realize_phase.cpp
 * @brief Unit tests for ACES Realize Phase implementation.
 *
 * Tests verify that the realize phase correctly:
 * - Creates export fields for all species in the configuration
 * - Allocates memory for export fields on the provided grid
 * - Validates import/export state pointers
 * - Handles configuration parsing errors gracefully
 *
 * All tests use real ESMF objects (no mocking) in the JCSDA Docker environment.
 */

#include <ESMC.h>
#include <gtest/gtest.h>

#include <fstream>
#include <string>

// C interface to realize phase
extern "C" {
void aces_core_realize(void* data_ptr, void* importState_ptr, void* exportState_ptr, void* grid_ptr,
                       int* rc);
void aces_core_initialize_p1(void** data_ptr_ptr, int* rc);
}

// Global ESMF initialization - done once for all tests
class ESMFEnvironment : public ::testing::Environment {
   public:
    void SetUp() override {
        int rc = ESMC_Initialize(nullptr, ESMC_ArgLast);
        if (rc != ESMF_SUCCESS) {
            std::cerr << "Failed to initialize ESMF" << std::endl;
            exit(1);
        }
    }

    void TearDown() override {
        ESMC_Finalize();
    }
};

class RealizePhaseTest : public ::testing::Test {
   protected:
    void SetUp() override {
        int rc;

        // Create a simple 2D grid for testing (10x10)
        int maxIndex[2] = {10, 10};
        ESMC_InterArrayInt iMaxIndex;
        ESMC_InterArrayIntSet(&iMaxIndex, maxIndex, 2);
        grid_ = ESMC_GridCreateNoPeriDim(&iMaxIndex, nullptr, nullptr, nullptr, &rc);
        ASSERT_EQ(rc, ESMF_SUCCESS) << "Failed to create ESMF grid";

        // Create import and export states
        importState_ = ESMC_StateCreate("ImportState", &rc);
        ASSERT_EQ(rc, ESMF_SUCCESS) << "Failed to create import state";

        exportState_ = ESMC_StateCreate("ExportState", &rc);
        ASSERT_EQ(rc, ESMF_SUCCESS) << "Failed to create export state";
    }

    void TearDown() override {
        // Destroy states
        if (exportState_.ptr != nullptr) {
            ESMC_StateDestroy(&exportState_);
        }
        if (importState_.ptr != nullptr) {
            ESMC_StateDestroy(&importState_);
        }

        // Destroy grid
        if (grid_.ptr != nullptr) {
            ESMC_GridDestroy(&grid_);
        }
    }

    // Helper to create a minimal test configuration
    void CreateTestConfig(const std::string& filename, int num_species) {
        std::ofstream config(filename);
        config << "species:\n";
        for (int i = 0; i < num_species; ++i) {
            config << "  species_" << i << ":\n";
            config << "    - field: \"test_field_" << i << "\"\n";
            config << "      operation: \"add\"\n";
            config << "      scale: 1.0\n";
            config << "      hierarchy: 1\n";
        }
        config << "\nmeteorology:\n";
        config << "  temperature: \"air_temperature\"\n";
        config << "\nscale_factors: {}\n";
        config << "masks: {}\n";
        config << "physics_schemes: []\n";
        config.close();
    }

    ESMC_Grid grid_;
    ESMC_State importState_;
    ESMC_State exportState_;
    void* data_ptr_;
};

/**
 * Test: Realize phase creates export fields for all species
 * Validates: Requirements 4.5, 4.6
 */
TEST_F(RealizePhaseTest, CreatesExportFieldsForAllSpecies) {
    // Create test configuration with 3 species
    CreateTestConfig("aces_config.yaml", 3);

    // Initialize core to create internal data structure
    data_ptr_ = nullptr;
    int init_rc = -1;
    aces_core_initialize_p1(&data_ptr_, &init_rc);
    ASSERT_EQ(init_rc, ESMF_SUCCESS) << "Phase 1 initialization should succeed";
    ASSERT_NE(data_ptr_, nullptr) << "Phase 1 should create internal data structure";

    int rc = -1;
    aces_core_realize(data_ptr_, importState_.ptr, exportState_.ptr, grid_.ptr, &rc);

    // Verify success
    EXPECT_EQ(rc, 0) << "Realize phase should succeed";

    // Verify each field exists and can be retrieved
    // Note: ACES may create fields in any order, so check all expected species
    std::vector<std::string> expected_species = {"species_0", "species_1", "species_2"};
    int found_count = 0;

    for (const auto& species_name : expected_species) {
        ESMC_Field field;
        int local_rc = ESMC_StateGetField(exportState_, species_name.c_str(), &field);
        if (local_rc == ESMF_SUCCESS && field.ptr != nullptr) {
            found_count++;
        }
    }

    EXPECT_EQ(found_count, 3) << "Should find all 3 species fields in export state";
}

/**
 * Test: Realize phase handles null export state pointer
 * Validates: Requirements 4.5
 */
TEST_F(RealizePhaseTest, HandlesNullExportState) {
    CreateTestConfig("aces_config.yaml", 1);

    // Initialize core to create internal data structure
    data_ptr_ = nullptr;
    int init_rc = -1;
    aces_core_initialize_p1(&data_ptr_, &init_rc);
    ASSERT_EQ(init_rc, ESMF_SUCCESS) << "Phase 1 initialization should succeed";

    int rc = 0;
    aces_core_realize(data_ptr_, importState_.ptr, nullptr, grid_.ptr, &rc);

    // Should fail gracefully
    EXPECT_NE(rc, 0) << "Should fail with null export state";
}

/**
 * Test: Realize phase handles null grid pointer
 * Validates: Requirements 4.5
 */
TEST_F(RealizePhaseTest, HandlesNullGrid) {
    CreateTestConfig("aces_config.yaml", 1);

    // Initialize core to create internal data structure
    data_ptr_ = nullptr;
    int init_rc = -1;
    aces_core_initialize_p1(&data_ptr_, &init_rc);
    ASSERT_EQ(init_rc, ESMF_SUCCESS) << "Phase 1 initialization should succeed";

    int rc = 0;
    aces_core_realize(data_ptr_, importState_.ptr, exportState_.ptr, nullptr, &rc);

    // Should fail gracefully
    EXPECT_NE(rc, 0) << "Should fail with null grid";
}

/**
 * Test: Realize phase handles missing configuration file
 * Validates: Requirements 4.5
 */
TEST_F(RealizePhaseTest, HandlesMissingConfigFile) {
    // Remove config file if it exists
    std::remove("aces_config.yaml");

    // Try to initialize - this may fail if no config exists, which is expected
    data_ptr_ = nullptr;
    int init_rc = -1;
    aces_core_initialize_p1(&data_ptr_, &init_rc);
    // Note: Phase 1 may fail with missing config, and that's expected

    int rc = 0;
    aces_core_realize(data_ptr_, importState_.ptr, exportState_.ptr, grid_.ptr, &rc);

    // Should fail gracefully
    EXPECT_NE(rc, 0) << "Should fail with missing config file";
}

/**
 * Test: Realize phase handles invalid configuration
 * Validates: Requirements 4.5
 */
TEST_F(RealizePhaseTest, HandlesInvalidConfig) {
    // Create invalid config
    std::ofstream config("aces_config.yaml");
    config << "invalid: yaml: syntax:\n";
    config << "  - this is not valid\n";
    config.close();

    // Try to initialize - this may fail with invalid config, which is expected
    data_ptr_ = nullptr;
    int init_rc = -1;
    aces_core_initialize_p1(&data_ptr_, &init_rc);
    // Phase 1 may fail with invalid config, which is expected

    int rc = 0;
    aces_core_realize(data_ptr_, importState_.ptr, exportState_.ptr, grid_.ptr, &rc);

    // Should fail gracefully
    EXPECT_NE(rc, 0) << "Should fail with invalid config";
}

/**
 * Test: Realize phase works with empty species list
 * Validates: Requirements 4.5
 */
TEST_F(RealizePhaseTest, HandlesEmptySpeciesList) {
    CreateTestConfig("aces_config.yaml", 0);

    // Initialize core to create internal data structure
    data_ptr_ = nullptr;
    int init_rc = -1;
    aces_core_initialize_p1(&data_ptr_, &init_rc);
    ASSERT_EQ(init_rc, ESMF_SUCCESS) << "Phase 1 initialization should succeed";

    int rc = -1;
    aces_core_realize(data_ptr_, importState_.ptr, exportState_.ptr, grid_.ptr, &rc);

    // Should succeed with no fields created
    EXPECT_EQ(rc, 0) << "Should succeed with empty species list";
}

/**
 * Test: Realize phase accepts null import state (standalone mode)
 * Validates: Requirements 4.6
 */
TEST_F(RealizePhaseTest, AcceptsNullImportState) {
    CreateTestConfig("aces_config.yaml", 2);

    // Initialize core to create internal data structure
    data_ptr_ = nullptr;
    int init_rc = -1;
    aces_core_initialize_p1(&data_ptr_, &init_rc);
    ASSERT_EQ(init_rc, ESMF_SUCCESS) << "Phase 1 initialization should succeed";

    int rc = -1;
    aces_core_realize(data_ptr_, nullptr, exportState_.ptr, grid_.ptr, &rc);

    // Should succeed - import state is optional for standalone mode
    EXPECT_EQ(rc, 0) << "Should succeed with null import state";

    // Verify export fields were still created
    std::vector<std::string> expected_species = {"species_0", "species_1"};
    int found_count = 0;

    for (const auto& species_name : expected_species) {
        ESMC_Field field;
        int local_rc = ESMC_StateGetField(exportState_, species_name.c_str(), &field);
        if (local_rc == ESMF_SUCCESS && field.ptr != nullptr) {
            found_count++;
        }
    }

    EXPECT_EQ(found_count, 2) << "Should find both species fields in export state";
}

/**
 * Test: Realize phase with realistic configuration
 * Validates: Requirements 4.5, 4.6
 */
TEST_F(RealizePhaseTest, WorksWithRealisticConfig) {
    // Create a more realistic configuration
    std::ofstream config("aces_config.yaml");
    config << "species:\n";
    config << "  nox:\n";
    config << "    - field: \"anthro_nox\"\n";
    config << "      operation: \"add\"\n";
    config << "      scale: 1.0\n";
    config << "      hierarchy: 1\n";
    config << "  co:\n";
    config << "    - field: \"anthro_co\"\n";
    config << "      operation: \"add\"\n";
    config << "      scale: 1.0\n";
    config << "      hierarchy: 1\n";
    config << "  so2:\n";
    config << "    - field: \"anthro_so2\"\n";
    config << "      operation: \"add\"\n";
    config << "      scale: 1.0\n";
    config << "      hierarchy: 1\n";
    config << "\nmeteorology:\n";
    config << "  temperature: \"air_temperature\"\n";
    config << "  pressure: \"air_pressure\"\n";
    config << "  wind_speed: \"wind_speed\"\n";
    config << "\nscale_factors:\n";
    config << "  land_fraction: \"land_fraction\"\n";
    config << "\nmasks:\n";
    config << "  land_mask: \"land_mask\"\n";
    config << "\nphysics_schemes: []\n";
    config.close();

    // Initialize core to create internal data structure
    data_ptr_ = nullptr;
    int init_rc = -1;
    aces_core_initialize_p1(&data_ptr_, &init_rc);
    ASSERT_EQ(init_rc, ESMF_SUCCESS) << "Phase 1 initialization should succeed";

    int rc = -1;
    aces_core_realize(data_ptr_, importState_.ptr, exportState_.ptr, grid_.ptr, &rc);

    EXPECT_EQ(rc, 0) << "Should succeed with realistic config";

    // Verify each species field exists
    std::vector<std::string> expected_species = {"nox", "co", "so2"};
    int found_count = 0;

    for (const auto& species_name : expected_species) {
        ESMC_Field field;
        int local_rc = ESMC_StateGetField(exportState_, species_name.c_str(), &field);
        if (local_rc == ESMF_SUCCESS && field.ptr != nullptr) {
            found_count++;
        }
    }

    EXPECT_EQ(found_count, 3) << "Should find all 3 realistic species fields in export state";
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new ESMFEnvironment);
    return RUN_ALL_TESTS();
}
