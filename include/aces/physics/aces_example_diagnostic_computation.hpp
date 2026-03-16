/**
 * @file aces_example_diagnostic_computation.hpp
 * @brief Example physics scheme demonstrating diagnostic computation pattern
 * @details
 * This example shows how to implement a physics scheme that computes
 * diagnostic fields for validation and analysis. Diagnostic fields are
 * intermediate variables that are not part of the main emission output
 * but are useful for:
 * - Validating physics implementations
 * - Debugging model behavior
 * - Analyzing model performance
 * - Post-processing and visualization
 *
 * ## Pattern Overview
 * 1. Read input fields (meteorology, base emissions, etc.)
 * 2. Compute intermediate diagnostic variables
 * 3. Register and store diagnostic fields
 * 4. Write diagnostics to NetCDF output files
 *
 * ## Key Features Demonstrated
 * - Diagnostic field registration
 * - Computing intermediate variables
 * - Storing results in diagnostic fields
 * - Proper field resolution and caching
 */

#ifndef ACES_EXAMPLE_DIAGNOSTIC_COMPUTATION_HPP
#define ACES_EXAMPLE_DIAGNOSTIC_COMPUTATION_HPP

#include <Kokkos_Core.hpp>
#include <yaml-cpp/yaml.h>

#include "aces/aces_diagnostics.hpp"
#include "aces/aces_state.hpp"
#include "aces/physics_scheme.hpp"

namespace aces {

/**
 * @class ExampleDiagnosticComputation
 * @brief Example scheme that computes diagnostic fields
 *
 * This scheme demonstrates the diagnostic computation pattern by computing
 * and storing intermediate variables that are useful for validation and
 * analysis. It includes:
 * - Diagnostic field registration
 * - Computing intermediate variables
 * - Storing results in diagnostic fields
 * - Proper field resolution and caching
 */
class ExampleDiagnosticComputation : public BasePhysicsScheme {
   public:
    ExampleDiagnosticComputation() = default;
    ~ExampleDiagnosticComputation() override = default;

    void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) override;
    void Run(AcesImportState& import_state, AcesExportState& export_state) override;
    void Finalize() override;

   private:
    // Diagnostic fields
    DualView3D temperature_anomaly_;  ///< Temperature deviation from reference
    DualView3D emission_efficiency_;  ///< Emission per unit temperature anomaly
    DualView3D quality_flag_;         ///< Data quality flag (0=good, 1=suspect, 2=bad)

    // Configuration parameters
    double reference_temperature_ = 298.15;  ///< Reference temperature for anomaly [K]
    bool compute_quality_flags_ = true;      ///< Whether to compute quality flags
    double quality_threshold_ = 0.1;         ///< Threshold for quality flagging

    /**
     * @brief Compute temperature anomaly
     * @param temperature Current temperature [K]
     * @param reference Reference temperature [K]
     * @return Temperature anomaly [K]
     */
    KOKKOS_INLINE_FUNCTION
    static double ComputeTemperatureAnomaly(double temperature, double reference) {
        return temperature - reference;
    }

    /**
     * @brief Compute emission efficiency
     * @details Emission per unit temperature anomaly (useful for validation)
     * @param emissions Emission rate [kg/m2/s]
     * @param temperature_anomaly Temperature deviation [K]
     * @return Emission efficiency [kg/m2/s/K]
     */
    KOKKOS_INLINE_FUNCTION
    static double ComputeEmissionEfficiency(double emissions, double temperature_anomaly) {
        // Avoid division by zero
        if (Kokkos::abs(temperature_anomaly) < 1.0e-10) {
            return 0.0;
        }
        return emissions / temperature_anomaly;
    }

    /**
     * @brief Compute quality flag for diagnostic data
     * @details Flags suspicious or invalid data
     * @param emissions Emission rate [kg/m2/s]
     * @param temperature Temperature [K]
     * @param threshold Quality threshold
     * @return Quality flag (0=good, 1=suspect, 2=bad)
     */
    KOKKOS_INLINE_FUNCTION
    static int ComputeQualityFlag(double emissions, double temperature, double threshold) {
        // Flag negative emissions as bad
        if (emissions < 0.0) {
            return 2;
        }
        // Flag extreme temperatures as suspect
        if (temperature < 200.0 || temperature > 330.0) {
            return 1;
        }
        // Flag very large emissions as suspect
        if (emissions > 1.0e-3) {
            return 1;
        }
        return 0;
    }
};

}  // namespace aces

#endif  // ACES_EXAMPLE_DIAGNOSTIC_COMPUTATION_HPP
