#ifndef CECE_VOLCANO_HPP
#define CECE_VOLCANO_HPP

#include "cece/physics_scheme.hpp"

namespace cece {

/**
 * @class VolcanoScheme
 * @brief Native C++ implementation of the volcanic emission scheme.
 */
class VolcanoScheme : public BasePhysicsScheme {
   public:
    VolcanoScheme() = default;
    ~VolcanoScheme() override = default;

    void Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;

   private:
    int target_i_ = 1;
    int target_j_ = 1;
    double volcano_sulf_ = 1.0;
    double volcano_elv_ = 600.0;
    double volcano_cld_ = 2000.0;
};

}  // namespace cece

#endif  // CECE_VOLCANO_HPP
