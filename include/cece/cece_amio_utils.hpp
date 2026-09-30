#ifndef CECE_AMIO_UTILS_HPP
#define CECE_AMIO_UTILS_HPP

/**
 * @file cece_amio_utils.hpp
 * @brief Reading AMIO view payloads as double, honouring dtype and CF packing.
 */

#include <amio/amio.h>

#include <cstddef>
#include <string>
#include <vector>

namespace cece {
namespace detail {

/// Size in bytes of one @p dtype element, or 0 if CECE cannot handle it.
///
/// This duplicates amio::detail::element_size() from AMIO's
/// factory/backend_driver.hpp, which AMIO does not export; its own Zarr, GRIB2
/// and core translation units already carry copies for the same reason. The
/// byte width of AMIO's dtypes is AMIO's fact to state, so this belongs in
/// amio.h.
std::size_t amio_dtype_size(amio_dtype_t dtype);

/// Widen @p n elements of an AMIO view payload to double, applying CF packing
/// (`value = stored * scale + offset`). Handles every AMIO numeric type, so an
/// integer-typed coordinate or packed variable decodes correctly rather than
/// being reinterpreted. Returns false for a dtype CECE does not handle.
bool widen_amio_elements(const void* data, amio_dtype_t dtype, std::size_t n, double scale, double offset, std::vector<double>& out);

/// Read CF packing attributes for @p var; absent attributes leave the identity
/// transform.
void read_cf_packing(amio_dataset_handle dataset, const std::string& var, double& scale, double& offset);

}  // namespace detail
}  // namespace cece

#endif  // CECE_AMIO_UTILS_HPP
