#ifndef ACES_SOIL_NOX_FORTRAN_HPP
#define ACES_SOIL_NOX_FORTRAN_HPP

#include "aces/physics_scheme.hpp"

namespace aces {

/**
 * @class SoilNoxFortranScheme
 * @brief Fortran bridge implementation of the Soil NOx emission scheme.
 */
class SoilNoxFortranScheme : public BasePhysicsScheme {
   public:
    SoilNoxFortranScheme() = default;
    ~SoilNoxFortranScheme() override = default;

    void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) override;
    void Run(AcesImportState& import_state, AcesExportState& export_state) override;
};

}  // namespace aces

#endif  // ACES_SOIL_NOX_FORTRAN_HPP
