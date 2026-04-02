/**
 * @file aces_core_realize_wrapper.cpp
 * @brief Wrapper for aces_core_realize that accepts ESMF state and grid pointers.
 *
 * This wrapper provides a C interface that accepts ESMF state and grid pointers
 * for testing purposes. The actual ESMF field creation is handled by the Fortran
 * cap, so this wrapper simply validates the inputs and calls the core realize
 * function.
 *
 * Requirements: 10.2, 10.3
 */

#include <iostream>

extern "C" {

/**
 * @brief Core realize function (implemented in aces_core_realize.cpp)
 * This is the actual implementation that only needs data_ptr and rc.
 */
void aces_core_realize_impl(void* data_ptr, int* rc);

/**
 * @brief 2-parameter wrapper for aces_core_realize (called by Fortran cap).
 *
 * This wrapper is used by the Fortran cap to call the realize phase.
 *
 * @param data_ptr Pointer to AcesInternalData structure
 * @param rc Return code (0 = success, non-zero = error)
 *
 * Requirements: 10.2, 10.3
 */
void aces_core_realize(void* data_ptr, int* rc) {
    aces_core_realize_impl(data_ptr, rc);
}

/**
 * @brief 5-parameter wrapper for aces_core_realize (called by tests).
 *
 * This wrapper is used by tests to call the realize phase with ESMF state and grid
 * information. The wrapper validates inputs and calls the core realize function.
 *
 * @param data_ptr Pointer to AcesInternalData structure
 * @param importState_ptr Pointer to ESMF import state (can be null)
 * @param exportState_ptr Pointer to ESMF export state (must not be null)
 * @param grid_ptr Pointer to ESMF grid (must not be null)
 * @param rc Return code (0 = success, non-zero = error)
 *
 * Requirements: 10.2, 10.3
 *
 * Note: This function has a different name than the 2-parameter version because
 * C doesn't support function overloading. Tests that need this signature should
 * declare it explicitly.
 */
void aces_core_realize_with_states(void* data_ptr, void* importState_ptr, void* exportState_ptr,
                                   void* grid_ptr, int* rc) {
    // Initialize return code
    if (rc != nullptr) {
        *rc = 0;
    }

    // Validate required pointers
    if (exportState_ptr == nullptr) {
        std::cerr << "ERROR: aces_core_realize_with_states - exportState_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (grid_ptr == nullptr) {
        std::cerr << "ERROR: aces_core_realize_with_states - grid_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    // importState_ptr can be null (standalone mode)
    // data_ptr can be null for testing purposes

    // Call the core realize function
    aces_core_realize_impl(data_ptr, rc);
}

}  // extern "C"
