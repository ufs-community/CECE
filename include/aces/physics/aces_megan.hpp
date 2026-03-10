#ifndef ACES_MEGAN_HPP
#define ACES_MEGAN_HPP

#include "aces/physics_scheme.hpp"

namespace aces {

/**
 * @class MeganScheme
 * @brief Native C++ implementation of the MEGAN biogenics emission scheme.
 */
class MeganScheme : public BasePhysicsScheme {
   public:
    MeganScheme() = default;
    ~MeganScheme() override = default;

    void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) override;
    void Run(AcesImportState& import_state, AcesExportState& export_state) override;

   private:
    double gamma_co2_ = 0.0;
    double beta_ = 0.13;
    double ct1_ = 95.0;
    double ceo_ = 2.0;
    double ldf_ = 1.0;
    double aef_isop_ = 1.0e-9;
};

}  // namespace aces

#endif  // ACES_MEGAN_HPP
