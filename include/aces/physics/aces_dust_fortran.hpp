#ifndef ACES_DUST_FORTRAN_HPP
#define ACES_DUST_FORTRAN_HPP

#include "aces/physics_scheme.hpp"

namespace aces {

/**
 * @class DustFortranScheme
 * @brief Fortran bridge implementation of the Ginoux dust emission scheme.
 */
class DustFortranScheme : public BasePhysicsScheme {
   public:
    DustFortranScheme() = default;
    ~DustFortranScheme() override = default;

    void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) override;
    void Run(AcesImportState& import_state, AcesExportState& export_state) override;
};

}  // namespace aces

#endif  // ACES_DUST_FORTRAN_HPP
