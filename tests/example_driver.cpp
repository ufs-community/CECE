#include "ESMC.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdlib>

// Explicitly declare component entry points
extern "C" {
void ACES_Initialize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock, int* rc);
void ACES_Run(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock, int* rc);
void ACES_Finalize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock, int* rc);
}

// Global registry to simulate ESMF internal state if ESMC_GridCompSetInternalState is not usable
// in this standalone driver context.
static void* global_internal_data = nullptr;

extern "C" {
/**
 * @brief Mock implementation of ESMC_GridCompSetInternalState for standalone driver.
 *
 * The ACES cap uses weak symbols for these, so if the driver provides them,
 * they take precedence over the real ESMF calls. This allows the driver to
 * provide storage for the component's internal state even if ESMC_GridCompCreate
 * doesn't return a fully functional handle in this specific environment.
 */
int Mock_ESMC_GridCompSetInternalState(ESMC_GridComp comp, void* data) {
    global_internal_data = data;
    return ESMF_SUCCESS;
}

/**
 * @brief Mock implementation of ESMC_GridCompGetInternalState for standalone driver.
 */
void* Mock_ESMC_GridCompGetInternalState(ESMC_GridComp comp, int* rc) {
    if (rc) *rc = ESMF_SUCCESS;
    return global_internal_data;
}
}

/**
 * @brief Simulates an Atmosphere component that provides input data.
 */
void Atmosphere_ProvideData(ESMC_Field f_temp, ESMC_Field f_wind, int step, int nx, int ny, int nz) {
    int rc;
    double* temp_ptr = (double*)ESMC_FieldGetPtr(f_temp, 0, &rc);
    double* wind_ptr = (double*)ESMC_FieldGetPtr(f_wind, 0, &rc);

    for (int i = 0; i < nx * ny * nz; ++i) {
        temp_ptr[i] = 280.0 + 10.0 * std::sin(step * 0.1 + i * 0.0001);
        wind_ptr[i] = 5.0 + 2.0 * std::cos(step * 0.1 + i * 0.0001);
    }
}

/**
 * @brief Simulates an Anthropogenic Emissions component that provides base inventory.
 */
void Anthro_ProvideData(ESMC_Field f_anthro, int nx, int ny, int nz) {
    int rc;
    double* anthro_ptr = (double*)ESMC_FieldGetPtr(f_anthro, 0, &rc);
    for (int i = 0; i < nx * ny * nz; ++i) {
        anthro_ptr[i] = 1.0e-9;
    }
}

/**
 * @brief Main driver program.
 */
int main(int argc, char** argv) {
    int rc;

    // 1. Initialize ESMF
    ESMC_Initialize(NULL, ESMC_ArgLast);
    std::cout << "[Driver] ESMF Initialized." << std::endl;

    // 2. Define grid dimensions
    const int nx = 72;
    const int ny = 46;
    const int nz = 10;

    // 3. Create ESMF Grid
    int maxIndex3D[3] = {nx, ny, nz};
    ESMC_InterArrayInt iMaxIndex;
    ESMC_InterArrayIntSet(&iMaxIndex, maxIndex3D, 3);

    ESMC_Grid grid = ESMC_GridCreateNoPeriDim(&iMaxIndex, NULL, NULL, NULL, &rc);
    if (rc != ESMF_SUCCESS) {
        std::cerr << "[Driver] Error creating grid" << std::endl;
        return 1;
    }

    // 4. Create Import and Export States
    ESMC_State importState = ESMC_StateCreate("ImportState", &rc);
    ESMC_State exportState = ESMC_StateCreate("ExportState", &rc);

    // 5. Create Fields and add to States
    auto createField = [&](ESMC_State state, const char* name) {
        ESMC_Field field = ESMC_FieldCreateGridTypeKind(grid, ESMC_TYPEKIND_R8, ESMC_STAGGERLOC_CENTER, NULL, NULL, NULL, name, &rc);
        ESMC_StateAddField(state, field);
        return field;
    };

    ESMC_Field f_temp = createField(importState, "temperature");
    ESMC_Field f_wind = createField(importState, "wind_speed_10m");
    ESMC_Field f_anthro = createField(importState, "base_anthropogenic_nox");
    ESMC_Field f_total = createField(exportState, "total_nox_emissions");

    // 6. Create Clock for simulation control
    ESMC_Time startTime, stopTime;
    ESMC_Calendar cal = ESMC_CalendarCreate("Gregorian", ESMC_CALKIND_GREGORIAN, &rc);
    ESMC_TimeSet(&startTime, 2024, 1, cal, ESMC_CALKIND_GREGORIAN, 0);
    ESMC_TimeSet(&stopTime, 2024, 1, cal, ESMC_CALKIND_GREGORIAN, 24);
    ESMC_TimeInterval timeStep;
    ESMC_TimeIntervalSet(&timeStep, 1);
    ESMC_Clock clock = ESMC_ClockCreate("SimulationClock", timeStep, startTime, stopTime, &rc);

    // 7. Initialize ACES component
    ESMC_GridComp acesComp;
    acesComp.ptr = (void*)0x1; // Standalone dummy handle

    // Prepare configuration file
    std::system("echo 'species:\n  nox:\n    - field: \"base_anthropogenic_nox\"\n      operation: \"add\"\n      scale: 1.0\n    - field: \"temperature\"\n      operation: \"add\"\n      scale: 1.0e-12\nphysics_schemes: []' > aces_config.yaml");

    std::cout << "[Driver] Initializing ACES component..." << std::endl;
    ACES_Initialize(acesComp, importState, exportState, &clock, &rc);

    // 8. Simulation Loop
    std::cout << "[Driver] Starting simulation loop (5 timesteps)..." << std::endl;

    for (int step = 0; step < 5; ++step) {
        std::cout << "--- Timestep " << step << " ---" << std::endl;

        // Simulate external data providers
        Atmosphere_ProvideData(f_temp, f_wind, step, nx, ny, nz);
        Anthro_ProvideData(f_anthro, nx, ny, nz);

        // Execute ACES component
        ACES_Run(acesComp, importState, exportState, &clock, &rc);

        // Show sample result
        double* total_ptr = (double*)ESMC_FieldGetPtr(f_total, 0, &rc);
        if (total_ptr) {
            std::cout << "  [Driver] total_nox_emissions[0]: " << total_ptr[0] << std::endl;
        }

        // Diagnostic output using ESMF I/O
        char filename[64];
        std::sprintf(filename, "output_step_%d.nc", step);
        std::cout << "  [Driver] Writing diagnostic to " << filename << std::endl;
        ESMC_FieldWrite(f_total, filename, "total_nox", 1, ESMC_FILESTATUS_REPLACE, step + 1, ESMF_IOFMT_NETCDF);

        // Advance simulation time
        ESMC_ClockAdvance(clock);
    }

    // 9. Finalize
    std::cout << "[Driver] Finalizing ACES component..." << std::endl;
    ACES_Finalize(acesComp, importState, exportState, &clock, &rc);

    // Cleanup ESMF objects
    ESMC_StateDestroy(&importState);
    ESMC_StateDestroy(&exportState);
    ESMC_GridDestroy(&grid);
    ESMC_ClockDestroy(&clock);
    ESMC_CalendarDestroy(&cal);

    ESMC_Finalize();
    std::cout << "[Driver] ESMF Finalized. Dummy driver finished successfully." << std::endl;

    return 0;
}
