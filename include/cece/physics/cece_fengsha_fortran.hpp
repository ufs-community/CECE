/**
 * @file cece_fengsha_fortran.hpp
 * @brief Fortran bridge header for the FENGSHA dust emission scheme.
 */
#ifndef CECE_FENGSHA_FORTRAN_HPP
#define CECE_FENGSHA_FORTRAN_HPP

#include "cece/physics_scheme.hpp"

namespace cece {

/**
 * @class FengshaFortranScheme
 * @brief Fortran bridge implementation of the FENGSHA dust emission scheme.
 *
 * Calls the Fortran kernel (fengsha_kernel_mod) via extern "C" linkage,
 * syncing DualViews between host and device as needed.
 */
class FengshaFortranScheme : public BasePhysicsScheme {
   public:
    FengshaFortranScheme() = default;
    ~FengshaFortranScheme() override = default;

    void Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;

   private:
    double alpha_ = 1.0;
    double gamma_ = 1.0;
    double kvhmax_ = 2.45e-4;
    double grav_ = 9.81;
    double drylimit_factor_ = 1.0;
    int num_bins_ = 5;
};

}  // namespace cece

#endif  // CECE_FENGSHA_FORTRAN_HPP
