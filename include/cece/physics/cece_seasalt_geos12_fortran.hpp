#ifndef CECE_SEASALT_GEOS12_FORTRAN_HPP
#define CECE_SEASALT_GEOS12_FORTRAN_HPP

/**
 * @file cece_seasalt_geos12_fortran.hpp
 * @brief Fortran bridge header for the GEOS-12 sea salt emission scheme.
 *
 * Declares SeaSaltGeos12FortranScheme which extends BasePhysicsScheme and
 * delegates the sea salt mass/number emission computation to the Fortran
 * kernel run_seasalt_geos12_fortran via the C-Fortran bridge. The C++ side
 * loads per-bin size properties from the shared MICM mechanism file and an
 * aerosol map (mechanism_file / speciation_file / speciation_dataset), drives
 * the kernel, and publishes one export field per named size bin
 * (seasalt_mass_<bin>, seasalt_number_<bin>) plus column totals, keeping the
 * grid vertical dimension free of bin indexing. The static per-bin effective
 * radius is exposed as a diagnostic.
 *
 * Registered as "sea_salt_geos12_fortran" (guarded by CECE_HAS_FORTRAN).
 */

#include <string>
#include <vector>

#include "cece/physics_scheme.hpp"

namespace cece {

/**
 * @class SeaSaltGeos12FortranScheme
 * @brief Fortran bridge implementation of the GEOS-12 sea salt emission scheme.
 */
class SeaSaltGeos12FortranScheme : public BasePhysicsScheme {
   public:
    SeaSaltGeos12FortranScheme() = default;
    ~SeaSaltGeos12FortranScheme() override = default;

    void Initialize(const conf::Value& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;

   private:
    int num_species_ = 0;
    bool weibull_flag_ = false;
    double scale_factor_ = 1.0;

    std::vector<std::string> species_names_;    ///< bin identifiers, one per size bin
    std::vector<double> species_density_;       ///< dry particle density [kg/m^3], per bin
    std::vector<double> species_radius_;        ///< effective dry radius [um], per bin (informational)
    std::vector<double> species_lower_radius_;  ///< lower dry radius [um], per bin
    std::vector<double> species_upper_radius_;  ///< upper dry radius [um], per bin
    std::vector<double> species_scale_;         ///< per-bin emission scale from the aerosol map

    // Effective dry radius diagnostic (nx, ny, num_species); only allocated
    // when a diagnostic manager is present.
    DualView3D diag_effective_radius_;
};

}  // namespace cece

#endif  // CECE_SEASALT_GEOS12_FORTRAN_HPP
