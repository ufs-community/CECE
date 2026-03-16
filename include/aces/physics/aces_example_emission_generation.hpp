/**
 * @file aces_example_emission_generation.hpp
 * @brief Example physics scheme demonstrating emission generation pattern
 * @details
 * This example shows how to implement a physics scheme that generates
 * emissions from meteorological inputs. This is the most common pattern
 * in ACES, used for schemes like MEGAN (biogenic emissions), DMS (ocean
 * emissions), and soil NOx.
 *
 * ## Pattern Overview
 * 1. Read meteorological inputs (temperature, solar radiation, etc.)
 * 2. Apply empirical or mechanistic relationships to compute emissions
 * 3. Write results to export fields
 * 4. Optionally compute and store diagnostic fields
 *
 * ## Key Features Demonstrated
 * - Configuration parameter reading from YAML
 * - Field resolution with caching
 * - Kokkos::parallel_for kernel implementation
 * - Diagnostic field registration and output
 * - Device-to-host synchronization signaling
 */

#ifndef ACES_EXAMPLE_EMISSION_GENERATION_HPP
#define ACES_EXAMPLE_EMISSION_GENERATION_HPP

#include <Kokkos_Core.hpp>
#include <yaml-cpp/yaml.h>

#include "aces/aces_diagnostics.hpp"
#include "aces/aces_state.hpp"
#include "aces/physics_scheme.hpp"

namespace aces {

/**
 * @class ExampleEmissionGeneration
 * @brief Example scheme that generates emissions from meteorological inputs
 *
 * This scheme demonstrates the emission generation pattern by computing
 * emissions as a function of temperature and solar radiation. It includes:
 * - Configuration parameter reading
 * - Diagnostic field output
 * - Proper Kokkos kernel structure
 * - Best practices for performance portability
 */
class ExampleEmissionGeneration : public BasePhysicsScheme {
   public:
    ExampleEmissionGeneration() = default;
    ~ExampleEmissionGeneration() override = default;

    void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) override;
    void Run(AcesImportState& import_state, AcesExportState& export_state) override;
    void Finalize() override;

   private:
    // Configuration parameters (read from YAML in Initialize)
    double base_emission_factor_ = 1.0e-6;  ///< Base emission factor [kg/m2/s]
    double temperature_ref_ = 298.15;       ///< Reference temperature [K]
    double q10_ = 2.0;                      ///< Q10 temperature sensitivity factor
    double solar_factor_ = 0.5;             ///< Solar radiation scaling factor

    // Diagnostic fields
    DualView3D temperature_factor_;  ///< Temperature scaling factor for diagnostics
    DualView3D solar_factor_diag_;   ///< Solar scaling factor for diagnostics

    /**
     * @brief Compute temperature-dependent scaling factor
     * @details Uses Q10 parameterization: factor = Q10^((T-Tref)/10)
     * @param temperature Temperature in Kelvin
     * @param q10 Q10 factor (typically 2.0)
     * @param temp_ref Reference temperature in Kelvin
     * @return Temperature scaling factor (dimensionless)
     */
    KOKKOS_INLINE_FUNCTION
    static double ComputeTemperatureFactor(double temperature, double q10, double temp_ref) {
        // Use Horner's method for efficient polynomial evaluation
        // Q10^((T-Tref)/10) = exp(ln(Q10) * (T-Tref) / 10)
        return Kokkos::exp(Kokkos::log(q10) * (temperature - temp_ref) / 10.0);
    }

    /**
     * @brief Compute solar radiation scaling factor
     * @details Simple linear scaling: factor = 1 + solar_factor * (rad / rad_ref)
     * @param solar_radiation Solar radiation in W/m2
     * @param solar_factor Scaling factor coefficient
     * @return Solar scaling factor (dimensionless)
     */
    KOKKOS_INLINE_FUNCTION
    static double ComputeSolarFactor(double solar_radiation, double solar_factor) {
        // Normalize to reference solar radiation (1000 W/m2)
        const double solar_ref = 1000.0;
        return 1.0 + solar_factor * (solar_radiation / solar_ref);
    }
};

}  // namespace aces

#endif  // ACES_EXAMPLE_EMISSION_GENERATION_HPP
