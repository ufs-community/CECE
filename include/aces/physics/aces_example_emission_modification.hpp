/**
 * @file aces_example_emission_modification.hpp
 * @brief Example physics scheme demonstrating emission modification pattern
 * @details
 * This example shows how to implement a physics scheme that modifies
 * emissions computed by other schemes. This pattern is useful for:
 * - Applying chemical transformations (e.g., NOx to NO2 partitioning)
 * - Applying temporal variations (diurnal, weekly, seasonal cycles)
 * - Applying spatial masks or regional adjustments
 * - Applying vertical distribution to map 2D to 3D
 *
 * ## Pattern Overview
 * 1. Read base emissions from export state (computed by previous schemes)
 * 2. Read meteorological inputs or other modifying factors
 * 3. Apply modification logic to update emissions in-place
 * 4. Mark fields as modified for device-to-host sync
 *
 * ## Key Features Demonstrated
 * - Reading from export state (previously computed fields)
 * - In-place field modification
 * - Temporal cycle application (diurnal example)
 * - Proper field resolution order
 */

#ifndef ACES_EXAMPLE_EMISSION_MODIFICATION_HPP
#define ACES_EXAMPLE_EMISSION_MODIFICATION_HPP

#include <Kokkos_Core.hpp>
#include <yaml-cpp/yaml.h>

#include "aces/aces_diagnostics.hpp"
#include "aces/aces_state.hpp"
#include "aces/physics_scheme.hpp"

namespace aces {

/**
 * @class ExampleEmissionModification
 * @brief Example scheme that modifies emissions computed by other schemes
 *
 * This scheme demonstrates the emission modification pattern by applying
 * a diurnal cycle to base emissions. It includes:
 * - Reading from export state (previously computed fields)
 * - In-place field modification
 * - Temporal cycle application
 * - Proper field resolution order
 */
class ExampleEmissionModification : public BasePhysicsScheme {
   public:
    ExampleEmissionModification() = default;
    ~ExampleEmissionModification() override = default;

    void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) override;
    void Run(AcesImportState& import_state, AcesExportState& export_state) override;
    void Finalize() override;

   private:
    // Configuration parameters
    bool apply_diurnal_cycle_ = true;  ///< Whether to apply diurnal cycle
    double peak_hour_ = 14.0;          ///< Hour of peak emissions (0-24)
    double peak_factor_ = 1.5;         ///< Emission factor at peak hour

    // Diurnal cycle factors (24 hourly values)
    // These represent the fraction of daily average emissions at each hour
    // Example: 0.5 at midnight, 1.5 at peak hour, 0.5 at midnight again
    Kokkos::View<double[24], Kokkos::HostSpace> diurnal_cycle_;

    /**
     * @brief Compute diurnal cycle factor for a given hour
     * @details Uses a simple Gaussian-like profile centered at peak_hour
     * @param hour Hour of day (0-24)
     * @param peak_hour Hour of peak emissions
     * @param peak_factor Maximum emission factor
     * @return Diurnal cycle factor (dimensionless)
     */
    KOKKOS_INLINE_FUNCTION
    static double ComputeDiurnalFactor(double hour, double peak_hour, double peak_factor) {
        // Gaussian profile: factor = 1 + (peak_factor - 1) * exp(-(hour - peak_hour)^2 / sigma^2)
        const double sigma = 4.0;  // Width of the Gaussian profile
        double delta = hour - peak_hour;
        // Handle wrap-around at day boundary
        if (delta > 12.0) delta -= 24.0;
        if (delta < -12.0) delta += 24.0;
        return 1.0 + (peak_factor - 1.0) * Kokkos::exp(-(delta * delta) / (sigma * sigma));
    }
};

}  // namespace aces

#endif  // ACES_EXAMPLE_EMISSION_MODIFICATION_HPP
