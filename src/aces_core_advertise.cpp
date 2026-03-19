/**
 * @file aces_core_advertise.cpp
 * @brief Implementation of the NUOPC Advertise Phase for ACES.
 *
 * The Advertise Phase is the first NUOPC phase where components declare their
 * import and export fields WITHOUT allocating memory. This phase allows the
 * coupling framework to understand what fields each component needs and provides.
 *
 * This implementation provides the field list to the Fortran cap, which handles
 * the actual NUOPC field advertisement.
 *
 * @note This is a C++ bridge function called from the Fortran NUOPC cap.
 */

#include <iostream>
#include <string>

#include "aces/aces_config.hpp"
#include "aces/aces_config_path.hpp"

extern "C" {

/**
 * @brief Advertise Phase implementation for ACES.
 *
 * This function is called during the NUOPC Advertise Phase to provide the
 * list of fields that ACES will export. The Fortran cap handles the actual
 * NUOPC field advertisement.
 *
 * @param importState_ptr Pointer to ESMF ImportState (void* for C compatibility)
 * @param exportState_ptr Pointer to ESMF ExportState (void* for C compatibility)
 * @param rc Return code (0 = success, non-zero = error)
 *
 * Requirements: 4.3, 4.4
 */
void aces_core_advertise(void* importState_ptr, void* exportState_ptr, int* rc) {
    // Initialize return code to success
    if (rc != nullptr) {
        *rc = 0;
    }

    std::cout << "INFO: ACES Advertise Phase - Preparing field list from configuration"
              << std::endl;

    // Parse configuration to get field list
    aces::AcesConfig config;
    try {
        const std::string& config_path = aces::GetConfigFilePath();
        config = aces::ParseConfig(config_path);
    } catch (const std::exception& e) {
        std::cerr << "ERROR in aces_core_advertise: Failed to parse config: " << e.what()
                  << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    // Log the fields that will be advertised
    std::cout << "INFO: Will advertise " << config.species_layers.size() << " emission species:"
              << std::endl;
    for (const auto& [species, layers] : config.species_layers) {
        std::cout << "  - " << species << std::endl;
    }

    std::cout << "INFO: ACES Advertise Phase complete - Fortran cap will advertise fields"
              << std::endl;
}

}  // extern "C"
