#ifndef CECE_DUST_FORTRAN_HPP
#define CECE_DUST_FORTRAN_HPP

#include "cece/physics_scheme.hpp"

namespace cece {

/**
 * @class DustFortranScheme
 * @brief Fortran bridge implementation of the Ginoux dust emission scheme.
 */
class DustFortranScheme : public BasePhysicsScheme {
   public:
    DustFortranScheme() = default;
    ~DustFortranScheme() override = default;

    void Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;
};

}  // namespace cece

#endif  // CECE_DUST_FORTRAN_HPP
