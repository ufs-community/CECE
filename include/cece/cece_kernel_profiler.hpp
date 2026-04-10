#ifndef CECE_KERNEL_PROFILER_HPP
#define CECE_KERNEL_PROFILER_HPP

/**
 * @file cece_kernel_profiler.hpp
 * @brief Kokkos kernel profiling infrastructure for performance analysis.
 *
 * Provides tools to measure kernel execution time, memory bandwidth,
 * and identify performance bottlenecks in CECE compute kernels.
 */

#include <Kokkos_Core.hpp>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace cece {

/**
 * @brief Kernel profiling metrics.
 */
struct KernelMetrics {
    std::string kernel_name;
    int grid_size_x = 0;
    int grid_size_y = 0;
    int grid_size_z = 0;
    double execution_time_ms = 0.0;
    double memory_bandwidth_gbps = 0.0;
    double peak_bandwidth_gbps = 0.0;
    double bandwidth_efficiency_percent = 0.0;
    long long bytes_accessed = 0;
    int num_iterations = 0;
};

/**
 * @brief Kokkos kernel profiler for measuring performance characteristics.
 *
 * Provides utilities to:
 * - Measure kernel execution time with proper synchronization
 * - Estimate memory bandwidth utilization
 * - Identify bottlenecks in compute kernels
 * - Compare performance across execution spaces
 */
class KernelProfiler {
   public:
    /**
     * @brief Measure kernel execution time with warm-up and synchronization.
     *
     * @param kernel_name Name of the kernel for logging
     * @param iterations Number of iterations to run
     * @param kernel_func Function to execute
     * @return Execution time in milliseconds
     */
    static double MeasureKernelTime(const std::string& kernel_name, int iterations,
                                    std::function<void()> kernel_func) {
        // Warm-up iterations to stabilize performance
        for (int i = 0; i < 2; ++i) {
            kernel_func();
        }
        Kokkos::fence();

        // Timed execution
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            kernel_func();
        }
        Kokkos::fence();
        auto t1 = std::chrono::high_resolution_clock::now();

        double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / iterations;
        return elapsed_ms;
    }

    /**
     * @brief Get peak memory bandwidth for current execution space.
     *
     * Returns approximate peak bandwidth based on execution space:
     * - Serial: ~10 GB/s (single-threaded memory bandwidth)
     * - OpenMP: ~50 GB/s (multi-threaded, typical modern CPU)
     * - CUDA: ~900 GB/s (NVIDIA A100 typical)
     * - HIP: ~900 GB/s (AMD MI250X typical)
     *
     * @return Peak memory bandwidth in GB/s
     */
    static double GetPeakMemoryBandwidth() {
        std::string space_name = Kokkos::DefaultExecutionSpace::name();

        if (space_name == "Serial") {
            return 10.0;
        } else if (space_name == "OpenMP") {
            return 50.0;
        } else if (space_name == "CUDA") {
            return 900.0;
        } else if (space_name == "HIP") {
            return 900.0;
        }
        return 50.0;
    }

    /**
     * @brief Calculate memory bandwidth from execution time and data volume.
     *
     * @param bytes_accessed Total bytes accessed (reads + writes)
     * @param time_ms Execution time in milliseconds
     * @return Memory bandwidth in GB/s
     */
    static double CalculateBandwidth(long long bytes_accessed, double time_ms) {
        if (time_ms <= 0.0) return 0.0;
        double time_seconds = time_ms / 1000.0;
        double bandwidth_gbps = (bytes_accessed / 1e9) / time_seconds;
        return bandwidth_gbps;
    }

    /**
     * @brief Measure memory bandwidth of a simple copy kernel.
     *
     * @param nx X dimension
     * @param ny Y dimension
     * @param nz Z dimension
     * @param iterations Number of iterations
     * @return Achieved memory bandwidth in GB/s
     */
    static double MeasureMemoryBandwidth(int nx, int ny, int nz, int iterations = 10) {
        Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> src("src", nx,
                                                                                       ny, nz);
        Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> dst("dst", nx,
                                                                                       ny, nz);

        // Initialize source
        Kokkos::parallel_for(
            "init_src", Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {nx, ny, nz}),
            KOKKOS_LAMBDA(int i, int j, int k) { src(i, j, k) = i + j + k; });
        Kokkos::fence();

        // Measure copy bandwidth
        auto copy_kernel = [&]() {
            Kokkos::parallel_for(
                "copy_kernel", Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {nx, ny, nz}),
                KOKKOS_LAMBDA(int i, int j, int k) { dst(i, j, k) = src(i, j, k); });
        };

        double time_ms = MeasureKernelTime("copy", iterations, copy_kernel);

        // Calculate bandwidth: 2 * nx * ny * nz * 8 bytes per iteration
        long long bytes_per_iter = 2LL * nx * ny * nz * sizeof(double);
        double bandwidth_gbps = CalculateBandwidth(bytes_per_iter, time_ms);

        return bandwidth_gbps;
    }

    /**
     * @brief Profile a kernel and return detailed metrics.
     *
     * @param kernel_name Name of the kernel
     * @param nx X dimension
     * @param ny Y dimension
     * @param nz Z dimension
     * @param bytes_accessed Total bytes accessed in kernel
     * @param kernel_func Function to execute
     * @param iterations Number of iterations
     * @return KernelMetrics structure with profiling results
     */
    static KernelMetrics ProfileKernel(const std::string& kernel_name, int nx, int ny, int nz,
                                       long long bytes_accessed, std::function<void()> kernel_func,
                                       int iterations = 10) {
        KernelMetrics metrics;
        metrics.kernel_name = kernel_name;
        metrics.grid_size_x = nx;
        metrics.grid_size_y = ny;
        metrics.grid_size_z = nz;
        metrics.bytes_accessed = bytes_accessed;
        metrics.num_iterations = iterations;

        // Measure execution time
        metrics.execution_time_ms = MeasureKernelTime(kernel_name, iterations, kernel_func);

        // Calculate bandwidth
        metrics.peak_bandwidth_gbps = GetPeakMemoryBandwidth();
        metrics.memory_bandwidth_gbps =
            CalculateBandwidth(bytes_accessed, metrics.execution_time_ms);
        metrics.bandwidth_efficiency_percent =
            (metrics.memory_bandwidth_gbps / metrics.peak_bandwidth_gbps) * 100.0;

        return metrics;
    }

    /**
     * @brief Print profiling metrics in human-readable format.
     *
     * @param metrics Metrics to print
     */
    static void PrintMetrics(const KernelMetrics& metrics) {
        std::cout << "\n=== Kernel Profile: " << metrics.kernel_name << " ===" << std::endl;
        std::cout << "Grid: " << metrics.grid_size_x << "x" << metrics.grid_size_y << "x"
                  << metrics.grid_size_z << std::endl;
        std::cout << "Execution Space: " << Kokkos::DefaultExecutionSpace::name() << std::endl;
        std::cout << "Execution Time: " << std::fixed << std::setprecision(4)
                  << metrics.execution_time_ms << " ms" << std::endl;
        std::cout << "Memory Bandwidth: " << std::fixed << std::setprecision(2)
                  << metrics.memory_bandwidth_gbps << " GB/s" << std::endl;
        std::cout << "Peak Bandwidth: " << std::fixed << std::setprecision(2)
                  << metrics.peak_bandwidth_gbps << " GB/s" << std::endl;
        std::cout << "Bandwidth Efficiency: " << std::fixed << std::setprecision(2)
                  << metrics.bandwidth_efficiency_percent << "%" << std::endl;
    }

    /**
     * @brief Print comparison of multiple kernel metrics.
     *
     * @param metrics_list Vector of metrics to compare
     */
    static void PrintComparison(const std::vector<KernelMetrics>& metrics_list) {
        std::cout << "\n=== Kernel Performance Comparison ===" << std::endl;
        std::cout << std::left << std::setw(30) << "Kernel" << std::setw(15) << "Time (ms)"
                  << std::setw(15) << "BW (GB/s)" << std::setw(15) << "Efficiency (%)" << std::endl;
        std::cout << std::string(75, '-') << std::endl;

        for (const auto& m : metrics_list) {
            std::cout << std::left << std::setw(30) << m.kernel_name << std::fixed
                      << std::setprecision(4) << std::setw(15) << m.execution_time_ms
                      << std::setprecision(2) << std::setw(15) << m.memory_bandwidth_gbps
                      << std::setw(15) << m.bandwidth_efficiency_percent << std::endl;
        }
    }
};

}  // namespace cece

#endif  // CECE_KERNEL_PROFILER_HPP
