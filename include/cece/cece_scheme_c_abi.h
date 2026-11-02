#ifndef CECE_SCHEME_C_ABI_H
#define CECE_SCHEME_C_ABI_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* ABI v1. Null-terminated UTF-8 strings; double host buffers in (column,axis)
 * Fortran order. Buffers are borrowed for the call only. Status 0 = success,
 * -1 = failure. Error text is truncated safely and NUL-terminated when cap>0.
 * Integer handles are never pointers and remain invalid after destruction.
 * Calls are serialized at the ABI boundary; kernels may use Kokkos parallelism.
 */
typedef uint64_t cece_scheme_handle;
int cece_scheme_create(const char* name, const char* options_yaml, int ncol, int nlev,
                       cece_scheme_handle* handle, char* error, int error_capacity);
int cece_scheme_set_field(cece_scheme_handle handle, const char* name, const double* data,
                          size_t count, int extent, char* error, int error_capacity);
int cece_scheme_step(cece_scheme_handle handle, double start_seconds, double dt_seconds,
                     char* error, int error_capacity);
/* Copy-out is transactional: a bad name/size leaves the destination unchanged. */
int cece_scheme_get_field(cece_scheme_handle handle, const char* name, double* data,
                          size_t count, char* error, int error_capacity);
int cece_scheme_output_count(cece_scheme_handle handle, int* count, char* error, int error_capacity);
int cece_scheme_output_name(cece_scheme_handle handle, int index, char* name, int name_capacity,
                            char* error, int error_capacity);
int cece_scheme_destroy(cece_scheme_handle handle, char* error, int error_capacity);
/* Explicit application shutdown, only after all handles are destroyed. Does
 * not finalize a Kokkos runtime supplied by the embedding host. Kokkos cannot
 * be restarted after finalization; invoke this only at final application exit.
 */
int cece_scheme_finalize_runtime(char* error, int error_capacity);
#ifdef __cplusplus
}
#endif
#endif
