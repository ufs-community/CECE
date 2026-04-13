#ifndef CECE_MEGAN_FORTRAN_HPP
#define CECE_MEGAN_FORTRAN_HPP

#include "cece/physics_scheme.hpp"

namespace cece {

/**
 * @class MeganFortranScheme
 * @brief Fortran bridge implementation of the MEGAN biogenics emission scheme.
 */
class MeganFortranScheme : public BasePhysicsScheme {
   public:
    MeganFortranScheme() = default;
    ~MeganFortranScheme() override = default;

    void Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;
};

}  // namespace cece

#endif  // CECE_MEGAN_FORTRAN_HPP
