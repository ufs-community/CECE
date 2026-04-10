#ifndef CECE_SOIL_NOX_HPP
#define CECE_SOIL_NOX_HPP

#include "cece/physics_scheme.hpp"

namespace cece {

/**
 * @class SoilNoxScheme
 * @brief Native C++ implementation of the Soil NOx emission scheme.
 */
class SoilNoxScheme : public BasePhysicsScheme {
   public:
    SoilNoxScheme() = default;
    ~SoilNoxScheme() override = default;

    void Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;

   private:
    double a_biome_wet_ = 0.5;
    double tc_max_ = 30.0;
    double exp_coeff_ = 0.103;
    double wet_c1_ = 5.5;
    double wet_c2_ = -5.55;
};

}  // namespace cece

#endif  // CECE_SOIL_NOX_HPP
