/**
 * @file cece_ginoux_fortran.hpp
 * @brief Fortran bridge header for the Ginoux (GOCART2G) dust emission scheme.
 */
#ifndef CECE_GINOUX_FORTRAN_HPP
#define CECE_GINOUX_FORTRAN_HPP

#include "cece/physics_scheme.hpp"

namespace cece {

/**
 * @class GinouxFortranScheme
 * @brief Fortran bridge implementation of the Ginoux (GOCART2G) dust emission scheme.
 *
 * Calls the Ginoux Fortran kernel (ginoux_kernel_mod) via extern "C" linkage,
 * syncing DualViews between host and device as needed.
 */
class GinouxFortranScheme : public BasePhysicsScheme {
   public:
    GinouxFortranScheme() = default;
    ~GinouxFortranScheme() override = default;

    void Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;

   private:
    double ch_du_ = 0.8e-9;
    double grav_ = 9.81;
    int num_bins_ = 5;
};

}  // namespace cece

#endif  // CECE_GINOUX_FORTRAN_HPP
