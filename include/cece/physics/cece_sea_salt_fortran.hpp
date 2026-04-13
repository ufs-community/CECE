#ifndef CECE_SEA_SALT_FORTRAN_HPP
#define CECE_SEA_SALT_FORTRAN_HPP

#include "cece/physics_scheme.hpp"

namespace cece {

/**
 * @class SeaSaltFortranScheme
 * @brief Fortran bridge implementation of the Sea Salt emission scheme.
 */
class SeaSaltFortranScheme : public BasePhysicsScheme {
   public:
    SeaSaltFortranScheme() = default;
    ~SeaSaltFortranScheme() override = default;

    void Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;

   private:
    double scaling_factor_ = 1.0e-11;
};

}  // namespace cece

#endif  // CECE_SEA_SALT_FORTRAN_HPP
