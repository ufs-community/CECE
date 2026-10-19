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
    if (diag_manager != nullptr) {
        temperature_anomaly_ = ResolveDiagnostic("temperature_anomaly", 1, 1, 1, "K", "Temperature deviation from reference");
        emission_efficiency_ = ResolveDiagnostic("emission_efficiency", 1, 1, 1, "kg/m2/s/K", "Emission per unit temperature anomaly");
        if (compute_quality_flags_) {
            quality_flag_ = ResolveDiagnostic("quality_flag", 1, 1, 1, "dimensionless", "Data quality flag (0=good, 1=suspect, 2=bad)");
        }
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

    // Determine the field shape: prefer a live input, else the registered
    // diagnostic extent (grid-sized after Phase 2 binding).
    int nx = 0, ny = 0, nz = 0;
    if (emissions.data() != nullptr) {
        nx = static_cast<int>(emissions.extent(0));
        ny = static_cast<int>(emissions.extent(1));
        nz = static_cast<int>(emissions.extent(2));
    } else if (temperature.data() != nullptr) {
        nx = static_cast<int>(temperature.extent(0));
        ny = static_cast<int>(temperature.extent(1));
        nz = static_cast<int>(temperature.extent(2));
    } else if (temperature_anomaly_.view_host().data() != nullptr) {
        nx = static_cast<int>(temperature_anomaly_.view_host().extent(0));
        ny = static_cast<int>(temperature_anomaly_.view_host().extent(1));
        nz = static_cast<int>(temperature_anomaly_.view_host().extent(2));
    } else {
        nx = static_cast<int>(emission_efficiency_.view_host().extent(0));
        ny = static_cast<int>(emission_efficiency_.view_host().extent(1));
        nz = static_cast<int>(emission_efficiency_.view_host().extent(2));
    }

    // The standalone driver has no meteorology ingest path, so a run with
    // only this scheme has neither input. Substitute zero-filled constant
    // views of the right shape: the diagnostics then hold deterministic
    // values (anomaly = -reference, efficiency = 0) instead of the run
    // silently computing nothing. When a live field exists, it wins.
    if (temperature.data() == nullptr) {
        if (zero_temperature_.data() == nullptr || static_cast<int>(zero_temperature_.extent(0)) != nx ||
            static_cast<int>(zero_temperature_.extent(1)) != ny || static_cast<int>(zero_temperature_.extent(2)) != nz) {
            zero_temperature_ = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>("zero_temperature", nx, ny, nz);
            Kokkos::deep_copy(zero_temperature_, 0.0);
        }
        temperature = Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>(zero_temperature_);
    }
    if (emissions.data() == nullptr) {
        if (zero_emissions_.data() == nullptr || static_cast<int>(zero_emissions_.extent(0)) != nx ||
            static_cast<int>(zero_emissions_.extent(1)) != ny || static_cast<int>(zero_emissions_.extent(2)) != nz) {
            zero_emissions_ = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>("zero_emissions", nx, ny, nz);
            Kokkos::deep_copy(zero_emissions_, 0.0);
        }
        emissions = Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>(zero_emissions_);
    }

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
