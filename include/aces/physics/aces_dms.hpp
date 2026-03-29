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

   private:
    double sc_c0_, sc_c1_, sc_c2_, sc_c3_;
    double kw_c0_, kw_c1_;
};

}  // namespace aces

#endif  // ACES_DMS_HPP
