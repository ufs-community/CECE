#ifndef ACES_UTILS_HPP
#define ACES_UTILS_HPP

/**
 * @file aces_utils.hpp
 * @brief Utility functions for Kokkos view wrapping.
 */

#include "aces/aces_state.hpp"

namespace aces {

/**
 * @brief Wraps a raw double pointer into an UnmanagedHostView3D.
 *
 * The memory remains managed by the caller. No copy is performed.
 *
 * @param ptr Raw pointer to the data.
 * @param dim1 Size of the first dimension.
 * @param dim2 Size of the second dimension.
 * @param dim3 Size of the third dimension.
 * @return UnmanagedHostView3D wrapping the provided pointer.
 */
inline UnmanagedHostView3D WrapRawPtr(double* ptr, int dim1, int dim2, int dim3) {
    return UnmanagedHostView3D(ptr, dim1, dim2, dim3);
}

}  // namespace aces

#endif  // ACES_UTILS_HPP
