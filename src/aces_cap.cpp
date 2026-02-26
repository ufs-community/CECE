#include "aces/aces.hpp"
#include "aces/aces_state.hpp"
#include "aces/aces_utils.hpp"
#include "aces/physics/aces_seasalt.hpp"
#include "aces/physics/aces_dust.hpp"
#include "aces/physics/aces_biogenics.hpp"
#include <iostream>
#include <Kokkos_Core.hpp>

namespace aces {

/**
 * @brief Global implementation of ACES component initialization.
 */
void Initialize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock, int* rc) {
    if (!Kokkos::is_initialized()) {
        Kokkos::initialize();
        std::cout << "ACES_Initialize: Kokkos initialized." << std::endl;
    }
    if (rc) *rc = ESMF_SUCCESS;
}

/**
 * @brief Global implementation of ACES component run phase.
 */
void Run(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock, int* rc) {
    std::cout << "ACES_Run: Executing." << std::endl;

    // TODO: Retrieve actual dimensions from ESMF Grid/Field
    int nx = 360, ny = 180, nz = 72;

    // Populate Import State
    AcesImportState aces_import;
    ESMC_Field field;

    ESMC_StateGetField(importState, "temperature", &field);
    aces_import.temperature = WrapESMCField(field, nx, ny, nz);

    ESMC_StateGetField(importState, "wind_speed_10m", &field);
    aces_import.wind_speed_10m = WrapESMCField(field, nx, ny, 1);

    ESMC_StateGetField(importState, "base_anthropogenic_nox", &field);
    aces_import.base_anthropogenic_nox = WrapESMCField(field, nx, ny, nz);

    // Populate Export State
    AcesExportState aces_export;
    ESMC_StateGetField(exportState, "total_nox_emissions", &field);
    aces_export.total_nox_emissions = WrapESMCField(field, nx, ny, nz);

    ESMC_StateGetField(exportState, "sea_salt_emissions", &field);
    aces_export.sea_salt_emissions = WrapESMCField(field, nx, ny, nz);

    ESMC_StateGetField(exportState, "dust_emissions", &field);
    aces_export.dust_emissions = WrapESMCField(field, nx, ny, nz);

    ESMC_StateGetField(exportState, "biogenic_emissions", &field);
    aces_export.biogenic_emissions = WrapESMCField(field, nx, ny, nz);

    // Main compute logic: Dispatch physics modules.
    physics::RunSeaSalt(aces_import, aces_export);
    physics::RunDust(aces_import, aces_export);
    physics::RunBiogenics(aces_import, aces_export);

    if (rc) *rc = ESMF_SUCCESS;
}

/**
 * @brief Global implementation of ACES component finalization.
 */
void Finalize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock, int* rc) {
    if (Kokkos::is_initialized()) {
        Kokkos::finalize();
        std::cout << "ACES_Finalize: Kokkos finalized." << std::endl;
    }
    if (rc) *rc = ESMF_SUCCESS;
}

} // namespace aces

// C-linkage entry points for ESMF
extern "C" {

/**
 * @brief ESMF initialization entry point.
 * Calls aces::Initialize.
 */
void ACES_Initialize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock, int* rc) {
    aces::Initialize(comp, importState, exportState, clock, rc);
}

/**
 * @brief ESMF run entry point.
 * Calls aces::Run.
 */
void ACES_Run(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock, int* rc) {
    aces::Run(comp, importState, exportState, clock, rc);
}

/**
 * @brief ESMF finalization entry point.
 * Calls aces::Finalize.
 */
void ACES_Finalize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock, int* rc) {
    aces::Finalize(comp, importState, exportState, clock, rc);
}

/**
 * @brief Registers the ACES component services (Initialize, Run, Finalize).
 *
 * @param comp The ESMF Grid Component.
 * @param rc Return code pointer.
 */
void ACES_SetServices(ESMC_GridComp comp, int* rc) {
    if (rc) *rc = ESMF_SUCCESS;

    // Register initialization (Phase 1)
    ESMC_GridCompSetEntryPoint(comp, ESMF_METHOD_INITIALIZE, ACES_Initialize, 1);

    // Register run (Phase 1)
    ESMC_GridCompSetEntryPoint(comp, ESMF_METHOD_RUN, ACES_Run, 1);

    // Register finalize (Phase 1)
    ESMC_GridCompSetEntryPoint(comp, ESMF_METHOD_FINALIZE, ACES_Finalize, 1);

    std::cout << "ACES_SetServices: Services set." << std::endl;
}

} // extern "C"
