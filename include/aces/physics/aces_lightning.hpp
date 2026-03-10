#ifndef ACES_LIGHTNING_HPP
#define ACES_LIGHTNING_HPP

#include "aces/physics_scheme.hpp"

namespace aces {

/**
 * @class LightningScheme
 * @brief Native C++ implementation of the Lightning NOx emission scheme.
 */
class LightningScheme : public BasePhysicsScheme {
   public:
    LightningScheme() = default;
    ~LightningScheme() override = default;

    void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) override;
    void Run(AcesImportState& import_state, AcesExportState& export_state) override;

   private:
    double yield_land_ = 3.011e26;
    double yield_ocean_ = 1.566e26;
};

}  // namespace aces

#endif  // ACES_LIGHTNING_HPP
