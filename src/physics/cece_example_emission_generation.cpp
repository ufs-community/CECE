#include "cece/physics/cece_example_emission_generation.hpp"

#include <Kokkos_Core.hpp>
#include <cmath>

#include "cece/cece_physics_factory.hpp"

namespace cece {

// Self-registration: allows PhysicsFactory to discover this scheme
static PhysicsRegistration<ExampleEmissionGeneration> register_example_emission_generation(
    "example_emission_generation");

void ExampleEmissionGeneration::Initialize(const YAML::Node& config,
                                           CeceDiagnosticManager* diag_manager) {
    // Call base class initialization to set up field name mappings
    BasePhysicsScheme::Initialize(config, diag_manager);

    // Read configuration parameters from YAML
    // Example YAML config:
    //   physics_schemes:
    //     - name: example_emission_generation
    //       options:
    //         base_emission_factor: 1.0e-6
    //         temperature_ref: 298.15
    //         q10: 2.0
    //         solar_factor: 0.5

    if (config["base_emission_factor"]) {
        base_emission_factor_ = config["base_emission_factor"].as<double>();
    }
    if (config["temperature_ref"]) {
        temperature_ref_ = config["temperature_ref"].as<double>();
    }
    if (config["q10"]) {
        q10_ = config["q10"].as<double>();
    }
    if (config["solar_factor"]) {
        solar_factor_ = config["solar_factor"].as<double>();
    }

    // Register diagnostic fields for output
    // These will be written to NetCDF if diagnostics are enabled
    if (diag_manager != nullptr) {
        temperature_factor_ = ResolveDiagnostic("temperature_factor", 1, 1, 1, "dimensionless",
                                                "Temperature scaling factor");
        solar_factor_diag_ = ResolveDiagnostic("solar_factor", 1, 1, 1, "dimensionless",
                                               "Solar radiation scaling factor");
    }
}

void ExampleEmissionGeneration::Run(CeceImportState& import_state, CeceExportState& export_state) {
    // Resolve input meteorological fields
    // These are provided by the atmospheric model or TIDE
    auto temperature = ResolveImport("temperature", import_state);
    auto solar_radiation = ResolveImport("solar_radiation", import_state);

    // Resolve output emission field
    // This is what we will compute in this scheme
    auto emissions = ResolveExport("emissions", export_state);

    // Validate that all required fields were resolved
    if (temperature.data() == nullptr || solar_radiation.data() == nullptr ||
        emissions.data() == nullptr) {
        return;
    }

    // Get grid dimensions from the first export field
    int nx = emissions.extent(0);
    int ny = emissions.extent(1);
    int nz = emissions.extent(2);

    // ========================================================================
    // Kokkos Parallel Kernel: Compute Emissions
    // ========================================================================
    // This kernel demonstrates the emission generation pattern:
    // 1. Read meteorological inputs
    // 2. Apply empirical relationships to compute emissions
    // 3. Write results to output fields
    //
    // Key points:
    // - Use Kokkos::DefaultExecutionSpace for automatic device dispatch
    // - Use KOKKOS_LAMBDA for device-compatible lambda functions
    // - Avoid std::cout or file I/O inside kernels
    // - Use Kokkos::atomic_add for race-condition-free accumulation
    // - Use Kokkos::parallel_reduce for global reductions

    Kokkos::parallel_for(
        "ExampleEmissionGenerationKernel",
        Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<3>>({0, 0, 0},
                                                                              {nx, ny, nz}),
        KOKKOS_LAMBDA(int i, int j, int k) {
            // Get meteorological inputs for this grid point
            double temp = temperature(i, j, k);
            double solar = solar_radiation(i, j, k);

            // Compute temperature scaling factor using Q10 parameterization
            // Q10^((T-Tref)/10) represents exponential temperature dependence
            double temp_factor = ComputeTemperatureFactor(temp, q10_, temperature_ref_);

            // Compute solar radiation scaling factor
            // Linear scaling with solar radiation
            double solar_factor = ComputeSolarFactor(solar, solar_factor_);

            // Compute final emissions as product of base factor and scaling factors
            // This is a simplified example; real schemes may have more complex relationships
            emissions(i, j, k) = base_emission_factor_ * temp_factor * solar_factor;

            // Note: In a real scheme, you might also:
            // - Apply land-use masks to zero out emissions over water
            // - Apply diurnal cycles for anthropogenic emissions
            // - Apply seasonal cycles for biogenic emissions
            // - Apply vertical distribution to map 2D to 3D
        });

    // Ensure all threads complete before proceeding
    Kokkos::fence();

    // Signal that the emissions field has been updated on the device
    // This ensures proper device-to-host synchronization before ESMF accesses the data
    MarkModified("emissions", export_state);
}

void ExampleEmissionGeneration::Finalize() {
    // No special cleanup needed for this example
    // BasePhysicsScheme provides a default no-op implementation
}

}  // namespace cece
