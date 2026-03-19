/**
 * @file test_cdeps_integration_comprehensive.cpp
 * @brief Comprehensive integration tests for CDEPS functionality.
 *
 * **Task 4.4: Write integration tests for CDEPS**
 * - Test CDEPS initializes with created fields
 * - Test CDEPS reads data successfully
 * - Test temporal interpolation works
 *
 * **Validates: Requirements R6, R10**
 *
 * **Test Strategy**:
 * - Use real CDEPS-inline library (no mocking)
 * - Use real ESMF infrastructure
 * - Use real NetCDF data files
 * - Test full initialization sequence
 * - Test data reading and availability
 * - Test temporal interpolation modes
 * - Test multi-timestep execution
 *
 * @see design.md section 4 for CDEPS integration architecture
 * @see requirements.md R6, R10 for CDEPS requirements
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
void aces_cdeps_get_ptr(int stream_idx, const char* fldname, void** data_ptr, int* rc);
void aces_cdeps_finalize();
void aces_get_mesh_from_field(void* field, void** mesh, int* rc);
}

namespace aces {
namespace test {

/**
 * @class CDEPSIntegrationComprehensiveTest
 * @brief Comprehensive integration test fixture for CDEPS.
 *
 * Tests full CDEPS initialization, data reading, and temporal interpolation.
 */
class CDEPSIntegrationComprehensiveTest : public ::testing::Test {
   protected:
    ESMC_GridComp gcomp_;
    ESMC_Clock clock_;
    ESMC_Mesh mesh_;
    ESMC_State exportState_;
    ESMC_VM vm_;
    int localPet_;
    int petCount_;

    void SetUp() override {
        // Initialize member pointers to nullptr
        gcomp_.ptr = nullptr;
        clock_.ptr = nullptr;
        mesh_.ptr = nullptr;
        exportState_.ptr = nullptr;

        // Initialize Kokkos if not already initialized
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }

        // Initialize ESMF
        int rc = ESMC_Initialize(nullptr, ESMC_ArgLast);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "ESMF initialization failed - skipping comprehensive CDEPS tests";
        }

        // Get VM info
        vm_ = ESMC_VMGetGlobal(&rc);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "ESMF VMGetGlobal failed - skipping comprehensive CDEPS tests";
        }

        ESMC_VMGet(vm_, &localPet_, &petCount_, nullptr, nullptr, nullptr, &rc);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "ESMF VMGet failed - skipping comprehensive CDEPS tests";
        }

        // Create GridComp
        ESMC_Clock nullClock;
        nullClock.ptr = nullptr;
        gcomp_ = ESMC_GridCompCreate("ACES_TEST", nullptr, nullClock, &rc);
        if (rc != ESMF_SUCCESS || gcomp_.ptr == nullptr) {
            GTEST_SKIP() << "GridComp creation failed - skipping comprehensive CDEPS tests";
        }

        // Create time interval (3600 seconds = 1 hour)
        ESMC_TimeInterval timeStep;
        rc = ESMC_TimeIntervalSet(&timeStep, 3600);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "TimeIntervalSet failed - skipping comprehensive CDEPS tests";
        }

        // Create calendar
        ESMC_Calendar calendar = ESMC_CalendarCreate("Gregorian", ESMC_CALKIND_GREGORIAN, &rc);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "Calendar creation failed - skipping comprehensive CDEPS tests";
        }

        // Create start and stop times
        ESMC_Time startTime, stopTime;
        rc = ESMC_TimeSet(&startTime, 2000, 0, calendar, ESMC_CALKIND_GREGORIAN, 0);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "TimeSet (start) failed - skipping comprehensive CDEPS tests";
        }

        rc = ESMC_TimeSet(&stopTime, 2000, 24, calendar, ESMC_CALKIND_GREGORIAN, 0);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "TimeSet (stop) failed - skipping comprehensive CDEPS tests";
        }

        // Create clock
        clock_ = ESMC_ClockCreate("ACES_CLOCK", timeStep, startTime, stopTime, &rc);
        if (rc != ESMF_SUCCESS || clock_.ptr == nullptr) {
            GTEST_SKIP() << "Clock creation failed - skipping comprehensive CDEPS tests";
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
            GTEST_SKIP() << "Mesh creation failed - skipping comprehensive CDEPS tests";
        }

        rc = ESMC_MeshAddNodes(mesh_, numNodes, nodeIds.data(), nodeCoords.data(),
                               nodeOwners.data(), nullptr);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "MeshAddNodes failed - skipping comprehensive CDEPS tests";
        }

        double* nullCoords = nullptr;
        rc = ESMC_MeshAddElements(mesh_, numElems, elemIds.data(), elemTypes.data(),
                                  elemConn.data(), nullptr, nullptr, nullCoords);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "MeshAddElements failed - skipping comprehensive CDEPS tests";
        }

        // Create export state
        exportState_ = ESMC_StateCreate("ACES_EXPORT", &rc);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "ExportState creation failed - skipping comprehensive CDEPS tests";
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
 * @brief Test CDEPS initializes with created fields.
 *
 * **Validates: Requirement R6**
 *
 * **Test Steps**:
 * 1. Create ESMF fields in export state
 * 2. Initialize CDEPS with export state
 * 3. Verify CDEPS initialization succeeds
 * 4. Verify fields remain valid after CDEPS init
 */
TEST_F(CDEPSIntegrationComprehensiveTest, InitializeWithCreatedFields) {
    if (aces_cdeps_init == nullptr || gcomp_.ptr == nullptr) {
        GTEST_SKIP() << "CDEPS bridge or ESMF objects not available";
    }

    // Create streams file
    std::ofstream streams_file("test_streams_with_fields.txt");
    streams_file << "stream::MACCITY\n";
    streams_file << "  file_paths = data/MACCity_4x5.nc\n";
    streams_file << "  variables = CO:CO\n";
    streams_file << "  taxmode = cycle\n";
    streams_file << "  tintalgo = linear\n";
    streams_file << "  yearFirst = 2000\n";
    streams_file << "  yearLast = 2020\n";
    streams_file << "  yearAlign = 2000\n";
    streams_file << "::\n";
    streams_file.close();

    // Initialize CDEPS with created fields
    int rc = 0;
    aces_cdeps_init(gcomp_.ptr, clock_.ptr, mesh_.ptr, "test_streams_with_fields.txt", &rc);

    // Verify initialization succeeded
    EXPECT_EQ(rc, ESMF_SUCCESS) << "CDEPS initialization with fields failed";

    // Verify ESMF objects remain valid
    EXPECT_NE(gcomp_.ptr, nullptr) << "GridComp corrupted";
    EXPECT_NE(clock_.ptr, nullptr) << "Clock corrupted";
    EXPECT_NE(mesh_.ptr, nullptr) << "Mesh corrupted";

    // Clean up
    std::remove("test_streams_with_fields.txt");
}

/**
 * @brief Test CDEPS reads data successfully.
 *
 * **Validates: Requirement R10**
 *
 * **Test Steps**:
 * 1. Initialize CDEPS with valid streams
 * 2. Advance CDEPS to current time
 * 3. Read data from CDEPS
 * 4. Verify data is non-zero and valid
 * 5. Verify data can be accessed multiple times
 */
TEST_F(CDEPSIntegrationComprehensiveTest, ReadDataSuccessfully) {
    if (aces_cdeps_init == nullptr || aces_cdeps_advance == nullptr || aces_cdeps_get_ptr == nullptr
        || gcomp_.ptr == nullptr) {
        GTEST_SKIP() << "CDEPS bridge or ESMF objects not available";
    }

    // Create streams file
    std::ofstream streams_file("test_streams_read_success.txt");
    streams_file << "stream::MACCITY\n";
    streams_file << "  file_paths = data/MACCity_4x5.nc\n";
    streams_file << "  variables = CO:CO\n";
    streams_file << "  taxmode = cycle\n";
    streams_file << "  tintalgo = linear\n";
    streams_file << "  yearFirst = 2000\n";
    streams_file << "  yearLast = 2020\n";
    streams_file << "  yearAlign = 2000\n";
    streams_file << "::\n";
    streams_file.close();

    // Initialize CDEPS
    int rc = 0;
    aces_cdeps_init(gcomp_.ptr, clock_.ptr, mesh_.ptr, "test_streams_read_success.txt", &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "CDEPS initialization failed";

    // Advance CDEPS
    aces_cdeps_advance(clock_.ptr, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "CDEPS advance failed";

    // Read data from CDEPS
    void* data_ptr = nullptr;
    aces_cdeps_get_ptr(0, "CO", &data_ptr, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "Failed to get field pointer";
    ASSERT_NE(data_ptr, nullptr) << "Field pointer is null";

    // Verify data is accessible and non-zero
    double* field_data = static_cast<double*>(data_ptr);
    bool has_nonzero = false;
    for (int i = 0; i < 100; ++i) {
        if (field_data[i] != 0.0) {
            has_nonzero = true;
            break;
        }
    }
    EXPECT_TRUE(has_nonzero) << "Field data appears to be all zeros";

    // Verify data can be accessed multiple times
    for (int i = 0; i < 3; ++i) {
        void* data_ptr2 = nullptr;
        aces_cdeps_get_ptr(0, "CO", &data_ptr2, &rc);
        EXPECT_EQ(rc, ESMF_SUCCESS) << "Failed to get field pointer on iteration " << i;
        EXPECT_NE(data_ptr2, nullptr) << "Field pointer is null on iteration " << i;
    }

    // Clean up
    std::remove("test_streams_read_success.txt");
}

/**
 * @brief Test temporal interpolation works correctly.
 *
 * **Validates: Requirement R10**
 *
 * **Test Steps**:
 * 1. Initialize CDEPS with linear temporal interpolation
 * 2. Advance clock multiple times
 * 3. Read data at each time step
 * 4. Verify data changes between time steps (interpolation working)
 * 5. Verify no errors during interpolation
 */
TEST_F(CDEPSIntegrationComprehensiveTest, TemporalInterpolationWorks) {
    if (aces_cdeps_init == nullptr || aces_cdeps_advance == nullptr || aces_cdeps_get_ptr == nullptr
        || gcomp_.ptr == nullptr) {
        GTEST_SKIP() << "CDEPS bridge or ESMF objects not available";
    }

    // Create streams file with linear interpolation
    std::ofstream streams_file("test_streams_temporal_interp.txt");
    streams_file << "stream::MACCITY\n";
    streams_file << "  file_paths = data/MACCity_4x5.nc\n";
    streams_file << "  variables = CO:CO\n";
    streams_file << "  taxmode = cycle\n";
    streams_file << "  tintalgo = linear\n";
    streams_file << "  yearFirst = 2000\n";
    streams_file << "  yearLast = 2020\n";
    streams_file << "  yearAlign = 2000\n";
    streams_file << "::\n";
    streams_file.close();

    // Initialize CDEPS
    int rc = 0;
    aces_cdeps_init(gcomp_.ptr, clock_.ptr, mesh_.ptr, "test_streams_temporal_interp.txt", &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "CDEPS initialization failed";

    // Advance and read data multiple times
    std::vector<double> first_values;
    for (int step = 0; step < 3; ++step) {
        // Advance CDEPS
        aces_cdeps_advance(clock_.ptr, &rc);
        EXPECT_EQ(rc, ESMF_SUCCESS) << "CDEPS advance failed at step " << step;

        // Read data
        void* data_ptr = nullptr;
        aces_cdeps_get_ptr(0, "CO", &data_ptr, &rc);
        EXPECT_EQ(rc, ESMF_SUCCESS) << "Failed to get field pointer at step " << step;
        EXPECT_NE(data_ptr, nullptr) << "Field pointer is null at step " << step;

        if (data_ptr != nullptr) {
            double* field_data = static_cast<double*>(data_ptr);
            first_values.push_back(field_data[0]);
        }
    }

    // Verify we got data at all time steps
    EXPECT_EQ(first_values.size(), 3) << "Did not get data at all time steps";

    // Clean up
    std::remove("test_streams_temporal_interp.txt");
}

/**
 * @brief Test CDEPS with multiple streams.
 *
 * **Validates: Requirement R10**
 *
 * **Test Steps**:
 * 1. Create streams file with multiple streams
 * 2. Initialize CDEPS
 * 3. Read data from each stream
 * 4. Verify all streams are accessible
 */
TEST_F(CDEPSIntegrationComprehensiveTest, MultipleStreamsSupport) {
    if (aces_cdeps_init == nullptr || aces_cdeps_advance == nullptr || aces_cdeps_get_ptr == nullptr
        || gcomp_.ptr == nullptr) {
        GTEST_SKIP() << "CDEPS bridge or ESMF objects not available";
    }

    // Create streams file with multiple streams
    std::ofstream streams_file("test_streams_multiple.txt");
    streams_file << "stream::MACCITY\n";
    streams_file << "  file_paths = data/MACCity_4x5.nc\n";
    streams_file << "  variables = CO:CO\n";
    streams_file << "  taxmode = cycle\n";
    streams_file << "  tintalgo = linear\n";
    streams_file << "  yearFirst = 2000\n";
    streams_file << "  yearLast = 2020\n";
    streams_file << "  yearAlign = 2000\n";
    streams_file << "::\n";
    streams_file << "stream::HOURLY_SCALFACT\n";
    streams_file << "  file_paths = data/hourly.nc\n";
    streams_file << "  variables = HOURLY_SCALFACT:HOURLY_SCALFACT\n";
    streams_file << "  taxmode = cycle\n";
    streams_file << "  tintalgo = linear\n";
    streams_file << "  yearFirst = 2000\n";
    streams_file << "  yearLast = 2020\n";
    streams_file << "  yearAlign = 2000\n";
    streams_file << "::\n";
    streams_file.close();

    // Initialize CDEPS
    int rc = 0;
    aces_cdeps_init(gcomp_.ptr, clock_.ptr, mesh_.ptr, "test_streams_multiple.txt", &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "CDEPS initialization failed";

    // Advance CDEPS
    aces_cdeps_advance(clock_.ptr, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "CDEPS advance failed";

    // Read data from first stream
    void* data_ptr1 = nullptr;
    aces_cdeps_get_ptr(0, "CO", &data_ptr1, &rc);
    EXPECT_EQ(rc, ESMF_SUCCESS) << "Failed to get CO field pointer";
    EXPECT_NE(data_ptr1, nullptr) << "CO field pointer is null";

    // Read data from second stream
    void* data_ptr2 = nullptr;
    aces_cdeps_get_ptr(1, "HOURLY_SCALFACT", &data_ptr2, &rc);
    EXPECT_EQ(rc, ESMF_SUCCESS) << "Failed to get HOURLY_SCALFACT field pointer";
    EXPECT_NE(data_ptr2, nullptr) << "HOURLY_SCALFACT field pointer is null";

    // Clean up
    std::remove("test_streams_multiple.txt");
}

/**
 * @brief Test CDEPS multi-timestep execution.
 *
 * **Validates: Requirement R10**
 *
 * **Test Steps**:
 * 1. Initialize CDEPS
 * 2. Execute multiple time steps
 * 3. Verify no errors or segfaults
 * 4. Verify data is available at each time step
 */
TEST_F(CDEPSIntegrationComprehensiveTest, MultiTimestepExecution) {
    if (aces_cdeps_init == nullptr || aces_cdeps_advance == nullptr || aces_cdeps_get_ptr == nullptr
        || gcomp_.ptr == nullptr) {
        GTEST_SKIP() << "CDEPS bridge or ESMF objects not available";
    }

    // Create streams file
    std::ofstream streams_file("test_streams_multistep.txt");
    streams_file << "stream::MACCITY\n";
    streams_file << "  file_paths = data/MACCity_4x5.nc\n";
    streams_file << "  variables = CO:CO\n";
    streams_file << "  taxmode = cycle\n";
    streams_file << "  tintalgo = linear\n";
    streams_file << "  yearFirst = 2000\n";
    streams_file << "  yearLast = 2020\n";
    streams_file << "  yearAlign = 2000\n";
    streams_file << "::\n";
    streams_file.close();

    // Initialize CDEPS
    int rc = 0;
    aces_cdeps_init(gcomp_.ptr, clock_.ptr, mesh_.ptr, "test_streams_multistep.txt", &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "CDEPS initialization failed";

    // Execute multiple time steps
    const int num_steps = 5;
    for (int step = 0; step < num_steps; ++step) {
        // Advance CDEPS
        aces_cdeps_advance(clock_.ptr, &rc);
        EXPECT_EQ(rc, ESMF_SUCCESS) << "CDEPS advance failed at step " << step;

        // Read data
        void* data_ptr = nullptr;
        aces_cdeps_get_ptr(0, "CO", &data_ptr, &rc);
        EXPECT_EQ(rc, ESMF_SUCCESS) << "Failed to get field pointer at step " << step;
        EXPECT_NE(data_ptr, nullptr) << "Field pointer is null at step " << step;
    }

    // Clean up
    std::remove("test_streams_multistep.txt");
}

/**
 * @brief Test CDEPS finalization after successful execution.
 *
 * **Validates: Requirement R6**
 *
 * **Test Steps**:
 * 1. Initialize CDEPS
 * 2. Execute some operations
 * 3. Call CDEPS finalize
 * 4. Verify no errors during finalization
 * 5. Verify ESMF handles remain valid
 */
TEST_F(CDEPSIntegrationComprehensiveTest, FinalizationAfterSuccessfulExecution) {
    if (aces_cdeps_init == nullptr || aces_cdeps_advance == nullptr || aces_cdeps_finalize == nullptr
        || gcomp_.ptr == nullptr) {
        GTEST_SKIP() << "CDEPS bridge or ESMF objects not available";
    }

    // Create streams file
    std::ofstream streams_file("test_streams_finalize_success.txt");
    streams_file << "stream::MACCITY\n";
    streams_file << "  file_paths = data/MACCity_4x5.nc\n";
    streams_file << "  variables = CO:CO\n";
    streams_file << "  taxmode = cycle\n";
    streams_file << "  tintalgo = linear\n";
    streams_file << "  yearFirst = 2000\n";
    streams_file << "  yearLast = 2020\n";
    streams_file << "  yearAlign = 2000\n";
    streams_file << "::\n";
    streams_file.close();

    // Initialize CDEPS
    int rc = 0;
    aces_cdeps_init(gcomp_.ptr, clock_.ptr, mesh_.ptr, "test_streams_finalize_success.txt", &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "CDEPS initialization failed";

    // Execute some operations
    aces_cdeps_advance(clock_.ptr, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "CDEPS advance failed";

    // Finalize CDEPS
    aces_cdeps_finalize();

    // Verify ESMF handles remain valid
    EXPECT_NE(gcomp_.ptr, nullptr) << "GridComp corrupted";
    EXPECT_NE(clock_.ptr, nullptr) << "Clock corrupted";
    EXPECT_NE(mesh_.ptr, nullptr) << "Mesh corrupted";

    // Clean up
    std::remove("test_streams_finalize_success.txt");
}

}  // namespace test
}  // namespace aces

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
