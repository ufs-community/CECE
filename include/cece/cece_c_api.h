#ifndef CECE_C_API_H
#define CECE_C_API_H

/**
 * @file cece_c_api.h
 * @brief C-linkage API for querying CECE meteorology registry.
 *
 * Provides functions for the Fortran NUOPC cap to dynamically discover
 * meteorology variable names and their aliases from the parsed configuration.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Returns the number of meteorology variables in the registry.
 */
size_t cece_get_met_registry_count(void);

/**
 * @brief Returns the internal name of the meteorology variable at index @p idx.
 * @param idx Zero-based index into the registry.
 * @return Pointer to a null-terminated string, or NULL if @p idx is out of range.
 *         The pointer is valid for the lifetime of the configuration.
 */
const char* cece_get_met_registry_internal_name(size_t idx);

/**
 * @brief Returns the number of aliases for the meteorology variable at index @p idx.
 * @param idx Zero-based index into the registry.
 * @return Number of aliases, or 0 if @p idx is out of range.
 */
size_t cece_get_met_registry_alias_count(size_t idx);

/**
 * @brief Returns the alias at @p alias_idx for the meteorology variable at @p idx.
 * @param idx Zero-based index into the registry.
 * @param alias_idx Zero-based index into the alias list.
 * @return Pointer to a null-terminated string, or NULL if out of range.
 *         The pointer is valid for the lifetime of the configuration.
 */
const char* cece_get_met_registry_alias(size_t idx, size_t alias_idx);

/** @defgroup cece_core_c The C API Functions that we expect libraries using cece_core to call
 * @{
 */

/**
 * @brief Initialize Phase 1 (IPDv00p1) implementation for CECE.
 *
 * This function performs core initialization that does not depend on other
 * components or field realization. It sets up the internal data structures
 * and prepares CECE for field binding in Phase 2.
 *
 * Operations Performed:
 * 1. Initialize Kokkos if not already initialized (track ownership)
 * 2. Parse cece_config.yaml configuration file
 * 3. Allocate CeceInternalData structure for persistent state
 * 4. Initialize PhysicsFactory and instantiate all configured physics schemes
 * 5. Initialize StackingEngine with configuration
 * 6. Initialize DiagnosticManager
 *
 * @param data_ptr_ptr Pointer to void* that will receive the CeceInternalData pointer
 * @param rc Return code (0 = success, non-zero = error)
 *
 * @note The data_ptr_ptr is a double pointer because we need to allocate and return
 *       a new pointer to the caller (Fortran cap).
 * @note Kokkos initialization is tracked - if CECE initializes Kokkos, it will
 *       finalize it. If Kokkos was already initialized, CECE will not finalize it.
 * @note All physics schemes are instantiated and their Initialize methods are called.
 * @note Configuration parsing errors result in rc = -1 and early return.
 */
void cece_core_initialize_p1(void** data_ptr_ptr, int* rc);

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
 */
void cece_core_initialize_p2(void* data_ptr, int* nx, int* ny, int* nz, int* rc);
/**
 * @brief Get the number of species from the configuration.
 *
 * @param data_ptr Pointer to CeceInternalData
 * @param count Output: number of species
 * @param rc Return code (0 = success, non-zero = error)
 */
void cece_core_get_species_count(void* data_ptr, int* count, int* rc);
/**
 * @brief Get the name of a species by index.
 *
 * @param data_ptr Pointer to CeceInternalData
 * @param index Zero-based index of the species
 * @param name Output: species name (C string, null-terminated)
 * @param name_len Output: length of the species name (excluding null terminator)
 * @param rc Return code (0 = success, non-zero = error)
 */
void cece_core_get_species_name(void* data_ptr, int* index, char* name, int* name_len, int* rc);

/**
 * @brief Bind field data pointers to internal data.
 *
 * Stores the field data pointers passed from Fortran in the CeceInternalData
 * structure for later access during the Run phase.
 *
 * @param data_ptr Pointer to CeceInternalData
 * @param field_ptrs Array of field data pointers (one per species)
 * @param num_fields Number of fields in the array
 * @param rc Return code (0 = success, non-zero = error)
 */
void cece_core_bind_fields(void* data_ptr, void** field_ptrs, int* num_fields, int* rc);

/**
 * @brief CECE Finalize phase implementation.
 *
 * Releases all resources in the correct order to avoid use-after-free:
 * Kokkos views inside CeceInternalData are destroyed (via delete) before
 * Kokkos::finalize() is called, satisfying the Kokkos teardown contract.
 *
 * @param data_ptr Pointer to CeceInternalData structure.
 * @param rc Return code (0 on success).
 */
void cece_core_finalize(void* data_ptr, int* rc);

/**
 * @brief 2-parameter wrapper for cece_core_realize (called by Fortran cap).
 *
 * This one-line wrapper is used by the Fortran cap to call the realize phase.
 *
 * @param data_ptr Pointer to CeceInternalData structure
 * @param rc Return code (0 = success, non-zero = error)
 *
 */
void cece_core_realize(void* data_ptr, int* rc);

/**
 * @brief C interface to set the configuration file path.
 *
 * This function allows the Fortran NUOPC cap to specify a custom configuration
 * file path. Thread-safe via mutex.
 *
 * @param config_path C-string path to the configuration file (null-terminated)
 * @param path_len Length of the config_path string (for Fortran compatibility)
 */
void cece_set_config_file_path(const char* config_path, int path_len);

/**
 * @brief Configure run logging and print the startup banner.
 *
 * Safe to call once early in initialization. Determines MPI rank/size from
 * MPI_COMM_WORLD (if MPI is initialized) and reads logging/grid/stream options
 * from the YAML configuration file.
 *
 * @param config_path Path to the YAML control file.
 * @param path_len    Length of config_path (Fortran-friendly); pass 0 to use
 *                    the C string length.
 */
void cece_run_log_setup(const char* config_path, int path_len);

/**
 * @brief CECE Run phase.
 * @param data_ptr    Pointer to CeceInternalData.
 * @param hour        Hour of day (0-23), extracted by the driver from the simulation clock.
 * @param day_of_week Day of week (0=Sunday..6=Saturday).
 * @param rc          0 on success, 1 on simulation complete, -1 on failure.
 */
void cece_core_run(void* data_ptr, int hour, int day_of_week, int* rc);
/** @}
 */

#ifdef __cplusplus
}
#endif

#endif /* CECE_C_API_H */
