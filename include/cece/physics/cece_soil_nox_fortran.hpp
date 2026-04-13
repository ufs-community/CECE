#ifndef CECE_SOIL_NOX_FORTRAN_HPP
#define CECE_SOIL_NOX_FORTRAN_HPP

#include "cece/physics_scheme.hpp"

namespace cece {

/**
 * @class SoilNoxFortranScheme
 * @brief Fortran bridge implementation of the Soil NOx emission scheme.
 */
class SoilNoxFortranScheme : public BasePhysicsScheme {
   public:
    SoilNoxFortranScheme() = default;
    ~SoilNoxFortranScheme() override = default;

    void Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;
};

}  // namespace cece

#endif  // CECE_SOIL_NOX_FORTRAN_HPP
