#ifndef ACES_DMS_FORTRAN_HPP
#define ACES_DMS_FORTRAN_HPP

#include "aces/physics_scheme.hpp"

namespace aces {

/**
 * @class DMSFortranScheme
 * @brief Fortran bridge implementation of the DMS air-sea flux scheme.
 */
class DMSFortranScheme : public BasePhysicsScheme {
   public:
    DMSFortranScheme() = default;
    ~DMSFortranScheme() override = default;

    void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) override;
    void Run(AcesImportState& import_state, AcesExportState& export_state) override;
};

}  // namespace aces

#endif  // ACES_DMS_FORTRAN_HPP
