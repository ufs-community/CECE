// SPDX-License-Identifier: Apache-2.0
// Copyright (c) HELM Project Contributors

/**
 * @file cece_simulation.cpp
 * @brief Implementation of the shared simulation lifecycle (CeceSimulation).
 *
 * Everything that must be byte-identical between the C++ standalone driver
 * and the NUOPC cap lives here: the fixed initialization ordering, the
 * band-local export-field registration, the ingest-at-step-start /
 * stamp-at-step-end time convention, the completion signal, and the teardown
 * sequence. This logic was previously inline in src/main.cpp; relocating it
 * here makes the cap's adoption of the same behavior structural rather than
 * a parallel reimplementation.
 */

#include "cece/cece_simulation.hpp"

#include <mpi.h>

#include <Kokkos_Core.hpp>
#include <ctime>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "cece/cece_config.hpp"
#include "cece/cece_driver_facade.hpp"
#include "cece/cece_fatal.hpp"
#include "cece/cece_internal.hpp"
#include "cece/cece_logger.hpp"

// CECE Core C-linkage lifecycle functions (framework-free C ABI, declared
// the same way the standalone driver has always declared them).
extern "C" {
void cece_set_config_file_path(const char* config_path, int path_len);
void cece_run_log_setup(const char* config_path, int path_len);
void cece_core_initialize_p1(void** data_ptr_ptr, int* rc);
void cece_core_realize(void* data_ptr, int* rc);
void cece_core_initialize_p2(void* data_ptr, int* nx, int* ny, int* nz, int* rc);
void cece_core_run(void* data_ptr, int hour, int day_of_week, int* rc);
void cece_core_finalize(void* data_ptr, int* rc);
void cece_core_writer_initialize_with_coords(void* data_ptr, int nx, int ny, int nz, const double* lon_coords, int lon_len, const double* lat_coords,
                                             int lat_len, const char* start_time_iso8601, int start_time_len, int mpi_comm_f, int* rc);
void cece_core_write_step(void* data_ptr, double time_seconds, int step_index, int* rc);
void cece_core_set_export_field(void* data_ptr, const char* name, int name_len, const double* field_data, int nx, int ny, int nz, int* rc);
void cece_core_local_time_init(void* data_ptr, int nx, int ny, int nz, const double* lon_coords, int lon_len, const double* lat_coords, int lat_len,
                               int mpi_comm_f, int* rc);
}

namespace cece {

namespace {

/// Wall-clock decomposition of an ISO8601 "YYYY-MM-DDTHH:MM:SS" string.
/// Mirrors CeceClock::DecomposeTime (gmtime semantics, dow 0=Sunday). The
/// core clock owns the authoritative hour/DOW/month for physics; these
/// values are passed to cece_core_run for the framework-free path and for
/// diagnostics only.
void DecomposeIso8601(const std::string& iso, int& hour, int& day_of_week) {
    hour = 0;
    day_of_week = 1;
    if (iso.size() < 19) {
        return;
    }
    std::tm tm{};
    tm.tm_year = std::stoi(iso.substr(0, 4)) - 1900;
    tm.tm_mon = std::stoi(iso.substr(5, 2)) - 1;
    tm.tm_mday = std::stoi(iso.substr(8, 2));
    hour = std::stoi(iso.substr(11, 2));
    tm.tm_hour = hour;
    tm.tm_min = std::stoi(iso.substr(14, 2));
    tm.tm_sec = std::stoi(iso.substr(17, 2));
    const std::time_t epoch = timegm(&tm);
    std::tm* gm = std::gmtime(&epoch);
    if (gm) {
        day_of_week = gm->tm_wday;
    }
}

}  // namespace

std::unique_ptr<CeceSimulation> CeceSimulation::create(const std::string& config_path, const GridSpec& grid, MPI_Comm comm, int* rc_out) {
    if (rc_out) {
        *rc_out = 0;
    }
    // Backs the C ABI (cece_sim_create): no exception may cross it. The body
    // lives in create_internal; any throw becomes a logged failure return,
    // and the unwinding unique_ptr finalizes whatever state was allocated.
    try {
        return create_internal(config_path, grid, comm, rc_out);
    } catch (const std::exception& e) {
        CECE_LOG_ERROR(std::string{"[SIM] initialization threw: "} + e.what());
        if (rc_out) {
            *rc_out = -1;
        }
        return nullptr;
    }
}

std::unique_ptr<CeceSimulation> CeceSimulation::create_internal(const std::string& config_path, const GridSpec& grid, MPI_Comm comm, int* rc_out) {
    auto fail = [&](int code, const std::string& msg) {
        CECE_LOG_ERROR("[SIM] " + msg);
        if (rc_out) {
            *rc_out = code;
        }
        return std::unique_ptr<CeceSimulation>(nullptr);
    };

    try {
        grid.validate();
    } catch (const std::exception& e) {
        return fail(-1, std::string("invalid grid specification: ") + e.what());
    }

    // The configured vertical layer count must be consistent with the input
    // data's declared levels; a mismatch is a loud failure, never a silent
    // reinterpretation of the vertical dimension.
    try {
        validate_nz_against_streams(config_path, grid.nz);
    } catch (const std::exception& e) {
        return fail(-1, e.what());
    }

    // Non-const locals: cece_core_initialize_p2 takes int* (by-reference ABI)
    // even though it only reads them.
    int nx = grid.nx;
    int ny = grid.ny;
    int nz = grid.nz;
    const int mpi_comm_f = MPI_Comm_c2f(comm);

    // The unique_ptr `sim` unwinds on any throw below, whose destructor
    // finalizes whatever core/driver state was already allocated; create()
    // converts the exception itself into the failure return.
    // 1. Configuration path + run logging (shared with both drivers so the
    //    banner/log behavior is identical regardless of launch path).
    cece_set_config_file_path(config_path.c_str(), static_cast<int>(config_path.length()));
    cece_run_log_setup(config_path.c_str(), static_cast<int>(config_path.length()));

    auto sim = std::unique_ptr<CeceSimulation>(new CeceSimulation());
    sim->config_path_ = config_path;
    sim->grid_ = grid;
    sim->comm_ = comm;

    int rc = 0;

    // 2. Phase 1: allocate internal structures (StackingEngine, DiagnosticManager).
    cece_core_initialize_p1(&sim->core_data_ptr_, &rc);
    if (rc < 0) {
        return fail(rc, "cece_core_initialize_p1 failed with rc=" + std::to_string(rc));
    }

    // 3. Realize: validate and lock configuration.
    cece_core_realize(sim->core_data_ptr_, &rc);
    if (rc < 0) {
        return fail(rc, "cece_core_realize failed with rc=" + std::to_string(rc));
    }

    // 4. Phase 2: complete grid binding (dynamically sized).
    cece_core_initialize_p2(sim->core_data_ptr_, &nx, &ny, &nz, &rc);
    if (rc < 0) {
        return fail(rc, "cece_core_initialize_p2 failed with rc=" + std::to_string(rc));
    }

    const cece::CeceConfig parsed_config = cece::ParseConfig(config_path);

    // 5. Register the export fields configured for output with persistent
    //    memory buffers, via the parsed config — the single authoritative
    //    interpretation of output.fields. Data fields only: the collection
    //    also carries the writer-managed coordinate variables.
    //
    //    Buffers are band-local (nx x ny_local x nz): the core writes back
    //    only the rank's band via SyncAndCopyState, and the writer assembles
    //    the global field at output time. On a single rank ny_local == ny,
    //    byte-identical to the replicated allocation.
    sim->band_ = BandDecomposition::compute(ny, comm);
    for (const cece::CeceOutputField& field : parsed_config.output_config.fields.GetDataFields()) {
        std::vector<double>& buffer = sim->export_buffers_[field.name] =
            std::vector<double>(static_cast<std::size_t>(nx) * sim->band_.ny_local * nz, 0.0);
        cece_core_set_export_field(sim->core_data_ptr_, field.name.c_str(), static_cast<int>(field.name.length()), buffer.data(), nx,
                                   sim->band_.ny_local, nz, &rc);
        if (rc < 0) {
            return fail(rc, "cece_core_set_export_field failed for '" + field.name + "' with rc=" + std::to_string(rc));
        }
    }

    // 6. Create the driver orchestrator facade (offline AMIO reading + AXIS
    //    regridding pipeline) on the resolved grid coordinates.
    cece_driver_create(config_path.c_str(), static_cast<int>(config_path.length()), nx, ny, nz, grid.lon_coords.data(),
                       static_cast<int>(grid.lon_coords.size()), grid.lat_coords.data(), static_cast<int>(grid.lat_coords.size()), mpi_comm_f,
                       &sim->driver_ptr_, &rc);
    if (rc < 0) {
        return fail(rc, "cece_driver_create failed with rc=" + std::to_string(rc));
    }

    // 7. Standalone writer: initialize output writing with the resolved
    //    coordinates (every from_yaml path produces concrete coordinates).
    const std::string& start_time_str = parsed_config.driver_config.start_time;
    cece_core_writer_initialize_with_coords(sim->core_data_ptr_, nx, ny, nz, grid.lon_coords.data(), static_cast<int>(grid.lon_coords.size()),
                                            grid.lat_coords.data(), static_cast<int>(grid.lat_coords.size()), start_time_str.c_str(),
                                            static_cast<int>(start_time_str.length()), mpi_comm_f, &rc);
    if (rc < 0) {
        return fail(rc, "standalone writer initialization failed with rc=" + std::to_string(rc));
    }

    // 7b. Local-time service: decode the UTC-offset grid once and attach it
    //     to the core (no-op when local_time.enabled is false, so the
    //     default path stays byte-identical). Lives in the shared facade so
    //     the standalone driver and the NUOPC cap behave identically.
    cece_core_local_time_init(sim->core_data_ptr_, nx, ny, nz, grid.lon_coords.data(), static_cast<int>(grid.lon_coords.size()),
                              grid.lat_coords.data(), static_cast<int>(grid.lat_coords.size()), mpi_comm_f, &rc);
    if (rc < 0) {
        return fail(rc, "local-time initialization failed with rc=" + std::to_string(rc));
    }

    // 8. Driver-side clock anchors for the output stamp: elapsed seconds are
    //    measured from the configured start time, matching the historical
    //    standalone driver convention (stamp at step end).
    tick::Gregorian_Calendar cal;
    sim->start_time_ = cal.to_time_point(tick::parse_iso8601(start_time_str));
    sim->dt_seconds_ = static_cast<double>(parsed_config.driver_config.timestep_seconds);

    if (rc_out) {
        *rc_out = 0;
    }
    return sim;
}

StepOutcome CeceSimulation::step(const std::string& step_start_iso, const std::string& step_end_iso, int step_index) {
    StepOutcome result;
    int rc = 0;

    // The whole sequence is exception-guarded: Step backs the C ABI
    // cece_sim_step and reports failures through StepOutcome instead.
    try {
        // A. Ingest at the STEP-START instant: offline AMIO reading and AXIS
        //    regridding for the time the step represents.
        cece_driver_advance_time(driver_ptr_, step_start_iso.c_str(), static_cast<int>(step_start_iso.length()), core_data_ptr_, &rc);
        if (rc < 0) {
            result.rc = rc;
            result.error = true;
            // Emit on both the log (real stdout, all ranks) and stderr so the
            // failure is never lost regardless of how output is captured.
            LogFatal("[SIM FATAL] cece_driver_advance_time failed to ingest data step - aborting simulation!");
            return result;
        }

        // B. Execute the compute core. The core's own clock derives hour /
        //    day-of-week / month for physics; the arguments below feed the
        //    framework-free path and diagnostics with the real calendar values
        //    of the step start (no hardcoded day-of-week).
        int hour = 0;
        int day_of_week = 1;
        DecomposeIso8601(step_start_iso, hour, day_of_week);
        cece_core_run(core_data_ptr_, hour, day_of_week, &rc);
        if (rc < 0) {
            result.rc = rc;
            result.error = true;
            LogFatal("[SIM] cece_core_run failed with rc=" + std::to_string(rc));
            return result;
        }
        if (rc == 1) {
            result.complete = true;
        }

        // C. Stamp the output record at the STEP-END elapsed time.
        tick::Gregorian_Calendar cal;
        tick::Time_Point step_end = cal.to_time_point(tick::parse_iso8601(step_end_iso));
        const double elapsed_seconds = static_cast<double>((step_end - start_time_).nanos()) / 1e9;

        cece_core_write_step(core_data_ptr_, elapsed_seconds, step_index, &rc);
        if (rc < 0) {
            result.rc = rc;
            result.error = true;
            LogFatal("[SIM] cece_core_write_step failed with rc=" + std::to_string(rc));
            return result;
        }

        result.rc = 0;
        return result;
    } catch (const std::exception& e) {
        result.rc = -1;
        result.error = true;
        LogFatal(std::string{"[SIM FATAL] Step threw: "} + e.what());
        return result;
    }
}

bool CeceSimulation::bind_export_field(const std::string& species, double* data_ptr, int nx, int ny_local, int nz, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    auto fail = [&](const std::string& msg) {
        CECE_LOG_ERROR("[SIM] cece_sim_bind_export_field: " + msg);
        if (rc != nullptr) {
            *rc = -1;
        }
        return false;
    };

    if (core_data_ptr_ == nullptr) {
        return fail("simulation has no core data (not created, or already finalized)");
    }
    if (data_ptr == nullptr) {
        return fail("null data pointer for species '" + species + "'");
    }
    if (nx <= 0 || ny_local <= 0 || nz <= 0) {
        return fail("invalid extents (" + std::to_string(nx) + ", " + std::to_string(ny_local) + ", " + std::to_string(nz) + ") for species '" +
                    species + "'");
    }

    auto* data = static_cast<cece::CeceInternalData*>(core_data_ptr_);

    // The species must already exist in the export state: fields are created
    // at config realization, and the managed DualView there is what the
    // stacking engine computes into every step. This binder only redirects
    // where the synced host copy lands — it must never replace the managed
    // view itself (doing so would dangle the device view the engine holds).
    auto field_it = data->export_state.fields.find(species);
    if (field_it == data->export_state.fields.end()) {
        return fail("unknown export species '" + species + "' (not present in export_state.fields)");
    }

    // Shape contract: the ESMF field's per-PET extents must match BOTH the
    // managed view the write-back deep-copies from (otherwise the per-step
    // copy would be a size mismatch) and this rank's band geometry owned by
    // the facade. The facade — not the cap — knows the decomposition, so a
    // mismatch between the ESMF decomposition and the band fails here,
    // loudly, instead of writing back out of bounds.
    const auto& host_view = field_it->second.view_host();
    const int view_nx = static_cast<int>(host_view.extent(0));
    const int view_ny = static_cast<int>(host_view.extent(1));
    const int view_nz = static_cast<int>(host_view.extent(2));
    if (view_nx != nx || view_ny != ny_local || view_nz != nz) {
        return fail("extent mismatch for species '" + species + "': field (" + std::to_string(nx) + ", " + std::to_string(ny_local) + ", " +
                    std::to_string(nz) + ") != managed view (" + std::to_string(view_nx) + ", " + std::to_string(view_ny) + ", " +
                    std::to_string(view_nz) + ")");
    }
    if (nx != data->nx || nz != data->nz) {
        return fail("extent mismatch for species '" + species + "': field (" + std::to_string(nx) + ", " + std::to_string(ny_local) + ", " +
                    std::to_string(nz) + ") != core grid (" + std::to_string(data->nx) + ", *, " + std::to_string(data->nz) + ")");
    }
    if (ny_local != band_.ny_local) {
        return fail("band mismatch for species '" + species + "': field ny_local=" + std::to_string(ny_local) +
                    " != facade band ny_local=" + std::to_string(band_.ny_local));
    }

    // Pointer-map update only. ESMF owns the memory; the core never frees or
    // reallocates it, and each step SyncAndCopyState deep-copies the managed
    // host view into whatever this map points at.
    data->persistent_export_ptrs[species] = data_ptr;
    CECE_LOG_INFO("[SIM] Bound export field '" + species + "' to coupled storage (" + std::to_string(nx) + "x" + std::to_string(ny_local) + "x" +
                  std::to_string(nz) + ")");
    return true;
}

bool CeceSimulation::set_import_field(const std::string& field, const double* data_ptr, int nx, int ny_local, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    auto fail = [&](const std::string& msg) {
        CECE_LOG_ERROR("[SIM] cece_sim_set_import_field: " + msg);
        if (rc != nullptr) {
            *rc = -1;
        }
        return false;
    };

    if (core_data_ptr_ == nullptr) {
        return fail("simulation has no core data (not created, or already finalized)");
    }
    if (data_ptr == nullptr) {
        return fail("null data pointer for import field '" + field + "'");
    }
    if (nx <= 0 || ny_local <= 0) {
        return fail("invalid extents (" + std::to_string(nx) + ", " + std::to_string(ny_local) + ") for import field '" + field + "'");
    }

    auto* data = static_cast<cece::CeceInternalData*>(core_data_ptr_);

    // The cap passes the configured internal input name (a key of the
    // meteorology/scale-factor/mask mappings). Resolve it to the import-state
    // key the compute-side resolver reads under, using the same mapping order
    // (CeceStateResolver::ResolveName): meteorology, then scale factor, then
    // mask, else the name itself. A name that maps to nothing is still a valid
    // import target under its own key, so resolution never fails here; the
    // config parser already rejected unknown import keys.
    std::string key = field;
    if (auto it = data->config.met_mapping.find(field); it != data->config.met_mapping.end()) {
        key = it->second;
    } else if (auto it = data->config.scale_factor_mapping.find(field); it != data->config.scale_factor_mapping.end()) {
        key = it->second;
    } else if (auto it = data->config.mask_mapping.find(field); it != data->config.mask_mapping.end()) {
        key = it->second;
    }

    // Shape contract: the ESMF field's per-PET extents must match this rank's
    // band. The facade — not the cap — owns the decomposition, so a mismatch
    // between the ESMF grid decomposition and the band fails here, loudly,
    // instead of copying out of bounds. Longitude and layer count are the core
    // grid's; a met import is a 2-D surface field stored with one layer.
    if (nx != data->nx) {
        return fail("extent mismatch for import field '" + field + "': field nx=" + std::to_string(nx) +
                    " != core grid nx=" + std::to_string(data->nx));
    }
    if (ny_local != band_.ny_local) {
        return fail("band mismatch for import field '" + field + "': field ny_local=" + std::to_string(ny_local) +
                    " != facade band ny_local=" + std::to_string(band_.ny_local));
    }

    // Create the managed DualView on first use (a host-only field with no file
    // stream). Same single-layer shape the resolver reads a 2-D surface field
    // in: (nx, ny_local, 1).
    auto field_it = data->import_state.fields.find(key);
    if (field_it == data->import_state.fields.end()) {
        DualView3D new_field(key, static_cast<size_t>(nx), static_cast<size_t>(ny_local), 1);
        field_it = data->import_state.fields.emplace(key, std::move(new_field)).first;
    }

    auto& dual_view = field_it->second;
    auto host_view = dual_view.view_host();

    // The view may already exist from file ingest at a different vertical
    // extent; a coupled import supplies one surface layer, so require the
    // horizontal shape to match the band and reject an incompatible view
    // rather than silently mis-copying.
    if (static_cast<int>(host_view.extent(0)) != nx || static_cast<int>(host_view.extent(1)) != ny_local) {
        return fail("shape mismatch for import field '" + field + "' (key '" + key + "'): managed view (" + std::to_string(host_view.extent(0)) +
                    ", " + std::to_string(host_view.extent(1)) + ", " + std::to_string(host_view.extent(2)) + ") != field (" + std::to_string(nx) +
                    ", " + std::to_string(ny_local) + ", 1)");
    }

    // Wrap the borrowed ESMF storage as an unmanaged LayoutLeft host view and
    // copy into the managed host mirror, then sync to device so this step's
    // compute reads the host values. The layer count of the borrowed field is
    // one; the unmanaged view spans the same (nx, ny_local, 1) shape.
    UnmanagedHostView3D src(const_cast<double*>(data_ptr), static_cast<size_t>(nx), static_cast<size_t>(ny_local), 1);
    Kokkos::deep_copy(host_view, src);
    dual_view.modify_host();
    dual_view.sync_device();

    CECE_LOG_INFO("[SIM] Set import field '" + field + "' (key '" + key + "') from coupled storage (" + std::to_string(nx) + "x" +
                  std::to_string(ny_local) + "x1)");
    return true;
}

void CeceSimulation::finalize(int& rc_out) {
    if (finalized_) {
        rc_out = 0;
        return;
    }
    finalized_ = true;

    int destroy_rc = 0;
    cece_driver_destroy(driver_ptr_, &destroy_rc);
    driver_ptr_ = nullptr;
    if (destroy_rc != 0) {
        CECE_LOG_ERROR("[SIM] AMIO teardown reported failures during cece_driver_destroy (rc=" + std::to_string(destroy_rc) +
                       "); output data was already flushed, but some resources may have leaked.");
    }

    int rc = 0;
    cece_core_finalize(core_data_ptr_, &rc);
    core_data_ptr_ = nullptr;
    if (rc < 0) {
        CECE_LOG_ERROR("[SIM] cece_core_finalize failed with rc=" + std::to_string(rc));
    }

    if (destroy_rc != 0 || rc < 0) {
        rc_out = -1;
    } else {
        rc_out = 0;
    }
}

CeceSimulation::~CeceSimulation() {
    if (!finalized_ && (core_data_ptr_ != nullptr || driver_ptr_ != nullptr)) {
        int rc = 0;
        finalize(rc);
    }
}

int CeceSimulation::nz_from_config(const std::string& config_path) {
    conf::Config config = conf::Config::from_file(config_path);
    int nz = 1;
    if (config.has("driver.grid")) {
        nz = config.get_or("driver.grid.nz", 1);
    }
    if (nz <= 0) {
        throw std::invalid_argument("driver.grid.nz must be positive (got " + std::to_string(nz) + ")");
    }
    return nz;
}

void CeceSimulation::validate_nz_against_streams(const std::string& config_path, int nz) {
    conf::Config config = conf::Config::from_file(config_path);
    if (!config.has("cece_data.streams")) {
        return;
    }
    const conf::Value streams = config.at("cece_data.streams");
    for (std::size_t s = 0; s < streams.size(); ++s) {
        const conf::Value vars = streams[s]["variables"];
        if (!vars.is_defined()) {
            continue;
        }
        for (std::size_t v = 0; v < vars.size(); ++v) {
            const conf::Value var = vars[v];
            const conf::Value levels_val = var["levels"];
            if (!levels_val.is_defined()) {
                continue;  // inherits the grid nz (the data model's default)
            }
            const int levels = levels_val.as_int();
            if (levels > 1 && levels != nz) {
                throw std::invalid_argument("[GRID] configured nz=" + std::to_string(nz) + " conflicts with stream variable '" +
                                            var["model"].string_or(var["file"].string_or("<unnamed>")) + "' declaring levels=" +
                                            std::to_string(levels) + "; the input data cannot be stacked onto the target layers");
            }
        }
    }
}

}  // namespace cece
