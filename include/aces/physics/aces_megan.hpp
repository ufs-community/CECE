#ifndef ACES_MEGAN_HPP
#define ACES_MEGAN_HPP

#include "aces/physics_scheme.hpp"

namespace aces {

/**
 * @class MeganScheme
 * @brief Native C++ implementation of the MEGAN biogenics emission scheme.
 */
class MeganScheme : public BasePhysicsScheme {
   public:
    MeganScheme() = default;
    ~MeganScheme() override = default;

    void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) override;
    void Run(AcesImportState& import_state, AcesExportState& export_state) override;
};

}  // namespace aces

#endif  // ACES_MEGAN_HPP
