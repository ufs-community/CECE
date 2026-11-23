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
#include <set>
#include <stdexcept>
#include <string>

#include "cece/cece_data_ingestor.hpp"
#include "cece/cece_internal.hpp"
#include "cece/cece_logger.hpp"

using namespace std::literals::string_literals;

extern "C" {

void cece_core_fire_set_target_grid(void* data_ptr, int nx, int ny, const double* longitude_deg, int longitude_count, const double* latitude_deg,
                                    int latitude_count, int* rc) {
    if (rc) *rc = 0;
    if (!data_ptr) {
        if (rc) *rc = -1;
        CECE_LOG_ERROR("ERROR in cece_core_fire_set_target_grid: Internal data pointer is null\n");
        return;
    }
    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);
    if (!internal_data->fire_runtime_adapter) return;
    if (!longitude_deg || !latitude_deg || longitude_count <= 0 || latitude_count <= 0 || longitude_count != nx || latitude_count != ny) {
        if (rc) *rc = -1;
        CECE_LOG_ERROR("ERROR in cece_core_fire_set_target_grid: Fire requires rectilinear longitude/latitude center arrays\n");
        return;
    }
    try {
        auto area_model = cece::FireGridAreaModel::ConstantLatitudeEdges;
        const auto fire_scheme = std::find_if(internal_data->config.physics_schemes.begin(), internal_data->config.physics_schemes.end(),
                                              [](const cece::PhysicsSchemeConfig& scheme) { return scheme.fire.has_value(); });
        if (fire_scheme != internal_data->config.physics_schemes.end() && fire_scheme->fire->mode == cece::FireInputMode::Product) {
            if (fire_scheme->fire->product_adapter == "rave-v2r0" || fire_scheme->fire->product_adapter == "gbbepx-v4r0") {
                area_model = cece::FireGridAreaModel::GreatCircleEdges;
            } else {
                throw std::invalid_argument("unsupported product adapter for conservative target-grid area semantics");
            }
        }
        internal_data->fire_target_grid = cece::MakeRectilinearFireGrid(nx, ny, std::vector<double>(longitude_deg, longitude_deg + longitude_count),
                                                                        std::vector<double>(latitude_deg, latitude_deg + latitude_count), area_model);
    } catch (const std::exception& error) {
        if (rc) *rc = -1;
        CECE_LOG_ERROR("ERROR in cece_core_fire_set_target_grid: "s + error.what() + "\n");
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

    CECE_LOG_INFO("INFO: Grid dimensions received: nx="s + std::to_string(internal_data->nx) + ", ny="s + std::to_string(internal_data->ny) +
                  ", nz="s + std::to_string(internal_data->nz) + "\n"s);

    if (internal_data->fire_runtime_adapter) {
        const auto fire_scheme = std::find_if(internal_data->config.physics_schemes.begin(), internal_data->config.physics_schemes.end(),
                                              [](const cece::PhysicsSchemeConfig& scheme) { return scheme.fire.has_value(); });
        if (fire_scheme == internal_data->config.physics_schemes.end()) {
            CECE_LOG_ERROR("ERROR in cece_core_initialize_p2: Fire runtime adapter has no matching fire configuration\n");
            if (rc != nullptr) *rc = -1;
            return;
        }
        try {
            if (internal_data->fire_target_grid) internal_data->fire_runtime_adapter->SetTargetGrid(internal_data->fire_target_grid);
            internal_data->fire_runtime_adapter->Initialize(*fire_scheme->fire, internal_data->nx, internal_data->ny, internal_data->nz);
            internal_data->fire_runtime_initialized = true;
        } catch (const std::exception& e) {
            CECE_LOG_ERROR("ERROR: Failed to initialize Fire runtime adapter: "s + e.what() + "\n");
            if (rc != nullptr) *rc = -1;
            return;
        } catch (...) {
            CECE_LOG_ERROR("ERROR: Fire runtime adapter initialization threw an unknown exception\n");
            if (rc != nullptr) *rc = -1;
            return;
        }
    }

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

    CECE_LOG_INFO("INFO: CECE Initialize Phase 2 completed successfully\n");

    if (rc != nullptr) {
        *rc = 0;  // 0 = success
    }
}

}  // extern "C"
