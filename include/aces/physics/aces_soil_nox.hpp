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
};

}  // namespace aces

#endif  // ACES_SOIL_NOX_HPP
