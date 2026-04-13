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

#ifdef __cplusplus
}
#endif

#endif /* CECE_C_API_H */
