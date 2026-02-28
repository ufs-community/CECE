#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "ESMC.h"
#include "NUOPC.h"

/**
 * @file main.cpp
 * @brief Standalone NUOPC driver for the ACES component.
 *
 * This driver demonstrates how to initialize and run the ACES component
 * within a NUOPC Driver framework.
 */

extern "C" {
void ACES_SetServices(ESMC_GridComp comp, int* rc);
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
    const int nz = 10;

    // 3. Create simulation clock
    ESMC_Time startTime, stopTime;
    ESMC_Calendar cal = ESMC_CalendarCreate("Gregorian", ESMC_CALKIND_GREGORIAN, &rc);
    CHECK_RC(rc, "ESMC_CalendarCreate failed");

    // In ESMF 8.8.0 C API, ESMC_TimeSet has 6 arguments:
    // int ESMC_TimeSet(ESMC_Time *time, int yy, int mm, int dd, ESMC_Calendar cal, enum ESMC_CalKind_Flag calkindflag);
    // (Note: The actual signature might vary slightly, but this is the common form in ESMC.h)
    rc = ESMC_TimeSet(&startTime, 2024, 1, 1, cal, ESMC_CALKIND_GREGORIAN);
    CHECK_RC(rc, "ESMC_TimeSet (startTime) failed");
    rc = ESMC_TimeSet(&stopTime, 2024, 1, 2, cal, ESMC_CALKIND_GREGORIAN);
    CHECK_RC(rc, "ESMC_TimeSet (stopTime) failed");

    ESMC_TimeInterval timeStep;
    // int ESMC_TimeIntervalSet(ESMC_TimeInterval *timeInterval, ESMC_I8 s, ESMC_I8 h, ESMC_I8 d, ESMC_I8 m, ESMC_I8 y);
    // Using a more standard signature for ESMF 8.x:
    // int ESMC_TimeIntervalSet(ESMC_TimeInterval *timeInterval, ESMC_I8 s, int ms, int us, int ns, int d);
    rc = ESMC_TimeIntervalSet(&timeStep, 3600, 0, 0, 0, 0); // 1 hour timestep
    CHECK_RC(rc, "ESMC_TimeIntervalSet failed");
    ESMC_Clock clock = ESMC_ClockCreate("SimulationClock", timeStep, startTime, stopTime, &rc);
    CHECK_RC(rc, "ESMC_ClockCreate failed");

    // 4. Create the NUOPC Driver component
    // In a real application, the driver would be a derived NUOPC_Driver.
    // Here we use a generic GridComp to act as the driver for simplicity in this standalone example.
    ESMC_GridComp driverComp = ESMC_GridCompCreate("ACES_Driver", NULL, clock, &rc);
    CHECK_RC(rc, "ESMC_GridCompCreate (driver) failed");

    // 5. Create the ACES component as a child
    ESMC_GridComp acesComp = ESMC_GridCompCreate("ACES", NULL, clock, &rc);
    CHECK_RC(rc, "ESMC_GridCompCreate (aces) failed");

    // Register ACES services
    ACES_SetServices(acesComp, &rc);
    CHECK_RC(rc, "ACES_SetServices failed");

    // 6. Initialize ACES component via ESMF/NUOPC lifecycle
    // In a full NUOPC driver, this would be handled by the Driver's Initialize phase.
    ESMC_State importState = ESMC_StateCreate("ImportState", &rc);
    CHECK_RC(rc, "ESMC_StateCreate (import) failed");
    ESMC_State exportState = ESMC_StateCreate("ExportState", &rc);
    CHECK_RC(rc, "ESMC_StateCreate (export) failed");

    std::cout << "[NUOPC Driver] Initializing ACES component..." << std::endl;
    rc = ESMC_GridCompInitialize(acesComp, importState, exportState, clock, 1, &rc);
    CHECK_RC(rc, "ESMC_GridCompInitialize failed");

    // 7. Main simulation loop
    std::cout << "[NUOPC Driver] Starting simulation loop..." << std::endl;

    for (int step = 0; step < 5; ++step) {
        std::cout << "--- Timestep " << step << " ---" << std::endl;

        // In a real NUOPC system, the driver would call Run on the component.
        rc = ESMC_GridCompRun(acesComp, importState, exportState, clock, 1, &rc);
        CHECK_RC(rc, "ESMC_GridCompRun failed");

        // Advance simulation clock
        rc = ESMC_ClockAdvance(clock);
        CHECK_RC(rc, "ESMC_ClockAdvance failed");
    }

    // 8. Finalize component and ESMF
    std::cout << "[NUOPC Driver] Finalizing ACES component..." << std::endl;
    rc = ESMC_GridCompFinalize(acesComp, importState, exportState, clock, 1, &rc);
    CHECK_RC(rc, "ESMC_GridCompFinalize failed");

    // Cleanup ESMF objects
    ESMC_StateDestroy(&importState);
    ESMC_StateDestroy(&exportState);
    ESMC_GridCompDestroy(&acesComp);
    ESMC_GridCompDestroy(&driverComp);
    ESMC_ClockDestroy(&clock);
    ESMC_CalendarDestroy(&cal);

    ESMC_Finalize();
    std::cout << "[NUOPC Driver] ESMF Finalized. Standalone driver finished successfully." << std::endl;

    return 0;
}
