#ifndef CECE_SEASALT_GEOS12_HPP
#define CECE_SEASALT_GEOS12_HPP

/**
 * @file cece_seasalt_geos12.hpp
 * @brief Native Kokkos header for the GEOS-12 sea salt emission scheme.
 *
 * Declares SeaSaltGeos12Scheme, a GPU-portable Kokkos scheme implementing the
 * GEOS-12 sea salt emissions (Jaeglé et al. 2011 SST correction, Gong 2003
 * source function, optional Fan & Toon 2011 Weibull wind correction). Per-bin
 * size properties are loaded from the shared MICM mechanism file plus an aerosol map
 * (mechanism_file / speciation_file / speciation_dataset) and emissions are
 * published as one export field per named size bin (seasalt_mass_<bin>,
 * seasalt_number_<bin>) plus column totals, keeping the grid vertical dimension
 * free of bin indexing. The static per-bin effective radius is a diagnostic.
 *
 * Registered as "sea_salt_geos12" (always compiled; no Fortran dependency).
 */

#include <Kokkos_Core.hpp>
#include <string>
#include <vector>

#include "cece/physics_scheme.hpp"

namespace cece {

/**
 * @class SeaSaltGeos12Scheme
 * @brief GPU-portable Kokkos implementation of the GEOS-12 sea salt scheme.
 */
class SeaSaltGeos12Scheme : public BasePhysicsScheme {
   public:
    SeaSaltGeos12Scheme() = default;
    ~SeaSaltGeos12Scheme() override = default;

    void Initialize(const conf::Value& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;

   private:
    int num_species_ = 0;
    bool weibull_flag_ = false;
    double scale_factor_ = 1.0;

    std::vector<std::string> species_names_;  ///< bin identifiers, one per size bin
    std::vector<double> species_radius_;      ///< effective dry radius [um], per bin (informational)

    // Per-bin size properties mirrored onto the device for the compute kernel.
    Kokkos::View<double*, Kokkos::DefaultExecutionSpace> density_d_;  ///< dry particle density [kg/m^3]
    Kokkos::View<double*, Kokkos::DefaultExecutionSpace> rlow_d_;     ///< lower dry radius [um]
    Kokkos::View<double*, Kokkos::DefaultExecutionSpace> rup_d_;      ///< upper dry radius [um]
    Kokkos::View<double*, Kokkos::DefaultExecutionSpace> scale_d_;    ///< per-bin emission scale from the aerosol map

    // Per-bin scratch buffers (nx, ny, num_species); reallocated when the grid
    // changes. The compute kernel fills these, then each bin slab is scattered
    // into its named export field.
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> mass_scratch_;
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> number_scratch_;

    // Effective dry radius diagnostic (nx, ny, num_species); only allocated
    // when a diagnostic manager is present.
    DualView3D diag_effective_radius_;
};

}  // namespace cece

#endif  // CECE_SEASALT_GEOS12_HPP
