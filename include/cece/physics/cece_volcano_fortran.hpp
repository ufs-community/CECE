#ifndef CECE_VOLCANO_FORTRAN_HPP
#define CECE_VOLCANO_FORTRAN_HPP

#include "cece/physics_scheme.hpp"

namespace cece {

/**
 * @class VolcanoFortranScheme
 * @brief Fortran bridge implementation of the volcanic emission scheme.
 */
class VolcanoFortranScheme : public BasePhysicsScheme {
   public:
    VolcanoFortranScheme() = default;
    ~VolcanoFortranScheme() override = default;

    void Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;
};

}  // namespace cece

#endif  // CECE_VOLCANO_FORTRAN_HPP
