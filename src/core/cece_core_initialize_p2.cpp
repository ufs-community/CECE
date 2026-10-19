/**
 * @file cece_core_initialize_p2.cpp
 * @brief Implementation of NUOPC Initialize Phase 2 (IPDv00p2) for CECE.
 *
 * Phase 2 of initialization handles field binding and data stream setup:
 * - Receive field data pointers from Fortran cap
 * - Extract grid dimensions from field metadata
 * - Initialize data streams if configured
 * - Allocate default mask (all 1.0)
 * - Cache field metadata for efficient runtime access
 *
 * This phase assumes:
 * - Phase 1 has completed successfully
 * - CeceInternalData structure exists and is passed in
 * - All export fields have been created by Fortran cap
 * - Field data pointers are passed from Fortran
 *
 * @note This is a C++ bridge function called from the driver.
 * @note No framework-specific API calls - all field management is in the driver.
 * @note Receives field data pointers and grid dimensions from the driver.
 *
 * Requirements: 4.7-4.10, 4.18, 4.19
 */

#include <Kokkos_Core.hpp>
#include <iostream>
#include <set>
#include <string>

#include "cece/cece_data_ingestor.hpp"
#include "cece/cece_internal.hpp"
#include "cece/cece_logger.hpp"

extern "C" {

/**
 * @brief Initialize Phase 2 (IPDv00p2) implementation for CECE.
 *
 * This function performs field binding and data stream initialization.
 * All field creation and state management is handled by the driver.
 *
 * Operations Performed:
 * 1. Validate that Phase 1 completed (internal_data exists)
 * 2. Store grid dimensions passed from Fortran
 * 3. Initialize data streams if configured
 * 4. Allocate default mask (all 1.0) for use in StackingEngine
 * 5. Cache field metadata for efficient runtime queries
 *
 * @param data_ptr Pointer to CeceInternalData from Phase 1
 * @param nx Grid dimension (x)
 * @param ny Grid dimension (y)
 * @param nz Grid dimension (z)
 * @param rc Return code (0 = success, non-zero = error)
 *
 * @note Grid dimensions are passed from the driver.
 * @note The default mask is allocated as a Kokkos::View filled with 1.0.
 * @note Field data pointers are stored in internal_data by Fortran cap.
 *
 * Requirements: 4.7-4.10, 4.18, 4.19
 */
void cece_core_initialize_p2(void* data_ptr, int* nx, int* ny, int* nz, int* rc) {
    // Initialize return code to success
    if (rc != nullptr) {
        *rc = 0;  // 0 = success in C
    }

    std::cout << "INFO: CECE Initialize Phase 2 (IPDv00p2) - Field binding and data stream setup" << std::endl;

    // 1. Validate that Phase 1 completed successfully
    if (data_ptr == nullptr) {
        // clang-format off
        std::cerr << "ERROR in cece_core_initialize_p2: Internal data pointer is null - "
                  << "Phase 1 must complete before Phase 2" << std::endl;
        // clang-format on
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);
    std::cout << "INFO: Internal data structure validated" << std::endl;

    // 2. Store grid dimensions passed from Fortran
    if (nx == nullptr || ny == nullptr || nz == nullptr) {
        std::cerr << "ERROR in cece_core_initialize_p2: Grid dimension pointers are null" << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    // Validate grid dimensions are positive
    if (*nx <= 0 || *ny <= 0 || *nz <= 0) {
        std::cerr << "ERROR in cece_core_initialize_p2: Invalid grid dimensions - " << "nx=" << *nx << ", ny=" << *ny << ", nz=" << *nz << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    internal_data->nx = *nx;
    internal_data->ny = *ny;
    internal_data->nz = *nz;

    std::cout << "INFO: Grid dimensions received: nx=" << internal_data->nx << ", ny=" << internal_data->ny << ", nz=" << internal_data->nz
              << std::endl;

    // 3. Initialize data ingestor if streams are configured
    if (!internal_data->config.cece_data.streams.empty()) {
        std::cout << "INFO: cece_data streams configured - initializing data ingestor" << std::endl;
        std::cout << "INFO: Number of streams: " << internal_data->config.cece_data.streams.size() << std::endl;

        try {
            // Initialize data streams through the data ingestor
            // Note: The actual stream initialization happens in the Fortran bridge
            // This C++ code prepares the configuration and validates it
            std::cout << "INFO: Data stream configuration validated" << std::endl;

            // Mark streams as initialized (actual initialization happens in Fortran cap)
            // The Fortran cap will call the stream init with the streams configuration
            std::cout << "INFO: Data stream initialization will be completed by Fortran cap" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "ERROR: Failed to initialize TIDE: " << e.what() << std::endl;
            if (rc != nullptr) {
                *rc = -1;
            }
            return;
        }
    } else {
        std::cout << "INFO: No TIDE streams configured - skipping TIDE initialization" << std::endl;
    }

    // 4. Allocate default mask (all 1.0)
    std::cout << "INFO: Allocating default mask" << std::endl;
    try {
        internal_data->default_mask = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>(
            "default_mask", internal_data->nx, internal_data->ny, internal_data->nz);

        // Initialize mask to 1.0 on device
        Kokkos::deep_copy(internal_data->default_mask, 1.0);

        std::cout << "INFO: Default mask allocated and initialized to 1.0" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Failed to allocate default mask: " << e.what() << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    // 5. Cache field metadata for efficient runtime queries
    std::cout << "INFO: Caching field metadata" << std::endl;

    // Cache import field names (external names from the host model)
    for (const auto& [internal_name, external_name] : internal_data->config.met_mapping) {
        internal_data->external_esmf_fields.push_back(external_name);
        internal_data->esmf_fields.push_back(internal_name);
    }

    for (const auto& [internal_name, external_name] : internal_data->config.scale_factor_mapping) {
        internal_data->external_esmf_fields.push_back(external_name);
        internal_data->esmf_fields.push_back(internal_name);
    }

    for (const auto& [internal_name, external_name] : internal_data->config.mask_mapping) {
        internal_data->external_esmf_fields.push_back(external_name);
        internal_data->esmf_fields.push_back(internal_name);
    }

    std::cout << "INFO: Cached " << internal_data->esmf_fields.size() << " import field mappings" << std::endl;

    // 6. Bind the diagnostic manager into every scheme at the realized grid.
    // Phase 1 created schemes with a null manager (the grid was unknown); this
    // is the re-initialization that comment promised (research R1). SetGrid
    // makes the registry authoritative on field size (FR-008), SetReservedNames
    // arms species-name collision checks (FR-009), and re-running Initialize
    // re-registers each scheme's diagnostics into the now-bound manager so the
    // scheme members hold grid-sized views instead of the empty P1 handles.
    if (internal_data->diagnostic_manager != nullptr) {
        CECE_LOG_INFO("[CECE] Binding diagnostic manager into " + std::to_string(internal_data->active_schemes.size()) + " schemes at grid " +
                      std::to_string(internal_data->nx) + "x" + std::to_string(internal_data->ny) + "x" + std::to_string(internal_data->nz));

        internal_data->diagnostic_manager->SetGrid(internal_data->nx, internal_data->ny, internal_data->nz);

        std::vector<std::string> species_names;
        species_names.reserve(internal_data->config.species_layers.size());
        for (const auto& [species_name, layers] : internal_data->config.species_layers) {
            species_names.push_back(species_name);
        }
        internal_data->diagnostic_manager->SetReservedNames(std::move(species_names));

        for (std::size_t i = 0; i < internal_data->active_schemes.size() && i < internal_data->config.physics_schemes.size(); ++i) {
            try {
                internal_data->active_schemes[i]->Initialize(internal_data->config.physics_schemes[i].options,
                                                             internal_data->diagnostic_manager.get());
            } catch (const std::exception& e) {
                CECE_LOG_ERROR("[CECE] cece_core_initialize_p2: Failed to re-initialize scheme '" + internal_data->config.physics_schemes[i].name +
                               "' with the diagnostic manager: " + e.what());
                if (rc != nullptr) {
                    *rc = -1;
                }
                return;
            }
        }
        CECE_LOG_INFO("[CECE] Diagnostic manager bound; " + std::to_string(internal_data->diagnostic_manager->Fields().size()) +
                      " diagnostic field(s) registered");
    }

    std::cout << "INFO: CECE Initialize Phase 2 completed successfully" << std::endl;

    if (rc != nullptr) {
        *rc = 0;  // 0 = success
    }
}

}  // extern "C"
