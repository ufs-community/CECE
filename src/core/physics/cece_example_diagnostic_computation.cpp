#include "cece/physics/cece_example_diagnostic_computation.hpp"

#include <Kokkos_Core.hpp>
#include <cmath>

#include "cece/cece_physics_factory.hpp"

namespace cece {

// Self-registration
static PhysicsRegistration<ExampleDiagnosticComputation> register_example_diagnostic_computation("example_diagnostic_computation");

void ExampleDiagnosticComputation::Initialize(const conf::Value& config, CeceDiagnosticManager* diag_manager) {
    // Call base class initialization
    BasePhysicsScheme::Initialize(config, diag_manager);

    // Read configuration parameters
    if (config["reference_temperature"]) {
        reference_temperature_ = config["reference_temperature"].as_double();
    }
    if (config["compute_quality_flags"]) {
        compute_quality_flags_ = config["compute_quality_flags"].as_bool();
    }
    if (config["quality_threshold"]) {
        quality_threshold_ = config["quality_threshold"].as_double();
    }

    // Register diagnostic fields
    // These fields will be written to NetCDF output if diagnostics are enabled
    temperature_anomaly_ = ResolveDiagnostic("temperature_anomaly", "K", "Temperature deviation from reference");
    emission_efficiency_ = ResolveDiagnostic("emission_efficiency", "kg/m2/s/K", "Emission per unit temperature anomaly");
    if (compute_quality_flags_) {
        quality_flag_ = ResolveDiagnostic("quality_flag", "dimensionless", "Data quality flag (0=good, 1=suspect, 2=bad)");
    }
}

void ExampleDiagnosticComputation::BindDiagnostics(CeceDiagnosticManager* diag_manager) {
    BasePhysicsScheme::BindDiagnostics(diag_manager);
    temperature_anomaly_ = ResolveDiagnostic("temperature_anomaly", "K", "Temperature deviation from reference");
    emission_efficiency_ = ResolveDiagnostic("emission_efficiency", "kg/m2/s/K", "Emission per unit temperature anomaly");
    if (compute_quality_flags_) {
        quality_flag_ = ResolveDiagnostic("quality_flag", "dimensionless", "Data quality flag (0=good, 1=suspect, 2=bad)");
    }
}

void ExampleDiagnosticComputation::Run(CeceImportState& import_state, CeceExportState& export_state) {
    // Nothing to fill when no diagnostic manager was bound (the views are
    // null in that case).
    if (temperature_anomaly_.view_host().data() == nullptr && emission_efficiency_.view_host().data() == nullptr &&
        quality_flag_.view_host().data() == nullptr) {
        return;
    }

    // Resolve input fields.
    Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> temperature = ResolveImport("temperature", import_state);
    // Emissions may come from a previous scheme through the export state.
    Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> emissions =
        ResolveInput("emissions", import_state, export_state);

    // This example's diagnostics require both live inputs. Do not manufacture
    // plausible-looking values from zero-filled substitutes when either is
    // unavailable.
    if (temperature.data() == nullptr || emissions.data() == nullptr) {
        return;
    }
    const int nx = static_cast<int>(emissions.extent(0));
    const int ny = static_cast<int>(emissions.extent(1));
    const int nz = static_cast<int>(emissions.extent(2));

    // Device-side writable handles for the registered diagnostic fields.
    auto anomaly_device = temperature_anomaly_.view_device();
    auto efficiency_device = emission_efficiency_.view_device();
    auto quality_device = quality_flag_.view_device();

    // ========================================================================
    // Kokkos Parallel Kernel: Compute and STORE diagnostic fields
    // ========================================================================
    // 1. Read input fields (meteorology, base emissions, or zero fallbacks)
    // 2. Compute intermediate diagnostic variables
    // 3. Store results in the diagnostic fields registered in Initialize()
    Kokkos::parallel_for(
        "ExampleDiagnosticComputationKernel", Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<3>>({0, 0, 0}, {nx, ny, nz}),
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

            // Store diagnostic results in the registered fields
            if (anomaly_device.data() != nullptr) {
                anomaly_device(i, j, k) = temp_anom;
            }
            if (efficiency_device.data() != nullptr) {
                efficiency_device(i, j, k) = emis_eff;
            }
            if (quality_device.data() != nullptr) {
                quality_device(i, j, k) = static_cast<double>(quality);
            }
        });

    Kokkos::fence();

    // The host side of each diagnostic is stale after the device writes;
    // mark them modified-on-device so the writer's sync_host() picks up
    // the computed values (the export state itself is untouched — this is
    // a diagnostics-only scheme).
    if (temperature_anomaly_.view_host().data() != nullptr) {
        temperature_anomaly_.modify_device();
    }
    if (emission_efficiency_.view_host().data() != nullptr) {
        emission_efficiency_.modify_device();
    }
    if (quality_flag_.view_host().data() != nullptr) {
        quality_flag_.modify_device();
    }
}

void ExampleDiagnosticComputation::Finalize() {
    // No special cleanup needed
}

}  // namespace cece
