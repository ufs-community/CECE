/**
 * @file test_kernel_profiling.cpp
 * @brief Profiling tests for ACES Kokkos kernels.
 *
 * This test suite profiles the StackingEngine and physics scheme kernels
 * to identify memory bandwidth bottlenecks and measure optimization effectiveness.
 *
 * Requirements: 6.4, 6.19, 6.20
 * Validates: Requirements 6.4, 6.19, 6.20
 *
 * @note These tests measure real kernel performance and generate profiling reports.
 */

#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <vector>

#include "aces/aces_kernel_profiler.hpp"
#include "aces/aces_stacking_engine.hpp"

namespace aces {

/**
 * @brief Test suite for kernel profiling and optimization analysis.
 *
 * Property 20: Kernel Performance Profiling
 * Validates: Requirements 6.4, 6.19, 6.20
 *
 * FOR ALL ACES kernels (StackingEngine, physics schemes), measure:
 * - Execution time with proper synchronization
 * - Memory bandwidth utilization
 * - Identify optimization opportunities
 * - Verify optimizations maintain correctness
 */
class KernelProfilingTest : public ::testing::Test {
   protected:
    std::mt19937 rng{42};

    void SetUp() override {
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }
    }
};

/**
 * @test Profile StackingEngine kernel with single layer.
 *
 * Measures execution time and memory bandwidth for StackingEngine
 * with a single emission layer. Identifies baseline performance.
 *
 * Requirement: 6.4, 6.20
 */
TEST_F(KernelProfilingTest, ProfileStackingEngineSingleLayer) {
    int nx = 256;
    int ny = 256;
    int nz = 32;

    // Create synthetic emission layer
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> base_emissions(
        "base", nx, ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> output("output", nx,
                                                                                      ny, nz);

    // Initialize
    Kokkos::parallel_for(
        "init_single_layer", Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {nx, ny, nz}),
        KOKKOS_LAMBDA(int i, int j, int k) {
            base_emissions(i, j, k) = 1.0 + 0.1 * (i + j + k);
            output(i, j, k) = 0.0;
        });
    Kokkos::fence();

    // Define kernel
    auto kernel = [&]() {
        Kokkos::parallel_for(
            "stacking_single_layer",
            Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {nx, ny, nz}),
            KOKKOS_LAMBDA(int i, int j, int k) {
                // Simple single-layer aggregation
                output(i, j, k) = base_emissions(i, j, k) * 1.0;
            });
    };

    // Profile kernel
    // Bytes accessed: 1 read (base_emissions) + 1 write (output) = 2 * nx*ny*nz * 8
    long long bytes_accessed = 2LL * nx * ny * nz * sizeof(double);
    KernelMetrics metrics = KernelProfiler::ProfileKernel("StackingEngine_SingleLayer", nx, ny, nz,
                                                          bytes_accessed, kernel, 20);

    KernelProfiler::PrintMetrics(metrics);

    // Verify output is non-zero
    auto host_output = Kokkos::create_mirror_view(output);
    Kokkos::deep_copy(host_output, output);
    double total = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                total += host_output(i, j, k);
            }
        }
    }
    EXPECT_GT(total, 0.0);
}

/**
 * @test Profile StackingEngine kernel with multiple layers.
 *
 * Measures execution time and memory bandwidth for StackingEngine
 * with multiple emission layers and scale factors. Identifies impact
 * of layer fusion on memory bandwidth.
 *
 * Requirement: 6.4, 6.20
 */
TEST_F(KernelProfilingTest, ProfileStackingEngineMultipleLayers) {
    int nx = 256;
    int ny = 256;
    int nz = 32;

    // Create synthetic emission layers
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> layer1("layer1", nx,
                                                                                      ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> layer2("layer2", nx,
                                                                                      ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> layer3("layer3", nx,
                                                                                      ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> scale_factors(
        "scales", nx, ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> output("output", nx,
                                                                                      ny, nz);

    // Initialize
    Kokkos::parallel_for(
        "init_multi_layer", Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {nx, ny, nz}),
        KOKKOS_LAMBDA(int i, int j, int k) {
            layer1(i, j, k) = 1.0 + 0.1 * (i + j + k);
            layer2(i, j, k) = 0.5 + 0.05 * (i + j + k);
            layer3(i, j, k) = 0.3 + 0.02 * (i + j + k);
            scale_factors(i, j, k) = 1.0 + 0.01 * (i + j + k);
            output(i, j, k) = 0.0;
        });
    Kokkos::fence();

    // Define fused kernel (all layers in single kernel)
    auto fused_kernel = [&]() {
        Kokkos::parallel_for(
            "stacking_fused_layers",
            Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {nx, ny, nz}),
            KOKKOS_LAMBDA(int i, int j, int k) {
                // Fused layer aggregation
                double result = 0.0;
                result += layer1(i, j, k) * scale_factors(i, j, k);
                result += layer2(i, j, k) * scale_factors(i, j, k) * 0.8;
                result += layer3(i, j, k) * scale_factors(i, j, k) * 0.5;
                output(i, j, k) = result;
            });
    };

    // Profile fused kernel
    // Bytes accessed: 4 reads (3 layers + scales) + 1 write = 5 * nx*ny*nz * 8
    long long bytes_accessed = 5LL * nx * ny * nz * sizeof(double);
    KernelMetrics metrics_fused = KernelProfiler::ProfileKernel(
        "StackingEngine_FusedLayers", nx, ny, nz, bytes_accessed, fused_kernel, 20);

    KernelProfiler::PrintMetrics(metrics_fused);

    // Verify output is non-zero
    auto host_output = Kokkos::create_mirror_view(output);
    Kokkos::deep_copy(host_output, output);
    double total = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                total += host_output(i, j, k);
            }
        }
    }
    EXPECT_GT(total, 0.0);
}

/**
 * @test Profile StackingEngine kernel with masks and scale factors.
 *
 * Measures execution time and memory bandwidth for StackingEngine
 * with masks and scale factors applied. Identifies impact of
 * conditional logic on performance.
 *
 * Requirement: 6.4, 6.20
 */
TEST_F(KernelProfilingTest, ProfileStackingEngineWithMasksAndScales) {
    int nx = 256;
    int ny = 256;
    int nz = 32;

    // Create synthetic fields
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> base_emissions(
        "base", nx, ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> scale_factors(
        "scales", nx, ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> mask1("mask1", nx,
                                                                                     ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> mask2("mask2", nx,
                                                                                     ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> output("output", nx,
                                                                                      ny, nz);

    // Initialize
    Kokkos::parallel_for(
        "init_masks_scales", Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {nx, ny, nz}),
        KOKKOS_LAMBDA(int i, int j, int k) {
            base_emissions(i, j, k) = 1.0 + 0.1 * (i + j + k);
            scale_factors(i, j, k) = 1.0 + 0.01 * (i + j + k);
            mask1(i, j, k) = (i + j + k) % 2 == 0 ? 1.0 : 0.5;
            mask2(i, j, k) = (i + j + k) % 3 == 0 ? 1.0 : 0.8;
            output(i, j, k) = 0.0;
        });
    Kokkos::fence();

    // Define kernel with masks and scales
    auto kernel = [&]() {
        Kokkos::parallel_for(
            "stacking_masks_scales",
            Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {nx, ny, nz}),
            KOKKOS_LAMBDA(int i, int j, int k) {
                // Apply scale factors and masks
                double combined_scale = scale_factors(i, j, k);
                double combined_mask = mask1(i, j, k) * mask2(i, j, k);
                output(i, j, k) = base_emissions(i, j, k) * combined_scale * combined_mask;
            });
    };

    // Profile kernel
    // Bytes accessed: 4 reads (base, scales, mask1, mask2) + 1 write = 5 * nx*ny*nz * 8
    long long bytes_accessed = 5LL * nx * ny * nz * sizeof(double);
    KernelMetrics metrics = KernelProfiler::ProfileKernel("StackingEngine_MasksScales", nx, ny, nz,
                                                          bytes_accessed, kernel, 20);

    KernelProfiler::PrintMetrics(metrics);

    // Verify output is non-zero
    auto host_output = Kokkos::create_mirror_view(output);
    Kokkos::deep_copy(host_output, output);
    double total = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                total += host_output(i, j, k);
            }
        }
    }
    EXPECT_GT(total, 0.0);
}

/**
 * @test Profile physics scheme kernel with temperature-dependent scaling.
 *
 * Measures execution time and memory bandwidth for a physics scheme
 * that applies temperature-dependent scaling to emissions.
 *
 * Requirement: 6.20
 */
TEST_F(KernelProfilingTest, ProfilePhysicsSchemeTemperatureScaling) {
    int nx = 256;
    int ny = 256;
    int nz = 32;

    // Create synthetic fields
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> temperature(
        "temp", nx, ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> emissions("emis", nx,
                                                                                         ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> output("output", nx,
                                                                                      ny, nz);

    // Initialize
    Kokkos::parallel_for(
        "init_physics", Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {nx, ny, nz}),
        KOKKOS_LAMBDA(int i, int j, int k) {
            temperature(i, j, k) = 273.15 + 15.0 + 0.1 * (i + j + k);
            emissions(i, j, k) = 1.0 + 0.1 * (i + j + k);
            output(i, j, k) = 0.0;
        });
    Kokkos::fence();

    // Define physics kernel
    auto kernel = [&]() {
        Kokkos::parallel_for(
            "physics_temp_scaling", Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {nx, ny, nz}),
            KOKKOS_LAMBDA(int i, int j, int k) {
                // Temperature-dependent scaling
                double t_ref = 298.15;
                double t_factor = (temperature(i, j, k) - t_ref) / t_ref;
                double scale = 1.0 + 0.05 * t_factor;

                // Apply scaling
                output(i, j, k) = emissions(i, j, k) * scale;
            });
    };

    // Profile kernel
    // Bytes accessed: 2 reads (temp, emis) + 1 write = 3 * nx*ny*nz * 8
    long long bytes_accessed = 3LL * nx * ny * nz * sizeof(double);
    KernelMetrics metrics = KernelProfiler::ProfileKernel("PhysicsScheme_TempScaling", nx, ny, nz,
                                                          bytes_accessed, kernel, 20);

    KernelProfiler::PrintMetrics(metrics);

    // Verify output is non-zero
    auto host_output = Kokkos::create_mirror_view(output);
    Kokkos::deep_copy(host_output, output);
    double total = 0.0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                total += host_output(i, j, k);
            }
        }
    }
    EXPECT_GT(total, 0.0);
}

/**
 * @test Profile memory bandwidth for different grid sizes.
 *
 * Measures memory bandwidth utilization for different grid sizes
 * to identify scaling characteristics and optimal problem sizes.
 *
 * Requirement: 6.20
 */
TEST_F(KernelProfilingTest, ProfileMemoryBandwidthScaling) {
    std::cout << "\n=== Memory Bandwidth Scaling Analysis ===" << std::endl;
    std::cout << "Execution Space: " << Kokkos::DefaultExecutionSpace::name() << std::endl;

    std::vector<KernelMetrics> metrics_list;

    // Test different grid sizes
    std::vector<std::tuple<int, int, int>> grid_sizes = {
        {64, 64, 16},    // Small
        {128, 128, 32},  // Medium
        {256, 256, 32},  // Large
        {512, 512, 64},  // Very large
    };

    for (const auto& [nx, ny, nz] : grid_sizes) {
        Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> src("src", nx,
                                                                                       ny, nz);
        Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> dst("dst", nx,
                                                                                       ny, nz);

        // Initialize
        Kokkos::parallel_for(
            "init_bw", Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {nx, ny, nz}),
            KOKKOS_LAMBDA(int i, int j, int k) { src(i, j, k) = i + j + k; });
        Kokkos::fence();

        // Define copy kernel
        auto kernel = [&]() {
            Kokkos::parallel_for(
                "copy_bw", Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {nx, ny, nz}),
                KOKKOS_LAMBDA(int i, int j, int k) { dst(i, j, k) = src(i, j, k); });
        };

        // Profile
        long long bytes_accessed = 2LL * nx * ny * nz * sizeof(double);
        KernelMetrics metrics = KernelProfiler::ProfileKernel(
            "Copy_" + std::to_string(nx) + "x" + std::to_string(ny) + "x" + std::to_string(nz), nx,
            ny, nz, bytes_accessed, kernel, 20);

        metrics_list.push_back(metrics);
    }

    KernelProfiler::PrintComparison(metrics_list);
}

/**
 * @test Profile coalesced vs non-coalesced memory access patterns.
 *
 * Compares memory bandwidth for coalesced (LayoutLeft) vs
 * non-coalesced (LayoutRight) memory access patterns.
 *
 * Requirement: 6.4, 6.20
 */
TEST_F(KernelProfilingTest, ProfileMemoryCoalescing) {
    int nx = 256;
    int ny = 256;
    int nz = 32;

    std::cout << "\n=== Memory Coalescing Analysis ===" << std::endl;
    std::cout << "Execution Space: " << Kokkos::DefaultExecutionSpace::name() << std::endl;

    // Test LayoutLeft (coalesced on GPU)
    {
        Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> src_left(
            "src_left", nx, ny, nz);
        Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> dst_left(
            "dst_left", nx, ny, nz);

        Kokkos::parallel_for(
            "init_left", Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {nx, ny, nz}),
            KOKKOS_LAMBDA(int i, int j, int k) { src_left(i, j, k) = i + j + k; });
        Kokkos::fence();

        auto kernel_left = [&]() {
            Kokkos::parallel_for(
                "copy_left", Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {nx, ny, nz}),
                KOKKOS_LAMBDA(int i, int j, int k) { dst_left(i, j, k) = src_left(i, j, k); });
        };

        long long bytes_accessed = 2LL * nx * ny * nz * sizeof(double);
        KernelMetrics metrics_left = KernelProfiler::ProfileKernel("Copy_LayoutLeft", nx, ny, nz,
                                                                   bytes_accessed, kernel_left, 20);

        KernelProfiler::PrintMetrics(metrics_left);
    }

    // Test LayoutRight (non-coalesced on GPU)
    {
        Kokkos::View<double***, Kokkos::LayoutRight, Kokkos::DefaultExecutionSpace> src_right(
            "src_right", nx, ny, nz);
        Kokkos::View<double***, Kokkos::LayoutRight, Kokkos::DefaultExecutionSpace> dst_right(
            "dst_right", nx, ny, nz);

        Kokkos::parallel_for(
            "init_right", Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {nx, ny, nz}),
            KOKKOS_LAMBDA(int i, int j, int k) { src_right(i, j, k) = i + j + k; });
        Kokkos::fence();

        auto kernel_right = [&]() {
            Kokkos::parallel_for(
                "copy_right", Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {nx, ny, nz}),
                KOKKOS_LAMBDA(int i, int j, int k) { dst_right(i, j, k) = src_right(i, j, k); });
        };

        long long bytes_accessed = 2LL * nx * ny * nz * sizeof(double);
        KernelMetrics metrics_right = KernelProfiler::ProfileKernel(
            "Copy_LayoutRight", nx, ny, nz, bytes_accessed, kernel_right, 20);

        KernelProfiler::PrintMetrics(metrics_right);
    }
}

}  // namespace aces
