#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "ESMC.h"

/**
 * @file hemco_comparison_driver.cpp
 * @brief Driver for verifying HEMCO-like behavior in ACES with configurable parameters.
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
    std::cout << "[ComparisonDriver] ESMF Initialized." << "\n";

    const int nx = 10;
    const int ny = 10;
    const int nz = 1;

    std::array<int, 3> maxIndex3D = {nx, ny, nz};
    ESMC_InterArrayInt iMaxIndex;
    ESMC_InterArrayIntSet(&iMaxIndex, maxIndex3D.data(), 3);

    ESMC_Grid grid = ESMC_GridCreateNoPeriDim(&iMaxIndex, nullptr, nullptr, nullptr, &rc);
    ESMC_State importState = ESMC_StateCreate("ImportState", &rc);
    ESMC_State exportState = ESMC_StateCreate("ExportState", &rc);

    auto createField = [&](ESMC_State state, const char* name) {
        ESMC_Field field = ESMC_FieldCreateGridTypeKind(
            grid, ESMC_TYPEKIND_R8, ESMC_STAGGERLOC_CENTER, nullptr, nullptr, nullptr, name, &rc);
        ESMC_StateAddField(state, field);
        return field;
    };

    // Fields for DMS comparison
    ESMC_Field f_u10 = createField(importState, "wind_speed_10m");
    ESMC_Field f_tskin = createField(importState, "tskin");
    ESMC_Field f_seaconc = createField(importState, "DMS_seawater");
    ESMC_Field f_dms = createField(exportState, "dms");

    // Provide data for a single point
    double* u10_ptr = static_cast<double*>(ESMC_FieldGetPtr(f_u10, 0, &rc));
    double* tskin_ptr = static_cast<double*>(ESMC_FieldGetPtr(f_tskin, 0, &rc));
    double* seaconc_ptr = static_cast<double*>(ESMC_FieldGetPtr(f_seaconc, 0, &rc));
    double* dms_ptr = static_cast<double*>(ESMC_FieldGetPtr(f_dms, 0, &rc));

    for (int i = 0; i < nx * ny * nz; ++i) {
        u10_ptr[i] = 10.0;
        tskin_ptr[i] = 293.15;  // 20C
        seaconc_ptr[i] = 1.0e-6;
        dms_ptr[i] = 0.0;
    }

    ESMC_Time startTime;
    ESMC_Time stopTime;
    ESMC_Calendar cal = ESMC_CalendarCreate("Gregorian", ESMC_CALKIND_GREGORIAN, &rc);
    ESMC_TimeSet(&startTime, 2024, 1, cal, ESMC_CALKIND_GREGORIAN, 0);
    ESMC_TimeSet(&stopTime, 2024, 1, cal, ESMC_CALKIND_GREGORIAN, 2);
    ESMC_TimeInterval timeStep;
    ESMC_TimeIntervalSet(&timeStep, 1);
    ESMC_Clock clock = ESMC_ClockCreate("SimulationClock", timeStep, startTime, stopTime, &rc);

    ESMC_GridComp acesComp = ESMC_GridCompCreate("ACES", nullptr, clock, &rc);

    // Create a config file with custom coefficients for DMS
    const char* config_content =
        "physics_schemes:\n"
        "  - name: \"dms\"\n"
        "    options:\n"
        "      schmidt_coeff: [2674.0, -147.12, 3.726, -0.038]\n"
        "      kw_coeff: [0.222, 0.333]\n";

    FILE* fp = fopen("aces_config.yaml", "w");
    fputs(config_content, fp);
    fclose(fp);

    std::cout << "[ComparisonDriver] Initializing ACES component..." << "\n";
    ACES_Initialize(acesComp, importState, exportState, &clock, &rc);

    std::cout << "[ComparisonDriver] Running ACES..." << "\n";
    ACES_Run(acesComp, importState, exportState, &clock, &rc);

    // Verification logic
    // tc = 20.0
    // sc_w = 2674.0 + 20.0 * (-147.12 + 20.0 * (3.726 + 20.0 * -0.038))
    // sc_w = 2674.0 + 20.0 * (-147.12 + 20.0 * (2.966))
    // sc_w = 2674.0 + 20.0 * (-147.12 + 59.32)
    // sc_w = 2674.0 + 20.0 * (-87.8)
    // sc_w = 2674.0 - 1756.0 = 918.0

    // kw = (0.222 * 10^2 + 0.333 * 10) * (918 / 600)^-0.5
    // kw = (22.2 + 3.33) * (1.53)^-0.5
    // kw = 25.53 * 0.808 = 20.63 cm/hr
    // kw = 20.63 / 360000 = 5.73e-5 m/s

    // expected = 5.73e-5 * 1.0e-6 = 5.73e-11

    double expected = 5.73e-11;
    bool success = true;
    for (int i = 0; i < nx * ny * nz; ++i) {
        if (std::abs(dms_ptr[i] - expected) / expected > 0.01) {
            std::cerr << "Mismatch at " << i << ": expected ~" << expected << ", got " << dms_ptr[i]
                      << "\n";
            success = false;
            break;
        }
    }

    if (success) {
        std::cout << "[ComparisonDriver] DMS Verification SUCCESSFUL!" << "\n";
    } else {
        std::cout << "[ComparisonDriver] DMS Verification FAILED!" << "\n";
    }

    ACES_Finalize(acesComp, importState, exportState, &clock, &rc);
    ESMC_GridCompDestroy(&acesComp);
    ESMC_Finalize();

    return success ? 0 : 1;
}
