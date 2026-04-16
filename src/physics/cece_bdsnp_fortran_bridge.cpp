/**
 * @file cece_bdsnp_fortran_bridge.cpp
 * @brief Fortran bridge for the BDSNP soil NO emission scheme.
 *
 * This module provides the C++/Fortran interface for the BDSNP soil NO
 * emission scheme. It handles:
 * - YAML configuration parsing for soil_no_method selection
 * - Memory synchronization between host and device via DualViews
 * - Calling the Fortran run_bdsnp_fortran subroutine
 * - Marking "soil_nox_emissions" as modified on host after Fortran returns
 *
 * Registered as "bdsnp_fortran" (guarded by CECE_HAS_FORTRAN), replacing
 * the existing "soil_nox_fortran" registration.
 *
 * @author CECE Team
 * @date 2024
 */

#include <Kokkos_Core.hpp>
#include <iostream>

#include "cece/cece_physics_factory.hpp"
#include "cece/physics/cece_bdsnp_fortran.hpp"

extern "C" {
/**
 * @brief External Fortran subroutine for BDSNP soil NO emission calculations.
 *
 * Implements both YL95 and BDSNP algorithms in Fortran, matching the C++
 * BdsnpScheme logic. The algorithm is selected via the soil_no_method parameter.
 *
 * @param soil_temp     Soil temperature field [K]
 * @param soil_moisture Soil moisture field [0-1]
 * @param soil_nox      Output soil NO emissions [kg/m²/s]
 * @param nx            Grid dimension in x-direction
 * @param ny            Grid dimension in y-direction
 * @param nz            Grid dimension in z-direction
 * @param soil_no_method Algorithm selector: 0 = BDSNP, 1 = YL95
 */
void run_bdsnp_fortran(double* soil_temp, double* soil_moisture,
                       double* soil_nox,
                       int nx, int ny, int nz,
                       int soil_no_method);
}

namespace cece {

#ifdef CECE_HAS_FORTRAN
/// @brief Self-registration for the BDSNP Fortran bridge scheme.
static PhysicsRegistration<BdsnpFortranScheme> register_scheme("bdsnp_fortran");
#endif

// ============================================================================
// Initialize
// ============================================================================

void BdsnpFortranScheme::Initialize(const YAML::Node& config,
                                    CeceDiagnosticManager* diag_manager) {
    // Call base class to parse input_mapping, output_mapping, diagnostics
    BasePhysicsScheme::Initialize(config, diag_manager);

    // Read soil_no_method (default "bdsnp", fallback "yl95")
    soil_no_method_ = "bdsnp";
    if (config["soil_no_method"]) {
        soil_no_method_ = config["soil_no_method"].as<std::string>();
    }
    if (soil_no_method_ != "bdsnp" && soil_no_method_ != "yl95") {
        std::cout << "BdsnpFortranScheme: WARNING - Unknown soil_no_method '"
                  << soil_no_method_ << "', falling back to 'bdsnp'\n";
        soil_no_method_ = "bdsnp";
    }

    std::cout << "BdsnpFortranScheme: Initialized with soil_no_method='"
              << soil_no_method_ << "'\n";
}

// ============================================================================
// Run
// ============================================================================

void BdsnpFortranScheme::Run(CeceImportState& import_state,
                             CeceExportState& export_state) {
    auto it_temp = import_state.fields.find(MapInput("soil_temperature"));
    auto it_moisture = import_state.fields.find(MapInput("soil_moisture"));
    auto it_soil_nox = export_state.fields.find(MapOutput("soil_nox_emissions"));

    if (it_temp == import_state.fields.end() ||
        it_moisture == import_state.fields.end() ||
        it_soil_nox == export_state.fields.end()) {
        return;
    }

    auto& dv_temp = it_temp->second;
    auto& dv_moisture = it_moisture->second;
    auto& dv_soil_nox = it_soil_nox->second;

    // Sync DualViews to host for Fortran access
    dv_temp.sync<Kokkos::HostSpace>();
    dv_moisture.sync<Kokkos::HostSpace>();
    dv_soil_nox.sync<Kokkos::HostSpace>();

    int nx = static_cast<int>(dv_soil_nox.extent(0));
    int ny = static_cast<int>(dv_soil_nox.extent(1));
    int nz = static_cast<int>(dv_soil_nox.extent(2));

    // Encode soil_no_method as integer for Fortran: 0 = bdsnp, 1 = yl95
    int method_flag = (soil_no_method_ == "yl95") ? 1 : 0;

    // Call Fortran subroutine
    run_bdsnp_fortran(dv_temp.view_host().data(),
                      dv_moisture.view_host().data(),
                      dv_soil_nox.view_host().data(),
                      nx, ny, nz,
                      method_flag);

    // Mark soil_nox_emissions as modified on host
    dv_soil_nox.modify<Kokkos::HostSpace>();
    dv_soil_nox.sync<Kokkos::DefaultExecutionSpace>();
}

}  // namespace cece
