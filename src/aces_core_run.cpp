/**
 * @file aces_core_run.cpp
 * @brief ACES Run phase -- ESMF-free C++ core.
 *
 * Time info (hour, day_of_week) is extracted in the Fortran cap via ESMF_ClockGet
 * and passed as plain integers. No ESMC.h dependency.
 */

#include <iostream>
#include <stdexcept>
#include <chrono>
#include <thread>

#include "aces/aces_internal.hpp"
#include "aces/aces_stacking_engine.hpp"
#include "aces/aces_state.hpp"
#include "aces/physics_scheme.hpp"

extern "C" {

/**
 * @brief ACES Run phase.
 * @param data_ptr    Pointer to AcesInternalData.
 * @param hour        Hour of day (0-23), extracted by Fortran cap from ESMF_Clock.
 * @param day_of_week Day of week (0=Sunday..6=Saturday).
 * @param rc          0 on success, -1 on failure.
 */
void aces_core_run(void* data_ptr, int hour, int day_of_week, int* rc) {
    *rc = 0;
    try {
        std::cout << "ACES_Run: executing step (hour=" << hour
                  << ", day=" << day_of_week << ")\n";

        auto* d = static_cast<aces::AcesInternalData*>(data_ptr);
        if (!d) { std::cerr << "ACES_Run: null data_ptr\n"; *rc = -1; return; }

        // Ingest emissions from configured streams before stacking
        if (!d->config.aces_data.streams.empty()) {
            try {
                d->ingestor.IngestEmissionsInline(d->config.aces_data, d->import_state, d->nx, d->ny, d->nz);
            } catch (const std::exception& e) {
                std::cerr << "ACES_Run: ingest failed: " << e.what() << "\n";
            } catch (...) {
                std::cerr << "ACES_Run: ingest failed (unknown)\n";
            }
        }

        if (d->stacking_engine) {
            aces::AcesStateResolver resolver(
                d->import_state, d->export_state,
                d->config.met_mapping,
                d->config.scale_factor_mapping,
                d->config.mask_mapping);
            d->stacking_engine->Execute(
                resolver, d->nx, d->ny, d->nz,
                d->default_mask, hour, day_of_week);
        }

        for (auto& scheme : d->active_schemes) {
            if (scheme) {
                try { scheme->Run(d->import_state, d->export_state); }
                catch (const std::exception& e) {
                    std::cerr << "ACES_Run: scheme: " << e.what() << "\n";
                }
            }
        }

        for (auto& [name, field] : d->export_state.fields) {
            field.sync_host();
        }

        // Critical: Comprehensive Kokkos synchronization for large grids
        int total_points = d->nx * d->ny;
        if (total_points > 50000) {  // Large grid threshold (e.g., >223x223)
            std::cout << "INFO: [ACES] Large grid (" << total_points
                      << " points) - forcing comprehensive device synchronization..." << std::endl;

            // Multiple Kokkos fence barriers for bulletproof synchronization
            Kokkos::fence("ACES::Run::LargeGrid::Barrier1");
            Kokkos::fence("ACES::Run::LargeGrid::Barrier2");
            Kokkos::fence("ACES::Run::LargeGrid::Barrier3");

            std::cout.flush();  // Ensure I/O completion
            std::cout << "INFO: [ACES] Large grid synchronization complete" << std::endl;
        } else {
            // Single barrier for smaller grids
            Kokkos::fence("ACES::Run::StandardGrid");
        }

    } catch (const std::exception& e) {
        std::cerr << "ACES_Run: " << e.what() << "\n"; *rc = -1;
    } catch (...) { std::cerr << "ACES_Run: unknown\n"; *rc = -1; }
}

}  // extern "C"
