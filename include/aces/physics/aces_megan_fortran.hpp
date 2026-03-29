#ifndef ACES_MEGAN_FORTRAN_HPP
#define ACES_MEGAN_FORTRAN_HPP

#include "aces/physics_scheme.hpp"

namespace aces {

/**
 * @class MeganFortranScheme
 * @brief Fortran bridge implementation of the MEGAN biogenics emission scheme.
 */
class MeganFortranScheme : public BasePhysicsScheme {
   public:
    MeganFortranScheme() = default;
    ~MeganFortranScheme() override = default;

    void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) override;
    void Run(AcesImportState& import_state, AcesExportState& export_state) override;
};

}  // namespace aces

#endif  // ACES_MEGAN_FORTRAN_HPP
