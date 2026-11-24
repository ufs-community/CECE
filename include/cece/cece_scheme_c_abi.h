#ifndef CECE_SCHEME_C_ABI_H
#define CECE_SCHEME_C_ABI_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/** @file
 * @brief Driver-independent C ABI for CECE physics schemes.
 *
 * ABI v1 uses null-terminated UTF-8 strings and double host buffers in (column,axis)
 * Fortran order. Buffers are borrowed for the call only. Status 0 = success,
 * -1 = failure. Error text is truncated safely and NUL-terminated when cap>0.
 * Integer handles are never pointers and remain invalid after destruction.
 * Calls are serialized at the ABI boundary; kernels may use Kokkos parallelism.
 */
/// Opaque integer identifier for one scheme instance; zero is invalid.
typedef uint64_t cece_scheme_handle;
/** Create one scheme instance.
 * @param name Registered scheme name.
 * @param options_yaml YAML options, or NULL for an empty mapping.
 * @param ncol Number of columns.
 * @param nlev Number of levels per column.
 * @param[out] handle Created handle, or zero on failure.
 * @param[out] error Optional diagnostic buffer.
 * @param error_capacity Diagnostic buffer size in bytes.
 * @return Zero on success, -1 on failure.
 */
int cece_scheme_create(const char* name, const char* options_yaml, int ncol, int nlev, cece_scheme_handle* handle, char* error, int error_capacity);
/** Copy an input field to a scheme.
 * @param handle Scheme instance.
 * @param name Canonical input field name.
 * @param data Host array in Fortran (column, axis) order.
 * @param count Number of doubles in data.
 * @param extent Size of the non-column axis.
 * @param[out] error Optional diagnostic buffer.
 * @param error_capacity Diagnostic buffer size in bytes.
 * @return Zero on success, -1 on failure.
 */
int cece_scheme_set_field(cece_scheme_handle handle, const char* name, const double* data, size_t count, int extent, char* error, int error_capacity);
/** Run a contiguous interval.
 * @param handle Scheme instance.
 * @param start_seconds Interval start time in seconds.
 * @param dt_seconds Positive interval duration in seconds.
 * @param[out] error Optional diagnostic buffer.
 * @param error_capacity Diagnostic buffer size in bytes.
 * @return Zero on success, -1 on failure.
 */
int cece_scheme_step(cece_scheme_handle handle, double start_seconds, double dt_seconds, char* error, int error_capacity);
/** Copy an output field to a host array.
 * A bad name or size leaves the destination unchanged.
 * @param handle Scheme instance.
 * @param name Canonical output field name.
 * @param[out] data Destination in Fortran (column, axis) order.
 * @param count Number of doubles in the destination.
 * @param[out] error Optional diagnostic buffer.
 * @param error_capacity Diagnostic buffer size in bytes.
 * @return Zero on success, -1 on failure.
 */
int cece_scheme_get_field(cece_scheme_handle handle, const char* name, double* data, size_t count, char* error, int error_capacity);
/** Count output fields. @param handle Scheme instance. @param[out] count Output count.
 * @param[out] error Optional diagnostic buffer. @param error_capacity Buffer size in bytes.
 * @return Zero on success, -1 on failure. */
int cece_scheme_output_count(cece_scheme_handle handle, int* count, char* error, int error_capacity);
/** Copy an output name by zero-based index.
 * @param handle Scheme instance. @param index Zero-based output index.
 * @param[out] name Destination buffer for the NUL-terminated name.
 * @param name_capacity Destination size in bytes, including the terminator.
 * @param[out] error Optional diagnostic buffer. @param error_capacity Buffer size in bytes.
 * @return Zero on success, -1 on failure. */
int cece_scheme_output_name(cece_scheme_handle handle, int index, char* name, int name_capacity, char* error, int error_capacity);
/** Destroy an instance and invalidate its handle. @param handle Scheme instance.
 * @param[out] error Optional diagnostic buffer. @param error_capacity Buffer size in bytes.
 * @return Zero on success, -1 on failure. */
int cece_scheme_destroy(cece_scheme_handle handle, char* error, int error_capacity);
/** Explicit application shutdown, only after all handles are destroyed. Does
 * not finalize a Kokkos runtime supplied by the embedding host. Kokkos cannot
 * be restarted after finalization; invoke this only at final application exit.
 * @param[out] error Optional diagnostic buffer.
 * @param error_capacity Diagnostic buffer size in bytes.
 * @return Zero on success, -1 on failure.
 */
int cece_scheme_finalize_runtime(char* error, int error_capacity);
#ifdef __cplusplus
}
#endif
#endif
