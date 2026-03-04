#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "ESMC.h"

/**
 * @file example_driver.cpp
 * @brief Reference implementation of a standalone driver for the ACES
 * component.
 *
 * This driver demonstrates how to integrate ACES into a larger Earth System
 * Model (ESM). it simulates external components providing input meteorology and
 * base emissions, manages the ESMF lifecycle (Initialize, Run, Finalize), and
 * handles diagnostic I/O.
 */

// Explicitly declare ACES component entry points (normally provided by
// NUOPC/ESMF)
extern "C" {
void ACES_Initialize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState,
                     ESMC_Clock* clock, int* rc);
void ACES_Run(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock,
              int* rc);
void ACES_Finalize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState,
                   ESMC_Clock* clock, int* rc);
}

/**
 * @brief Simulates an Atmosphere component (e.g., UFS) that provides
 * meteorology.
 *
 * In a real model, this data would come from the atmosphere's own state or
 * be received via a coupler.
 *
 * @param f_temp Field for air temperature.
 * @param f_wind Field for wind speed.
 * @param step Current simulation timestep index.
 * @param nx X dimension size.
 * @param ny Y dimension size.
 * @param nz Z dimension size (levels).
 */
void Atmosphere_ProvideData(ESMC_Field f_temp, ESMC_Field f_wind, int step, int nx, int ny,
                            int nz) {
    int rc;
    double* temp_ptr = static_cast<double*>(ESMC_FieldGetPtr(f_temp, 0, &rc));
    double* wind_ptr = static_cast<double*>(ESMC_FieldGetPtr(f_wind, 0, &rc));

    for (int i = 0; i < nx * ny * nz; ++i) {
        // Create some time-varying dummy data
        temp_ptr[i] = 280.0 + 10.0 * std::sin(step * 0.1 + i * 0.0001);
        wind_ptr[i] = 5.0 + 2.0 * std::cos(step * 0.1 + i * 0.0001);
    }
}

/**
 * @brief Simulates an Anthropogenic Emissions component (e.g., CDEPS/HEMCO).
 *
 * Provides the base inventory emissions that ACES will process.
 *
 * @param f_anthro Field for base anthropogenic emissions.
 * @param nx X dimension size.
 * @param ny Y dimension size.
 * @param nz Z dimension size.
 */
void Anthro_ProvideData(ESMC_Field f_anthro, int nx, int ny, int nz) {
    int rc;
    double* anthro_ptr = static_cast<double*>(ESMC_FieldGetPtr(f_anthro, 0, &rc));
    for (int i = 0; i < nx * ny * nz; ++i) {
        anthro_ptr[i] = 1.0e-9;
    }
}

/**
 * @brief Main simulation entry point.
 */
int main(int argc, char** argv) {
    int rc;

    // 1. Initialize ESMF Framework
    ESMC_Initialize(nullptr, ESMC_ArgLast);
    std::cout << "[Driver] ESMF Initialized." << "\n";

    // 2. Define simulation grid dimensions
    const int nx = 72;
    const int ny = 46;
    const int nz = 10;

    // 3. Create ESMF Grid and decomposition
    int maxIndex3D[3] = {nx, ny, nz};
    ESMC_InterArrayInt iMaxIndex;
    ESMC_InterArrayIntSet(&iMaxIndex, maxIndex3D, 3);

    ESMC_Grid grid = ESMC_GridCreateNoPeriDim(&iMaxIndex, nullptr, nullptr, nullptr, &rc);
    if (rc != ESMF_SUCCESS) {
        std::cerr << "[Driver] Error creating grid" << "\n";
        return 1;
    }

    // 4. Create ESMF States for data coupling
    ESMC_State importState = ESMC_StateCreate("ImportState", &rc);
    ESMC_State exportState = ESMC_StateCreate("ExportState", &rc);

    // 5. Create ESMF Fields and add them to the states
    auto createField = [&](ESMC_State state, const char* name) {
        ESMC_Field field = ESMC_FieldCreateGridTypeKind(
            grid, ESMC_TYPEKIND_R8, ESMC_STAGGERLOC_CENTER, nullptr, nullptr, nullptr, name, &rc);
        ESMC_StateAddField(state, field);
        return field;
    };

    ESMC_Field f_temp = createField(importState, "temperature");
    ESMC_Field f_wind = createField(importState, "wind_speed_10m");
    ESMC_Field f_anthro = createField(importState, "base_anthropogenic_nox");
    ESMC_Field f_total = createField(exportState, "nox");

    // 6. Create simulation clock
    ESMC_Time startTime;
    ESMC_Time stopTime;
    ESMC_Calendar cal = ESMC_CalendarCreate("Gregorian", ESMC_CALKIND_GREGORIAN, &rc);
    ESMC_TimeSet(&startTime, 2024, 1, cal, ESMC_CALKIND_GREGORIAN, 0);
    ESMC_TimeSet(&stopTime, 2024, 1, cal, ESMC_CALKIND_GREGORIAN, 24);
    ESMC_TimeInterval timeStep;
    ESMC_TimeIntervalSet(&timeStep, 1);
    ESMC_Clock clock = ESMC_ClockCreate("SimulationClock", timeStep, startTime, stopTime, &rc);

    // 7. Initialize ACES component
    ESMC_GridComp acesComp;
    acesComp.ptr = (void*)0x1;  // Standalone dummy handle used for state tracking

    // Setup component configuration file
    std::system(
        "echo 'species:\n  nox:\n    - field: \"base_anthropogenic_nox\"\n      "
        "operation: "
        "\"add\"\n      scale: 1.0\n    - field: \"temperature\"\n      "
        "operation: \"add\"\n      "
        "scale: 1.0e-12\nphysics_schemes: []' > aces_config.yaml");

    std::cout << "[Driver] Initializing ACES component..." << "\n";
    ACES_Initialize(acesComp, importState, exportState, &clock, &rc);

    // 8. Main simulation loop
    std::cout << "[Driver] Starting simulation loop (5 timesteps)..." << "\n";

    for (int step = 0; step < 5; ++step) {
        std::cout << "--- Timestep " << step << " ---" << "\n";

        // STEP A: External components generate/provide data
        Atmosphere_ProvideData(f_temp, f_wind, step, nx, ny, nz);
        Anthro_ProvideData(f_anthro, nx, ny, nz);

        // STEP B: Run the ACES emissions component
        ACES_Run(acesComp, importState, exportState, &clock, &rc);

        // STEP C: Process results (Diagnostics and I/O)
        double* total_ptr = static_cast<double*>(ESMC_FieldGetPtr(f_total, 0, &rc));
        if (total_ptr != nullptr) {
            std::cout << "  [Driver] nox[0]: " << total_ptr[0] << "\n";
        }

        // Write the calculated emissions to a NetCDF file using ESMF I/O
        char filename[64];
        std::sprintf(filename, "output_step_%d.nc", step);
        std::cout << "  [Driver] Writing diagnostic to " << filename << "\n";
        ESMC_FieldWrite(f_total, filename, "total_nox", 1, ESMC_FILESTATUS_REPLACE, step + 1,
                        ESMF_IOFMT_NETCDF);

        // STEP D: Advance simulation clock
        ESMC_ClockAdvance(clock);
    }

    // 9. Finalize component and ESMF
    std::cout << "[Driver] Finalizing ACES component..." << "\n";
    ACES_Finalize(acesComp, importState, exportState, &clock, &rc);

    // Cleanup ESMF objects
    ESMC_StateDestroy(&importState);
    ESMC_StateDestroy(&exportState);
    ESMC_GridDestroy(&grid);
    ESMC_ClockDestroy(&clock);
    ESMC_CalendarDestroy(&cal);

    ESMC_Finalize();
    std::cout << "[Driver] ESMF Finalized. Dummy driver finished successfully." << "\n";

    return 0;
}
