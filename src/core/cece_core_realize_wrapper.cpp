/**
 * @file cece_core_realize_wrapper.cpp
 * @brief Wrapper for cece_core_realize that accepts state and grid pointers.
 *
 * This wrapper provides a C interface that accepts state and grid pointers
 * for testing purposes. The actual field creation is handled by the driver
 * cap, so this wrapper simply validates the inputs and calls the core realize
 * function.
 *
 * Requirements: 10.2, 10.3
 */

#include <iostream>

extern "C" {
void cece_core_realize(void*, int*);

/**
 * @brief 5-parameter wrapper for cece_core_realize (called by tests).
 *
 * This wrapper is used by tests to call the realize phase with state and grid
 * information. The wrapper validates inputs and calls the core realize function.
 *
 * @param data_ptr Pointer to CeceInternalData structure
 * @param importState_ptr Pointer to import state (can be null)
 * @param exportState_ptr Pointer to export state (must not be null)
 * @param grid_ptr Pointer to grid (must not be null)
 * @param rc Return code (0 = success, non-zero = error)
 *
 * Requirements: 10.2, 10.3
 *
 * Note: This function has a different name than the 2-parameter version because
 * C doesn't support function overloading. Tests that need this signature should
 * declare it explicitly.
 */
void cece_core_realize_with_states(void* data_ptr, void* importState_ptr, void* exportState_ptr, void* grid_ptr, int* rc) {
    // Initialize return code
    if (rc != nullptr) {
        *rc = 0;
    }

    // Validate required pointers
    if (exportState_ptr == nullptr) {
        std::cerr << "ERROR: cece_core_realize_with_states - exportState_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    if (grid_ptr == nullptr) {
        std::cerr << "ERROR: cece_core_realize_with_states - grid_ptr is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    // importState_ptr can be null (standalone mode)
    // data_ptr can be null for testing purposes

    // Call the core realize function
    cece_core_realize(data_ptr, rc);
}

}  // extern "C"
