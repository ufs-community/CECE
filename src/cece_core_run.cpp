/**
 * @file cece_core_run.cpp
 * @brief Implementation of CECE Run phase execution for NUOPC integration.
 *
 * This module provides the core computational loop for CECE emission processing.
 * It coordinates data ingestion from TIDE streams, physics scheme execution,
 * and field stacking operations during each model timestep.
 *
 * The run phase operates independently of ESMF, receiving only time information
 * extracted by the Fortran cap. This design maintains separation of concerns
 * and allows for easier testing and debugging.
 *
 * Key responsibilities:
 * - Time-dependent field ingestion from TIDE data streams
 * - Coordination of physics scheme execution
 * - Emission layer stacking and combination
 * - Error handling and performance monitoring
 *
 * @note This is an ESMF-free C++ implementation called from Fortran cap
 * @note Time info (hour, day_of_week) extracted via ESMF_ClockGet in Fortran
 * @note No ESMC.h dependency for easier testing and deployment
 *
 * @author Barry Baker
 * @date 2024
 * @version 1.0
 */

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "cece/cece_internal.hpp"
#include "cece/cece_stacking_engine.hpp"
#include "cece/cece_state.hpp"
#include "cece/physics_scheme.hpp"

extern "C" {

/**
 * @brief CECE Run phase.
 * @param data_ptr    Pointer to CeceInternalData.
 * @param hour        Hour of day (0-23), extracted by Fortran cap from ESMF_Clock.
 * @param day_of_week Day of week (0=Sunday..6=Saturday).
 * @param rc          0 on success, -1 on failure.
 */
void cece_core_run(void* data_ptr, int hour, int day_of_week, int* rc) {
    *rc = 0;
    try {
        std::cout << "CECE_Run: executing step (hour=" << hour << ", day=" << day_of_week << ")\n";

        auto* d = static_cast<cece::CeceInternalData*>(data_ptr);
        if (!d) {
            std::cerr << "CECE_Run: null data_ptr\n";
            *rc = -1;
            return;
        }

        // Ingest emissions from configured streams before stacking
        if (!d->config.cece_data.streams.empty()) {
            try {
                d->ingestor.IngestEmissionsInline(d->config.cece_data, d->import_state, d->nx, d->ny, d->nz);
            } catch (const std::exception& e) {
                std::cerr << "CECE_Run: ingest failed: " << e.what() << "\n";
            } catch (...) {
                std::cerr << "CECE_Run: ingest failed (unknown)\n";
            }
        }

        if (d->stacking_engine) {
            cece::CeceStateResolver resolver(d->import_state, d->export_state, d->config.met_mapping, d->config.scale_factor_mapping,
                                             d->config.mask_mapping);
            d->stacking_engine->Execute(resolver, d->nx, d->ny, d->nz, d->default_mask, hour, day_of_week);
        }

        for (auto& scheme : d->active_schemes) {
            if (scheme) {
                try {
                    scheme->Run(d->import_state, d->export_state);
                } catch (const std::exception& e) {
                    std::cerr << "CECE_Run: scheme: " << e.what() << "\n";
                }
            }
        }

        for (auto& [name, field] : d->export_state.fields) {
            field.sync_host();
        }

        // Also sync import state fields to ensure ESMF can access them
        for (auto& [name, field] : d->import_state.fields) {
            field.sync_host();
        }

        // Critical: Kokkos synchronization to ensure all device operations complete
        Kokkos::fence("CECE::Run::PostStep");

    } catch (const std::exception& e) {
        std::cerr << "CECE_Run: " << e.what() << "\n";
        *rc = -1;
    } catch (...) {
        std::cerr << "CECE_Run: unknown\n";
        *rc = -1;
    }
}

}  // extern "C"
