#include "aces/physics/aces_native_example.hpp"

#include <Kokkos_Core.hpp>
#include <iostream>

#include "aces/aces_physics_factory.hpp"

/**
 * @file aces_native_example.cpp
 * @brief Implementation of a sample native C++ physics scheme.
 */

namespace aces {

/// Self-registration for the NativePhysicsExample scheme.
static PhysicsRegistration<NativePhysicsExample> register_scheme("native_example");

/**
 * @brief Initializes the native physics scheme.
 * @param config YAML node containing scheme-specific options.
 * @param diag_manager Pointer to the diagnostic manager.
 */
void NativePhysicsExample::Initialize(const YAML::Node& config,
                                      AcesDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);
    std::cout << "NativePhysicsExample: Initialized.\n";
    if (diag_manager != nullptr) {
        // Register an example diagnostic variable
        diag_manager->RegisterDiagnostic("native_example_diag", 1, 1, 1);
    }
}

/**
 * @brief Executes the native physics scheme kernel.
 *
 * This example simply doubles the base NOX emissions and adds them to the
 * total. It demonstrates the use of Kokkos DualViews and parallel dispatch.
 *
 * @param import_state Input data.
 * @param export_state Output data.
 */
void NativePhysicsExample::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto base_nox = ResolveImport("base_anthropogenic_nox", import_state);
    auto secondary_input = ResolveInput("secondary_input", import_state, export_state);
    auto nox = ResolveExport("nox", export_state);

    if (base_nox.data() == nullptr || nox.data() == nullptr) {
        return;
    }

    int nx = static_cast<int>(nox.extent(0));
    int ny = static_cast<int>(nox.extent(1));
    int nz = static_cast<int>(nox.extent(2));

    auto multiplier_diag = ResolveDiagnostic("nox_multiplier_effect", nx, ny, nz);

    // Dispatch the computational kernel to the default execution space (e.g. GPU)
    Kokkos::parallel_for(
        "NativePhysicsExampleKernel",
        Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<3>>({0, 0, 0},
                                                                              {nx, ny, nz}),
        KOKKOS_LAMBDA(int i, int j, int k) {
            double multiplier = 2.0;
            if (secondary_input.data() != nullptr) {
                multiplier = secondary_input(i, j, k);
            }
            // Apply a dummy calculation
            double effect = base_nox(i, j, k) * multiplier;
            nox(i, j, k) += effect;

            if (multiplier_diag.view_device().data() != nullptr) {
                multiplier_diag.view_device()(i, j, k) = effect;
            }
        });
    // Fence to ensure completion before returning control
    Kokkos::fence();

    // Mark the device data as modified so it can be synced back to the host
    // correctly.
    MarkModified("nox", export_state);
    if (multiplier_diag.view_device().data() != nullptr) {
        multiplier_diag.modify_device();
    }
}

}  // namespace aces
