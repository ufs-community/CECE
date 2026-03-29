#ifndef ACES_SOIL_NOX_HPP
#define ACES_SOIL_NOX_HPP

#include "aces/physics_scheme.hpp"

namespace aces {

/**
 * @class SoilNoxScheme
 * @brief Native C++ implementation of the Soil NOx emission scheme.
 */
class SoilNoxScheme : public BasePhysicsScheme {
   public:
    SoilNoxScheme() = default;
    ~SoilNoxScheme() override = default;

    void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) override;
    void Run(AcesImportState& import_state, AcesExportState& export_state) override;

   private:
    double a_biome_wet_ = 0.5;
    double tc_max_ = 30.0;
    double exp_coeff_ = 0.103;
    double wet_c1_ = 5.5;
    double wet_c2_ = -5.55;
};

}  // namespace aces

#endif  // ACES_SOIL_NOX_HPP
