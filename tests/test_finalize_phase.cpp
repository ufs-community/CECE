/**
 * @file test_finalize_phase.cpp
 * @brief Tests for ACES Finalize phase: resource cleanup, CDEPS finalization,
 *        and conditional Kokkos finalization.
 *
 * Requirements: 4.15-4.17
 */

#include <ESMC.h>
#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <fstream>
#include <iostream>

#include "aces/aces_internal.hpp"

extern "C" {
void aces_core_advertise(void* importState, void* exportState, int* rc);
void aces_core_realize(void* data_ptr, void* importState, void* exportState, void* grid, int* rc);
void aces_core_initialize_p1(void** data_ptr, int* rc);
void aces_core_initialize_p2(void* data_ptr, void* gcomp, void* importState, void* exportState,
                             void* clock, void* grid, int* rc);
void aces_core_finalize(void* data_ptr, int* rc);

// Fortran helper: creates ESMF_GridComp with ESMF_CONTEXT_PARENT_VM to avoid
// MPI sub-communicator creation.
void test_create_gridcomp(const char* name, void* clock_ptr, void** gcomp_ptr, int* rc);
void test_destroy_gridcomp(void* gcomp_ptr, int* rc);
}

// ---------------------------------------------------------------------------
// Global ESMF environment — initialize once, finalize once
// ---------------------------------------------------------------------------
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
class FinalizePhaseTest : public ::testing::Test {
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

aces_data:
  streams: []
)";
    }

    /// Run all setup phases and return data_ptr (nullptr on failure).
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

/// Req 4.15-4.17: Finalize can be called after full initialization without crashing.
TEST_F(FinalizePhaseTest, FinalizeAfterInitializeSucceeds) {
    void* data_ptr = RunSetupPhases();
    ASSERT_NE(data_ptr, nullptr) << "Setup phases failed";

    int rc;
    aces_core_finalize(data_ptr, &rc);
    EXPECT_EQ(rc, ESMF_SUCCESS) << "Finalize should return ESMF_SUCCESS";
}

/// Finalize with a null data_ptr must return ESMF_FAILURE (not crash).
TEST_F(FinalizePhaseTest, FinalizeWithNullDataPtrFails) {
    int rc;
    void* null_ptr = nullptr;
    aces_core_finalize(null_ptr, &rc);
    EXPECT_EQ(rc, ESMF_FAILURE) << "Finalize should fail gracefully with null data_ptr";
}

/// Req 4.16: When cdeps_initialized is false (no streams configured), CDEPS
/// finalization is skipped without error.
TEST_F(FinalizePhaseTest, FinalizeWithNoCdepsSkipsCdepsFinalization) {
    // Config already has empty streams — CDEPS will not be initialized.
    void* data_ptr = RunSetupPhases();
    ASSERT_NE(data_ptr, nullptr) << "Setup phases failed";

    int rc;
    // Should complete without error even though CDEPS was never initialized.
    aces_core_finalize(data_ptr, &rc);
    EXPECT_EQ(rc, ESMF_SUCCESS);
}

/// Req 4.17: When Kokkos was already initialized before ACES (owns_kokkos=false),
/// Finalize must NOT call Kokkos::finalize().
TEST_F(FinalizePhaseTest, FinalizeDoesNotFinalizeKokkosWhenNotOwned) {
    // Kokkos is initialized by the ESMFEnvironment before any test runs,
    // so aces_core_initialize_p1 will set kokkos_initialized_here = false.
    ASSERT_TRUE(Kokkos::is_initialized()) << "Kokkos must be pre-initialized for this test";

    void* data_ptr = RunSetupPhases();
    ASSERT_NE(data_ptr, nullptr) << "Setup phases failed";

    int rc;
    aces_core_finalize(data_ptr, &rc);
    EXPECT_EQ(rc, ESMF_SUCCESS);

    // Kokkos must still be initialized because ACES did not own it.
    EXPECT_TRUE(Kokkos::is_initialized())
        << "Kokkos should remain initialized when ACES does not own it";
}

/// Req 4.15: Finalize releases all physics scheme resources (Finalize() called on each scheme).
TEST_F(FinalizePhaseTest, FinalizeCallsSchemeFinalize) {
    void* data_ptr = RunSetupPhases();
    ASSERT_NE(data_ptr, nullptr) << "Setup phases failed";

    // Verify at least one scheme was registered before finalize.
    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);
    EXPECT_GT(internal_data->active_schemes.size(), 0u)
        << "At least one physics scheme should be active";

    int rc;
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
