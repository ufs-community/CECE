#ifndef ACES_VOLCANO_HPP
#define ACES_VOLCANO_HPP

#include "aces/physics_scheme.hpp"

namespace aces {

/**
 * @class VolcanoScheme
 * @brief Native C++ implementation of the volcanic emission scheme.
 */
class VolcanoScheme : public BasePhysicsScheme {
   public:
    VolcanoScheme() = default;
    ~VolcanoScheme() override = default;

    void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) override;
    void Run(AcesImportState& import_state, AcesExportState& export_state) override;
};

}  // namespace aces

#endif  // ACES_VOLCANO_HPP
