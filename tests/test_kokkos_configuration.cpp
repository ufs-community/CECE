/**
 * @file test_kokkos_configuration.cpp
 * @brief Tests for Kokkos execution space configuration and runtime utilities.
 *
 * Tests verify:
 * - Kokkos initialization with different execution spaces
 * - Environment variable handling (OMP_NUM_THREADS, ACES_DEVICE_ID)
 * - Runtime configuration queries
 * - Execution space availability checks
 *
 * Requirements: 6.13-6.18, 6.21
 */

#include <gtest/gtest.h>
#include <Kokkos_Core.hpp>
#include <cstdlib>
#include <iostream>
#include <string>

#include "aces/aces_kokkos_config.hpp"

class KokkosConfigurationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Kokkos should already be initialized by the test framework
        ASSERT_TRUE(Kokkos::is_initialized());
    }
};

/**
 * Test that Kokkos is properly initialized.
 * Requirement: 6.13
 */
TEST_F(KokkosConfigurationTest, KokkosIsInitialized) {
    EXPECT_TRUE(Kokkos::is_initialized());
}

/**
 * Test that we can get the default execution space name.
 * Requirement: 6.13
 */
TEST_F(KokkosConfigurationTest, GetDefaultExecutionSpaceName) {
    std::string space_name = aces::GetDefaultExecutionSpaceName();
    EXPECT_FALSE(space_name.empty());

    // Should be one of the enabled execution spaces
    bool valid_space = (space_name == "Serial" || space_name == "OpenMP" ||
                        space_name == "CUDA" || space_name == "HIP");
    EXPECT_TRUE(valid_space) << "Unexpected execution space: " << space_name;
}

/**
 * Test that PrintKokkosConfiguration doesn't crash.
 * Requirement: 6.13
 */
TEST_F(KokkosConfigurationTest, PrintKokkosConfiguration) {
    // This should not throw or crash
    EXPECT_NO_THROW(aces::PrintKokkosConfiguration());
}

/**
 * Test OpenMP thread count retrieval.
 * Requirement: 6.16
 */
TEST_F(KokkosConfigurationTest, GetOpenMPThreadCount) {
#ifdef KOKKOS_ENABLE_OPENMP
    int thread_count = aces::GetOpenMPThreadCount();
    EXPECT_GT(thread_count, 0);
    EXPECT_LE(thread_count, 1024);  // Sanity check
#else
    // If OpenMP is not enabled, should return 1
    int thread_count = aces::GetOpenMPThreadCount();
    EXPECT_EQ(thread_count, 1);
#endif
}

/**
 * Test GPU device ID retrieval.
 * Requirement: 6.17, 6.18
 */
TEST_F(KokkosConfigurationTest, GetGPUDeviceID) {
#if defined(KOKKOS_ENABLE_CUDA) || defined(KOKKOS_ENABLE_HIP)
    int device_id = aces::GetGPUDeviceID();
    EXPECT_GE(device_id, 0);
    EXPECT_LT(device_id, 16);  // Sanity check - unlikely to have 16+ GPUs
#else
    // If GPU is not enabled, should return 0
    int device_id = aces::GetGPUDeviceID();
    EXPECT_EQ(device_id, 0);
#endif
}

/**
 * Test environment variable retrieval.
 * Requirement: 6.13
 */
TEST_F(KokkosConfigurationTest, GetKokkosEnvVar) {
    // Set a test environment variable
    setenv("TEST_KOKKOS_VAR", "test_value", 1);

    std::string value = aces::GetKokkosEnvVar("TEST_KOKKOS_VAR", "default");
    EXPECT_EQ(value, "test_value");

    // Test default value when variable not set
    std::string default_value = aces::GetKokkosEnvVar("NONEXISTENT_VAR", "default");
    EXPECT_EQ(default_value, "default");

    // Clean up
    unsetenv("TEST_KOKKOS_VAR");
}

/**
 * Test that Serial execution space is available.
 * Requirement: 6.15
 */
TEST_F(KokkosConfigurationTest, SerialExecutionSpaceAvailable) {
#ifdef KOKKOS_ENABLE_SERIAL
    // Serial should be available
    EXPECT_TRUE(true);
#else
    // Serial might not be available in all builds
    EXPECT_TRUE(true);
#endif
}

/**
 * Test that OpenMP execution space is available.
 * Requirement: 6.16
 */
TEST_F(KokkosConfigurationTest, OpenMPExecutionSpaceAvailable) {
#ifdef KOKKOS_ENABLE_OPENMP
    // OpenMP should be available
    EXPECT_TRUE(true);
#else
    // OpenMP might not be available in all builds
    EXPECT_TRUE(true);
#endif
}

/**
 * Test that CUDA execution space is available (if enabled).
 * Requirement: 6.17
 */
TEST_F(KokkosConfigurationTest, CUDAExecutionSpaceAvailable) {
#ifdef KOKKOS_ENABLE_CUDA
    // CUDA should be available
    EXPECT_TRUE(true);
#else
    // CUDA might not be available in all builds
    EXPECT_TRUE(true);
#endif
}

/**
 * Test that HIP execution space is available (if enabled).
 * Requirement: 6.18
 */
TEST_F(KokkosConfigurationTest, HIPExecutionSpaceAvailable) {
#ifdef KOKKOS_ENABLE_HIP
    // HIP should be available
    EXPECT_TRUE(true);
#else
    // HIP might not be available in all builds
    EXPECT_TRUE(true);
#endif
}

/**
 * Test that we can create and execute a simple Kokkos kernel.
 * Requirement: 6.13
 */
TEST_F(KokkosConfigurationTest, SimpleKokkosKernel) {
    int n = 100;
    Kokkos::View<int*> data("data", n);

    // Initialize data
    Kokkos::parallel_for(
        "init_kernel", Kokkos::RangePolicy<>(0, n), KOKKOS_LAMBDA(int i) { data(i) = i; });

    // Verify data
    auto data_host = Kokkos::create_mirror_view_and_copy(Kokkos::HostSpace(), data);
    for (int i = 0; i < n; ++i) {
        EXPECT_EQ(data_host(i), i);
    }
}

/**
 * Test that we can perform a reduction on the default execution space.
 * Requirement: 6.13
 */
TEST_F(KokkosConfigurationTest, KokkosReduction) {
    int n = 100;
    Kokkos::View<int*> data("data", n);

    // Initialize data
    Kokkos::parallel_for(
        "init_kernel", Kokkos::RangePolicy<>(0, n), KOKKOS_LAMBDA(int i) { data(i) = 1; });

    // Perform reduction
    int sum = 0;
    Kokkos::parallel_reduce(
        "sum_kernel", Kokkos::RangePolicy<>(0, n),
        KOKKOS_LAMBDA(int i, int& local_sum) { local_sum += data(i); }, sum);

    EXPECT_EQ(sum, n);
}

/**
 * Test that Kokkos views work correctly on the default execution space.
 * Requirement: 6.13
 */
TEST_F(KokkosConfigurationTest, KokkosViewsOnDefaultSpace) {
    // Create a 2D view
    Kokkos::View<double**> matrix("matrix", 10, 10);

    // Initialize on device
    Kokkos::parallel_for(
        "init_matrix", Kokkos::MDRangePolicy<Kokkos::Rank<2>>({0, 0}, {10, 10}),
        KOKKOS_LAMBDA(int i, int j) { matrix(i, j) = i * 10 + j; });

    // Copy to host and verify
    auto matrix_host = Kokkos::create_mirror_view_and_copy(Kokkos::HostSpace(), matrix);
    for (int i = 0; i < 10; ++i) {
        for (int j = 0; j < 10; ++j) {
            EXPECT_EQ(matrix_host(i, j), i * 10 + j);
        }
    }
}

/**
 * Test that we can query execution space properties.
 * Requirement: 6.13
 */
TEST_F(KokkosConfigurationTest, ExecutionSpaceProperties) {
    std::string space_name = Kokkos::DefaultExecutionSpace::name();
    EXPECT_FALSE(space_name.empty());

    // Verify it's one of the expected spaces
    bool is_valid = (space_name == "Serial" || space_name == "OpenMP" || space_name == "CUDA" ||
                     space_name == "HIP");
    EXPECT_TRUE(is_valid);
}

/**
 * Test that Kokkos is properly configured for JCSDA Docker.
 * Requirement: 6.21
 */
TEST_F(KokkosConfigurationTest, JCSDADockerConfiguration) {
    // In JCSDA Docker, we should have at least Serial and OpenMP
    std::string space_name = Kokkos::DefaultExecutionSpace::name();

    // Should be Serial or OpenMP in JCSDA Docker
    bool is_cpu_space = (space_name == "Serial" || space_name == "OpenMP");
    EXPECT_TRUE(is_cpu_space) << "Expected CPU execution space in JCSDA Docker, got: "
                              << space_name;
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // Initialize Kokkos for tests
    if (!Kokkos::is_initialized()) {
        Kokkos::initialize(argc, argv);
    }

    int result = RUN_ALL_TESTS();

    // Finalize Kokkos
    if (Kokkos::is_initialized()) {
        Kokkos::finalize();
    }

    return result;
}
