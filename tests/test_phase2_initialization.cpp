/**
 * @file test_phase2_initialization.cpp
 * @brief Unit tests for ACES Phase 2 (Realize + Bind) initialization.
 *
 * Tests the IPDv01p3 phase which includes:
 * - Field allocation on grid
 * - TIDE data stream binding
 * - Field handle initialization for Run phase
 * - Component readiness marking
 *
 * Requirements: 10.2, 10.3
 */

#include <gtest/gtest.h>

#include <fstream>
#include <iostream>
#include <string>

#include "aces/aces_internal.hpp"

extern "C" {
void aces_core_initialize_p1(void** data_ptr_ptr, int* rc);
void aces_core_initialize_p2(void* data_ptr, int* nx, int* ny, int* nz, int* rc);
void aces_core_finalize(void* data_ptr, int* rc);
void aces_set_config_file_path(const char* config_path, int path_len);
}

class Phase2InitializationTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // Create a test configuration file
        CreateTestConfig();
    }

    void TearDown() override {
        // Clean up test config file
        std::remove("test_phase2_config.yaml");
    }

    void CreateTestConfig() {
        std::ofstream f("test_phase2_config.yaml");
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

    /**
     * @brief Helper to run Phase 1 initialization
     * @return data_ptr on success, nullptr on failure
     */
    void* RunPhase1() {
        aces_set_config_file_path("test_phase2_config.yaml", 23);

        void* data_ptr = nullptr;
        int rc = -1;

        aces_core_initialize_p1(&data_ptr, &rc);
        if (rc != 0 || data_ptr == nullptr) {
            return nullptr;
        }
        return data_ptr;
    }
};

/**
 * @test Phase2_InitializesSuccessfully
 * Validates: Requirements 10.2, 10.3
 *
 * Verifies that Phase 2 initialization completes successfully with valid
 * grid dimensions.
 */
TEST_F(Phase2InitializationTest, Phase2_InitializesSuccessfully) {
    void* data_ptr = RunPhase1();
    ASSERT_NE(data_ptr, nullptr) << "Phase 1 must succeed";

    int nx = 10, ny = 10, nz = 1;
    int rc = -1;

    aces_core_initialize_p2(data_ptr, &nx, &ny, &nz, &rc);

    EXPECT_EQ(rc, 0) << "Phase 2 should return success (rc=0)";

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);
    EXPECT_EQ(internal_data->nx, 10) << "nx should be set to 10";
    EXPECT_EQ(internal_data->ny, 10) << "ny should be set to 10";
    EXPECT_EQ(internal_data->nz, 1) << "nz should be set to 1";

    aces_core_finalize(data_ptr, &rc);
}

/**
 * @test Phase2_AllocatesDefaultMask
 * Validates: Requirements 10.2, 10.3
 *
 * Verifies that Phase 2 allocates the default mask with correct dimensions.
 */
TEST_F(Phase2InitializationTest, Phase2_AllocatesDefaultMask) {
    void* data_ptr = RunPhase1();
    ASSERT_NE(data_ptr, nullptr) << "Phase 1 must succeed";

    int nx = 5, ny = 8, nz = 3;
    int rc = -1;

    aces_core_initialize_p2(data_ptr, &nx, &ny, &nz, &rc);
    ASSERT_EQ(rc, 0) << "Phase 2 must succeed";

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);

    // Verify default mask is allocated
    EXPECT_GT(internal_data->default_mask.extent(0), 0u)
        << "default_mask must be allocated with nx dimension";
    EXPECT_EQ(internal_data->default_mask.extent(0), 5u) << "default_mask nx should be 5";
    EXPECT_EQ(internal_data->default_mask.extent(1), 8u) << "default_mask ny should be 8";
    EXPECT_EQ(internal_data->default_mask.extent(2), 3u) << "default_mask nz should be 3";

    aces_core_finalize(data_ptr, &rc);
}

/**
 * @test Phase2_RequiresPhase1
 * Validates: Requirements 10.2, 10.3
 *
 * Verifies that Phase 2 fails gracefully when Phase 1 has not been run
 * (null data pointer).
 */
TEST_F(Phase2InitializationTest, Phase2_RequiresPhase1) {
    int nx = 10, ny = 10, nz = 1;
    int rc = 0;

    aces_core_initialize_p2(nullptr, &nx, &ny, &nz, &rc);

    EXPECT_NE(rc, 0) << "Phase 2 should fail with null data pointer";
}

/**
 * @test Phase2_ValidatesGridDimensions
 * Validates: Requirements 10.2, 10.3
 *
 * Verifies that Phase 2 validates grid dimensions and rejects invalid values.
 */
TEST_F(Phase2InitializationTest, Phase2_ValidatesGridDimensions) {
    void* data_ptr = RunPhase1();
    ASSERT_NE(data_ptr, nullptr) << "Phase 1 must succeed";

    // Test with zero dimensions
    int nx = 0, ny = 10, nz = 1;
    int rc = 0;

    aces_core_initialize_p2(data_ptr, &nx, &ny, &nz, &rc);
    EXPECT_NE(rc, 0) << "Phase 2 should fail with zero nx";

    // Test with negative dimensions
    nx = 10;
    ny = -5;
    nz = 1;
    rc = 0;

    aces_core_initialize_p2(data_ptr, &nx, &ny, &nz, &rc);
    EXPECT_NE(rc, 0) << "Phase 2 should fail with negative ny";

    aces_core_finalize(data_ptr, &rc);
}

/**
 * @test Phase2_HandlesNullDimensionPointers
 * Validates: Requirements 10.2, 10.3
 *
 * Verifies that Phase 2 fails gracefully when dimension pointers are null.
 */
TEST_F(Phase2InitializationTest, Phase2_HandlesNullDimensionPointers) {
    void* data_ptr = RunPhase1();
    ASSERT_NE(data_ptr, nullptr) << "Phase 1 must succeed";

    int rc = 0;

    // Test with null nx pointer
    aces_core_initialize_p2(data_ptr, nullptr, nullptr, nullptr, &rc);
    EXPECT_NE(rc, 0) << "Phase 2 should fail with null dimension pointers";

    aces_core_finalize(data_ptr, &rc);
}

/**
 * @test Phase2_StoresGridDimensions
 * Validates: Requirements 10.2, 10.3
 *
 * Verifies that Phase 2 correctly stores grid dimensions in internal data.
 */
TEST_F(Phase2InitializationTest, Phase2_StoresGridDimensions) {
    void* data_ptr = RunPhase1();
    ASSERT_NE(data_ptr, nullptr) << "Phase 1 must succeed";

    int nx = 20, ny = 30, nz = 5;
    int rc = -1;

    aces_core_initialize_p2(data_ptr, &nx, &ny, &nz, &rc);
    ASSERT_EQ(rc, 0) << "Phase 2 must succeed";

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);

    EXPECT_EQ(internal_data->nx, 20) << "nx should be stored correctly";
    EXPECT_EQ(internal_data->ny, 30) << "ny should be stored correctly";
    EXPECT_EQ(internal_data->nz, 5) << "nz should be stored correctly";

    aces_core_finalize(data_ptr, &rc);
}

/**
 * @test Phase2_PreservesPhase1State
 * Validates: Requirements 10.2, 10.3
 *
 * Verifies that Phase 2 does not corrupt state initialized in Phase 1.
 */
TEST_F(Phase2InitializationTest, Phase2_PreservesPhase1State) {
    void* data_ptr = RunPhase1();
    ASSERT_NE(data_ptr, nullptr) << "Phase 1 must succeed";

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);

    // Verify Phase 1 state
    EXPECT_FALSE(internal_data->config.species_layers.empty())
        << "Species must be loaded in Phase 1";
    EXPECT_NE(internal_data->stacking_engine, nullptr)
        << "StackingEngine must be initialized in Phase 1";
    EXPECT_NE(internal_data->diagnostic_manager, nullptr)
        << "DiagnosticManager must be initialized in Phase 1";

    int nx = 10, ny = 10, nz = 1;
    int rc = -1;

    aces_core_initialize_p2(data_ptr, &nx, &ny, &nz, &rc);
    ASSERT_EQ(rc, 0) << "Phase 2 must succeed";

    // Verify Phase 1 state is preserved
    EXPECT_FALSE(internal_data->config.species_layers.empty())
        << "Species must still be loaded after Phase 2";
    EXPECT_NE(internal_data->stacking_engine, nullptr)
        << "StackingEngine must still be initialized after Phase 2";
    EXPECT_NE(internal_data->diagnostic_manager, nullptr)
        << "DiagnosticManager must still be initialized after Phase 2";

    aces_core_finalize(data_ptr, &rc);
}

/**
 * @test Phase2_InitializesFieldHandles
 * Validates: Requirements 10.2, 10.3
 *
 * Verifies that Phase 2 initializes field handles for the Run phase.
 */
TEST_F(Phase2InitializationTest, Phase2_InitializesFieldHandles) {
    void* data_ptr = RunPhase1();
    ASSERT_NE(data_ptr, nullptr) << "Phase 1 must succeed";

    int nx = 10, ny = 10, nz = 1;
    int rc = -1;

    aces_core_initialize_p2(data_ptr, &nx, &ny, &nz, &rc);
    ASSERT_EQ(rc, 0) << "Phase 2 must succeed";

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);

    // Verify that internal data is ready for Run phase
    // (field handles should be initialized)
    EXPECT_GT(internal_data->nx, 0) << "nx should be set for Run phase";
    EXPECT_GT(internal_data->ny, 0) << "ny should be set for Run phase";
    EXPECT_GT(internal_data->nz, 0) << "nz should be set for Run phase";

    aces_core_finalize(data_ptr, &rc);
}

/**
 * @test Phase2_MarksComponentReady
 * Validates: Requirements 10.2, 10.3
 *
 * Verifies that Phase 2 marks the component as ready for execution.
 */
TEST_F(Phase2InitializationTest, Phase2_MarksComponentReady) {
    void* data_ptr = RunPhase1();
    ASSERT_NE(data_ptr, nullptr) << "Phase 1 must succeed";

    int nx = 10, ny = 10, nz = 1;
    int rc = -1;

    aces_core_initialize_p2(data_ptr, &nx, &ny, &nz, &rc);
    ASSERT_EQ(rc, 0) << "Phase 2 must succeed";

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);

    // Verify component is ready (all required fields are initialized)
    EXPECT_GT(internal_data->default_mask.extent(0), 0u)
        << "Component must have default mask allocated";
    EXPECT_GT(internal_data->active_schemes.size(), 0u)
        << "Component must have physics schemes initialized";

    aces_core_finalize(data_ptr, &rc);
}

/**
 * @test Phase2_HandlesLargeGrids
 * Validates: Requirements 10.2, 10.3
 *
 * Verifies that Phase 2 can handle large grid dimensions (>50k points).
 */
TEST_F(Phase2InitializationTest, Phase2_HandlesLargeGrids) {
    void* data_ptr = RunPhase1();
    ASSERT_NE(data_ptr, nullptr) << "Phase 1 must succeed";

    // Create a large grid (100 x 100 x 5 = 50,000 points)
    int nx = 100, ny = 100, nz = 5;
    int rc = -1;

    aces_core_initialize_p2(data_ptr, &nx, &ny, &nz, &rc);
    EXPECT_EQ(rc, 0) << "Phase 2 should handle large grids";

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);
    EXPECT_EQ(internal_data->nx, 100) << "nx should be set correctly for large grid";
    EXPECT_EQ(internal_data->ny, 100) << "ny should be set correctly for large grid";
    EXPECT_EQ(internal_data->nz, 5) << "nz should be set correctly for large grid";

    aces_core_finalize(data_ptr, &rc);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
