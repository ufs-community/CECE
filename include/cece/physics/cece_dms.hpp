#ifndef CECE_DMS_HPP
#define CECE_DMS_HPP

#include "cece/physics_scheme.hpp"

namespace cece {

/**
 * @class DMSScheme
 * @brief Native C++ implementation of the DMS air-sea flux scheme.
 */
class DMSScheme : public BasePhysicsScheme {
   public:
    DMSScheme() = default;
    ~DMSScheme() override = default;

    void Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;

   private:
    double sc_c0_, sc_c1_, sc_c2_, sc_c3_;
    double kw_c0_, kw_c1_;
};

}  // namespace cece

#endif  // CECE_DMS_HPP
