#ifndef ACES_STATE_HPP
#define ACES_STATE_HPP

/**
 * @file aces_state.hpp
 * @brief Defines the core state structures for ACES.
 */

#include <Kokkos_Core.hpp>

namespace aces {

/**
 * @brief Alias for a 3D Kokkos View with unmanaged memory and Fortran layout.
 *
 * This alias is used for zero-copy wrapping of ESMF field data, which
 * follows the column-major (LayoutLeft) order and resides on the host.
 */
using UnmanagedHostView3D = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace, Kokkos::MemoryTraits<Kokkos::Unmanaged>>;

/**
 * @brief Structure containing all meteorology and base emissions imported from other components.
 */
struct AcesImportState {
    // Meteorology from Atmosphere (e.g., UFS)
    UnmanagedHostView3D temperature;        ///< Air temperature [K]
    UnmanagedHostView3D wind_speed_10m;     ///< Wind speed at 10m [m/s]

    // Base Emissions interpolated by CDEPS
    UnmanagedHostView3D base_anthropogenic_nox; ///< Base NOX emissions [kg/m2/s]
};

/**
 * @brief Structure containing all computed emissions to be exported back to the framework.
 */
struct AcesExportState {
    // Final calculated emissions
    UnmanagedHostView3D total_nox_emissions;    ///< Total calculated NOX emissions [kg/m2/s]
};

} // namespace aces

#endif // ACES_STATE_HPP
