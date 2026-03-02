#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "ESMC.h"

/**
 * @file hemco_mimic_driver.cpp
 * @brief Driver for verifying HEMCO regional override case.
 */

extern "C" {
void ACES_Initialize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState,
                     ESMC_Clock* clock, int* rc);
void ACES_Run(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock,
              int* rc);
void ACES_Finalize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState,
                   ESMC_Clock* clock, int* rc);
}

int main(int argc, char** argv) {
    int rc;

    ESMC_Initialize(nullptr, ESMC_ArgLast);
    std::cout << "[Driver] ESMF Initialized." << "\n";

    const int nx = 72;
    const int ny = 46;
    const int nz = 1;

    int maxIndex3D[3] = {nx, ny, nz};
    ESMC_InterArrayInt iMaxIndex;
    ESMC_InterArrayIntSet(&iMaxIndex, maxIndex3D, 3);

    ESMC_Grid grid = ESMC_GridCreateNoPeriDim(&iMaxIndex, nullptr, nullptr, nullptr, &rc);
    ESMC_State importState = ESMC_StateCreate("ImportState", &rc);
    ESMC_State exportState = ESMC_StateCreate("ExportState", &rc);

    auto createField = [&](ESMC_State state, const char* name) {
        ESMC_Field field = ESMC_FieldCreateGridTypeKind(
            grid, ESMC_TYPEKIND_R8, ESMC_STAGGERLOC_CENTER, nullptr, nullptr, nullptr, name, &rc);
        ESMC_StateAddField(state, field);
        return field;
    };

    ESMC_Field f_base = createField(importState, "base_nox");
    ESMC_Field f_euro = createField(importState, "europe_nox");
    ESMC_Field f_mask = createField(importState, "mask_europe");
    ESMC_Field f_total = createField(exportState, "total_nox_emissions");

    // Provide data
    double* base_ptr = static_cast<double*>(ESMC_FieldGetPtr(f_base, 0, &rc));
    double* euro_ptr = static_cast<double*>(ESMC_FieldGetPtr(f_euro, 0, &rc));
    double* mask_ptr = static_cast<double*>(ESMC_FieldGetPtr(f_mask, 0, &rc));
    for (int i = 0; i < nx * ny * nz; ++i) {
        base_ptr[i] = 100.0;
        euro_ptr[i] = 120.0;
        // Mask for roughly the eastern half
        int x = i % nx;
        mask_ptr[i] = (x > nx / 2) ? 1.0 : 0.0;
    }

    ESMC_Time startTime;
    ESMC_Time stopTime;
    ESMC_Calendar cal = ESMC_CalendarCreate("Gregorian", ESMC_CALKIND_GREGORIAN, &rc);
    ESMC_TimeSet(&startTime, 2024, 1, cal, ESMC_CALKIND_GREGORIAN, 0);
    ESMC_TimeSet(&stopTime, 2024, 1, cal, ESMC_CALKIND_GREGORIAN, 2);
    ESMC_TimeInterval timeStep;
    ESMC_TimeIntervalSet(&timeStep, 1);
    ESMC_Clock clock = ESMC_ClockCreate("SimulationClock", timeStep, startTime, stopTime, &rc);

    // In ESMF 8.8.0 C API, ESMC_GridCompCreate has 4 arguments
    ESMC_GridComp acesComp = ESMC_GridCompCreate("ACES", nullptr, clock, &rc);

    // Use the new config file
    std::system("cp ../hemco_mimic_config.yaml aces_config.yaml");

    std::cout << "[Driver] Initializing ACES component..." << "\n";
    ACES_Initialize(acesComp, importState, exportState, &clock, &rc);

    std::cout << "[Driver] Running ACES..." << "\n";
    ACES_Run(acesComp, importState, exportState, &clock, &rc);

    // Verification
    double* total_ptr = static_cast<double*>(ESMC_FieldGetPtr(f_total, 0, &rc));
    bool success = true;
    for (int i = 0; i < nx * ny * nz; ++i) {
        int x = i % nx;
        double expected = (x > nx / 2) ? (120.0 * 1.2) : 100.0;
        if (std::abs(total_ptr[i] - expected) > 1e-6) {
            std::cerr << "Mismatch at " << i << " (x=" << x << "): expected " << expected
                      << ", got " << total_ptr[i] << "\n";
            success = false;
            break;
        }
    }

    if (success) {
        std::cout << "[Driver] Verification SUCCESSFUL!" << "\n";
    } else {
        std::cout << "[Driver] Verification FAILED!" << "\n";
    }

    ACES_Finalize(acesComp, importState, exportState, &clock, &rc);
    ESMC_GridCompDestroy(&acesComp);
    ESMC_Finalize();

    return success ? 0 : 1;
}
