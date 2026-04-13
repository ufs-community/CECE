#include "cece/physics/cece_fortran_bridge.hpp"

#include <Kokkos_Core.hpp>
#include <iostream>

#include "cece/cece_physics_factory.hpp"

/**
 * @file cece_fortran_bridge.cpp
 * @brief Implementation of the bridge between C++ CECE and legacy Fortran
 * physics.
 */

// Declare the external Fortran function (implemented in legacy_fortran.F90)
extern "C" {
void run_legacy_fortran(double* temp, double* wind, double* nox, int nx, int ny, int nz);
}

namespace cece {

#ifdef CECE_HAS_FORTRAN
/// Self-registration for the FortranBridgeExample scheme.
static PhysicsRegistration<FortranBridgeExample> register_scheme("fortran_bridge_example");
#endif

/**
 * @brief Initializes the Fortran bridge scheme.
 * @param config YAML node containing scheme-specific options.
 * @param diag_manager Pointer to the diagnostic manager.
 */
void FortranBridgeExample::Initialize(const YAML::Node& config,
                                      CeceDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);
    std::cout << "FortranBridgeExample: Initialized.\n";
}

/**
 * @brief Executes the legacy Fortran kernel.
 *
 * This function handles the host-device synchronization required before and
 * after calling a CPU-only Fortran routine.
 *
 * @param import_state Input data.
 * @param export_state Output data.
 */
void FortranBridgeExample::Run(CeceImportState& import_state, CeceExportState& export_state) {
    auto it_temp = import_state.fields.find("temperature");
    auto it_wind = import_state.fields.find("wind_speed_10m");
    auto it_nox = export_state.fields.find("nox");

    if (it_temp == import_state.fields.end() || it_wind == import_state.fields.end() ||
        it_nox == export_state.fields.end()) {
        return;
    }

    auto& dv_temp = it_temp->second;
    auto& dv_wind = it_wind->second;
    auto& dv_nox = it_nox->second;

    // 1. Ensure CPU has latest data by syncing to Host Execution Space
    // This is crucial if CECE is running on a GPU.
    dv_temp.sync<Kokkos::DefaultHostExecutionSpace::memory_space>();
    dv_wind.sync<Kokkos::DefaultHostExecutionSpace::memory_space>();
    dv_nox.sync<Kokkos::DefaultHostExecutionSpace::memory_space>();

    // 2. Extract dimensions
    int nx = static_cast<int>(dv_nox.extent(0));
    int ny = static_cast<int>(dv_nox.extent(1));
    int nz = static_cast<int>(dv_nox.extent(2));

    if (nx == 0 || ny == 0 || nz == 0) {
        return;
    }

    // 3. Extract raw host pointers for C-Fortran interoperability
    double* temp_ptr = dv_temp.view_host().data();
    double* wind_ptr = dv_wind.view_host().data();
    double* nox_ptr = dv_nox.view_host().data();

    // 4. Call the Fortran routine
    run_legacy_fortran(temp_ptr, wind_ptr, nox_ptr, nx, ny, nz);

    // 5. Mark host modified and sync back to device
    dv_nox.modify<Kokkos::DefaultHostExecutionSpace::memory_space>();
    dv_nox.sync<Kokkos::DefaultExecutionSpace::memory_space>();

    std::cout << "FortranBridgeExample: Execution complete.\n";
}

}  // namespace cece
