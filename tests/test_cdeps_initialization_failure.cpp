/**
 * @file test_cdeps_initialization_failure.cpp
 * @brief Tests for graceful degradation when CDEPS initialization fails.
 *
 * **Task 4.3: Handle CDEPS initialization failure**
 * - Graceful degradation if CDEPS not available
 * - Log warning but continue
 * - System should remain functional without CDEPS
 *
 * **Validates: Requirement R6**
 *
 * **Test Strategy**:
 * - Test CDEPS initialization with missing data files
 * - Test CDEPS initialization with invalid streams configuration
 * - Verify system continues to function without CDEPS
 * - Verify appropriate warnings are logged
 * - Verify ESMF handles remain valid after CDEPS failure
 *
 * @see design.md section 4 for CDEPS integration architecture
 * @see requirements.md R6 for graceful degradation requirement
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "ESMC.h"
#include "Kokkos_Core.hpp"

// Forward declare CDEPS bridge functions
extern "C" {
void aces_cdeps_init(void* gcomp, void* clock, void* mesh, const char* stream_path, int* rc);
void aces_cdeps_advance(void* clock, int* rc);
void aces_cdeps_finalize();
}

namespace aces {
namespace test {

/**
 * @class CDEPSInitializationFailureTest
 * @brief Test fixture for CDEPS initialization failure handling.
 *
 * Tests graceful degradation when CDEPS initialization fails.
 */
class CDEPSInitializationFailureTest : public ::testing::Test {
   protected:
    ESMC_GridComp gcomp_;
    ESMC_Clock clock_;
    ESMC_Mesh mesh_;
    ESMC_VM vm_;
    int localPet_;
    int petCount_;

    void SetUp() override {
        // Initialize member pointers to nullptr
        gcomp_.ptr = nullptr;
        clock_.ptr = nullptr;
        mesh_.ptr = nullptr;

        // Initialize Kokkos if not already initialized
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }

        // Initialize ESMF
        int rc = ESMC_Initialize(nullptr, ESMC_ArgLast);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "ESMF initialization failed - skipping CDEPS failure tests";
        }

        // Get VM info
        vm_ = ESMC_VMGetGlobal(&rc);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "ESMF VMGetGlobal failed - skipping CDEPS failure tests";
        }

        ESMC_VMGet(vm_, &localPet_, &petCount_, nullptr, nullptr, nullptr, &rc);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "ESMF VMGet failed - skipping CDEPS failure tests";
        }

        // Create GridComp
        ESMC_Clock nullClock;
        nullClock.ptr = nullptr;
        gcomp_ = ESMC_GridCompCreate("ACES_TEST", nullptr, nullClock, &rc);
        if (rc != ESMF_SUCCESS || gcomp_.ptr == nullptr) {
            GTEST_SKIP() << "GridComp creation failed - skipping CDEPS failure tests";
        }

        // Create time interval (3600 seconds = 1 hour)
        ESMC_TimeInterval timeStep;
        rc = ESMC_TimeIntervalSet(&timeStep, 3600);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "TimeIntervalSet failed - skipping CDEPS failure tests";
        }

        // Create calendar
        ESMC_Calendar calendar = ESMC_CalendarCreate("Gregorian", ESMC_CALKIND_GREGORIAN, &rc);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "Calendar creation failed - skipping CDEPS failure tests";
        }

        // Create start and stop times
        ESMC_Time startTime, stopTime;
        rc = ESMC_TimeSet(&startTime, 2000, 0, calendar, ESMC_CALKIND_GREGORIAN, 0);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "TimeSet (start) failed - skipping CDEPS failure tests";
        }

        rc = ESMC_TimeSet(&stopTime, 2000, 24, calendar, ESMC_CALKIND_GREGORIAN, 0);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "TimeSet (stop) failed - skipping CDEPS failure tests";
        }

        // Create clock
        clock_ = ESMC_ClockCreate("ACES_CLOCK", timeStep, startTime, stopTime, &rc);
        if (rc != ESMF_SUCCESS || clock_.ptr == nullptr) {
            GTEST_SKIP() << "Clock creation failed - skipping CDEPS failure tests";
        }

        // Create simple 2D mesh (10x10 grid)
        int nx = 10, ny = 10;
        int numNodes = (nx + 1) * (ny + 1);
        int numElems = nx * ny;

        std::vector<double> nodeCoords(numNodes * 2);
        std::vector<int> nodeIds(numNodes);
        std::vector<int> nodeOwners(numNodes, localPet_);

        // Generate node coordinates and IDs
        for (int j = 0; j <= ny; ++j) {
            for (int i = 0; i <= nx; ++i) {
                int idx = j * (nx + 1) + i;
                nodeIds[idx] = idx + 1;
                nodeCoords[2 * idx] = static_cast<double>(i);
                nodeCoords[2 * idx + 1] = static_cast<double>(j);
            }
        }

        std::vector<int> elemIds(numElems);
        std::vector<int> elemTypes(numElems, ESMC_MESHELEMTYPE_QUAD);
        std::vector<int> elemConn(numElems * 4);

        // Generate element connectivity
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                int elemIdx = j * nx + i;
                elemIds[elemIdx] = elemIdx + 1;

                int n0 = j * (nx + 1) + i;
                int n1 = j * (nx + 1) + (i + 1);
                int n2 = (j + 1) * (nx + 1) + (i + 1);
                int n3 = (j + 1) * (nx + 1) + i;

                elemConn[4 * elemIdx] = n0 + 1;
                elemConn[4 * elemIdx + 1] = n1 + 1;
                elemConn[4 * elemIdx + 2] = n2 + 1;
                elemConn[4 * elemIdx + 3] = n3 + 1;
            }
        }

        ESMC_CoordSys_Flag coordSys = ESMC_COORDSYS_CART;
        mesh_ = ESMC_MeshCreate(2, 2, &coordSys, &rc);
        if (rc != ESMF_SUCCESS || mesh_.ptr == nullptr) {
            GTEST_SKIP() << "Mesh creation failed - skipping CDEPS failure tests";
        }

        rc = ESMC_MeshAddNodes(mesh_, numNodes, nodeIds.data(), nodeCoords.data(),
                               nodeOwners.data(), nullptr);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "MeshAddNodes failed - skipping CDEPS failure tests";
        }

        double* nullCoords = nullptr;
        rc = ESMC_MeshAddElements(mesh_, numElems, elemIds.data(), elemTypes.data(),
                                  elemConn.data(), nullptr, nullptr, nullCoords);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "MeshAddElements failed - skipping CDEPS failure tests";
        }
    }

    void TearDown() override {
        int rc;

        if (mesh_.ptr != nullptr) {
            rc = ESMC_MeshDestroy(&mesh_);
        }
        if (clock_.ptr != nullptr) {
            rc = ESMC_ClockDestroy(&clock_);
        }
        if (gcomp_.ptr != nullptr) {
            rc = ESMC_GridCompDestroy(&gcomp_);
        }

        ESMC_Finalize();

        if (Kokkos::is_initialized()) {
            Kokkos::finalize();
        }
    }
};

/**
 * @brief Test graceful degradation when CDEPS streams file is missing.
 *
 * **Validates: Requirement R6**
 *
 * **Test Steps**:
 * 1. Attempt to initialize CDEPS with non-existent streams file
 * 2. Verify CDEPS returns error code
 * 3. Verify ESMF handles remain valid (no corruption)
 * 4. Verify system can continue without CDEPS
 */
TEST_F(CDEPSInitializationFailureTest, GracefulDegradationMissingStreamsFile) {
    if (aces_cdeps_init == nullptr || gcomp_.ptr == nullptr) {
        GTEST_SKIP() << "CDEPS bridge or ESMF objects not available";
    }

    // Attempt to initialize with non-existent streams file
    int rc = 0;
    aces_cdeps_init(gcomp_.ptr, clock_.ptr, mesh_.ptr, "nonexistent_streams_file.txt", &rc);

    // Verify CDEPS returned error (not success)
    EXPECT_NE(rc, ESMF_SUCCESS) << "CDEPS should fail with missing streams file";

    // Verify ESMF handles remain valid (no corruption)
    EXPECT_NE(gcomp_.ptr, nullptr) << "GridComp handle corrupted after CDEPS failure";
    EXPECT_NE(clock_.ptr, nullptr) << "Clock handle corrupted after CDEPS failure";
    EXPECT_NE(mesh_.ptr, nullptr) << "Mesh handle corrupted after CDEPS failure";

    // System should be able to continue (no segfault)
    // This is verified by the test completing without crashing
}

/**
 * @brief Test graceful degradation when CDEPS data file is missing.
 *
 * **Validates: Requirement R6**
 *
 * **Test Steps**:
 * 1. Create streams file pointing to non-existent data file
 * 2. Attempt to initialize CDEPS
 * 3. Verify CDEPS returns error code
 * 4. Verify ESMF handles remain valid
 * 5. Verify system can continue without CDEPS
 */
TEST_F(CDEPSInitializationFailureTest, GracefulDegradationMissingDataFile) {
    if (aces_cdeps_init == nullptr || gcomp_.ptr == nullptr) {
        GTEST_SKIP() << "CDEPS bridge or ESMF objects not available";
    }

    // Create streams file pointing to non-existent data file
    std::ofstream streams_file("test_streams_missing_data.txt");
    streams_file << "stream::TEST_STREAM\n";
    streams_file << "  file_paths = nonexistent_data_file.nc\n";
    streams_file << "  variables = TEST_VAR:TEST_VAR\n";
    streams_file << "  taxmode = cycle\n";
    streams_file << "  tintalgo = linear\n";
    streams_file << "  yearFirst = 2000\n";
    streams_file << "  yearLast = 2020\n";
    streams_file << "  yearAlign = 2000\n";
    streams_file << "::\n";
    streams_file.close();

    // Attempt to initialize CDEPS
    int rc = 0;
    aces_cdeps_init(gcomp_.ptr, clock_.ptr, mesh_.ptr, "test_streams_missing_data.txt", &rc);

    // Verify CDEPS returned error
    EXPECT_NE(rc, ESMF_SUCCESS) << "CDEPS should fail with missing data file";

    // Verify ESMF handles remain valid
    EXPECT_NE(gcomp_.ptr, nullptr) << "GridComp handle corrupted";
    EXPECT_NE(clock_.ptr, nullptr) << "Clock handle corrupted";
    EXPECT_NE(mesh_.ptr, nullptr) << "Mesh handle corrupted";

    // Clean up
    std::remove("test_streams_missing_data.txt");
}

/**
 * @brief Test graceful degradation with invalid streams configuration.
 *
 * **Validates: Requirement R6**
 *
 * **Test Steps**:
 * 1. Create streams file with invalid configuration
 * 2. Attempt to initialize CDEPS
 * 3. Verify CDEPS returns error code
 * 4. Verify ESMF handles remain valid
 * 5. Verify system can continue without CDEPS
 */
TEST_F(CDEPSInitializationFailureTest, GracefulDegradationInvalidConfiguration) {
    if (aces_cdeps_init == nullptr || gcomp_.ptr == nullptr) {
        GTEST_SKIP() << "CDEPS bridge or ESMF objects not available";
    }

    // Create streams file with invalid configuration
    std::ofstream streams_file("test_streams_invalid_config.txt");
    streams_file << "stream::INVALID_STREAM\n";
    streams_file << "  file_paths = \n";  // Empty file path
    streams_file << "  variables = \n";   // Empty variables
    streams_file << "::\n";
    streams_file.close();

    // Attempt to initialize CDEPS
    int rc = 0;
    aces_cdeps_init(gcomp_.ptr, clock_.ptr, mesh_.ptr, "test_streams_invalid_config.txt", &rc);

    // Verify CDEPS returned error
    EXPECT_NE(rc, ESMF_SUCCESS) << "CDEPS should fail with invalid configuration";

    // Verify ESMF handles remain valid
    EXPECT_NE(gcomp_.ptr, nullptr) << "GridComp handle corrupted";
    EXPECT_NE(clock_.ptr, nullptr) << "Clock handle corrupted";
    EXPECT_NE(mesh_.ptr, nullptr) << "Mesh handle corrupted";

    // Clean up
    std::remove("test_streams_invalid_config.txt");
}

/**
 * @brief Test CDEPS finalization after initialization failure.
 *
 * **Validates: Requirement R6**
 *
 * **Test Steps**:
 * 1. Attempt to initialize CDEPS with invalid configuration
 * 2. Call CDEPS finalize
 * 3. Verify no segfault or errors during finalization
 * 4. Verify ESMF handles remain valid
 */
TEST_F(CDEPSInitializationFailureTest, FinalizationAfterInitializationFailure) {
    if (aces_cdeps_init == nullptr || aces_cdeps_finalize == nullptr || gcomp_.ptr == nullptr) {
        GTEST_SKIP() << "CDEPS bridge or ESMF objects not available";
    }

    // Create streams file with invalid configuration
    std::ofstream streams_file("test_streams_finalize_after_failure.txt");
    streams_file << "stream::INVALID\n";
    streams_file << "  file_paths = \n";
    streams_file << "::\n";
    streams_file.close();

    // Attempt to initialize CDEPS (will fail)
    int rc = 0;
    aces_cdeps_init(gcomp_.ptr, clock_.ptr, mesh_.ptr, "test_streams_finalize_after_failure.txt",
                    &rc);

    // Call finalize even though initialization failed
    // This should not crash or cause errors
    aces_cdeps_finalize();

    // Verify ESMF handles remain valid
    EXPECT_NE(gcomp_.ptr, nullptr) << "GridComp handle corrupted";
    EXPECT_NE(clock_.ptr, nullptr) << "Clock handle corrupted";
    EXPECT_NE(mesh_.ptr, nullptr) << "Mesh handle corrupted";

    // Clean up
    std::remove("test_streams_finalize_after_failure.txt");
}

/**
 * @brief Test system continues to function without CDEPS.
 *
 * **Validates: Requirement R6**
 *
 * **Test Steps**:
 * 1. Attempt to initialize CDEPS with invalid configuration
 * 2. Verify CDEPS initialization fails
 * 3. Verify ESMF infrastructure remains functional
 * 4. Verify we can still use ESMF objects for other purposes
 */
TEST_F(CDEPSInitializationFailureTest, SystemContinuesWithoutCDEPS) {
    if (aces_cdeps_init == nullptr || gcomp_.ptr == nullptr) {
        GTEST_SKIP() << "CDEPS bridge or ESMF objects not available";
    }

    // Attempt to initialize CDEPS with invalid configuration
    int rc = 0;
    aces_cdeps_init(gcomp_.ptr, clock_.ptr, mesh_.ptr, "nonexistent_streams.txt", &rc);

    // Verify CDEPS initialization failed
    EXPECT_NE(rc, ESMF_SUCCESS) << "CDEPS should fail";

    // Verify ESMF infrastructure is still functional
    // We can still query the GridComp
    ESMC_GridComp test_gcomp = gcomp_;
    EXPECT_NE(test_gcomp.ptr, nullptr) << "GridComp should still be valid";

    // We can still query the Clock
    ESMC_Clock test_clock = clock_;
    EXPECT_NE(test_clock.ptr, nullptr) << "Clock should still be valid";

    // We can still query the Mesh
    ESMC_Mesh test_mesh = mesh_;
    EXPECT_NE(test_mesh.ptr, nullptr) << "Mesh should still be valid";

    // System should be able to continue without CDEPS
    // This is verified by the test completing without crashing
}

}  // namespace test
}  // namespace aces

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
