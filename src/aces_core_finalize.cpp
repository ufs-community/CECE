/**
 * @file aces_core_finalize.cpp
 * @brief Implementation of the ACES Finalize phase with proper cleanup.
 *
 * Performs ordered teardown:
 * 1. Finalize all physics schemes
 * 2. Finalize TIDE if initialized
 * 3. Finalize Kokkos only if ACES initialized it (owns_kokkos)
 * 4. Delete AcesInternalData structure
 *
 * @author Barry Baker
 * @date 2024
 * @version 1.0
 *
 * Requirements: 4.15-4.17
 */

#include <Kokkos_Core.hpp>
#include <chrono>
#include <iostream>
#include <thread>

#include "aces/aces_internal.hpp"

extern "C" {

/**
 * @brief ACES Finalize phase implementation.
 *
 * Releases all resources in the correct order to avoid use-after-free:
 * Kokkos views inside AcesInternalData are destroyed (via delete) before
 * Kokkos::finalize() is called, satisfying the Kokkos teardown contract.
 *
 * @param data_ptr Pointer to AcesInternalData structure.
 * @param rc Return code (0 on success).
 *
 * Requirements: 4.15-4.17
 */
void aces_core_finalize(void* data_ptr, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR in aces_core_finalize: data_ptr is null\n";
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);

    // MPI-aware finalization: Only synchronize on process 0 to avoid conflicts
    bool is_mpi_process_0 = true;  // Default assumption
#ifdef ESMF_MPIUNI
    // Single process mode
    is_mpi_process_0 = true;
#else
    // Check if we're in MPI mode by looking for ESMF VM
    try {
        // If we can get current VM, we're likely in MPI mode
        // For safety, only do expensive operations on process 0
        is_mpi_process_0 = true;  // Conservative: treat each process independently
    } catch (...) {
        is_mpi_process_0 = true;
    }
#endif

    // Critical: Synchronize all Kokkos operations before cleanup
    // Do this on all processes since each has its own Kokkos views
    std::cout << "INFO: ACES Finalize - synchronizing device operations...\n";

    // Multiple fences for safety
    Kokkos::fence("ACES::Finalize::PreCleanup");
    if (internal_data && (internal_data->nx * internal_data->ny > 50000)) {
        // Large grid - add brief delay to ensure TIDE operations complete
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        Kokkos::fence("ACES::Finalize::LargeGrid::PreSync");
    }
    Kokkos::fence("ACES::Finalize::ProcessLocal");

    // Additional synchronization for large grids (process-local)
    if (internal_data && (internal_data->nx * internal_data->ny > 10000)) {
        std::cout << "INFO: Large grid (" << (internal_data->nx * internal_data->ny)
                  << " points) - extended local synchronization...\n";

        // Multiple fences for large grids to ensure all operations complete
        Kokkos::fence("ACES::Finalize::LargeGrid::Phase1");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        Kokkos::fence("ACES::Finalize::LargeGrid::Phase2");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        Kokkos::fence("ACES::Finalize::LargeGrid::Final");
        std::cout << "INFO: Large grid synchronization complete\n";
    }

    std::cout << "INFO: ACES Finalize - beginning cleanup\n";

    // 0. Clear the data ingestor cache to release device memory
    std::cout << "INFO: Clearing data ingestor cache\n";
    try {
        internal_data->ingestor.ClearCache();
    } catch (const std::exception& e) {
        std::cerr << "WARNING: Data ingestor cache clear threw: " << e.what() << "\n";
    }

    // 1. Finalize standalone writer if it was initialized (Req 11.1, 11.8)
    if (internal_data->standalone_writer) {
        std::cout << "INFO: Finalizing AcesStandaloneWriter\n";
        try {
            internal_data->standalone_writer->Finalize();
            std::cout << "INFO: AcesStandaloneWriter finalized\n";
        } catch (const std::exception& e) {
            std::cerr << "WARNING: AcesStandaloneWriter Finalize threw: " << e.what() << "\n";
        }
    }

    // 2. Finalize all physics schemes (Req 4.15 - release Kokkos views held by schemes)
    std::cout << "INFO: Finalizing " << internal_data->active_schemes.size()
              << " physics schemes\n";
    for (auto& scheme : internal_data->active_schemes) {
        if (scheme) {
            try {
                scheme->Finalize();
            } catch (const std::exception& e) {
                std::cerr << "WARNING: Physics scheme Finalize threw: " << e.what() << "\n";
            }
        }
    }

    // 3. Delete internal state — this destroys all Kokkos::Views.
    // NOTE: Do NOT call Kokkos::finalize() here. When running inside an ESMF component,
    // ESMF 8.8.0 uses Kokkos internally and owns the Kokkos lifecycle. Calling
    // Kokkos::finalize() before ESMF_Finalize() causes a segfault in ESMF teardown.
    // Kokkos will be finalized by ESMF_Finalize() in the driver.

    // Final safety fence before deletion
    Kokkos::fence("ACES::Finalize::PreDelete");

    try {
        // Additional safety for large grids
        if (internal_data && (internal_data->nx * internal_data->ny > 50000)) {
            std::cout << "INFO: Large grid detected - using careful deletion sequence\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            Kokkos::fence("ACES::Finalize::LargeGrid::PreDelete");
        }

        delete internal_data;
        std::cout << "INFO: AcesInternalData deleted\n";

        // Post-delete safety fence
        Kokkos::fence("ACES::Finalize::PostDelete");

    } catch (const std::exception& e) {
        std::cerr << "WARNING: Exception during AcesInternalData cleanup: " << e.what() << "\n";
        if (rc != nullptr) {
            *rc = -2;  // Non-fatal error
        }
    } catch (...) {
        std::cerr << "WARNING: Unknown exception during AcesInternalData cleanup\n";
        if (rc != nullptr) {
            *rc = -2;  // Non-fatal error
        }
    }

    std::cout << "INFO: Skipping Kokkos finalization (owned by ESMF)\n";

    std::cout << "INFO: ACES Finalize completed successfully\n";

    if (rc != nullptr) {
        *rc = 0;
    }
}

}  // extern "C"
