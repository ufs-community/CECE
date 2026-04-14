#ifndef CECE_CCPP_API_H
#define CECE_CCPP_API_H

/**
 * @file cece_ccpp_api.h
 * @brief C-linkage API for the CCPP (Common Community Physics Package) interface.
 *
 * Provides per-scheme initialization, execution, finalization, and field
 * marshalling functions that Fortran CCPP driver schemes call via
 * iso_c_binding. These operate on the existing CeceInternalData structure
 * but provide per-scheme granularity needed by the CCPP framework.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One-time core initialization: Kokkos, config parse, PhysicsFactory,
 *        StackingEngine, DiagnosticManager. Returns opaque data pointer.
 * @param data_ptr   [out] Opaque pointer to the allocated CeceInternalData.
 * @param config_path [in] Path to the CECE YAML configuration file.
 * @param config_path_len [in] Length of @p config_path (excluding null terminator).
 * @param nx         [in] Grid dimension in x.
 * @param ny         [in] Grid dimension in y.
 * @param nz         [in] Grid dimension in z.
 * @param rc         [out] Return code; 0 on success, non-zero on failure.
 */
void cece_ccpp_core_init(void** data_ptr, const char* config_path,
                         int config_path_len, int nx, int ny, int nz, int* rc);

/**
 * @brief Initialize a single named physics scheme within the core.
 *        Finds the scheme in active_schemes and verifies it is ready.
 * @param data_ptr       [in] Opaque pointer to CeceInternalData.
 * @param scheme_name    [in] Name of the physics scheme to initialize.
 * @param scheme_name_len [in] Length of @p scheme_name.
 * @param nx             [in] Grid dimension in x.
 * @param ny             [in] Grid dimension in y.
 * @param nz             [in] Grid dimension in z.
 * @param rc             [out] Return code; 0 on success, non-zero on failure.
 */
void cece_ccpp_scheme_init(void* data_ptr, const char* scheme_name,
                           int scheme_name_len, int nx, int ny, int nz, int* rc);

/**
 * @brief Execute a single named physics scheme.
 * @param data_ptr       [in] Opaque pointer to CeceInternalData.
 * @param scheme_name    [in] Name of the physics scheme to execute.
 * @param scheme_name_len [in] Length of @p scheme_name.
 * @param rc             [out] Return code; 0 on success, non-zero on failure.
 */
void cece_ccpp_scheme_run(void* data_ptr, const char* scheme_name,
                          int scheme_name_len, int* rc);

/**
 * @brief Finalize a single named physics scheme.
 * @param data_ptr       [in] Opaque pointer to CeceInternalData.
 * @param scheme_name    [in] Name of the physics scheme to finalize.
 * @param scheme_name_len [in] Length of @p scheme_name.
 * @param rc             [out] Return code; 0 on success, non-zero on failure.
 */
void cece_ccpp_scheme_finalize(void* data_ptr, const char* scheme_name,
                               int scheme_name_len, int* rc);

/**
 * @brief Copy a Fortran array into CeceImportState as a DualView3D.
 *        Performs host-to-device sync after copy.
 * @param data_ptr   [in] Opaque pointer to CeceInternalData.
 * @param field_name [in] Name of the import field.
 * @param name_len   [in] Length of @p field_name.
 * @param field_data [in] Pointer to the source Fortran array (contiguous, LayoutLeft).
 * @param nx         [in] First dimension extent.
 * @param ny         [in] Second dimension extent.
 * @param nz         [in] Third dimension extent.
 * @param rc         [out] Return code; 0 on success, non-zero on failure.
 */
void cece_ccpp_set_import_field(void* data_ptr, const char* field_name,
                                int name_len, const double* field_data,
                                int nx, int ny, int nz, int* rc);

/**
 * @brief Copy a CeceExportState DualView3D back into a Fortran array.
 *        Performs device-to-host sync before copy.
 * @param data_ptr   [in] Opaque pointer to CeceInternalData.
 * @param field_name [in] Name of the export field.
 * @param name_len   [in] Length of @p field_name.
 * @param field_data [out] Pointer to the destination Fortran array (contiguous, LayoutLeft).
 * @param nx         [in] First dimension extent.
 * @param ny         [in] Second dimension extent.
 * @param nz         [in] Third dimension extent.
 * @param rc         [out] Return code; 0 on success, non-zero on failure.
 */
void cece_ccpp_get_export_field(void* data_ptr, const char* field_name,
                                int name_len, double* field_data,
                                int nx, int ny, int nz, int* rc);

/**
 * @brief Execute the StackingEngine independently.
 * @param data_ptr    [in] Opaque pointer to CeceInternalData.
 * @param hour        [in] Current hour for temporal scaling.
 * @param day_of_week [in] Current day of week for temporal scaling.
 * @param rc          [out] Return code; 0 on success, non-zero on failure.
 */
void cece_ccpp_run_stacking(void* data_ptr, int hour, int day_of_week, int* rc);

/**
 * @brief Full core finalize: physics schemes, StackingEngine, optionally Kokkos.
 * @param data_ptr [in] Opaque pointer to CeceInternalData.
 * @param rc       [out] Return code; 0 on success, non-zero on failure.
 */
void cece_ccpp_core_finalize(void* data_ptr, int* rc);

/**
 * @brief Sync all import fields from host to device.
 * @param data_ptr [in] Opaque pointer to CeceInternalData.
 * @param rc       [out] Return code; 0 on success, non-zero on failure.
 */
void cece_ccpp_sync_import_to_device(void* data_ptr, int* rc);

/**
 * @brief Sync all export fields from device to host and fence.
 * @param data_ptr [in] Opaque pointer to CeceInternalData.
 * @param rc       [out] Return code; 0 on success, non-zero on failure.
 */
void cece_ccpp_sync_export_to_host(void* data_ptr, int* rc);

#ifdef __cplusplus
}
#endif

#endif /* CECE_CCPP_API_H */
