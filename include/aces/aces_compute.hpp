#ifndef ACES_COMPUTE_HPP
#define ACES_COMPUTE_HPP

#include <Kokkos_Core.hpp>
#include <Kokkos_DualView.hpp>
#include <functional>
#include <string>

#include "aces/aces_config.hpp"

namespace aces {

class StackingEngine;

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
 * @brief Interface for resolving fields by name into Kokkos Views.
 * This allows the compute engine to be decoupled from ESMF for testing.
 */
class FieldResolver {
   public:
    virtual ~FieldResolver() = default;
    virtual UnmanagedHostView3D ResolveImport(const std::string& name, int nx, int ny, int nz) = 0;
    virtual UnmanagedHostView3D ResolveExport(const std::string& name, int nx, int ny, int nz) = 0;

    /**
     * @brief Resolves an import field and returns its device-side View.
     */
    virtual Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>
    ResolveImportDevice(const std::string& name, int nx, int ny, int nz) = 0;

    /**
     * @brief Resolves an export field and returns its device-side View.
     */
    virtual Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>
    ResolveExportDevice(const std::string& name, int nx, int ny, int nz) = 0;
};

/**
 * @brief Performs the emission computation for all species defined in the
 * config.
 * @param config The ACES configuration.
 * @param resolver A FieldResolver to retrieve Kokkos Views for import/export
 * fields.
 * @param nx Grid X dimension.
 * @param ny Grid Y dimension.
 * @param nz Grid Z dimension.
 * @param default_mask Persistent 1.0 mask.
 * @param hour Current hour of the day (0-23) for diurnal cycles.
 * @param day_of_week Current day of the week (0-6) for weekly cycles.
 */
void ComputeEmissions(
    const AcesConfig& config, FieldResolver& resolver, int nx, int ny, int nz,
    const Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>& default_mask =
        {},
    int hour = 0, int day_of_week = 0, StackingEngine* engine = nullptr);

}  // namespace aces

#endif  // ACES_COMPUTE_HPP
