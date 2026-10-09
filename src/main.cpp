// SPDX-License-Identifier: Apache-2.0
// Copyright (c) HELM Project Contributors

/**
 * @file main.cpp
 * @brief CECE C++ standalone driver — thin adapter over the shared
 * simulation core.
 *
 * All lifecycle sequencing, export-field registration, the time convention
 * (ingest at step start, stamp at step end), and teardown live behind the
 * cece_sim_* C ABI (src/driver/cece_simulation.cpp), which the NUOPC cap
 * drives identically. This file only: initializes the runtime environment,
 * resolves the target grid from YAML (GridSpec::from_yaml), and runs the
 * step loop until the shared core reports completion. The two drivers differ
 * ONLY in how they produce the GridSpec.
 */

#include <mpi.h>

#include <Kokkos_Core.hpp>
#include <conf/conf.hpp>
#include <halo/communicator.hpp>
#include <halo/environment.hpp>
#include <iostream>
#include <string>
#include <tick/tick.hpp>

#include "cece/cece_fatal.hpp"
#include "cece/cece_logger.hpp"
#include "cece/cece_sim_c_abi.h"
#include "cece/cece_simulation.hpp"

// Run-logging entry points (core C ABI, unchanged). The shared core re-invokes
// both during cece_sim_create; the redirect, banner, and path are idempotent,
// but the banner must appear before grid resolution, so the driver calls them
// first.
extern "C" {
void cece_set_config_file_path(const char* config_path, int path_len);
void cece_run_log_setup(const char* config_path, int path_len);
}

int main(int argc, char* argv[]) {
    // 1. Initialize MPI with thread support
    int provided = 0;
    int mpi_rc = MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (mpi_rc != MPI_SUCCESS) {
        cece::LogFatal("[DRIVER FATAL] MPI_Init_thread failed with error code " + std::to_string(mpi_rc));
        return mpi_rc;
    }

    if (provided < MPI_THREAD_MULTIPLE) {
        CECE_LOG_WARNING("[DRIVER WARNING] MPI implementation provided thread level " + std::to_string(provided) +
                         ", which is less than requested MPI_THREAD_MULTIPLE (" + std::to_string(MPI_THREAD_MULTIPLE) +
                         "). Threaded operations may be restricted.");
    }

    // 2. Initialize Kokkos (allocates execution resources on GPU or CPU)
    Kokkos::initialize(argc, argv);
    {
        // Initialize the HALO Environment & Communicator
        halo::Environment::initialize();
        halo::Communicator world(MPI_COMM_WORLD);
        const int my_rank = world.rank();

        std::string config_file = "cece_control_mock.yaml";
        if (argc > 1) {
            config_file = argv[1];
        }

        // --- Load configuration up front (grid, timing, streams parsed below) ---
        conf::Config config = conf::Config::from_file(config_file);

        // Configure run logging (optional log file, per-rank stdout suppression)
        // and print the startup banner. Shared with the NUOPC cap so behavior is
        // identical regardless of how CECE is launched.
        cece_run_log_setup(config_file.c_str(), static_cast<int>(config_file.length()));

        // Set config file path dynamically
        cece_set_config_file_path(config_file.c_str(), static_cast<int>(config_file.length()));

        // 3. Resolve the target grid: named regular grids (F/R), an explicit or
        //    stream-inferred gridspec file read through AMIO, or uniform extents
        //    from driver.grid. This is the ONLY part of the driver that differs
        //    from the NUOPC cap, which resolves a GridSpec from an ESMF
        //    Grid/Mesh instead. Throws std::invalid_argument with a named
        //    diagnostic on any failure — never a silent fallback.
        cece::GridSpec grid_spec;
        try {
            grid_spec = cece::GridSpec::from_yaml(config_file, config);
        } catch (const std::exception& e) {
            cece::LogFatal(std::string{"[DRIVER FATAL] (rank "} + std::to_string(my_rank) + ") grid resolution failed: " + e.what());
            Kokkos::finalize();
            MPI_Finalize();
            return -1;
        }

        CECE_LOG_DEBUG("[DRIVER] Resolved grid nx = " + std::to_string(grid_spec.nx) + ", ny = " + std::to_string(grid_spec.ny) +
                       ", nz = " + std::to_string(grid_spec.nz));

        // 4. Simulation clock (driver-side): the loop steps from start_time to
        //    end_time in timestep_seconds increments. The shared core owns the
        //    authoritative physics calendar; these values only select which
        //    instants to ingest/stamp and when to stop.
        const std::string start_time_str = config.get_string("driver.start_time");
        const std::string end_time_str = config.get_string("driver.end_time");
        const int timestep_seconds = config.get_int("driver.timestep_seconds");

        tick::Gregorian_Calendar cal;
        tick::Time_Point step_start = cal.to_time_point(tick::parse_iso8601(start_time_str));
        const tick::Time_Point end_time = cal.to_time_point(tick::parse_iso8601(end_time_str));
        const tick::Duration dt = tick::seconds(timestep_seconds);

        // 5. Build the simulation through the shared C ABI: core init ->
        //    export-field registration -> driver orchestrator -> writer.
        CeceGridSpec c_grid{};
        c_grid.nx = grid_spec.nx;
        c_grid.ny = grid_spec.ny;
        c_grid.nz = grid_spec.nz;
        // cece::GridTopology and CeceGridTopology share the same enumerants
        // in the same order (Rectilinear=0, Curvilinear=1, Unstructured=2).
        c_grid.topology = static_cast<int>(grid_spec.topology);
        c_grid.lon_coords = grid_spec.lon_coords.data();
        c_grid.lon_len = static_cast<int>(grid_spec.lon_coords.size());
        c_grid.lat_coords = grid_spec.lat_coords.data();
        c_grid.lat_len = static_cast<int>(grid_spec.lat_coords.size());
        c_grid.gridspec_file = grid_spec.gridspec_file.empty() ? nullptr : grid_spec.gridspec_file.c_str();
        c_grid.gridspec_file_len = static_cast<int>(grid_spec.gridspec_file.size());

        CeceSimulation* sim = nullptr;
        int rc = 0;
        cece_sim_create(config_file.c_str(), static_cast<int>(config_file.length()), &c_grid, MPI_Comm_c2f(MPI_COMM_WORLD), &sim, &rc);
        if (rc < 0 || sim == nullptr) {
            cece::LogFatal("[DRIVER FATAL] (rank " + std::to_string(my_rank) + ") cece_sim_create failed with rc=" + std::to_string(rc));
            Kokkos::finalize();
            MPI_Finalize();
            return rc < 0 ? rc : -1;
        }

        // The local-time service is initialized inside the shared facade
        // (CeceSimulation::create), so it applies identically here and in the
        // NUOPC cap path.

        if (my_rank == 0) {
            CECE_LOG_INFO("[DRIVER] Initialization completed on " + std::to_string(grid_spec.nx) + "x" + std::to_string(grid_spec.ny) + "x" +
                          std::to_string(grid_spec.nz) + " grid. Entering run loop...");
        }

        // 6. Event-driven simulation run loop. The shared core ingests at
        //    step_start_iso, stamps the output at step_end_iso, and reports
        //    completion through complete_out — the same signal the NUOPC cap
        //    honors, so both drivers take identical step counts.
        int step_index = 0;
        while (step_start < end_time) {
            const tick::Time_Point step_end = step_start + dt;
            const std::string step_start_iso = tick::format_iso8601(cal.to_date_time(step_start));
            const std::string step_end_iso = tick::format_iso8601(cal.to_date_time(step_end));

            if (my_rank == 0) {
                CECE_LOG_INFO("[DRIVER] Advancing simulation to: " + step_start_iso);
            }

            // The writer counts steps 1-based (output frequency is checked as
            // step_index % output_freq), matching the historical counter.
            step_index++;

            int complete = 0;
            cece_sim_step(sim, step_start_iso.c_str(), static_cast<int>(step_start_iso.length()), step_end_iso.c_str(),
                          static_cast<int>(step_end_iso.length()), step_index, &complete, &rc);
            if (rc < 0) {
                cece::LogFatal("[DRIVER FATAL] (rank " + std::to_string(my_rank) + ") cece_sim_step failed with rc=" + std::to_string(rc));
                int cleanup_rc = 0;
                cece_sim_finalize(sim, &cleanup_rc);
                Kokkos::finalize();
                MPI_Finalize();
                return rc;
            }
            if (complete) {
                break;
            }
            step_start = step_end;
        }

        // 7. Cleanup and release resources. Teardown failures are warnings:
        //    output has already been flushed at this point.
        if (my_rank == 0) {
            CECE_LOG_INFO("[DRIVER] Standalone execution completed. Cleaning up...");
        }
        int finalize_rc = 0;
        cece_sim_finalize(sim, &finalize_rc);
        if (finalize_rc < 0) {
            cece::LogFatal("[DRIVER FATAL] (rank " + std::to_string(my_rank) + ") cece_sim_finalize failed with rc=" + std::to_string(finalize_rc));
            Kokkos::finalize();
            MPI_Finalize();
            return finalize_rc;
        }
    }
    Kokkos::finalize();
    MPI_Finalize();
    return 0;
}
