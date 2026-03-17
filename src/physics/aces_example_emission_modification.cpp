#include "aces/physics/aces_example_emission_modification.hpp"

#include <Kokkos_Core.hpp>
#include <cmath>

#include "aces/aces_physics_factory.hpp"

namespace aces {

// Self-registration
static PhysicsRegistration<ExampleEmissionModification> register_example_emission_modification(
    "example_emission_modification");

void ExampleEmissionModification::Initialize(const YAML::Node& config,
                                             AcesDiagnosticManager* diag_manager) {
    // Call base class initialization
    BasePhysicsScheme::Initialize(config, diag_manager);

    // Read configuration parameters
    if (config["apply_diurnal_cycle"]) {
        apply_diurnal_cycle_ = config["apply_diurnal_cycle"].as<bool>();
    }
    if (config["peak_hour"]) {
        peak_hour_ = config["peak_hour"].as<double>();
    }
    if (config["peak_factor"]) {
        peak_factor_ = config["peak_factor"].as<double>();
    }

    // Pre-compute diurnal cycle factors for all 24 hours
    // This avoids recomputing the same values for every grid point
    for (int hour = 0; hour < 24; ++hour) {
        diurnal_cycle_(hour) = ComputeDiurnalFactor(hour, peak_hour_, peak_factor_);
    }
}

void ExampleEmissionModification::Run(AcesImportState& import_state,
                                      AcesExportState& export_state) {
    // Resolve the emissions field that was computed by a previous scheme
    // This demonstrates reading from export state (previously computed fields)
    // We use ResolveInput which checks both import and export states
    auto emissions = ResolveExport("emissions", export_state);

    // Validate field resolution
    if (emissions.data() == nullptr) {
        return;
    }

    // Get grid dimensions
    int nx = emissions.extent(0);
    int ny = emissions.extent(1);
    int nz = emissions.extent(2);

    // Get current hour from the clock (in a real implementation)
    // For this example, we'll use a fixed hour
    // In practice, this would come from ESMF_Clock
    int current_hour = 12;  // TODO: Get from ESMF_Clock

    // ========================================================================
    // Kokkos Parallel Kernel: Apply Diurnal Cycle
    // ========================================================================
    // This kernel demonstrates the emission modification pattern:
    // 1. Read base emissions (computed by previous scheme)
    // 2. Apply modification logic (diurnal cycle in this case)
    // 3. Update emissions in-place
    //
    // Key points:
    // - This scheme modifies fields computed by other schemes
    // - Modifications are applied in-place to avoid extra memory allocation
    // - Multiple modification schemes can be chained together
    // - Order matters: schemes are executed in registration order

    if (apply_diurnal_cycle_) {
        // Create a device copy of the diurnal cycle factors
        auto diurnal_cycle_device =
            Kokkos::create_mirror_view_and_copy(Kokkos::DefaultExecutionSpace(), diurnal_cycle_);

        Kokkos::parallel_for(
            "ExampleEmissionModificationKernel",
            Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<3>>({0, 0, 0},
                                                                                  {nx, ny, nz}),
            KOKKOS_LAMBDA(int i, int j, int k) {
                // Apply diurnal cycle factor to emissions
                // This scales emissions based on the time of day
                double diurnal_factor = diurnal_cycle_device(current_hour);
                emissions(i, j, k) *= diurnal_factor;

                // Note: In a real scheme, you might also:
                // - Apply day-of-week cycles (7 factors)
                // - Apply seasonal cycles (12 monthly factors)
                // - Apply spatial masks (e.g., zero over water)
                // - Apply vertical distribution (map 2D to 3D)
                // - Apply chemical transformations (e.g., NOx partitioning)
            });

        Kokkos::fence();
    }

    // Signal that the emissions field has been modified on the device
    MarkModified("emissions", export_state);
}

void ExampleEmissionModification::Finalize() {
    // No special cleanup needed
}

}  // namespace aces
