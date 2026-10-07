#ifndef CECE_MEGAN3_HPP
#define CECE_MEGAN3_HPP

/**
 * @file cece_megan3.hpp
 * @brief MEGAN3-class biogenic emissions and chemical mechanism speciation.
 *
 * Computes 19 emission classes using bulk activity factors and chemical
 * mechanism speciation. Multilayer canopy helpers are not yet wired into this
 * runtime calculation. Consumes soil NO from the export state (produced by BdsnpScheme
 * or another soil NO scheme) rather than computing it internally.
 *
 * Registered as "megan3" via PhysicsRegistration. The existing "megan" scheme
 * remains unchanged for backward compatibility.
 */

#include <Kokkos_Core.hpp>
#include <string>
#include <vector>

#include "cece/physics/cece_canopy_model.hpp"
#include "cece/physics/cece_emission_activity.hpp"
#include "cece/physics/cece_megan.hpp"
#include "cece/physics/cece_speciation_config.hpp"
#include "cece/physics/cece_speciation_engine.hpp"
#include "cece/physics_scheme.hpp"

namespace cece {

/**
 * @class Megan3Scheme
 * @brief Native C++ MEGAN3-class activity and speciation scheme.
 *
 * Orchestrates the CanopyModel, EmissionActivityCalculator, SpeciationEngine,
 * and SpeciationConfigLoader to compute emissions for 19 MEGAN3 emission
 * classes and convert them to mechanism-specific output species.
 *
 * Usage in YAML configuration:
 * @code
 * physics_schemes:
 *   - name: megan3
 *     options:
 *       mechanism_file: mechanisms/cb6_ae7.yaml
 *       speciation_file: speciation/megan_cb6_ae7.yaml
 *       co2_concentration: 415.0
 *       emission_classes:
 *         ISOP:
 *           ldf: 0.9996
 *           ct1: 95.0
 *           ...
 * @endcode
 */
class Megan3Scheme : public BasePhysicsScheme {
   public:
    Megan3Scheme() = default;
    ~Megan3Scheme() override = default;

    void Initialize(const conf::Value& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;

   private:
    MeganHistory history_;
    /// @name Sub-components
    /// @{
    CanopyModel canopy_model_;
    EmissionActivityCalculator activity_calc_;
    SpeciationEngine speciation_engine_;
    SpeciationConfigLoader config_loader_;
    /// @}

    /// @name Emission class constants
    /// @{

    /// Number of MEGAN3 emission classes.
    static constexpr int NUM_CLASSES = 19;

    /// Canonical emission class names in index order.
    static constexpr const char* CLASS_NAMES[19] = {"ISOP", "MBO",    "MT_PINE", "MT_ACYC", "MT_CAMP", "MT_SABI", "MT_AROM",
                                                    "NO",   "SQT_HR", "SQT_LR",  "MEOH",    "ACTO",    "ETOH",    "ACID",
                                                    "LVOC", "OXPROD", "STRESS",  "OTHER",   "CO"};

    /// @}

    /// @name Device-side data
    /// @{

    /// Per-class default AEF amount fluxes [kmol class m-2 s-1].
    Kokkos::View<double[19], Kokkos::DefaultExecutionSpace> default_aef_;

    /// Intermediate 19-class amount fluxes [kmol class m-2 s-1].
    Kokkos::View<double**, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> class_totals_;

    /// @}

    /// Registered mechanism species export field names (with "MEGAN_" prefix).
    std::vector<std::string> export_field_names_;
};

}  // namespace cece

#endif  // CECE_MEGAN3_HPP
