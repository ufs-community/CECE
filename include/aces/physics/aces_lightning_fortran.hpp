#ifndef ACES_LIGHTNING_FORTRAN_HPP
#define ACES_LIGHTNING_FORTRAN_HPP

#include "aces/physics_scheme.hpp"

namespace aces {

/**
 * @class LightningFortranScheme
 * @brief Fortran bridge implementation of the Lightning NOx emission scheme.
 */
class LightningFortranScheme : public BasePhysicsScheme {
   public:
    LightningFortranScheme() = default;
    ~LightningFortranScheme() override = default;

    void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) override;
    void Run(AcesImportState& import_state, AcesExportState& export_state) override;
};

}  // namespace aces

#endif  // ACES_LIGHTNING_FORTRAN_HPP
