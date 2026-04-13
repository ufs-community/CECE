#ifndef CECE_DUST_HPP
#define CECE_DUST_HPP

#include "cece/physics_scheme.hpp"

namespace cece {

/**
 * @class DustScheme
 * @brief Native C++ implementation of the Ginoux dust emission scheme.
 */
class DustScheme : public BasePhysicsScheme {
   public:
    DustScheme() = default;
    ~DustScheme() override = default;

    void Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;

   private:
    double u_ts0_ = 0.0;
    double ch_dust_ = 9.375e-10;
    double G_const_ = 980.665;
    double RHOA_const_ = 1.25e-3;
};

}  // namespace cece

#endif  // CECE_DUST_HPP
