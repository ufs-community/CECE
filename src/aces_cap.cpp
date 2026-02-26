#include "aces/aces.hpp"
#include <iostream>
#include <Kokkos_Core.hpp>

namespace aces {

/**
 * @brief Global implementation of ACES component initialization.
 */
void Initialize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock clock, ESMC_VM vm, int* rc) {
    if (!Kokkos::is_initialized()) {
        Kokkos::initialize();
        std::cout << "ACES_Initialize: Kokkos initialized." << std::endl;
    }
    if (rc) *rc = ESMF_SUCCESS;
}

/**
 * @brief Global implementation of ACES component run phase.
 */
void Run(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock clock, ESMC_VM vm, int* rc) {
    std::cout << "ACES_Run: Executing." << std::endl;
    // Main compute logic will be added here
    if (rc) *rc = ESMF_SUCCESS;
}

/**
 * @brief Global implementation of ACES component finalization.
 */
void Finalize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock clock, ESMC_VM vm, int* rc) {
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
void ACES_Initialize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock clock, ESMC_VM vm, int* rc) {
    aces::Initialize(comp, importState, exportState, clock, vm, rc);
}

/**
 * @brief ESMF run entry point.
 * Calls aces::Run.
 */
void ACES_Run(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock clock, ESMC_VM vm, int* rc) {
    aces::Run(comp, importState, exportState, clock, vm, rc);
}

/**
 * @brief ESMF finalization entry point.
 * Calls aces::Finalize.
 */
void ACES_Finalize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock clock, ESMC_VM vm, int* rc) {
    aces::Finalize(comp, importState, exportState, clock, vm, rc);
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
    ESMC_GridCompSetEntryPoint(comp, ESMC_METHOD_INITIALIZE, ACES_Initialize, 1);

    // Register run (Phase 1)
    ESMC_GridCompSetEntryPoint(comp, ESMC_METHOD_RUN, ACES_Run, 1);

    // Register finalize (Phase 1)
    ESMC_GridCompSetEntryPoint(comp, ESMC_METHOD_FINALIZE, ACES_Finalize, 1);

    std::cout << "ACES_SetServices: Services set." << std::endl;
}

} // extern "C"
