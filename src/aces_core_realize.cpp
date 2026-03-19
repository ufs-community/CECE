/**
 * @file aces_core_realize.cpp
 * @brief Implementation of the NUOPC Realize Phase for ACES.
 *
 * The Realize Phase is the second NUOPC phase where components create and
 * allocate their export fields. This implementation focuses on ACES-specific
 * initialization that doesn't depend on ESMF field management.
 *
 * ESMF field creation is handled by the C++ cap using the ESMF C API,
 * keeping the ACES core clean and independent of framework details.
 *
 * @note This is a C++ bridge function called from the Fortran NUOPC cap.
 * @note The cap handles all ESMF field creation and state management.
 */

#include <iostream>
#include <string>

#include "aces/aces_config.hpp"
#include "aces/aces_config_path.hpp"
#include "aces/aces_internal.hpp"

extern "C" {

/**
 * @brief Realize Phase implementation for ACES.
 *
 * This function performs ACES-specific initialization that depends on
 * the grid being available. It validates configuration and prepares
 * internal data structures for field binding.
 *
 * Note: ESMF field creation is now handled by the Fortran cap using
 * the ESMF Fortran API, which has better integration with NUOPC.
 *
 * Operations Performed:
 * - Validate configuration
 * - Log expected fields (for debugging)
 * - Prepare internal data structures for field binding
 *
 * @param data_ptr Pointer to AcesInternalData structure
 * @param rc Return code (0 = success, non-zero = error)
 *
 * @note This function does NOT create ESMF fields
 * @note Field creation is handled by the Fortran cap
 *
 * Requirements: 4.5, 4.6
 */
void aces_core_realize(void* data_ptr, int* rc) {
    // Initialize return code to success
    if (rc != nullptr) {
        *rc = 0;
    }

    // Reconstitute internal data pointer
    aces::AcesInternalData* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);

    if (internal_data == nullptr) {
        std::cerr << "ERROR in aces_core_realize: Internal data pointer is null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    std::cout << "INFO: ACES Realize Phase - Validating configuration" << std::endl;

    // Parse YAML configuration to validate field requirements
    aces::AcesConfig config;
    try {
        const std::string& config_path = aces::GetConfigFilePath();
        config = aces::ParseConfig(config_path);
    } catch (const std::exception& e) {
        std::cerr << "ERROR in aces_core_realize: Failed to parse config: " << e.what()
                  << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    std::cout << "INFO: ACES Realize Phase - Expecting " << config.species_layers.size()
              << " export fields:" << std::endl;
    for (const auto& [species, layers] : config.species_layers) {
        std::cout << "  - " << species << std::endl;
    }

    // Log expected import fields
    if (!config.met_mapping.empty()) {
        std::cout << "INFO: Expecting " << config.met_mapping.size()
                  << " meteorological import fields" << std::endl;
    }

    if (!config.scale_factor_mapping.empty()) {
        std::cout << "INFO: Expecting " << config.scale_factor_mapping.size()
                  << " scale factor import fields" << std::endl;
    }

    if (!config.mask_mapping.empty()) {
        std::cout << "INFO: Expecting " << config.mask_mapping.size() << " mask import fields"
                  << std::endl;
    }

    std::cout << "INFO: ACES Realize Phase complete - ESMF fields managed by Fortran cap"
              << std::endl;

    if (rc != nullptr) {
        *rc = 0;
    }
}

}  // extern "C"
