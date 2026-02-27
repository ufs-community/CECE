#include "aces/physics/aces_native_example.hpp"

#include <Kokkos_Core.hpp>
#include <iostream>

/**
 * @file aces_native_example.cpp
 * @brief Implementation of a sample native C++ physics scheme.
 */

namespace aces {

/**
 * @brief Initializes the native physics scheme.
 * @param config YAML node containing scheme-specific options.
 * @param diag_manager Pointer to the diagnostic manager.
 */
void NativePhysicsExample::Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) {
    std::cout << "NativePhysicsExample: Initialized." << std::endl;
}

/**
 * @brief Executes the native physics scheme kernel.
 *
 * This example simply doubles the base NOX emissions and adds them to the total.
 * It demonstrates the use of Kokkos DualViews and parallel dispatch.
 *
 * @param import_state Input data.
 * @param export_state Output data.
 */
void NativePhysicsExample::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto it_base = import_state.fields.find("base_anthropogenic_nox");
    auto it_total = export_state.fields.find("total_nox_emissions");

    if (it_base == import_state.fields.end() || it_total == export_state.fields.end()) return;

    auto base_nox = it_base->second.view_device();
    auto total_nox = it_total->second.view_device();

    int nx = total_nox.extent(0);
    int ny = total_nox.extent(1);
    int nz = total_nox.extent(2);

    if (nx == 0 || ny == 0 || nz == 0) return;

    // Dispatch the computational kernel to the default execution space (e.g. GPU)
    Kokkos::parallel_for(
        "NativePhysicsExampleKernel",
        Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<3>>({0, 0, 0},
                                                                              {nx, ny, nz}),
        KOKKOS_LAMBDA(int i, int j, int k) {
            // Apply a dummy calculation
            total_nox(i, j, k) += base_nox(i, j, k) * 2.0;
        });
    // Fence to ensure completion before returning control
    Kokkos::fence();

    // Mark the device data as modified so it can be synced back to the host correctly.
    it_total->second.modify_device();
}

}  // namespace aces
