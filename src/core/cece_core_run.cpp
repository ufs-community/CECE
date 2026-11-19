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

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "cece/cece_clock.hpp"
#include "cece/cece_fire_runtime.hpp"
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
    // Latitude extent is the rank-local band height (ny_local == ny on a single rank).
    d.ingestor.IngestEmissionsInline(d.config.cece_data, d.import_state, d.nx, d.ny_local, d.nz);
}

void RunPhysicsSchemeByName(cece::CeceInternalData& d, const std::string& scheme_name, const cece::StepResult* step) {
    for (size_t i = 0; i < d.active_schemes.size(); ++i) {
        if (i < d.config.physics_schemes.size() && d.config.physics_schemes[i].name == scheme_name) {
            if (d.active_schemes[i] && d.config.physics_schemes[i].fire) {
                if (!step || !d.clock) throw std::runtime_error("Fire runtime requires the CECE clock for interval boundaries");
                if (!d.fire_runtime_adapter || !d.fire_runtime_initialized) {
                    throw std::runtime_error("Fire runtime adapter is unavailable or not initialized");
                }
                auto* fire_scheme = dynamic_cast<cece::FireScheme*>(d.active_schemes[i].get());
                if (!fire_scheme) throw std::logic_error("Fire configuration is attached to a non-Fire physics scheme");
                if (step->elapsed_seconds <= d.fire_last_committed_elapsed_seconds) {
                    throw std::invalid_argument("Fire clock interval must advance beyond the last committed interval");
                }
                const auto start_epoch = d.clock->StartEpochSeconds();
                const auto& fire_config = *d.config.physics_schemes[i].fire;
                cece::FireRuntimeIntervalRequest request{
                    cece::FireUtcTime::FromEpochSeconds(start_epoch + d.fire_last_committed_elapsed_seconds),
                    cece::FireUtcTime::FromEpochSeconds(start_epoch + step->elapsed_seconds),
                    fire_config.cutoff_mode == cece::FireCutoffMode::Fixed ? fire_config.availability_cutoff : std::nullopt, step->elapsed_seconds};
                try {
                    const auto inventory = d.fire_runtime_adapter->PrepareInterval(request);
                    if (const auto* bulk = std::get_if<std::reference_wrapper<const cece::FireBulkInventory>>(&inventory)) {
                        fire_scheme->StageBorrowedBulkInventory(bulk->get());
                    } else {
                        fire_scheme->StageBorrowedTargetSpecificInventory(
                            std::get<std::reference_wrapper<const cece::FireTargetSpecificInventory>>(inventory).get());
                    }
                    d.active_schemes[i]->Run(d.import_state, d.export_state);
                    d.fire_pending_elapsed_seconds = step->elapsed_seconds;
                    d.fire_interval_pending = true;
                } catch (...) {
                    fire_scheme->DiscardStagedInventory();
                    d.fire_runtime_adapter->AbortInterval();
                    throw;
                }
            } else if (d.active_schemes[i]) {
                d.active_schemes[i]->Run(d.import_state, d.export_state);
            }
            break;
        }
    }
}

void CommitPendingFireInterval(cece::CeceInternalData& d) {
    if (!d.fire_interval_pending) return;
    try {
        d.fire_runtime_adapter->CommitInterval();
        d.fire_last_committed_elapsed_seconds = d.fire_pending_elapsed_seconds;
        d.fire_interval_pending = false;
    } catch (...) {
        d.fire_runtime_adapter->AbortInterval();
        d.fire_interval_pending = false;
        throw;
    }
}

void AbortPendingFireInterval(cece::CeceInternalData& d) noexcept {
    if (!d.fire_interval_pending) return;
    for (auto& scheme : d.active_schemes) {
        if (auto* fire_scheme = dynamic_cast<cece::FireScheme*>(scheme.get())) fire_scheme->DiscardStagedInventory();
    }
    if (d.fire_runtime_adapter) d.fire_runtime_adapter->AbortInterval();
    d.fire_interval_pending = false;
}

void ExecuteStackingEngine(cece::CeceInternalData& d, int hour, int day_of_week, int month = 0, std::int64_t elapsed_seconds = 0) {
    if (d.stacking_engine) {
        cece::CeceStateResolver resolver(d.import_state, d.export_state, d.config.met_mapping, d.config.scale_factor_mapping, d.config.mask_mapping);
        // Compute over this rank's latitude band only (ny_local == ny on a single rank).
        // forward the local-time service (null when disabled) and the
        // step's elapsed seconds so opted-in layers scale at local time.
        d.stacking_engine->Execute(resolver, d.nx, d.ny_local, d.nz, d.default_mask, hour, day_of_week, month, nullptr, d.local_time.get(),
                                   elapsed_seconds);
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
            RunPhysicsSchemeByName(d, comp->name, &step);
            break;
        }
        case cece::ComponentType::kStackingEngine: {
            ExecuteStackingEngine(d, step.hour_of_day, step.day_of_week, step.month, step.elapsed_seconds);
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

    for (std::size_t i = 0; i < d.active_schemes.size(); ++i) {
        auto& scheme = d.active_schemes[i];
        if (scheme) {
            if (i < d.config.physics_schemes.size() && d.config.physics_schemes[i].fire) {
                throw std::runtime_error("Fire scheme cannot run without CECE clock interval metadata");
            }
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
            // Export write-back views are band-local; persistent_export_ptrs buffers are
            // sized nx*ny_local*nz (band extent; == nx*ny*nz on a single rank).
            UnmanagedHost h_view(it->second, d.nx, d.ny_local, d.nz);
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
        CommitPendingFireInterval(*d);

    } catch (const std::exception& e) {
        if (data_ptr) AbortPendingFireInterval(*static_cast<cece::CeceInternalData*>(data_ptr));
        std::cerr << "CECE_Run: " << e.what() << "\n";
        *rc = -1;
    } catch (...) {
        if (data_ptr) AbortPendingFireInterval(*static_cast<cece::CeceInternalData*>(data_ptr));
        std::cerr << "CECE_Run: unknown\n";
        *rc = -1;
    }
}

}  // extern "C"
