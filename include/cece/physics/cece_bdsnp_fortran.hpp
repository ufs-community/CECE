#ifndef CECE_BDSNP_FORTRAN_HPP
#define CECE_BDSNP_FORTRAN_HPP

/**
 * @file cece_bdsnp_fortran.hpp
 * @brief Fortran bridge implementation of the BDSNP soil NO emission scheme.
 *
 * Declares BdsnpFortranScheme which extends BasePhysicsScheme and delegates
 * the soil NO computation (YL95 or BDSNP algorithm) to a Fortran kernel via
 * the C-Fortran bridge. The C++ side handles YAML configuration parsing and
 * passes parameters to Fortran.
 *
 * Registered as "bdsnp_fortran" via PhysicsRegistration (guarded by CECE_HAS_FORTRAN),
 * replacing the existing "soil_nox_fortran" registration.
 */

#include <string>

#include "cece/physics_scheme.hpp"

namespace cece {

/**
 * @class BdsnpFortranScheme
 * @brief Fortran bridge implementation of the BDSNP soil NO emission scheme.
 *
 * The Run method follows the standard Fortran bridge pattern:
 *   1. Sync DualViews to host
 *   2. Extract raw double* pointers
 *   3. Call extern "C" Fortran subroutine run_bdsnp_fortran
 *   4. Mark "soil_nox_emissions" modified on host
 *   5. Sync back to device
 */
class BdsnpFortranScheme : public BasePhysicsScheme {
   public:
    BdsnpFortranScheme() = default;
    ~BdsnpFortranScheme() override = default;

    void Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;

   private:
    std::string soil_no_method_ = "bdsnp";  // "bdsnp" or "yl95"
};

}  // namespace cece

#endif  // CECE_BDSNP_FORTRAN_HPP
