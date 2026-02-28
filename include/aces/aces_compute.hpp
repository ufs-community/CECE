#ifndef ACES_COMPUTE_HPP
#define ACES_COMPUTE_HPP

#include <functional>
#include <string>

#include "aces/aces_config.hpp"
#include "aces/aces_state.hpp"

namespace aces {

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
 * @brief Performs the emission computation for all species defined in the config.
 * @param config The ACES configuration.
 * @param resolver A FieldResolver to retrieve Kokkos Views for import/export fields.
 * @param nx Grid X dimension.
 * @param ny Grid Y dimension.
 * @param nz Grid Z dimension.
 */
void ComputeEmissions(const AcesConfig& config, FieldResolver& resolver, int nx, int ny, int nz);

}  // namespace aces

#endif  // ACES_COMPUTE_HPP
