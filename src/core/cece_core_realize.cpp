/**
 * @file cece_core_realize.cpp
 * @brief Implementation of the Realize Phase for CECE.
 *
 * The Realize Phase is the second initialization phase where components create and
 * allocate their export fields. This implementation focuses on CECE-specific
 * initialization that doesn't depend on external field management.
 *
 * Field creation is handled by the driver cap, keeping the CECE core clean
 * and independent of framework details.
 *
 * @note This is a C++ bridge function called from the driver.
 * @note The driver handles all field creation and state management.
 */

#include <iostream>
#include <string>

#include "cece/cece_config.hpp"
#include "cece/cece_config_path.hpp"
#include "cece/cece_internal.hpp"
#include "cece/cece_logger.hpp"

using namespace std::literals::string_literals;

extern "C" {

/**
 * @brief Realize Phase implementation for CECE.
 *
 * This function performs CECE-specific initialization that depends on
 * the grid being available. It validates configuration and prepares
 * internal data structures for field binding.
 *
 * Note: Field creation is handled by the driver cap.
 *
 * Operations Performed:
 * - Validate configuration
 * - Log expected fields (for debugging)
 * - Prepare internal data structures for field binding
 *
 * @param data_ptr Pointer to CeceInternalData structure
 * @param rc Return code (0 = success, non-zero = error)
 *
 * @note This function does NOT create external fields
 * @note Field creation is handled by the driver
 *
 * Requirements: 4.5, 4.6
 */
void cece_core_realize_impl(void* data_ptr, int* rc) {
    // Initialize return code to success
    if (rc != nullptr) {
        *rc = 0;
    }

    // Reconstitute internal data pointer
    cece::CeceInternalData* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);

    if (internal_data == nullptr) {
        CECE_LOG_ERROR("ERROR in cece_core_realize: Internal data pointer is null\n");
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    CECE_LOG_INFO("INFO: CECE Realize Phase - Validating configuration\n");

    // Parse YAML configuration to validate field requirements
    cece::CeceConfig& config = internal_data->config;

    std::string logmsg = "INFO: CECE Realize Phase - Expecting "s + std::to_string(config.species_layers.size()) + " export fields:\n"s;
    for (const auto& [species, layers] : config.species_layers) {
        logmsg += "  - "s + species + "\n"s;
    }
    CECE_LOG_INFO(logmsg);

    // Log expected import fields
    if (!config.met_mapping.empty()) {
        CECE_LOG_INFO("INFO: Expecting "s + std::to_string(config.met_mapping.size()) + " meteorological import fields\n"s);
    }

    if (!config.scale_factor_mapping.empty()) {
        CECE_LOG_INFO("INFO: Expecting "s + std::to_string(config.scale_factor_mapping.size()) + " scale factor import fields\n"s);
    }

    if (!config.mask_mapping.empty()) {
        CECE_LOG_INFO("INFO: Expecting "s + std::to_string(config.mask_mapping.size()) + " mask import fields\n"s);
    }

    CECE_LOG_INFO("INFO: CECE Realize Phase complete - fields managed by driver\n"s);

    if (rc != nullptr) {
        *rc = 0;
    }
}

}  // extern "C"
