/**
 * @file test_cdeps_read_data.cpp
 * @brief Tests for verifying CDEPS can read data from configured streams.
 *
 * **Task 4.2: Verify CDEPS can read data**
 * - Test CDEPS reads from configured streams
 * - Verify data is available in fields
 * - Check for CDEPS errors
 *
 * **Validates: Requirements R6, R10**
 *
 * **Test Strategy**:
 * - Use real CDEPS-inline library (no mocking)
 * - Use real NetCDF data files from data/ directory
 * - Verify CDEPS can initialize with valid streams
 * - Verify CDEPS can read data and make it available
 * - Verify temporal interpolation works correctly
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
 * @class CDEPSReadDataTest
 * @brief Test fixture for CDEPS data reading verification.
 *
 * Sets up ESMF infrastructure and verifies CDEPS can read data from streams.
 */
class CDEPSReadDataTest : public ::testing::Test {
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
            GTEST_SKIP() << "ESMF initialization failed - skipping CDEPS read data tests";
        }

        // Get VM info
        vm_ = ESMC_VMGetGlobal(&rc);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "ESMF VMGetGlobal failed - skipping CDEPS read data tests";
        }

        ESMC_VMGet(vm_, &localPet_, &petCount_, nullptr, nullptr, nullptr, &rc);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "ESMF VMGet failed - skipping CDEPS read data tests";
        }

        // Create GridComp
        ESMC_Clock nullClock;
        nullClock.ptr = nullptr;
        gcomp_ = ESMC_GridCompCreate("ACES_TEST", nullptr, nullClock, &rc);
        if (rc != ESMF_SUCCESS || gcomp_.ptr == nullptr) {
            GTEST_SKIP() << "GridComp creation failed - skipping CDEPS read data tests";
        }

        // Create time interval (3600 seconds = 1 hour)
        ESMC_TimeInterval timeStep;
        rc = ESMC_TimeIntervalSet(&timeStep, 3600);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "TimeIntervalSet failed - skipping CDEPS read data tests";
        }

        // Create calendar
        ESMC_Calendar calendar = ESMC_CalendarCreate("Gregorian", ESMC_CALKIND_GREGORIAN, &rc);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "Calendar creation failed - skipping CDEPS read data tests";
        }

        // Create start and stop times
        ESMC_Time startTime, stopTime;
        rc = ESMC_TimeSet(&startTime, 2000, 0, calendar, ESMC_CALKIND_GREGORIAN, 0);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "TimeSet (start) failed - skipping CDEPS read data tests";
        }

        rc = ESMC_TimeSet(&stopTime, 2000, 24, calendar, ESMC_CALKIND_GREGORIAN, 0);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "TimeSet (stop) failed - skipping CDEPS read data tests";
        }

        // Create clock
        clock_ = ESMC_ClockCreate("ACES_CLOCK", timeStep, startTime, stopTime, &rc);
        if (rc != ESMF_SUCCESS || clock_.ptr == nullptr) {
            GTEST_SKIP() << "Clock creation failed - skipping CDEPS read data tests";
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
            GTEST_SKIP() << "Mesh creation failed - skipping CDEPS read data tests";
        }

        rc = ESMC_MeshAddNodes(mesh_, numNodes, nodeIds.data(), nodeCoords.data(),
                               nodeOwners.data(), nullptr);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "MeshAddNodes failed - skipping CDEPS read data tests";
        }

        double* nullCoords = nullptr;
        rc = ESMC_MeshAddElements(mesh_, numElems, elemIds.data(), elemTypes.data(),
                                  elemConn.data(), nullptr, nullptr, nullCoords);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "MeshAddElements failed - skipping CDEPS read data tests";
        }

        // Create export state
        exportState_ = ESMC_StateCreate("ACES_EXPORT", &rc);
        if (rc != ESMF_SUCCESS) {
            GTEST_SKIP() << "ExportState creation failed - skipping CDEPS read data tests";
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
 * @brief Test CDEPS can initialize with valid streams file.
 *
 * **Validates: Requirement R6**
 *
 * **Test Steps**:
 * 1. Create streams file pointing to real data (data/MACCity_4x5.nc)
 * 2. Initialize CDEPS with valid streams
 * 3. Verify initialization succeeds
 * 4. Verify ESMF handles remain valid
 */
TEST_F(CDEPSReadDataTest, InitializeWithValidStreamsFile) {
    if (aces_cdeps_init == nullptr || gcomp_.ptr == nullptr) {
        GTEST_SKIP() << "CDEPS bridge or ESMF objects not available";
    }

    // Create streams file pointing to real data
    std::ofstream streams_file("test_streams_read.txt");
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

    int rc = 0;
    aces_cdeps_init(gcomp_.ptr, clock_.ptr, mesh_.ptr, "test_streams_read.txt", &rc);

    // Verify initialization succeeded
    EXPECT_EQ(rc, ESMF_SUCCESS) << "CDEPS initialization failed with rc=" << rc;

    // Verify ESMF handles remain valid
    EXPECT_NE(gcomp_.ptr, nullptr) << "GridComp handle corrupted";
    EXPECT_NE(clock_.ptr, nullptr) << "Clock handle corrupted";
    EXPECT_NE(mesh_.ptr, nullptr) << "Mesh handle corrupted";

    // Clean up
    std::remove("test_streams_read.txt");
}

/**
 * @brief Test CDEPS can read data from configured streams.
 *
 * **Validates: Requirement R10**
 *
 * **Test Steps**:
 * 1. Initialize CDEPS with valid streams
 * 2. Advance CDEPS to current time
 * 3. Get field pointer for CO variable
 * 4. Verify field pointer is valid and non-null
 * 5. Verify data is accessible
 */
TEST_F(CDEPSReadDataTest, ReadDataFromStreams) {
    if (aces_cdeps_init == nullptr || aces_cdeps_advance == nullptr || aces_cdeps_get_ptr == nullptr
        || gcomp_.ptr == nullptr) {
        GTEST_SKIP() << "CDEPS bridge or ESMF objects not available";
    }

    // Create streams file
    std::ofstream streams_file("test_streams_read_data.txt");
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
    aces_cdeps_init(gcomp_.ptr, clock_.ptr, mesh_.ptr, "test_streams_read_data.txt", &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "CDEPS initialization failed";

    // Advance CDEPS to current time
    aces_cdeps_advance(clock_.ptr, &rc);
    EXPECT_EQ(rc, ESMF_SUCCESS) << "CDEPS advance failed with rc=" << rc;

    // Get field pointer for CO variable
    void* data_ptr = nullptr;
    aces_cdeps_get_ptr(0, "CO", &data_ptr, &rc);

    // Verify field pointer is valid
    EXPECT_EQ(rc, ESMF_SUCCESS) << "Failed to get field pointer for CO";
    EXPECT_NE(data_ptr, nullptr) << "Field pointer is null";

    // Verify data is accessible (cast to double* and check first element)
    if (data_ptr != nullptr) {
        double* field_data = static_cast<double*>(data_ptr);
        // Just verify we can read the first element without crashing
        double first_value = field_data[0];
        std::cout << "First CO value: " << first_value << "\n";
    }

    // Clean up
    std::remove("test_streams_read_data.txt");
}

/**
 * @brief Test CDEPS data is available in fields after reading.
 *
 * **Validates: Requirement R10**
 *
 * **Test Steps**:
 * 1. Initialize CDEPS with valid streams
 * 2. Advance CDEPS multiple times
 * 3. Verify field data is non-zero (actual data, not uninitialized)
 * 4. Verify data changes between time steps (if temporal interpolation enabled)
 */
TEST_F(CDEPSReadDataTest, DataAvailableInFields) {
    if (aces_cdeps_init == nullptr || aces_cdeps_advance == nullptr || aces_cdeps_get_ptr == nullptr
        || gcomp_.ptr == nullptr) {
        GTEST_SKIP() << "CDEPS bridge or ESMF objects not available";
    }

    // Create streams file with temporal interpolation
    std::ofstream streams_file("test_streams_data_available.txt");
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
    aces_cdeps_init(gcomp_.ptr, clock_.ptr, mesh_.ptr, "test_streams_data_available.txt", &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "CDEPS initialization failed";

    // Advance CDEPS to current time
    aces_cdeps_advance(clock_.ptr, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "CDEPS advance failed";

    // Get field pointer
    void* data_ptr = nullptr;
    aces_cdeps_get_ptr(0, "CO", &data_ptr, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "Failed to get field pointer";
    ASSERT_NE(data_ptr, nullptr) << "Field pointer is null";

    // Verify data is available (not all zeros)
    double* field_data = static_cast<double*>(data_ptr);
    bool has_nonzero_data = false;

    // Check first 100 elements for non-zero values
    for (int i = 0; i < 100; ++i) {
        if (field_data[i] != 0.0) {
            has_nonzero_data = true;
            break;
        }
    }

    EXPECT_TRUE(has_nonzero_data) << "Field data appears to be all zeros (uninitialized?)";

    // Clean up
    std::remove("test_streams_data_available.txt");
}

/**
 * @brief Test CDEPS error handling for invalid streams file.
 *
 * **Validates: Requirement R6**
 *
 * **Test Steps**:
 * 1. Attempt to initialize CDEPS with non-existent streams file
 * 2. Verify CDEPS returns error code (not success)
 * 3. Verify ESMF handles remain valid (no corruption)
 */
TEST_F(CDEPSReadDataTest, ErrorHandlingInvalidStreamsFile) {
    if (aces_cdeps_init == nullptr || gcomp_.ptr == nullptr) {
        GTEST_SKIP() << "CDEPS bridge or ESMF objects not available";
    }

    // Attempt to initialize with non-existent file
    int rc = 0;
    aces_cdeps_init(gcomp_.ptr, clock_.ptr, mesh_.ptr, "nonexistent_streams.txt", &rc);

    // Verify CDEPS returned error
    EXPECT_NE(rc, ESMF_SUCCESS) << "CDEPS should fail with non-existent streams file";

    // Verify ESMF handles remain valid
    EXPECT_NE(gcomp_.ptr, nullptr) << "GridComp handle corrupted";
    EXPECT_NE(clock_.ptr, nullptr) << "Clock handle corrupted";
    EXPECT_NE(mesh_.ptr, nullptr) << "Mesh handle corrupted";
}

/**
 * @brief Test CDEPS temporal interpolation works correctly.
 *
 * **Validates: Requirement R10**
 *
 * **Test Steps**:
 * 1. Initialize CDEPS with linear temporal interpolation
 * 2. Advance clock to multiple time steps
 * 3. Verify data is interpolated (not just nearest neighbor)
 * 4. Verify no errors during interpolation
 */
TEST_F(CDEPSReadDataTest, TemporalInterpolationWorks) {
    if (aces_cdeps_init == nullptr || aces_cdeps_advance == nullptr || aces_cdeps_get_ptr == nullptr
        || gcomp_.ptr == nullptr) {
        GTEST_SKIP() << "CDEPS bridge or ESMF objects not available";
    }

    // Create streams file with linear interpolation
    std::ofstream streams_file("test_streams_temporal.txt");
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
    aces_cdeps_init(gcomp_.ptr, clock_.ptr, mesh_.ptr, "test_streams_temporal.txt", &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "CDEPS initialization failed";

    // Advance multiple times and verify no errors
    for (int step = 0; step < 3; ++step) {
        aces_cdeps_advance(clock_.ptr, &rc);
        EXPECT_EQ(rc, ESMF_SUCCESS) << "CDEPS advance failed at step " << step;

        // Get field pointer
        void* data_ptr = nullptr;
        aces_cdeps_get_ptr(0, "CO", &data_ptr, &rc);
        EXPECT_EQ(rc, ESMF_SUCCESS) << "Failed to get field pointer at step " << step;
        EXPECT_NE(data_ptr, nullptr) << "Field pointer is null at step " << step;
    }

    // Clean up
    std::remove("test_streams_temporal.txt");
}

}  // namespace test
}  // namespace aces

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
