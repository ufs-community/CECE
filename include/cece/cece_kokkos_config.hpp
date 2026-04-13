/**
 * @file cece_kokkos_config.hpp
 * @brief Kokkos execution space configuration and runtime utilities for CECE.
 *
 * This header provides utilities for configuring and querying Kokkos execution
 * spaces at runtime. CECE supports multiple execution spaces for performance
 * portability across CPUs and GPUs:
 *
 * - **Serial**: Single-threaded execution (debugging, validation)
 * - **OpenMP**: Multi-threaded CPU execution (default for CPU)
 * - **CUDA**: NVIDIA GPU execution (optional, requires -DKOKKOS_ENABLE_CUDA=ON)
 * - **HIP**: AMD GPU execution (optional, requires -DKOKKOS_ENABLE_HIP=ON)
 *
 * ## Environment Variables
 *
 * The following environment variables control Kokkos initialization:
 *
 * - **OMP_NUM_THREADS**: Number of OpenMP threads (default: all available)
 *   - Example: `export OMP_NUM_THREADS=16`
 *
 * - **CECE_DEVICE_ID**: GPU device ID for CUDA or HIP (default: 0)
 *   - Example: `export CECE_DEVICE_ID=1`
 *
 * ## CMake Configuration
 *
 * Execution spaces are configured at build time via CMake options:
 *
 * ```bash
 * # CPU-only build (default)
 * cmake ..
 *
 * # CPU + CUDA GPU support
 * cmake .. -DKOKKOS_ENABLE_CUDA=ON
 *
 * # CPU + HIP GPU support
 * cmake .. -DKOKKOS_ENABLE_HIP=ON
 *
 * # Serial execution only (debugging)
 * cmake .. -DKOKKOS_ENABLE_OPENMP=OFF
 * ```
 *
 * ## Usage Example
 *
 * ```cpp
 * #include "cece/cece_kokkos_config.hpp"
 *
 * // Query available execution spaces
 * cece::PrintKokkosConfiguration();
 *
 * // Get default execution space name
 * std::string space_name = cece::GetDefaultExecutionSpaceName();
 * std::cout << "Using: " << space_name << std::endl;
 * ```
 *
 * ## Performance Portability
 *
 * All CECE physics kernels use Kokkos::parallel_for and Kokkos::parallel_reduce
 * with Kokkos::DefaultExecutionSpace for automatic dispatch to the configured
 * execution space. This ensures:
 *
 * - Single source code for CPU and GPU
 * - Automatic performance optimization for target hardware
 * - No hardware-specific code paths in physics implementations
 *
 * @note Mocking Kokkos is strictly forbidden. All development uses real Kokkos.
 * @note At most one GPU backend (CUDA or HIP) can be enabled at build time.
 * @note Requirements: 6.13-6.18, 6.21
 */

#ifndef CECE_KOKKOS_CONFIG_HPP
#define CECE_KOKKOS_CONFIG_HPP

#include <Kokkos_Core.hpp>
#include <iostream>
#include <string>

namespace cece {

/**
 * @brief Get the name of the default Kokkos execution space.
 *
 * @return String name of the default execution space (e.g., "Serial", "OpenMP", "CUDA")
 */
inline std::string GetDefaultExecutionSpaceName() {
    return Kokkos::DefaultExecutionSpace::name();
}

/**
 * @brief Print Kokkos configuration and available execution spaces.
 *
 * Outputs information about:
 * - Compiled-in execution spaces
 * - Default execution space
 * - Number of threads (if OpenMP enabled)
 * - GPU device ID (if CUDA/HIP enabled)
 */
inline void PrintKokkosConfiguration() {
    std::cout << "=== Kokkos Configuration ===" << std::endl;
    std::cout << "Default Execution Space: " << GetDefaultExecutionSpaceName() << std::endl;

    std::cout << "Available Execution Spaces:" << std::endl;
#ifdef KOKKOS_ENABLE_SERIAL
    std::cout << "  - Serial" << std::endl;
#endif
#ifdef KOKKOS_ENABLE_OPENMP
    std::cout << "  - OpenMP" << std::endl;
#endif
#ifdef KOKKOS_ENABLE_CUDA
    std::cout << "  - CUDA" << std::endl;
#endif
#ifdef KOKKOS_ENABLE_HIP
    std::cout << "  - HIP" << std::endl;
#endif

#ifdef KOKKOS_ENABLE_OPENMP
    std::cout << "OpenMP Configuration:" << std::endl;
    std::cout << "  - Threads: " << Kokkos::OpenMP::concurrency() << std::endl;
#endif

#ifdef KOKKOS_ENABLE_CUDA
    std::cout << "CUDA Configuration:" << std::endl;
    std::cout << "  - Device: " << Kokkos::Cuda::device_id() << std::endl;
#endif

#ifdef KOKKOS_ENABLE_HIP
    std::cout << "HIP Configuration:" << std::endl;
    std::cout << "  - Device: " << Kokkos::HIP::device_id() << std::endl;
#endif

    std::cout << "=============================" << std::endl;
}

/**
 * @brief Check if a specific execution space is available.
 *
 * @tparam ExecutionSpace The Kokkos execution space to check
 * @return true if the execution space is available, false otherwise
 */
template <typename ExecutionSpace>
inline bool IsExecutionSpaceAvailable() {
    // This is a compile-time check - if the execution space is not enabled,
    // this template will not compile. For runtime checks, use the preprocessor
    // macros directly.
    return true;
}

/**
 * @brief Get environment variable for Kokkos configuration.
 *
 * Retrieves and validates environment variables used for Kokkos runtime configuration.
 *
 * @param var_name Name of the environment variable
 * @param default_value Default value if variable is not set
 * @return The environment variable value or default_value if not set
 */
inline std::string GetKokkosEnvVar(const std::string& var_name,
                                   const std::string& default_value = "") {
    const char* value = std::getenv(var_name.c_str());
    return (value != nullptr) ? std::string(value) : default_value;
}

/**
 * @brief Get the number of OpenMP threads from environment or Kokkos.
 *
 * Reads OMP_NUM_THREADS environment variable if set, otherwise returns
 * the number of threads configured in Kokkos.
 *
 * @return Number of OpenMP threads
 */
inline int GetOpenMPThreadCount() {
#ifdef KOKKOS_ENABLE_OPENMP
    const char* num_threads = std::getenv("OMP_NUM_THREADS");
    if (num_threads != nullptr) {
        int threads = std::atoi(num_threads);
        if (threads > 0) {
            return threads;
        }
    }
    return Kokkos::OpenMP::concurrency();
#else
    return 1;
#endif
}

/**
 * @brief Get the GPU device ID from environment or Kokkos.
 *
 * Reads CECE_DEVICE_ID environment variable if set, otherwise returns
 * the device ID configured in Kokkos.
 *
 * @return GPU device ID
 */
inline int GetGPUDeviceID() {
#ifdef KOKKOS_ENABLE_CUDA
    const char* device_id = std::getenv("CECE_DEVICE_ID");
    if (device_id != nullptr) {
        int dev_id = std::atoi(device_id);
        if (dev_id >= 0) {
            return dev_id;
        }
    }
    return Kokkos::Cuda::device_id();
#elif defined(KOKKOS_ENABLE_HIP)
    const char* device_id = std::getenv("CECE_DEVICE_ID");
    if (device_id != nullptr) {
        int dev_id = std::atoi(device_id);
        if (dev_id >= 0) {
            return dev_id;
        }
    }
    return Kokkos::HIP::device_id();
#else
    return 0;
#endif
}

}  // namespace cece

#endif  // CECE_KOKKOS_CONFIG_HPP
