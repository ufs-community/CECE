/**
 * @file aces_core_finalize.cpp
 * @brief Implementation of the ACES Finalize phase with proper cleanup.
 *
 * Performs ordered teardown:
 * 1. Finalize all physics schemes
 * 2. Finalize CDEPS if initialized
 * 3. Finalize Kokkos only if ACES initialized it (owns_kokkos)
 * 4. Delete AcesInternalData structure
 *
 * Requirements: 4.15-4.17
 */

#include <ESMC.h>

#include <Kokkos_Core.hpp>
#include <iostream>

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
 * @param rc Return code (ESMF_SUCCESS on success).
 *
 * Requirements: 4.15-4.17
 */
void aces_core_finalize(void* data_ptr, int* rc) {
    if (rc != nullptr) {
        *rc = ESMF_SUCCESS;
    }

    std::cout << "INFO: ACES Finalize - beginning cleanup\n";

    if (data_ptr == nullptr) {
        std::cerr << "ERROR in aces_core_finalize: data_ptr is null\n";
        if (rc != nullptr) {
            *rc = ESMF_FAILURE;
        }
        return;
    }

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);

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

    // 3. Finalize CDEPS if it was initialized (Req 4.16)
    // FinalizeCDEPS is a no-op when the CDEPS bridge symbol is not available,
    // so it is safe to call unconditionally.
    try {
        internal_data->ingestor.FinalizeCDEPS();
        std::cout << "INFO: CDEPS finalized\n";
    } catch (const std::exception& e) {
        std::cerr << "WARNING: FinalizeCDEPS threw: " << e.what() << "\n";
    }

    // 4. Delete internal state — this destroys all Kokkos::Views.
    // NOTE: Do NOT call Kokkos::finalize() here. When running inside an ESMF component,
    // ESMF 8.8.0 uses Kokkos internally and owns the Kokkos lifecycle. Calling
    // Kokkos::finalize() before ESMF_Finalize() causes a segfault in ESMF teardown.
    // Kokkos will be finalized by ESMF_Finalize() in the driver.
    delete internal_data;
    std::cout << "INFO: AcesInternalData deleted\n";
    std::cout << "INFO: Skipping Kokkos finalization (owned by ESMF)\n";

    std::cout << "INFO: ACES Finalize completed successfully\n";

    if (rc != nullptr) {
        *rc = ESMF_SUCCESS;
    }
}

}  // extern "C"
