#ifndef ACES_STATE_HPP
#define ACES_STATE_HPP

/**
 * @file aces_state.hpp
 * @brief Defines the core state structures for ACES.
 */

#include <Kokkos_Core.hpp>
#include <Kokkos_DualView.hpp>
#include <map>
#include <string>

namespace aces {

/**
 * @brief Alias for a 3D Kokkos View with unmanaged memory and Fortran layout.
 *
 * This alias is used for zero-copy wrapping of ESMF field data, which
 * follows the column-major (LayoutLeft) order and resides on the host.
 */
using UnmanagedHostView3D = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace,
                                         Kokkos::MemoryTraits<Kokkos::Unmanaged>>;

/**
 * @brief Alias for a 3D Kokkos DualView with Fortran layout.
 */
using DualView3D = Kokkos::DualView<double***, Kokkos::LayoutLeft>;

/**
 * @brief Structure containing all meteorology and base emissions imported from other components.
 *
 * Uses a map to allow flexible addition of fields without hardcoding.
 */
struct AcesImportState {
    /// Map of field names to their respective DualViews.
    std::map<std::string, DualView3D> fields;
};

/**
 * @brief Structure containing all computed emissions to be exported back to the framework.
 */
struct AcesExportState {
    /// Map of field names to their respective DualViews.
    std::map<std::string, DualView3D> fields;
};

}  // namespace aces

#endif  // ACES_STATE_HPP
