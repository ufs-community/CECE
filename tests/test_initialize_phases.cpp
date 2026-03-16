/**
 * @file test_initialize_phases.cpp
 * @brief Unit tests for ACES multi-phase initialization (IPDv00p1 and IPDv00p2).
 *
 * Verifies the two-phase initialization pattern:
 * - Phase 1: Core initialization (Kokkos, config, PhysicsFactory, StackingEngine)
 * - Phase 2: Field binding and CDEPS setup
 *
 * Uses real ESMF objects (no mocking) in the JCSDA Docker environment.
 * All ESMF C API calls match ESMF 8.8.0 signatures.
 */

#include <ESMC.h>
#include <Kokkos_Core.hpp>
#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "aces/aces_internal.hpp"

extern "C" {
void aces_core_advertise(void* importState, void* exportState, int* rc);
void aces_core_realize(void* data_ptr, void* importState, void* exportState, void* grid, int* rc);
void aces_core_initialize_p1(void** data_ptr_ptr, int* rc);
void aces_core_initialize_p2(void* data_ptr, void* gcomp_ptr, void* importState_ptr,
                             void* exportState_ptr, void* clock_ptr, void* grid_ptr, int* rc);
void aces_core_finalize(void* data_ptr, int* rc);

void test_create_gridcomp(const char* name, void* clock_ptr, void** gcomp_ptr, int* rc);
void test_destroy_gridcomp(void* gcomp_ptr, int* rc);
}

// ---------------------------------------------------------------------------
// Global ESMF / Kokkos environment — initialize once, finalize once
// ---------------------------------------------------------------------------
class ESMFEnvironment : public ::testing::Environment {
   public:
    void SetUp() override {
        if (!Kokkos::is_initialized()) Kokkos::initialize();
        int rc = ESMC_Initialize(nullptr, ESMC_ArgLast);
        if (rc != ESMF_SUCCESS) { std::cerr << "ESMF init failed\n"; exit(1); }
    }
    void TearDown() override { ESMC_Finalize(); }
};

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------
class InitializePhasesTest : public ::testing::Test {
   protected:
    ESMC_Grid grid_{nullptr};
    ESMC_State import_state_{nullptr};
    ESMC_State export_state_{nullptr};
    ESMC_Clock clock_{nullptr};
    ESMC_GridComp gcomp_{nullptr};

    void SetUp() override {
        CreateTestConfig();

        int rc;
        ESMC_Calendar cal = ESMC_CalendarCreate("Gregorian", ESMC_CALKIND_GREGORIAN, &rc);
        if (rc != ESMF_SUCCESS) GTEST_SKIP() << "Calendar creation failed";

        ESMC_Time start_time, stop_time;
        rc = ESMC_TimeSet(&start_time, 2020, 0, cal, ESMC_CALKIND_GREGORIAN, 0);
        if (rc != ESMF_SUCCESS) GTEST_SKIP() << "TimeSet (start) failed";
        rc = ESMC_TimeSet(&stop_time, 2020, 24, cal, ESMC_CALKIND_GREGORIAN, 0);
        if (rc != ESMF_SUCCESS) GTEST_SKIP() << "TimeSet (stop) failed";

        ESMC_TimeInterval timestep;
        rc = ESMC_TimeIntervalSet(&timestep, 3600);
        if (rc != ESMF_SUCCESS) GTEST_SKIP() << "TimeIntervalSet failed";

        clock_ = ESMC_ClockCreate("ACES_Clock", timestep, start_time, stop_time, &rc);
        if (rc != ESMF_SUCCESS || !clock_.ptr) GTEST_SKIP() << "Clock creation failed";

        test_create_gridcomp("ACES", clock_.ptr, &gcomp_.ptr, &rc);
        if (rc != ESMF_SUCCESS || !gcomp_.ptr) GTEST_SKIP() << "GridComp creation failed";

        int maxIndex[3] = {10, 10, 5};
        ESMC_InterArrayInt iMax;
        ESMC_InterArrayIntSet(&iMax, maxIndex, 3);
        grid_ = ESMC_GridCreateNoPeriDim(&iMax, nullptr, nullptr, nullptr, &rc);
        if (rc != ESMF_SUCCESS) GTEST_SKIP() << "Grid creation failed";

        import_state_ = ESMC_StateCreate("ACES_Import", &rc);
        if (rc != ESMF_SUCCESS) GTEST_SKIP() << "ImportState creation failed";

        export_state_ = ESMC_StateCreate("ACES_Export", &rc);
        if (rc != ESMF_SUCCESS) GTEST_SKIP() << "ExportState creation failed";
    }

    void TearDown() override {
        int rc;
        if (export_state_.ptr) ESMC_StateDestroy(&export_state_);
        if (import_state_.ptr) ESMC_StateDestroy(&import_state_);
        if (grid_.ptr) ESMC_GridDestroy(&grid_);
        if (gcomp_.ptr) test_destroy_gridcomp(gcomp_.ptr, &rc);
        if (clock_.ptr) ESMC_ClockDestroy(&clock_);
        std::remove("aces_config.yaml");
    }

    void CreateTestConfig() {
        std::ofstream f("aces_config.yaml");
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

cdeps_inline_config:
  streams: []
)";
    }

    /// Run advertise + realize so export fields exist for p2 dimension extraction.
    bool RunPrerequisitePhases() {
        int rc;
        aces_core_advertise(import_state_.ptr, export_state_.ptr, &rc);
        if (rc != ESMF_SUCCESS) return false;
        // Note: data_ptr is null here since we haven't called initialize_p1 yet
        aces_core_realize(nullptr, import_state_.ptr, export_state_.ptr, grid_.ptr, &rc);
        return rc == ESMF_SUCCESS;
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

/**
 * @test Phase1_InitializesSuccessfully
 * Validates: Requirements 4.7-4.10
 */
TEST_F(InitializePhasesTest, Phase1_InitializesSuccessfully) {
    ASSERT_TRUE(RunPrerequisitePhases()) << "Advertise/Realize prerequisite failed";

    void* data_ptr = nullptr;
    int rc = -1;
    aces_core_initialize_p1(&data_ptr, &rc);

    EXPECT_EQ(rc, ESMF_SUCCESS) << "Phase 1 should return ESMF_SUCCESS";
    ASSERT_NE(data_ptr, nullptr) << "Phase 1 should allocate internal data structure";

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);
    EXPECT_FALSE(internal_data->config.species_layers.empty())
        << "Config must be loaded with at least one species";
    EXPECT_TRUE(internal_data->config.species_layers.count("CO") > 0);
    EXPECT_TRUE(internal_data->config.species_layers.count("NOx") > 0);
    EXPECT_GT(internal_data->active_schemes.size(), 0u) << "Should have at least 1 physics scheme";
    EXPECT_NE(internal_data->stacking_engine, nullptr) << "StackingEngine must be initialized";
    EXPECT_NE(internal_data->diagnostic_manager, nullptr) << "DiagnosticManager must be initialized";

    aces_core_finalize(data_ptr, &rc);
}

/**
 * @test Phase1_HandlesMissingConfig
 * Validates: Requirements 4.7, 10.1-10.2
 */
TEST_F(InitializePhasesTest, Phase1_HandlesMissingConfig) {
    std::remove("aces_config.yaml");

    void* data_ptr = nullptr;
    int rc = ESMF_SUCCESS;
    aces_core_initialize_p1(&data_ptr, &rc);

    EXPECT_NE(rc, ESMF_SUCCESS) << "Phase 1 should fail with missing config";
    EXPECT_EQ(data_ptr, nullptr) << "data_ptr must be null on failure";
}

/**
 * @test Phase1_TracksKokkosOwnership
 * Validates: Requirements 4.10, 4.17
 */
TEST_F(InitializePhasesTest, Phase1_TracksKokkosOwnership) {
    ASSERT_TRUE(RunPrerequisitePhases());
    ASSERT_TRUE(Kokkos::is_initialized()) << "Kokkos pre-initialized by ESMFEnvironment";

    void* data_ptr = nullptr;
    int rc = -1;
    aces_core_initialize_p1(&data_ptr, &rc);

    ASSERT_EQ(rc, ESMF_SUCCESS);
    ASSERT_NE(data_ptr, nullptr);

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);
    // Kokkos was already initialized, so ACES must NOT claim ownership
    EXPECT_FALSE(internal_data->kokkos_initialized_here)
        << "ACES must not claim Kokkos ownership when it was pre-initialized";

    aces_core_finalize(data_ptr, &rc);
    EXPECT_TRUE(Kokkos::is_initialized()) << "Kokkos must remain initialized when not owned by ACES";
}

/**
 * @test Phase2_RequiresPhase1
 * Validates: Requirements 4.18, 4.19
 */
TEST_F(InitializePhasesTest, Phase2_RequiresPhase1) {
    int rc = ESMF_SUCCESS;
    aces_core_initialize_p2(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &rc);
    EXPECT_NE(rc, ESMF_SUCCESS) << "Phase 2 must fail gracefully when Phase 1 data is null";
}

/**
 * @test Phase2_HandlesNullGrid
 * Validates: Requirements 4.7-4.10
 */
TEST_F(InitializePhasesTest, Phase2_HandlesNullGrid) {
    ASSERT_TRUE(RunPrerequisitePhases());

    void* data_ptr = nullptr;
    int rc = -1;
    aces_core_initialize_p1(&data_ptr, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);
    ASSERT_NE(data_ptr, nullptr);

    rc = ESMF_SUCCESS;
    aces_core_initialize_p2(data_ptr, gcomp_.ptr, import_state_.ptr, export_state_.ptr,
                            clock_.ptr, nullptr, &rc);
    EXPECT_NE(rc, ESMF_SUCCESS) << "Phase 2 must fail with null grid";

    delete static_cast<aces::AcesInternalData*>(data_ptr);
}

/**
 * @test BothPhases_CompleteSuccessfully
 * Validates: Requirements 4.7-4.10, 4.18, 4.19
 */
TEST_F(InitializePhasesTest, BothPhases_CompleteSuccessfully) {
    ASSERT_TRUE(RunPrerequisitePhases());

    // Phase 1
    void* data_ptr = nullptr;
    int rc = -1;
    aces_core_initialize_p1(&data_ptr, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "Phase 1 must succeed";
    ASSERT_NE(data_ptr, nullptr);

    // Phase 2
    rc = -1;
    aces_core_initialize_p2(data_ptr, gcomp_.ptr, import_state_.ptr, export_state_.ptr,
                            clock_.ptr, grid_.ptr, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "Phase 2 must succeed";

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);
    EXPECT_NE(internal_data->stacking_engine, nullptr);
    EXPECT_NE(internal_data->diagnostic_manager, nullptr);
    EXPECT_GT(internal_data->active_schemes.size(), 0u);
    EXPECT_GT(internal_data->nx, 0) << "nx must be populated after Phase 2";
    EXPECT_GT(internal_data->ny, 0) << "ny must be populated after Phase 2";
    EXPECT_GT(internal_data->nz, 0) << "nz must be populated after Phase 2";
    EXPECT_GT(internal_data->default_mask.extent(0), 0u) << "default_mask must be allocated";

    aces_core_finalize(data_ptr, &rc);
    EXPECT_EQ(rc, ESMF_SUCCESS);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new ESMFEnvironment);
    return RUN_ALL_TESTS();
}
