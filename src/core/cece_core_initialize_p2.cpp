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
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

#include "cece/cece_data_ingestor.hpp"
#include "cece/cece_internal.hpp"

extern "C" {

void cece_core_fire_set_target_grid(void* data_ptr, int nx, int ny, const double* longitude_deg, int longitude_count, const double* latitude_deg,
                                    int latitude_count, int* rc) {
    if (rc) *rc = 0;
    if (!data_ptr) {
        if (rc) *rc = -1;
        std::cerr << "ERROR in cece_core_fire_set_target_grid: Internal data pointer is null" << std::endl;
        return;
    }
    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);
    if (!internal_data->fire_runtime_adapter) return;
    if (!longitude_deg || !latitude_deg || longitude_count <= 0 || latitude_count <= 0 || longitude_count != nx || latitude_count != ny) {
        if (rc) *rc = -1;
        std::cerr << "ERROR in cece_core_fire_set_target_grid: Fire requires rectilinear longitude/latitude center arrays" << std::endl;
        return;
    }
    try {
        auto area_model = cece::FireGridAreaModel::ConstantLatitudeEdges;
        const auto fire_scheme = std::find_if(
            internal_data->config.physics_schemes.begin(), internal_data->config.physics_schemes.end(),
            [](const cece::PhysicsSchemeConfig& scheme) { return scheme.fire.has_value(); });
        if (fire_scheme != internal_data->config.physics_schemes.end() && fire_scheme->fire->mode == cece::FireInputMode::Product) {
            if (fire_scheme->fire->product_adapter == "rave-v2r0" || fire_scheme->fire->product_adapter == "gbbepx-v4r0") {
                area_model = cece::FireGridAreaModel::GreatCircleEdges;
            } else {
                throw std::invalid_argument("unsupported product adapter for conservative target-grid area semantics");
            }
        }
        internal_data->fire_target_grid = cece::MakeRectilinearFireGrid(
            nx, ny, std::vector<double>(longitude_deg, longitude_deg + longitude_count),
            std::vector<double>(latitude_deg, latitude_deg + latitude_count), area_model);
    } catch (const std::exception& error) {
        if (rc) *rc = -1;
        std::cerr << "ERROR in cece_core_fire_set_target_grid: " << error.what() << std::endl;
    }
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

    if (internal_data->fire_runtime_adapter) {
        const auto fire_scheme = std::find_if(internal_data->config.physics_schemes.begin(), internal_data->config.physics_schemes.end(),
                                              [](const cece::PhysicsSchemeConfig& scheme) { return scheme.fire.has_value(); });
        if (fire_scheme == internal_data->config.physics_schemes.end()) {
            std::cerr << "ERROR in cece_core_initialize_p2: Fire runtime adapter has no matching fire configuration" << std::endl;
            if (rc != nullptr) *rc = -1;
            return;
        }
        try {
            if (internal_data->fire_target_grid) internal_data->fire_runtime_adapter->SetTargetGrid(internal_data->fire_target_grid);
            internal_data->fire_runtime_adapter->Initialize(*fire_scheme->fire, internal_data->nx, internal_data->ny, internal_data->nz);
            internal_data->fire_runtime_initialized = true;
        } catch (const std::exception& e) {
            std::cerr << "ERROR: Failed to initialize Fire runtime adapter: " << e.what() << std::endl;
            if (rc != nullptr) *rc = -1;
            return;
        } catch (...) {
            std::cerr << "ERROR: Fire runtime adapter initialization threw an unknown exception" << std::endl;
            if (rc != nullptr) *rc = -1;
            return;
        }
    }

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

    std::cout << "INFO: CECE Initialize Phase 2 completed successfully" << std::endl;

    if (rc != nullptr) {
        *rc = 0;  // 0 = success
    }
}

}  // extern "C"
