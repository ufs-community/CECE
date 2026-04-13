#ifndef CECE_SEA_SALT_HPP
#define CECE_SEA_SALT_HPP

#include "cece/physics_scheme.hpp"

namespace cece {

/**
 * @class SeaSaltScheme
 * @brief Native C++ implementation of the Sea Salt emission scheme.
 */
class SeaSaltScheme : public BasePhysicsScheme {
   public:
    SeaSaltScheme() = default;
    ~SeaSaltScheme() override = default;

    void Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;

   private:
    double srrc_SALA_ = 0.0;
    double srrc_SALC_ = 0.0;
    double sst_c0_ = 0.329, sst_c1_ = 0.0904, sst_c2_ = -0.00717, sst_c3_ = 0.000207;
    double u_pow_ = 3.41;
};

}  // namespace cece

#endif  // CECE_SEA_SALT_HPP
