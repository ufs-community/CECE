#ifndef CECE_LIGHTNING_FORTRAN_HPP
#define CECE_LIGHTNING_FORTRAN_HPP

#include "cece/physics_scheme.hpp"

namespace cece {

/**
 * @class LightningFortranScheme
 * @brief Fortran bridge implementation of the Lightning NOx emission scheme.
 */
class LightningFortranScheme : public BasePhysicsScheme {
   public:
    LightningFortranScheme() = default;
    ~LightningFortranScheme() override = default;

    void Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;
};

}  // namespace cece

#endif  // CECE_LIGHTNING_FORTRAN_HPP
