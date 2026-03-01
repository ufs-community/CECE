#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "ESMC.h"
#include "NUOPC.h"

/**
 * @file main.cpp
 * @brief Standalone NUOPC-compatible driver for the ACES component.
 */

extern "C" {
void ACES_SetServices(ESMC_GridComp comp, int* rc);
void ACES_Initialize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState,
                     ESMC_Clock* clock, int* rc);
void ACES_Run(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock,
              int* rc);
void ACES_Finalize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState,
                   ESMC_Clock* clock, int* rc);
}

#define CHECK_RC(rc, msg) \
    if ((rc) != ESMF_SUCCESS) { \
        std::cerr << "[NUOPC Driver] Error: " << (msg) << " (rc=" << (rc) << ")" << std::endl; \
        return 1; \
    }

int main(int argc, char** argv) {
    int rc;

    // 1. Initialize ESMF Framework
    rc = ESMC_Initialize(NULL, ESMC_ArgLast);
    CHECK_RC(rc, "ESMC_Initialize failed");
    std::cout << "[NUOPC Driver] ESMF Initialized." << std::endl;

    // 2. Define simulation grid dimensions
    const int nx = 72;
    const int ny = 46;
    const int nz = 1;

    // 3. Create simulation clock
    ESMC_Calendar cal = ESMC_CalendarCreate("Gregorian", ESMC_CALKIND_GREGORIAN, &rc);
    CHECK_RC(rc, "ESMC_CalendarCreate failed");

    ESMC_Time startTime, stopTime;
    rc = ESMC_TimeSet(&startTime, 2024, 0, cal, ESMC_CALKIND_GREGORIAN, 0);
    CHECK_RC(rc, "ESMC_TimeSet (startTime) failed");
    rc = ESMC_TimeSet(&stopTime, 2024, 24, cal, ESMC_CALKIND_GREGORIAN, 0);
    CHECK_RC(rc, "ESMC_TimeSet (stopTime) failed");

    ESMC_TimeInterval timeStep;
    rc = ESMC_TimeIntervalSet(&timeStep, 1);
    CHECK_RC(rc, "ESMC_TimeIntervalSet failed");

    ESMC_Clock clock = ESMC_ClockCreate("SimulationClock", timeStep, startTime, stopTime, &rc);
    CHECK_RC(rc, "ESMC_ClockCreate failed");

    // 4. Setup Grid and Fields
    int maxIndex3D[3] = {nx, ny, nz};
    ESMC_InterArrayInt iMaxIndex;
    rc = ESMC_InterArrayIntSet(&iMaxIndex, maxIndex3D, 3);
    CHECK_RC(rc, "ESMC_InterArrayIntSet failed");
    ESMC_Grid grid = ESMC_GridCreateNoPeriDim(&iMaxIndex, NULL, NULL, NULL, &rc);
    CHECK_RC(rc, "ESMC_GridCreate failed");

    // 5. Create the ACES component
    ESMC_GridComp acesComp = ESMC_GridCompCreate("ACES", "", clock, &rc);
    CHECK_RC(rc, "ESMC_GridCompCreate (aces) failed");

    // Register ACES services
    ACES_SetServices(acesComp, &rc);
    CHECK_RC(rc, "ACES_SetServices failed");

    // 6. Create ESMF States and Fields
    ESMC_State importState = ESMC_StateCreate("ImportState", &rc);
    CHECK_RC(rc, "ESMC_StateCreate (import) failed");
    ESMC_State exportState = ESMC_StateCreate("ExportState", &rc);
    CHECK_RC(rc, "ESMC_StateCreate (export) failed");

    // Add a field to export state so ACES can discover dimensions
    ESMC_Field f_total = ESMC_FieldCreateGridTypeKind(
        grid, ESMC_TYPEKIND_R8, ESMC_STAGGERLOC_CENTER, NULL, NULL, NULL, "total_nox_emissions", &rc);
    CHECK_RC(rc, "ESMC_FieldCreate (export) failed");
    rc = ESMC_StateAddField(exportState, f_total);
    CHECK_RC(rc, "ESMC_StateAddField (export) failed");

    // 7. Initialize ACES component
    std::cout << "[NUOPC Driver] Initializing ACES component..." << std::endl;
    int rc_internal = 0;

    // For manual testing without full NUOPC driver, we call the entry points directly
    ACES_Initialize(acesComp, importState, exportState, &clock, &rc);
    CHECK_RC(rc, "ACES_Initialize failed");

    // 8. Main simulation loop
    std::cout << "[NUOPC Driver] Starting simulation loop..." << std::endl;
    for (int step = 0; step < 2; ++step) {
        std::cout << "--- Timestep " << step << " ---" << std::endl;
        ACES_Run(acesComp, importState, exportState, &clock, &rc);
        CHECK_RC(rc, "ACES_Run failed");
        rc = ESMC_ClockAdvance(clock);
        CHECK_RC(rc, "ESMC_ClockAdvance failed");
    }

    // 9. Finalize component and ESMF
    std::cout << "[NUOPC Driver] Finalizing ACES component..." << std::endl;
    ACES_Finalize(acesComp, importState, exportState, &clock, &rc);
    CHECK_RC(rc, "ACES_Finalize failed");

    // Cleanup
    ESMC_StateDestroy(&importState);
    ESMC_StateDestroy(&exportState);
    ESMC_GridDestroy(&grid);
    ESMC_GridCompDestroy(&acesComp);
    ESMC_ClockDestroy(&clock);
    ESMC_CalendarDestroy(&cal);

    ESMC_Finalize();
    std::cout << "[NUOPC Driver] ESMF Finalized. Standalone driver finished successfully." << std::endl;

    return 0;
}
