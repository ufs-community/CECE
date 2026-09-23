#ifndef CECE_BDSNP_HPP
#define CECE_BDSNP_HPP

#include "cece/physics_scheme.hpp"

namespace cece {

/**
 * @class BdsnpScheme
 * @brief Standalone soil NO physics module implementing the Berkeley-Dalhousie
 * Soil NOx Parameterization (BDSNP) with YL95 fallback.
 *
 * Provides a comprehensive soil-NO model alongside the legacy SoilNoxScheme
 * ("soil_nox"). Supports two algorithms selectable via the `soil_no_method`
 * YAML configuration key:
 *   - "bdsnp" (default): validated effective-input BDSNP calculation with
 *     24-biome weighting, temperature and moisture responses, canopy reduction,
 *     pulse scaling, fertilizer, and deposited nitrogen
 *   - "yl95": Yienger & Levy (1995) temperature and moisture response
 *
 * BDSNP pulse, canopy, fertilizer, and deposited-nitrogen terms are supplied as
 * effective input fields. Persistent pulse and deposited-nitrogen reservoir
 * evolution are outside this stateless scheme.
 *
 * Writes computed soil NO emissions to the export state field
 * "soil_nox_emissions" for consumption by MEGAN3 or other schemes.
 */
class BdsnpScheme : public BasePhysicsScheme {
   public:
    BdsnpScheme() = default;
    ~BdsnpScheme() override = default;

    void Initialize(const conf::Value& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;

   private:
    enum class SoilNoMethod { kBdsnp, kYl95 };

    struct Yl95Parameters {
        double biome_coefficient_wet = 0.5;
        double temperature_limit = 30.0;
        double temperature_exponential_coefficient = 0.103;
        double wetness_coefficient_1 = 5.5;
        double wetness_coefficient_2 = -5.55;
    };

    void RunBdsnp(CeceImportState& import_state, CeceExportState& export_state);
    void RunYl95(CeceImportState& import_state, CeceExportState& export_state);

    SoilNoMethod soil_no_method_ = SoilNoMethod::kBdsnp;
    bool use_soil_temperature_ = false;
    Yl95Parameters yl95_parameters_;
};

}  // namespace cece

#endif  // CECE_BDSNP_HPP
