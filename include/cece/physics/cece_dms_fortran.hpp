#ifndef CECE_DMS_FORTRAN_HPP
#define CECE_DMS_FORTRAN_HPP

#include "cece/physics_scheme.hpp"

namespace cece {

/**
 * @class DMSFortranScheme
 * @brief Fortran bridge implementation of the DMS air-sea flux scheme.
 */
class DMSFortranScheme : public BasePhysicsScheme {
   public:
    DMSFortranScheme() = default;
    ~DMSFortranScheme() override = default;

    void Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;
};

}  // namespace cece

#endif  // CECE_DMS_FORTRAN_HPP
