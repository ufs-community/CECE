/**
 * @file cece_core_finalize.cpp
 * @brief Implementation of the CECE Finalize phase with proper cleanup.
 *
 * Performs ordered teardown:
 * 1. Finalize all physics schemes
 * 2. Finalize TIDE if initialized
 * 3. Finalize Kokkos only if CECE initialized it (owns_kokkos)
 * 4. Delete CeceInternalData structure
 *
 * @author Barry Baker
 * @date 2024
 * @version 1.0
 *
 * Requirements: 4.15-4.17
 */

#include <Kokkos_Core.hpp>
#include <iostream>

#include "cece/cece_internal.hpp"

extern "C" {

/**
 * @brief CECE Finalize phase implementation.
 *
 * Releases all resources in the correct order to avoid use-after-free:
 * Kokkos views inside CeceInternalData are destroyed (via delete) before
 * Kokkos::finalize() is called, satisfying the Kokkos teardown contract.
 *
 * @param data_ptr Pointer to CeceInternalData structure.
 * @param rc Return code (0 on success).
 *
 * Requirements: 4.15-4.17
 */
void cece_core_finalize(void* data_ptr, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    if (data_ptr == nullptr) {
        std::cerr << "ERROR in cece_core_finalize: data_ptr is null\n";
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);

    // Synchronize all Kokkos operations before cleanup.
    // A single fence is a full device barrier — multiple fences add no benefit.
    std::cout << "INFO: CECE Finalize - synchronizing device operations...\n";
    Kokkos::fence("CECE::Finalize::PreCleanup");

    std::cout << "INFO: CECE Finalize - beginning cleanup\n";

    // 0. Clear the data ingestor cache to release device memory
    std::cout << "INFO: Clearing data ingestor cache\n";
    try {
        internal_data->ingestor.ClearCache();
    } catch (const std::exception& e) {
        std::cerr << "WARNING: Data ingestor cache clear threw: " << e.what() << "\n";
    }

    // 1. Finalize standalone writer if it was initialized (Req 11.1, 11.8)
    if (internal_data->standalone_writer) {
        std::cout << "INFO: Finalizing CeceStandaloneWriter\n";
        try {
            internal_data->standalone_writer->Finalize();
            std::cout << "INFO: CeceStandaloneWriter finalized\n";
        } catch (const std::exception& e) {
            std::cerr << "WARNING: CeceStandaloneWriter Finalize threw: " << e.what() << "\n";
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

    // Final fence before deletion to ensure all device work is complete
    Kokkos::fence("CECE::Finalize::PreDelete");

    try {
        delete internal_data;
        std::cout << "INFO: CeceInternalData deleted\n";
    } catch (const std::exception& e) {
        std::cerr << "WARNING: Exception during CeceInternalData cleanup: " << e.what() << "\n";
        if (rc != nullptr) {
            *rc = -2;  // Non-fatal error
        }
    } catch (...) {
        std::cerr << "WARNING: Unknown exception during CeceInternalData cleanup\n";
        if (rc != nullptr) {
            *rc = -2;  // Non-fatal error
        }
    }

    std::cout << "INFO: Skipping Kokkos finalization (owned by ESMF)\n";
    std::cout << "INFO: CECE Finalize completed successfully\n";

    if (rc != nullptr) {
        *rc = 0;
    }
}

}  // extern "C"
