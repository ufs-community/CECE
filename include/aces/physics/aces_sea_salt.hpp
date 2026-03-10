#ifndef ACES_SEA_SALT_HPP
#define ACES_SEA_SALT_HPP

#include "aces/physics_scheme.hpp"

namespace aces {

/**
 * @class SeaSaltScheme
 * @brief Native C++ implementation of the Sea Salt emission scheme.
 */
class SeaSaltScheme : public BasePhysicsScheme {
   public:
    SeaSaltScheme() = default;
    ~SeaSaltScheme() override = default;

    void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) override;
    void Run(AcesImportState& import_state, AcesExportState& export_state) override;

   private:
    double srrc_SALA_ = 0.0;
    double srrc_SALC_ = 0.0;
    double sst_c0_, sst_c1_, sst_c2_, sst_c3_;
    double u_pow_ = 3.41;
};

}  // namespace aces

#endif  // ACES_SEA_SALT_HPP
