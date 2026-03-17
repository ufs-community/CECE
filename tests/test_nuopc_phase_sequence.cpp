/**
 * @file test_nuopc_phase_sequence.cpp
 * @brief Unit tests for NUOPC phase execution order in ACES.
 *
 * Verifies the correct Advertise → Realize → Initialize (p1 → p2) → Run → Finalize
 * sequence, multi-phase initialization (IPDv00p1/IPDv00p2), field advertisement
 * without allocation, export field allocation after realize, and graceful failure
 * when phases are called out of order.
 *
 * All tests use real ESMF objects — no mocking permitted.
 *
 * Requirements: 2.5-2.10, 4.3-4.17
 */

#include <ESMC.h>
#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <fstream>
#include <iostream>
#include <string>

#include "aces/aces_internal.hpp"

extern "C" {
void aces_core_advertise(void* importState, void* exportState, int* rc);
void aces_core_realize(void* data_ptr, void* importState, void* exportState, void* grid, int* rc);
void aces_core_initialize_p1(void** data_ptr, int* rc);
void aces_core_initialize_p2(void* data_ptr, void* gcomp, void* importState, void* exportState,
                             void* clock, void* grid, int* rc);
void aces_core_run(void* data_ptr, void* importState, void* exportState, void* clock, int* rc);
void aces_core_finalize(void* data_ptr, int* rc);

/// Fortran helper: creates ESMF_GridComp with ESMF_CONTEXT_PARENT_VM to avoid
/// MPI sub-communicator creation.
void test_create_gridcomp(const char* name, void* clock_ptr, void** gcomp_ptr, int* rc);
void test_destroy_gridcomp(void* gcomp_ptr, int* rc);
}

// ---------------------------------------------------------------------------
// Global ESMF environment — initialize once, finalize once
// ---------------------------------------------------------------------------

/**
 * @class ESMFEnvironment
 * @brief GoogleTest global environment that initializes ESMF and Kokkos once
 *        for the entire test binary.
 */
class ESMFEnvironment : public ::testing::Environment {
   public:
    void SetUp() override {
        if (!Kokkos::is_initialized()) Kokkos::initialize();
        int rc = ESMC_Initialize(nullptr, ESMC_ArgLast);
        if (rc != ESMF_SUCCESS) {
            std::cerr << "ESMF initialization failed\n";
            exit(1);
        }
    }
    void TearDown() override {
        ESMC_Finalize();
    }
};

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

/**
 * @class NuopcPhaseSequenceTest
 * @brief Fixture that creates a full set of ESMF objects (Grid, Clock, States,
 *        GridComp) and a minimal ACES YAML config for each test.
 *
 * The config uses a single species (CO) with one NativeExample physics scheme
 * and no CDEPS streams, keeping the test environment self-contained.
 */
class NuopcPhaseSequenceTest : public ::testing::Test {
   protected:
    void SetUp() override {
        grid_.ptr = nullptr;
        import_state_.ptr = nullptr;
        export_state_.ptr = nullptr;
        clock_.ptr = nullptr;
        gcomp_.ptr = nullptr;

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
        if (rc != ESMF_SUCCESS || clock_.ptr == nullptr) GTEST_SKIP() << "Clock creation failed";

        test_create_gridcomp("ACES", clock_.ptr, &gcomp_.ptr, &rc);
        if (rc != ESMF_SUCCESS || gcomp_.ptr == nullptr)
            GTEST_SKIP() << "GridComp creation failed (rc=" << rc << ")";

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
        if (export_state_.ptr != nullptr) ESMC_StateDestroy(&export_state_);
        if (import_state_.ptr != nullptr) ESMC_StateDestroy(&import_state_);
        if (grid_.ptr != nullptr) ESMC_GridDestroy(&grid_);
        if (gcomp_.ptr != nullptr) test_destroy_gridcomp(gcomp_.ptr, &rc);
        if (clock_.ptr != nullptr) ESMC_ClockDestroy(&clock_);
        std::remove("aces_config.yaml");
    }

    /// Write a minimal ACES config with one species and no CDEPS streams.
    void CreateTestConfig() {
        std::ofstream f("aces_config.yaml");
        f << R"(
species:
  CO:
    - operation: add
      field: CO_anthro
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

    /// Run all phases up to and including initialize_p2.  Returns data_ptr or
    /// nullptr on any failure.
    void* RunSetupPhases() {
        int rc;
        void* data_ptr = nullptr;

        aces_core_advertise(import_state_.ptr, export_state_.ptr, &rc);
        if (rc != ESMF_SUCCESS) return nullptr;

        aces_core_realize(nullptr, import_state_.ptr, export_state_.ptr, grid_.ptr, &rc);
        if (rc != ESMF_SUCCESS) return nullptr;

        aces_core_initialize_p1(&data_ptr, &rc);
        if (rc != ESMF_SUCCESS || data_ptr == nullptr) return nullptr;

        aces_core_initialize_p2(data_ptr, gcomp_.ptr, import_state_.ptr, export_state_.ptr,
                                clock_.ptr, grid_.ptr, &rc);
        if (rc != ESMF_SUCCESS) return nullptr;

        return data_ptr;
    }

    ESMC_Grid grid_;
    ESMC_State import_state_;
    ESMC_State export_state_;
    ESMC_Clock clock_;
    ESMC_GridComp gcomp_;
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

/**
 * @test FullPhaseSequenceCompletes
 * @brief Executes the complete Advertise → Realize → p1 → p2 → Run → Finalize
 *        sequence and verifies every phase returns ESMF_SUCCESS.
 *
 * Validates: Requirements 2.5-2.10, 4.3-4.17
 */
TEST_F(NuopcPhaseSequenceTest, FullPhaseSequenceCompletes) {
    int rc;

    // Advertise
    aces_core_advertise(import_state_.ptr, export_state_.ptr, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "Advertise phase failed";

    // Realize
    aces_core_realize(nullptr, import_state_.ptr, export_state_.ptr, grid_.ptr, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "Realize phase failed";

    // Initialize p1
    void* data_ptr = nullptr;
    aces_core_initialize_p1(&data_ptr, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "Initialize p1 failed";
    ASSERT_NE(data_ptr, nullptr) << "data_ptr must be non-null after p1";

    // Initialize p2
    aces_core_initialize_p2(data_ptr, gcomp_.ptr, import_state_.ptr, export_state_.ptr, clock_.ptr,
                            grid_.ptr, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "Initialize p2 failed";

    // Run
    aces_core_run(data_ptr, import_state_.ptr, export_state_.ptr, clock_.ptr, &rc);
    EXPECT_EQ(rc, ESMF_SUCCESS) << "Run phase failed";

    // Finalize
    aces_core_finalize(data_ptr, &rc);
    EXPECT_EQ(rc, ESMF_SUCCESS) << "Finalize phase failed";
}

/**
 * @test AdvertisePhaseDeclaresFieldsWithoutAllocation
 * @brief Verifies that the Advertise phase parses the config and returns
 *        ESMF_SUCCESS without crashing, consistent with Req 4.3/4.4 (declare
 *        field names without allocating memory).
 *
 * Note: The ESMF C API does not expose a direct "is field data allocated?"
 * query for advertised-only fields.  We therefore verify the phase succeeds
 * and that no export fields with allocated data exist in the state before
 * Realize is called.
 *
 * Validates: Requirements 4.3, 4.4
 */
TEST_F(NuopcPhaseSequenceTest, AdvertisePhaseDeclaresFieldsWithoutAllocation) {
    int rc;
    aces_core_advertise(import_state_.ptr, export_state_.ptr, &rc);
    EXPECT_EQ(rc, ESMF_SUCCESS) << "Advertise phase should succeed";

    // Before Realize, the export state should NOT contain a realized CO field.
    ESMC_Field field;
    int query_rc = ESMC_StateGetField(export_state_, "CO", &field);
    // Either the field is absent (ESMF_FAILURE) or present but with no data
    // pointer — both are acceptable pre-Realize states.
    if (query_rc == ESMF_SUCCESS && field.ptr != nullptr) {
        // If the field exists, its data pointer must be null (not yet allocated).
        int localDe = 0;
        int ptr_rc = ESMF_SUCCESS;
        void* field_data = ESMC_FieldGetPtr(field, localDe, &ptr_rc);
        // A non-SUCCESS rc here means the field has no data — that is correct.
        if (ptr_rc == ESMF_SUCCESS) {
            EXPECT_EQ(field_data, nullptr)
                << "Export field data must not be allocated before Realize";
        }
    }
}

/**
 * @test RealizePhaseAllocatesExportFields
 * @brief After Advertise + Realize, the CO export field must exist in the
 *        ExportState and have an allocated (non-null) data pointer.
 *
 * Validates: Requirements 4.5, 4.6
 */
TEST_F(NuopcPhaseSequenceTest, RealizePhaseAllocatesExportFields) {
    int rc;

    aces_core_advertise(import_state_.ptr, export_state_.ptr, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "Advertise phase failed";

    aces_core_realize(nullptr, import_state_.ptr, export_state_.ptr, grid_.ptr, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "Realize phase failed";

    // The CO field must now exist in the export state.
    ESMC_Field field;
    int query_rc = ESMC_StateGetField(export_state_, "CO", &field);
    ASSERT_EQ(query_rc, ESMF_SUCCESS) << "CO field must be present after Realize";
    ASSERT_NE(field.ptr, nullptr) << "CO field pointer must be non-null";

    // The field must have allocated data.
    int localDe = 0;
    int ptr_rc = ESMF_SUCCESS;
    void* field_data = ESMC_FieldGetPtr(field, localDe, &ptr_rc);
    EXPECT_EQ(ptr_rc, ESMF_SUCCESS) << "ESMC_FieldGetPtr should succeed after Realize";
    EXPECT_NE(field_data, nullptr) << "Export field data must be allocated after Realize";
}

/**
 * @test MultiPhaseInitializationIPDv00
 * @brief Verifies the two-phase initialization pattern (IPDv00p1 / IPDv00p2).
 *
 * After p1:
 *   - data_ptr is non-null
 *   - Kokkos is initialized
 *   - config is loaded (at least one species)
 *   - at least one physics scheme is active
 *
 * After p2:
 *   - grid dimensions are populated (nx, ny, nz > 0)
 *   - default_mask is allocated
 *   - both phases return ESMF_SUCCESS
 *
 * Validates: Requirements 4.7-4.10, 4.18, 4.19
 */
TEST_F(NuopcPhaseSequenceTest, MultiPhaseInitializationIPDv00) {
    int rc;

    // Advertise + Realize must precede initialization.
    aces_core_advertise(import_state_.ptr, export_state_.ptr, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);
    aces_core_realize(nullptr, import_state_.ptr, export_state_.ptr, grid_.ptr, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    // --- Phase 1 ---
    void* data_ptr = nullptr;
    aces_core_initialize_p1(&data_ptr, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "IPDv00p1 must return ESMF_SUCCESS";
    ASSERT_NE(data_ptr, nullptr) << "data_ptr must be non-null after p1";

    EXPECT_TRUE(Kokkos::is_initialized()) << "Kokkos must be initialized after p1";

    auto* internal = static_cast<aces::AcesInternalData*>(data_ptr);
    EXPECT_FALSE(internal->config.species_layers.empty())
        << "Config must be loaded with at least one species";
    EXPECT_GT(internal->active_schemes.size(), 0u)
        << "At least one physics scheme must be instantiated after p1";
    EXPECT_NE(internal->stacking_engine, nullptr) << "StackingEngine must be initialized after p1";

    // --- Phase 2 ---
    aces_core_initialize_p2(data_ptr, gcomp_.ptr, import_state_.ptr, export_state_.ptr, clock_.ptr,
                            grid_.ptr, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "IPDv00p2 must return ESMF_SUCCESS";

    EXPECT_GT(internal->nx, 0) << "nx must be populated after p2";
    EXPECT_GT(internal->ny, 0) << "ny must be populated after p2";
    EXPECT_GT(internal->nz, 0) << "nz must be populated after p2";
    EXPECT_GT(internal->default_mask.extent(0), 0u) << "default_mask must be allocated after p2";

    // Cleanup
    aces_core_finalize(data_ptr, &rc);
    EXPECT_EQ(rc, ESMF_SUCCESS);
}

/**
 * @test RunPhaseExecutesAfterFullInit
 * @brief Completes the full init sequence then calls Run, verifying rc ==
 *        ESMF_SUCCESS.
 *
 * Validates: Requirements 2.8, 2.9, 4.11-4.14
 */
TEST_F(NuopcPhaseSequenceTest, RunPhaseExecutesAfterFullInit) {
    void* data_ptr = RunSetupPhases();
    ASSERT_NE(data_ptr, nullptr) << "Setup phases failed";

    int rc;
    aces_core_run(data_ptr, import_state_.ptr, export_state_.ptr, clock_.ptr, &rc);
    EXPECT_EQ(rc, ESMF_SUCCESS) << "Run phase must return ESMF_SUCCESS after full init";

    aces_core_finalize(data_ptr, &rc);
    EXPECT_EQ(rc, ESMF_SUCCESS);
}

/**
 * @test FinalizePhaseReleasesResources
 * @brief Runs the complete sequence through Run, then calls Finalize and
 *        verifies rc == ESMF_SUCCESS.  Kokkos must remain initialized because
 *        the ESMFEnvironment pre-initialized it (owns_kokkos == false).
 *
 * Validates: Requirements 2.10, 4.15-4.17
 */
TEST_F(NuopcPhaseSequenceTest, FinalizePhaseReleasesResources) {
    void* data_ptr = RunSetupPhases();
    ASSERT_NE(data_ptr, nullptr) << "Setup phases failed";

    int rc;
    aces_core_run(data_ptr, import_state_.ptr, export_state_.ptr, clock_.ptr, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "Run phase failed before Finalize test";

    aces_core_finalize(data_ptr, &rc);
    EXPECT_EQ(rc, ESMF_SUCCESS) << "Finalize must return ESMF_SUCCESS";

    // Kokkos was pre-initialized by ESMFEnvironment, so ACES must NOT finalize it.
    EXPECT_TRUE(Kokkos::is_initialized())
        << "Kokkos must remain initialized when ACES does not own it";
}

/**
 * @test PhaseOrderEnforcedByReturnCodes
 * @brief Attempts to call Run with a null data_ptr (simulating a missing
 *        initialize step) and verifies a graceful non-zero return code rather
 *        than a crash.
 *
 * Validates: Requirements 2.9 (robustness), 4.12
 */
TEST_F(NuopcPhaseSequenceTest, PhaseOrderEnforcedByReturnCodes) {
    int rc = ESMF_SUCCESS;
    void* null_ptr = nullptr;

    // Calling Run without prior initialization must fail gracefully.
    aces_core_run(null_ptr, import_state_.ptr, export_state_.ptr, clock_.ptr, &rc);
    EXPECT_NE(rc, ESMF_SUCCESS) << "Run with null data_ptr must return a non-zero (failure) rc";
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new ESMFEnvironment);
    return RUN_ALL_TESTS();
}
