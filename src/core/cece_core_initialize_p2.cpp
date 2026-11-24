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
#include <set>
#include <string>

#include "cece/cece_data_ingestor.hpp"
#include "cece/cece_internal.hpp"
#include "cece/cece_logger.hpp"

using namespace std::literals::string_literals;

extern "C" {

/** Set the rank-local band before Phase 2 allocates diagnostic fields. */
void cece_core_set_band_geometry(void* data_ptr, int ny_local, int j0, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }
    if (data_ptr == nullptr || ny_local < 0 || j0 < 0) {
        CECE_LOG_ERROR("[CECE] cece_core_set_band_geometry - invalid data pointer or band geometry");
        if (rc != nullptr) *rc = -1;
        return;
    }
    auto* data = static_cast<cece::CeceInternalData*>(data_ptr);
    data->ny_local = ny_local;
    data->j0 = j0;
    data->band_geometry_set = true;
}

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

    CECE_LOG_INFO("INFO: CECE Initialize Phase 2 (IPDv00p2) - Field binding and data stream setup\n");

    // 1. Validate that Phase 1 completed successfully
    if (data_ptr == nullptr) {
        // clang-format off
      CECE_LOG_ERROR("ERROR in cece_core_initialize_p2: Internal data pointer is null - "
                     "Phase 1 must complete before Phase 2\n");
        // clang-format on
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);
    CECE_LOG_INFO("INFO: Internal data structure validated\n");

    // 2. Store grid dimensions passed from Fortran
    if (nx == nullptr || ny == nullptr || nz == nullptr) {
        CECE_LOG_ERROR("ERROR in cece_core_initialize_p2: Grid dimension pointers are null"s);
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    // Validate grid dimensions are positive
    if (*nx <= 0 || *ny <= 0 || *nz <= 0) {
        CECE_LOG_ERROR("ERROR in cece_core_initialize_p2: Invalid grid dimensions - nx="s + std::to_string(*nx) + ", ny="s + std::to_string(*ny) +
                       ", nz="s + std::to_string(*nz) + "\n"s);
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    internal_data->nx = *nx;
    internal_data->ny = *ny;
    internal_data->nz = *nz;
    if (!internal_data->band_geometry_set) {
        internal_data->ny_local = internal_data->ny;
        internal_data->j0 = 0;
    } else if (internal_data->ny_local > internal_data->ny || internal_data->j0 > internal_data->ny - internal_data->ny_local) {
        CECE_LOG_ERROR("ERROR in cece_core_initialize_p2: Invalid local band (j0="s + std::to_string(internal_data->j0) +
                       ", ny_local=" + std::to_string(internal_data->ny_local) + ") for ny=" + std::to_string(internal_data->ny));
        if (rc != nullptr) *rc = -1;
        return;
    }

    CECE_LOG_INFO("INFO: Grid dimensions received: nx="s + std::to_string(internal_data->nx) + ", ny="s + std::to_string(internal_data->ny) +
                  ", nz="s + std::to_string(internal_data->nz) + "\n"s);

    // 3. Initialize data ingestor if streams are configured
    if (!internal_data->config.cece_data.streams.empty()) {
        CECE_LOG_INFO("INFO: cece_data streams configured - initializing data ingestor\n");
        CECE_LOG_INFO("INFO: Number of streams: "s + std::to_string(internal_data->config.cece_data.streams.size()) + "\n"s);
    } else {
        CECE_LOG_INFO("INFO: No TIDE streams configured - skipping driver initialization\n");
    }

    // 4. Allocate default mask (all 1.0)
    CECE_LOG_INFO("INFO: Allocating default mask\n");
    try {
        internal_data->default_mask = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>(
            "default_mask", internal_data->nx, internal_data->ny, internal_data->nz);

        // Initialize mask to 1.0 on device
        Kokkos::deep_copy(internal_data->default_mask, 1.0);

        CECE_LOG_INFO("INFO: Default mask allocated and initialized to 1.0\n");
    } catch (const std::exception& e) {
        CECE_LOG_ERROR("ERROR: Failed to allocate default mask: "s + e.what() + "\n"s);
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    // 5. Cache field metadata for efficient runtime queries
    CECE_LOG_INFO("INFO: Caching field metadata");

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

    CECE_LOG_INFO("INFO: Cached "s + std::to_string(internal_data->esmf_fields.size()) + " import field mappings\n"s);

    // 6. Bind the diagnostic manager into every scheme at the realized grid.
    // Allocate the pending declarations on this rank's band and refresh the
    // schemes' handles without repeating initialization.
    if (internal_data->diagnostic_manager != nullptr) {
        CECE_LOG_INFO("[CECE] Binding diagnostic manager into " + std::to_string(internal_data->active_schemes.size()) + " schemes at grid " +
                      std::to_string(internal_data->nx) + "x" + std::to_string(internal_data->ny_local) + "x" + std::to_string(internal_data->nz) +
                      " (local band; global ny=" + std::to_string(internal_data->ny) + ")");

        internal_data->diagnostic_manager->SetGrid(internal_data->nx, internal_data->ny_local, internal_data->nz);

        for (std::size_t i = 0; i < internal_data->active_schemes.size() && i < internal_data->config.physics_schemes.size(); ++i) {
            try {
                internal_data->active_schemes[i]->BindDiagnostics(internal_data->diagnostic_manager.get());
            } catch (const std::exception& e) {
                CECE_LOG_ERROR("[CECE] cece_core_initialize_p2: Failed to bind diagnostics for scheme '" +
                               internal_data->config.physics_schemes[i].name + "': " + e.what());
                if (rc != nullptr) {
                    *rc = -1;
                }
                return;
            }
        }
        CECE_LOG_INFO("[CECE] Diagnostic manager bound; " + std::to_string(internal_data->diagnostic_manager->Fields().size()) +
                      " diagnostic field(s) registered");
    }

    CECE_LOG_INFO("INFO: CECE Initialize Phase 2 completed successfully");

    if (rc != nullptr) {
        *rc = 0;  // 0 = success
    }
}

}  // extern "C"
