#ifndef ACES_STATE_HPP
#define ACES_STATE_HPP

/**
 * @file aces_state.hpp
 * @brief Data structures for managing ACES import and export states.
 */

#include <Kokkos_Core.hpp>
#include <Kokkos_DualView.hpp>

namespace aces {

/**
 * @brief Type alias for unmanaged host views from ESMF.
 * ESMF uses column-major (LayoutLeft) indexing.
 */
using UnmanagedHostView3D = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace, Kokkos::MemoryTraits<Kokkos::Unmanaged>>;

/**
 * @brief Type alias for dual views for host-device synchronization.
 */
using DualView3D = Kokkos::DualView<double***, Kokkos::LayoutLeft>;

/**
 * @brief Structure to hold imported fields from CDEPS.
 */
struct AcesImportState {
    DualView3D base_emissions;
    DualView3D scaling_factor;
    DualView3D mask;
};

/**
 * @brief Structure to hold exported fields for ESMF.
 */
struct AcesExportState {
    DualView3D scaled_emissions;
};

} // namespace aces

#endif // ACES_STATE_HPP
