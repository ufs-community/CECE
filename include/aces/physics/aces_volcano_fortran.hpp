#ifndef ACES_VOLCANO_FORTRAN_HPP
#define ACES_VOLCANO_FORTRAN_HPP

#include "aces/physics_scheme.hpp"

namespace aces {

/**
 * @class VolcanoFortranScheme
 * @brief Fortran bridge implementation of the volcanic emission scheme.
 */
class VolcanoFortranScheme : public BasePhysicsScheme {
   public:
    VolcanoFortranScheme() = default;
    ~VolcanoFortranScheme() override = default;

    void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) override;
    void Run(AcesImportState& import_state, AcesExportState& export_state) override;
};

}  // namespace aces

#endif  // ACES_VOLCANO_FORTRAN_HPP
