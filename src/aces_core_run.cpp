/**
 * @file aces_core_run.cpp
 * @brief Implementation of the ACES Run phase with CDEPS advancement and physics execution.
 */

#include <ESMC.h>

#include <iostream>
#include <stdexcept>

#include "aces/aces_internal.hpp"
#include "aces/aces_stacking_engine.hpp"
#include "aces/aces_state.hpp"
#include "aces/physics_scheme.hpp"

namespace aces {

/**
 * @brief Extract current hour and day of week from ESMF_Clock.
 * @param clock_ptr Pointer to ESMF_Clock.
 * @param hour Output: current hour (0-23).
 * @param day_of_week Output: current day of week (0-6, 0=Sunday).
 */
void ExtractTimeInfo(void* clock_ptr, int& hour, int& day_of_week) {
    ESMC_Clock clock = static_cast<ESMC_Clock>(clock_ptr);

    // Get current simulation time from clock
    ESMC_TimeInterval currSimTime;
    ESMC_I8 stepCount;
    int rc = ESMC_ClockGet(clock, &currSimTime, &stepCount);
    if (rc != ESMF_SUCCESS) {
        std::cerr << "ACES_Run: Failed to get current time from clock, rc=" << rc << "\n";
        hour = 0;
        day_of_week = 0;
        return;
    }

    // Convert time interval to seconds
    ESMC_I8 seconds_i8;
    rc = ESMC_TimeIntervalGet(currSimTime, &seconds_i8, nullptr);
    if (rc != ESMF_SUCCESS) {
        std::cerr << "ACES_Run: Failed to get seconds from time interval, rc=" << rc << "\n";
        hour = 0;
        day_of_week = 0;
        return;
    }

    long long total_seconds = static_cast<long long>(seconds_i8);

    // Calculate hour (0-23) from seconds since start
    // Assuming simulation starts at hour 0
    long long total_hours = total_seconds / 3600;
    hour = static_cast<int>(total_hours % 24);

    // Calculate day of week (0-6, 0=Sunday)
    // Assuming simulation starts on Sunday (day 0)
    long long total_days = total_hours / 24;
    day_of_week = static_cast<int>(total_days % 7);
}

}  // namespace aces

extern "C" {

/**
 * @brief ACES Run phase implementation.
 *
 * This function:
 * 1. Advances CDEPS to current clock time (handled in Fortran cap)
 * 2. Extracts current hour and day_of_week from ESMF_Clock for temporal cycles
 * 3. Executes StackingEngine with hybrid field resolution
 * 4. Executes all physics schemes in registration order
 * 5. Synchronizes device to host for all export fields
 *
 * @param data_ptr Pointer to AcesInternalData structure.
 * @param import_state_ptr Pointer to ESMF ImportState.
 * @param export_state_ptr Pointer to ESMF ExportState.
 * @param clock_ptr Pointer to ESMF_Clock.
 * @param rc Return code (ESMF_SUCCESS on success).
 */
void aces_core_run(void* data_ptr, void* import_state_ptr, void* export_state_ptr, void* clock_ptr,
                   int* rc) {
    *rc = ESMF_SUCCESS;

    try {
        // 1. Retrieve internal state
        auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);
        if (internal_data == nullptr) {
            std::cerr << "ACES_Run: Internal data pointer is null\n";
            *rc = ESMF_FAILURE;
            return;
        }

        // 2. Extract current hour and day_of_week from clock for temporal cycles
        int hour = 0;
        int day_of_week = 0;
        aces::ExtractTimeInfo(clock_ptr, hour, day_of_week);

        std::cout << "ACES_Run: Current time - Hour: " << hour << ", Day of week: " << day_of_week
                  << " (0=Sunday)\n";

        // 3. Execute StackingEngine with hybrid field resolution
        if (internal_data->stacking_engine) {
            std::cout << "ACES_Run: Executing StackingEngine...\n";

            // Create field resolver from import/export states
            aces::AcesStateResolver resolver(
                internal_data->import_state, internal_data->export_state,
                internal_data->config.met_mapping, internal_data->config.scale_factor_mapping,
                internal_data->config.mask_mapping);

            // Execute stacking engine
            internal_data->stacking_engine->Execute(resolver, internal_data->nx, internal_data->ny,
                                                    internal_data->nz, internal_data->default_mask,
                                                    hour, day_of_week);

            std::cout << "ACES_Run: StackingEngine execution complete\n";
        } else {
            std::cerr << "ACES_Run: StackingEngine not initialized\n";
        }

        // 4. Execute all physics schemes in registration order
        std::cout << "ACES_Run: Executing " << internal_data->active_schemes.size()
                  << " physics schemes...\n";

        for (size_t i = 0; i < internal_data->active_schemes.size(); ++i) {
            auto& scheme = internal_data->active_schemes[i];
            if (scheme) {
                try {
                    scheme->Run(internal_data->import_state, internal_data->export_state);
                    std::cout << "ACES_Run: Physics scheme " << (i + 1)
                              << " executed successfully\n";
                } catch (const std::exception& e) {
                    std::cerr << "ACES_Run: Physics scheme " << (i + 1) << " failed: " << e.what()
                              << "\n";
                    // Continue with other schemes (non-fatal)
                }
            }
        }

        // 5. Synchronize device to host for all export fields
        // This ensures ESMF can access the updated field data after kernel execution.
        // DualView::sync_host() copies data from device to host memory space.
        // This is required because Kokkos kernels execute on the default execution space
        // (which may be GPU), but ESMF expects data in host memory.
        std::cout << "ACES_Run: Synchronizing " << internal_data->export_state.fields.size()
                  << " export fields from device to host...\n";

        int sync_errors = 0;
        for (auto& [name, field] : internal_data->export_state.fields) {
            try {
                // DualView::sync_host() is a no-op if data is already on host,
                // but necessary if execution space is GPU (CUDA/HIP).
                // This call ensures the host view is up-to-date with device modifications.
                field.sync_host();
                std::cout << "ACES_Run: Field '" << name << "' synchronized to host\n";
            } catch (const std::exception& e) {
                std::cerr << "ACES_Run: Failed to sync field '" << name << "' to host: " << e.what()
                          << "\n";
                sync_errors++;
            }
        }

        if (sync_errors > 0) {
            std::cerr << "ACES_Run: " << sync_errors << " field(s) failed to synchronize\n";
            *rc = ESMF_FAILURE;
            return;
        }

        std::cout << "ACES_Run: Device-to-host synchronization complete for all "
                  << internal_data->export_state.fields.size() << " fields\n";

        // 6. Write output if standalone mode is enabled (Req 11.1, 11.4, 11.8)
        if (internal_data->standalone_mode && internal_data->standalone_writer) {
            // Check if this step should be written based on frequency_steps
            const int frequency_steps = internal_data->config.output_config.frequency_steps;

            if (internal_data->step_count % frequency_steps == 0) {
                try {
                    // Get current time in seconds since start
                    // For now, use step_count * time_step_seconds as approximation
                    // In a real implementation, this would come from the ESMF_Clock
                    double time_seconds = static_cast<double>(internal_data->step_count) * 3600.0;

                    std::cout << "ACES_Run: Writing output at step " << internal_data->step_count
                              << "\n";

                    int write_rc = internal_data->standalone_writer->WriteTimeStep(
                        internal_data->export_state.fields, time_seconds,
                        internal_data->step_count);

                    if (write_rc != 0) {
                        std::cerr << "ACES_Run: Warning - WriteTimeStep returned error code "
                                  << write_rc << "\n";
                        // Non-fatal: continue execution even if write fails
                    } else {
                        std::cout << "ACES_Run: Output written successfully\n";
                    }
                } catch (const std::exception& e) {
                    std::cerr << "ACES_Run: Warning - Exception during output write: " << e.what()
                              << "\n";
                    // Non-fatal: continue execution even if write fails
                }
            }

            // Increment step counter
            internal_data->step_count++;
        }

        std::cout << "ACES_Run: Run phase completed successfully\n";

    } catch (const std::exception& e) {
        std::cerr << "ACES_Run: Exception caught: " << e.what() << "\n";
        *rc = ESMF_FAILURE;
        return;
    } catch (...) {
        std::cerr << "ACES_Run: Unknown exception caught\n";
        *rc = ESMF_FAILURE;
        return;
    }

    *rc = ESMF_SUCCESS;
}

}  // extern "C"
