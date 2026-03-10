#ifndef ACES_DUST_HPP
#define ACES_DUST_HPP

#include "aces/physics_scheme.hpp"

namespace aces {

/**
 * @class DustScheme
 * @brief Native C++ implementation of the Ginoux dust emission scheme.
 */
class DustScheme : public BasePhysicsScheme {
   public:
    DustScheme() = default;
    ~DustScheme() override = default;

    void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) override;
    void Run(AcesImportState& import_state, AcesExportState& export_state) override;

   private:
    double u_ts0_ = 0.0;
    double ch_dust_ = 9.375e-10;
};

}  // namespace aces

#endif  // ACES_DUST_HPP
