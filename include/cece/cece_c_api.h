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

/** The C API Functions that we expect libraries using cece_core to call */

/** Part one of cece_core initialization */
void cece_core_initialize_p1(void** data_ptr_ptr, int* rc);
void cece_core_initialize_p2(void* data_ptr, int* nx, int* ny, int* nz, int* rc);
void cece_core_get_species_count(void* data_ptr, int* count, int* rc);
void cece_core_get_species_name(void* data_ptr, int* index, char* name, int* name_len, int* rc);
void cece_core_bind_fields(void* data_ptr, void** field_ptrs, int* num_fields, int* rc);
void cece_core_finalize(void* data_ptr, int* rc);

void cece_core_realize(void* data_ptr, int* rc);

void cece_set_config_file_path(const char* config_path, int path_len);

void cece_run_log_setup(const char* config_path, int path_len);
void cece_core_run(void* data_ptr, int hour, int day_of_week, int* rc);

#ifdef __cplusplus
}
#endif

#endif /* CECE_C_API_H */
