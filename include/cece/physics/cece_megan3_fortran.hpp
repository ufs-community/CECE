#ifndef CECE_MEGAN3_FORTRAN_HPP
#define CECE_MEGAN3_FORTRAN_HPP

/**
 * @file cece_megan3_fortran.hpp
 * @brief Fortran bridge implementation of the full MEGAN3 biogenic emission scheme.
 *
 * Declares Megan3FortranScheme which extends BasePhysicsScheme and delegates
 * the 19-emission-class computation, canopy model, gamma factors, and speciation
 * to a Fortran kernel via the C-Fortran bridge. The C++ side handles YAML
 * speciation configuration loading and passes parsed data to Fortran as flat arrays.
 *
 * Registered as "megan3_fortran" via PhysicsRegistration (guarded by CECE_HAS_FORTRAN).
 * The existing "megan_fortran" scheme remains unchanged for backward compatibility.
 */

#include "cece/physics_scheme.hpp"
#include "cece/physics/cece_speciation_config.hpp"

#include <vector>

namespace cece {

/**
 * @class Megan3FortranScheme
 * @brief Fortran bridge implementation of the full MEGAN3 biogenic emission scheme.
 *
 * Uses the C++ SpeciationConfigLoader to parse YAML configuration, then passes
 * the parsed speciation data (conversion factors, class indices, mechanism indices,
 * molecular weights) to the Fortran kernel as flat arrays.
 *
 * The Run method follows the standard Fortran bridge pattern:
 *   1. Sync DualViews to host
 *   2. Extract raw double* pointers
 *   3. Call extern "C" Fortran subroutine run_megan3_fortran
 *   4. Mark modified on host
 *   5. Sync back to device
 */
class Megan3FortranScheme : public BasePhysicsScheme {
   public:
    Megan3FortranScheme() = default;
    ~Megan3FortranScheme() override = default;

    void Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;

   private:
    /// Speciation config loader for parsing YAML files on the C++ side.
    SpeciationConfigLoader config_loader_;

    /// Flattened speciation data arrays for passing to Fortran.
    std::vector<double> scale_factors_;
    std::vector<int> class_indices_;
    std::vector<int> mechanism_indices_;
    std::vector<double> molecular_weights_;

    /// Number of speciation mappings and mechanism species.
    int num_mappings_ = 0;
    int num_mechanism_species_ = 0;

    /// Mechanism species export field names (with "MEGAN_" prefix).
    std::vector<std::string> export_field_names_;
};

}  // namespace cece

#endif  // CECE_MEGAN3_FORTRAN_HPP
