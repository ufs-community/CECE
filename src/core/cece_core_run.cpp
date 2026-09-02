/**
 * @file cece_core_run.cpp
 * @brief Implementation of CECE Run phase execution for NUOPC integration.
 *
 * This module provides the core computational loop for CECE emission processing.
 * It coordinates data ingestion from TIDE streams, physics scheme execution,
 * and field stacking operations during each model timestep.
 *
 * The run phase operates independently of the host framework, receiving only time information
 * extracted by the calling driver. This design maintains separation of concerns
 * and allows for easier testing and debugging.
 *
 * Key responsibilities:
 * - Time-dependent field ingestion from TIDE data streams
 * - Coordination of physics scheme execution
 * - Emission layer stacking and combination
 * - Error handling and performance monitoring
 *
 * @note This is a framework-free C++ implementation called from the driver
 * @note Time info (hour, day_of_week) extracted by the calling driver
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

#include "cece/cece_clock.hpp"
#include "cece/cece_internal.hpp"
#include "cece/cece_logger.hpp"
#include "cece/cece_stacking_engine.hpp"
#include "cece/cece_state.hpp"
#include "cece/physics_scheme.hpp"

namespace {

// These helpers deliberately do not catch failures. Ingestion, physics, and
// stacking exceptions must reach the cece_core_run C-ABI boundary below,
// which reports them through rc=-1.
void IngestEmissions(cece::CeceInternalData& d) {
    if (d.config.cece_data.streams.empty()) return;
    d.ingestor.IngestEmissionsInline(d.config.cece_data, d.import_state, d.nx, d.ny, d.nz);
}

void RunPhysicsSchemeByName(cece::CeceInternalData& d, const std::string& scheme_name) {
    for (size_t i = 0; i < d.active_schemes.size(); ++i) {
        if (i < d.config.physics_schemes.size() && d.config.physics_schemes[i].name == scheme_name) {
            if (d.active_schemes[i]) {
                d.active_schemes[i]->Run(d.import_state, d.export_state);
            }
            break;
        }
    }
}

void ExecuteStackingEngine(cece::CeceInternalData& d, int hour, int day_of_week, int month = 0) {
    if (d.stacking_engine) {
        cece::CeceStateResolver resolver(d.import_state, d.export_state, d.config.met_mapping, d.config.scale_factor_mapping, d.config.mask_mapping);
        d.stacking_engine->Execute(resolver, d.nx, d.ny, d.nz, d.default_mask, hour, day_of_week, month);
    }
}

void ExecuteClockComponent(cece::CeceInternalData& d, const cece::ClockComponent* comp, const cece::StepResult& step, bool& ingested) {
    switch (comp->type) {
        case cece::ComponentType::kDataStream: {
            if (!ingested) {
                IngestEmissions(d);
                ingested = true;
            }
            break;
        }
        case cece::ComponentType::kPhysicsScheme: {
            RunPhysicsSchemeByName(d, comp->name);
            break;
        }
        case cece::ComponentType::kStackingEngine: {
            ExecuteStackingEngine(d, step.hour_of_day, step.day_of_week, step.month);
            break;
        }
    }
}

void ExecuteStepClockGated(cece::CeceInternalData& d, int* rc) {
    cece::StepResult step = d.clock->Advance();

    if (step.due_components.empty()) {
        if (step.simulation_complete) {
            *rc = 1;
        }
        return;
    }

    std::cout << "CECE_Run: executing step (hour=" << step.hour_of_day << ", day_of_week=" << step.day_of_week << ", elapsed=" << step.elapsed_seconds
              << ")\n";

    bool ingested = false;
    for (const auto* comp : step.due_components) {
        ExecuteClockComponent(d, comp, step, ingested);
    }

    if (step.simulation_complete) {
        *rc = 1;
    }
}

void ExecuteStepUnconditional(cece::CeceInternalData& d, int hour, int day_of_week) {
    std::cout << "CECE_Run: executing step (hour=" << hour << ", day_of_week=" << day_of_week << ")\n";

    IngestEmissions(d);

    for (auto& scheme : d.active_schemes) {
        if (scheme) {
            scheme->Run(d.import_state, d.export_state);
        }
    }

    ExecuteStackingEngine(d, hour, day_of_week, 0);
}

void SyncAndCopyState(cece::CeceInternalData& d) {
    // Mark all export fields as modified on the device since they are computed on the device
    // by the Stacking Engine and physics schemes, ensuring Kokkos copies device updates to host.
    // Also copy the synced host values of managed views back to the persistent unmanaged export pointers!
    for (auto& [name, field] : d.export_state.fields) {
        field.modify<Kokkos::DefaultExecutionSpace>();
        field.sync_host();

        auto it = d.persistent_export_ptrs.find(name);
        if (it != d.persistent_export_ptrs.end() && it->second != nullptr) {
            using UnmanagedHost = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace, Kokkos::MemoryTraits<Kokkos::Unmanaged>>;
            UnmanagedHost h_view(it->second, d.nx, d.ny, d.nz);
            Kokkos::deep_copy(h_view, field.view_host());
        }
    }

    // Also sync import state fields to ensure the host framework can access them
    for (auto& [name, field] : d.import_state.fields) {
        field.sync_host();
    }

    // Critical: Kokkos synchronization to ensure all device operations complete
    Kokkos::fence("CECE::Run::PostStep");
}

}  // namespace

extern "C" {

/**
 * @brief CECE Run phase.
 * @param data_ptr    Pointer to CeceInternalData.
 * @param hour        Hour of day (0-23), extracted by the driver from the simulation clock.
 * @param day_of_week Day of week (0=Sunday..6=Saturday).
 * @param rc          0 on success, 1 on simulation complete, -1 on failure.
 */
void cece_core_run(void* data_ptr, int hour, int day_of_week, int* rc) {
    *rc = 0;
    try {
        auto* d = static_cast<cece::CeceInternalData*>(data_ptr);
        if (!d) {
            std::cerr << "CECE_Run: null data_ptr\n";
            *rc = -1;
            return;
        }

        if (d->clock) {
            ExecuteStepClockGated(*d, rc);
        } else {
            ExecuteStepUnconditional(*d, hour, day_of_week);
        }

        SyncAndCopyState(*d);

    } catch (const std::exception& e) {
        std::cerr << "CECE_Run: " << e.what() << "\n";
        *rc = -1;
    } catch (...) {
        std::cerr << "CECE_Run: unknown\n";
        *rc = -1;
    }
}

}  // extern "C"
