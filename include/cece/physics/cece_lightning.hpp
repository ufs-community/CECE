#ifndef CECE_LIGHTNING_HPP
#define CECE_LIGHTNING_HPP

#include "cece/physics_scheme.hpp"

namespace cece {

/**
 * @class LightningScheme
 * @brief Native C++ implementation of the Lightning NOx emission scheme.
 */
class LightningScheme : public BasePhysicsScheme {
   public:
    LightningScheme() = default;
    ~LightningScheme() override = default;

    void Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;

   private:
    double yield_land_ = 3.011e26;
    double yield_ocean_ = 1.566e26;
    double flash_rate_coeff_ = 3.44e-5;
    double flash_rate_pow_ = 4.9;
};

}  // namespace cece

#endif  // CECE_LIGHTNING_HPP
