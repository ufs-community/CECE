#include "aces/physics/aces_example_diagnostic_computation.hpp"

#include <Kokkos_Core.hpp>
#include <cmath>

#include "aces/aces_physics_factory.hpp"

namespace aces {

// Self-registration
static PhysicsRegistration<ExampleDiagnosticComputation> register_example_diagnostic_computation(
    "example_diagnostic_computation");

void ExampleDiagnosticComputation::Initialize(const YAML::Node& config,
                                              AcesDiagnosticManager* diag_manager) {
    // Call base class initialization
    BasePhysicsScheme::Initialize(config, diag_manager);

    // Read configuration parameters
    if (config["reference_temperature"]) {
        reference_temperature_ = config["reference_temperature"].as<double>();
    }
    if (config["compute_quality_flags"]) {
        compute_quality_flags_ = config["compute_quality_flags"].as<bool>();
    }
    if (config["quality_threshold"]) {
        quality_threshold_ = config["quality_threshold"].as<double>();
    }

    // Register diagnostic fields
    // These fields will be written to NetCDF output if diagnostics are enabled
    if (diag_manager != nullptr) {
        temperature_anomaly_ = ResolveDiagnostic("temperature_anomaly", 1, 1, 1, "K",
                                                 "Temperature deviation from reference");
        emission_efficiency_ = ResolveDiagnostic("emission_efficiency", 1, 1, 1, "kg/m2/s/K",
                                                 "Emission per unit temperature anomaly");
        if (compute_quality_flags_) {
            quality_flag_ = ResolveDiagnostic("quality_flag", 1, 1, 1, "dimensionless",
                                              "Data quality flag (0=good, 1=suspect, 2=bad)");
        }
    }
}

void ExampleDiagnosticComputation::Run(AcesImportState& import_state,
                                       AcesExportState& export_state) {
    // Resolve input fields
    auto temperature = ResolveImport("temperature", import_state);

    // Resolve emissions field (from export state, computed by previous scheme)
    auto emissions = ResolveInput("emissions", import_state, export_state);

    // Validate field resolution
    if (temperature.data() == nullptr || emissions.data() == nullptr) {
        return;
    }

    // Get grid dimensions
    int nx = emissions.extent(0);
    int ny = emissions.extent(1);
    int nz = emissions.extent(2);

    // ========================================================================
    // Kokkos Parallel Kernel: Compute Diagnostic Fields
    // ========================================================================
    // This kernel demonstrates the diagnostic computation pattern:
    // 1. Read input fields (meteorology, base emissions, etc.)
    // 2. Compute intermediate diagnostic variables
    // 3. Store results in diagnostic fields
    //
    // Key points:
    // - Diagnostic fields are optional and don't affect main computation
    // - They are useful for validation and debugging
    // - They can be written to NetCDF output for post-processing
    // - Multiple diagnostic schemes can be chained together
    // - Diagnostic computation should be efficient (avoid expensive operations)

    Kokkos::parallel_for(
        "ExampleDiagnosticComputationKernel",
        Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<3>>({0, 0, 0},
                                                                              {nx, ny, nz}),
        KOKKOS_LAMBDA(int i, int j, int k) {
            // Get input values
            double temp = temperature(i, j, k);
            double emis = emissions(i, j, k);

            // Compute temperature anomaly
            double temp_anom = ComputeTemperatureAnomaly(temp, reference_temperature_);

            // Compute emission efficiency
            double emis_eff = ComputeEmissionEfficiency(emis, temp_anom);

            // Compute quality flag
            int quality = ComputeQualityFlag(emis, temp, quality_threshold_);

            // Store diagnostic results
            // Note: In a real implementation, these would be stored in the
            // diagnostic fields registered in Initialize()
            // For this example, we just compute them
            // temperature_anomaly_(i, j, k) = temp_anom;
            // emission_efficiency_(i, j, k) = emis_eff;
            // quality_flag_(i, j, k) = quality;

            // Example diagnostic computations:
            // - Validate that emissions are physically reasonable
            // - Check for data quality issues
            // - Compute intermediate variables for analysis
            // - Flag suspicious or invalid data
        });

    Kokkos::fence();

    // Note: Unlike emission schemes, diagnostic schemes typically don't
    // modify export fields, so we don't call MarkModified() here.
    // However, if we were storing results in diagnostic fields, we would
    // need to mark those as modified.
}

void ExampleDiagnosticComputation::Finalize() {
    // No special cleanup needed
}

}  // namespace aces
