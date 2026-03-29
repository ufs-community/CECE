#ifndef ACES_SEA_SALT_FORTRAN_HPP
#define ACES_SEA_SALT_FORTRAN_HPP

#include "aces/physics_scheme.hpp"

namespace aces {

/**
 * @class SeaSaltFortranScheme
 * @brief Fortran bridge implementation of the Sea Salt emission scheme.
 */
class SeaSaltFortranScheme : public BasePhysicsScheme {
   public:
    SeaSaltFortranScheme() = default;
    ~SeaSaltFortranScheme() override = default;

    void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) override;
    void Run(AcesImportState& import_state, AcesExportState& export_state) override;

   private:
    double scaling_factor_ = 1.0e-11;
};

}  // namespace aces

#endif  // ACES_SEA_SALT_FORTRAN_HPP
