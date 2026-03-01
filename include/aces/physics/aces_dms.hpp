#ifndef ACES_DMS_HPP
#define ACES_DMS_HPP

#include "aces/physics_scheme.hpp"

namespace aces {

/**
 * @class DMSScheme
 * @brief Native C++ implementation of the DMS air-sea flux scheme.
 */
class DMSScheme : public BasePhysicsScheme {
   public:
    DMSScheme() = default;
    ~DMSScheme() override = default;

    void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) override;
    void Run(AcesImportState& import_state, AcesExportState& export_state) override;
};

}  // namespace aces

#endif  // ACES_DMS_HPP
