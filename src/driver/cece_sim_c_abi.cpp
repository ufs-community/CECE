// SPDX-License-Identifier: Apache-2.0
// Copyright (c) HELM Project Contributors

/**
 * @file cece_sim_c_abi.cpp
 * @brief extern "C" facade over cece::CeceSimulation for both drivers.
 *
 * The C++ standalone driver (src/main.cpp) and the NUOPC Fortran cap
 * (src/driver/nuopc/cece_cap.F90) both call these entry points. The facade
 * converts the C structs/strings to C++ types and delegates; all lifecycle
 * semantics live in CeceSimulation so they cannot diverge per driver.
 */

#include "cece/cece_sim_c_abi.h"

#include <mpi.h>

#include <conf/conf.hpp>
#include <cstring>
#include <string>
#include <tick/tick.hpp>
#include <utility>
#include <vector>

#include "cece/cece_fatal.hpp"
#include "cece/cece_simulation.hpp"

namespace {

/// Copy a (possibly non-NUL-terminated) C string buffer into a std::string,
/// tolerating callers that pass strlen() or strlen()+1 as the length (the
/// historical cece_core_* convention accepts both).
std::string cstr_to_string(const char* data, int len) {
    if (data == nullptr || len <= 0) {
        return {};
    }
    std::string s(data, static_cast<size_t>(len));
    const auto nul = s.find('\0');
    if (nul != std::string::npos) {
        s.resize(nul);
    }
    return s;
}

cece::GridSpec to_grid_spec(const CeceGridSpec& c) {
    cece::GridSpec spec;
    spec.nx = c.nx;
    spec.ny = c.ny;
    spec.nz = c.nz;
    switch (c.topology) {
        case CECE_GRID_CURVILINEAR:
            spec.topology = cece::GridTopology::Curvilinear;
            break;
        case CECE_GRID_UNSTRUCTURED:
            spec.topology = cece::GridTopology::Unstructured;
            break;
        case CECE_GRID_RECTILINEAR:
        default:
            spec.topology = cece::GridTopology::Rectilinear;
            break;
    }
    if (c.lon_coords != nullptr && c.lon_len > 0) {
        spec.lon_coords.assign(c.lon_coords, c.lon_coords + c.lon_len);
    }
    if (c.lat_coords != nullptr && c.lat_len > 0) {
        spec.lat_coords.assign(c.lat_coords, c.lat_coords + c.lat_len);
    }
    spec.gridspec_file = cstr_to_string(c.gridspec_file, c.gridspec_file_len);
    return spec;
}

/// Map the C++ topology enum to the C ABI integer (same enumerant order).
int to_c_topology(cece::GridTopology t) {
    switch (t) {
        case cece::GridTopology::Curvilinear:
            return CECE_GRID_CURVILINEAR;
        case cece::GridTopology::Unstructured:
            return CECE_GRID_UNSTRUCTURED;
        case cece::GridTopology::Rectilinear:
        default:
            return CECE_GRID_RECTILINEAR;
    }
}

/// Recover the owning unique_ptr from a facade handle without destroying it
/// (create released ownership to the caller; finalize reclaims and resets).
std::unique_ptr<cece::CeceSimulation> adopt(CeceSimulation* sim) {
    return std::unique_ptr<cece::CeceSimulation>(reinterpret_cast<cece::CeceSimulation*>(sim));
}

/// Write through an optional C-ABI out-parameter. The null check lives here
/// exactly once so each entry point validates its arguments in a single
/// guard instead of re-testing every write site. The value type is separate
/// from the pointer target so callers can pass implicitly convertible values
/// (e.g. nullptr for pointer outs, enum constants for int outs).
template <typename T, typename U>
void set_out(T* out, U&& value) {
    if (out != nullptr) {
        *out = std::forward<U>(value);
    }
}

/// Bounding extent of a coordinate array, or (0,0) when empty.
void extent_of(const std::vector<double>& v, double* lo, double* hi) {
    set_out(lo, v.empty() ? 0.0 : v.front());
    set_out(hi, v.empty() ? 0.0 : v.front());
    for (const double x : v) {
        if (lo != nullptr && x < *lo) *lo = x;
        if (hi != nullptr && x > *hi) *hi = x;
    }
}

}  // namespace

extern "C" {

void cece_sim_create(const char* config_path, int path_len, const CeceGridSpec* grid, int mpi_comm_f, CeceSimulation** out_sim, int* rc) {
    set_out(rc, 0);
    set_out(out_sim, nullptr);
    if (config_path == nullptr || grid == nullptr || out_sim == nullptr || rc == nullptr) {
        set_out(rc, -1);
        return;
    }

    const std::string path = cstr_to_string(config_path, path_len);
    const cece::GridSpec spec = to_grid_spec(*grid);
    MPI_Comm comm = (mpi_comm_f != 0) ? MPI_Comm_f2c(static_cast<MPI_Fint>(mpi_comm_f)) : MPI_COMM_WORLD;

    auto sim = cece::CeceSimulation::create(path, spec, comm, rc);
    if (sim) {
        *out_sim = reinterpret_cast<CeceSimulation*>(sim.release());
    }
}

void cece_sim_step(CeceSimulation* sim, const char* step_start_iso, int step_start_len, const char* step_end_iso, int step_end_len, int step_index,
                   int* complete_out, int* rc) {
    set_out(rc, 0);
    set_out(complete_out, 0);
    if (sim == nullptr || step_start_iso == nullptr || step_end_iso == nullptr || rc == nullptr) {
        set_out(rc, -1);
        return;
    }

    const std::string start = cstr_to_string(step_start_iso, step_start_len);
    const std::string end = cstr_to_string(step_end_iso, step_end_len);

    cece::CeceSimulation& cpp_sim = *reinterpret_cast<cece::CeceSimulation*>(sim);
    const cece::StepOutcome result = cpp_sim.step(start, end, step_index);

    if (complete_out != nullptr) {
        *complete_out = result.complete ? 1 : 0;
    }
    set_out(rc, result.error ? result.rc : 0);
}

void cece_sim_finalize(CeceSimulation* sim, int* rc) {
    if (sim == nullptr) {
        set_out(rc, 0);
        return;
    }
    auto owner = adopt(sim);
    int finalize_rc = 0;
    owner->finalize(finalize_rc);
    set_out(rc, finalize_rc);
}

void cece_sim_create_from_yaml(const char* config_path, int path_len, int mpi_comm_f, CeceSimulation** out_sim, int* rc) {
    set_out(rc, 0);
    set_out(out_sim, nullptr);
    if (config_path == nullptr || out_sim == nullptr || rc == nullptr) {
        set_out(rc, -1);
        return;
    }

    // Standalone (no parent grid) branch: resolve the grid from YAML with the
    // exact same code the C++ driver runs, then build the simulation on it.
    // Keeping both steps behind one C entry lets the cap reach parity by
    // construction without marshalling coordinate buffers through Fortran.
    try {
        const std::string path = cstr_to_string(config_path, path_len);
        conf::Config config = conf::Config::from_file(path);
        const cece::GridSpec spec = cece::GridSpec::from_yaml(path, config);

        CeceGridSpec c_grid{};
        c_grid.nx = spec.nx;
        c_grid.ny = spec.ny;
        c_grid.nz = spec.nz;
        c_grid.topology = to_c_topology(spec.topology);
        c_grid.lon_coords = spec.lon_coords.data();
        c_grid.lon_len = static_cast<int>(spec.lon_coords.size());
        c_grid.lat_coords = spec.lat_coords.data();
        c_grid.lat_len = static_cast<int>(spec.lat_coords.size());
        c_grid.gridspec_file = spec.gridspec_file.empty() ? nullptr : spec.gridspec_file.c_str();
        c_grid.gridspec_file_len = static_cast<int>(spec.gridspec_file.size());

        cece_sim_create(path.c_str(), static_cast<int>(path.length()), &c_grid, mpi_comm_f, out_sim, rc);
    } catch (const std::exception& e) {
        cece::LogFatal(std::string("[SIM FATAL] cece_sim_create_from_yaml grid resolution failed: ") + e.what());
        set_out(rc, -1);
        set_out(out_sim, nullptr);
    }
}

void cece_sim_grid_info(const CeceSimulation* sim, int* nx, int* ny, int* nz, int* topology_out, double* lon_min, double* lon_max, double* lat_min,
                        double* lat_max, int* rc) {
    set_out(rc, 0);
    if (sim == nullptr) {
        set_out(rc, -1);
        return;
    }
    const cece::CeceSimulation& cpp_sim = *reinterpret_cast<const cece::CeceSimulation*>(sim);
    const cece::GridSpec& spec = cpp_sim.grid();
    set_out(nx, spec.nx);
    set_out(ny, spec.ny);
    set_out(nz, spec.nz);
    set_out(topology_out, to_c_topology(spec.topology));
    extent_of(spec.lon_coords, lon_min, lon_max);
    extent_of(spec.lat_coords, lat_min, lat_max);
}

void cece_sim_nz_from_config(const char* config_path, int path_len, int* nz_out, int* rc) {
    set_out(rc, 0);
    set_out(nz_out, 0);
    if (config_path == nullptr || nz_out == nullptr || rc == nullptr) {
        set_out(rc, -1);
        return;
    }
    try {
        *nz_out = cece::CeceSimulation::nz_from_config(cstr_to_string(config_path, path_len));
    } catch (const std::exception& e) {
        cece::LogFatal(std::string("[SIM FATAL] cece_sim_nz_from_config failed: ") + e.what());
        set_out(rc, -1);
    }
}

void cece_sim_describe_yaml_grid(const char* config_path, int path_len, char* buf, int buf_max, int* buf_len_out, int* rc) {
    set_out(rc, 0);
    set_out(buf_len_out, 0);
    if (config_path == nullptr || buf_len_out == nullptr || rc == nullptr) {
        set_out(rc, -1);
        return;
    }
    try {
        const std::string path = cstr_to_string(config_path, path_len);
        conf::Config config = conf::Config::from_file(path);
        const std::string desc = cece::GridSpec::from_yaml(path, config).describe();
        *buf_len_out = static_cast<int>(desc.size());
        // Mirror the sibling nuopc_copy_str contract: the buffer holds the
        // description plus its NUL terminator, so a caller that treats buf as a
        // C string reads a valid value. buf_len_out reports the length excluding
        // the NUL. Only written when there is room for the terminator.
        if (buf != nullptr && static_cast<int>(desc.size()) < buf_max) {
            std::memcpy(buf, desc.data(), desc.size());
            buf[desc.size()] = '\0';
        }
    } catch (const std::exception& e) {
        cece::LogFatal(std::string("[SIM] cece_sim_describe_yaml_grid failed: ") + e.what());
        set_out(rc, -1);
    }
}

void cece_sim_grid_from_esmf(int nx, int ny, int nz, int is_rad, const double* lon_coords, int lon_len, const double* lat_coords, int lat_len,
                             int* topology_out, double* lon_out, int lon_out_max, int* lon_len_out, double* lat_out, int lat_out_max,
                             int* lat_len_out, int* rc) {
    set_out(rc, 0);
    set_out(topology_out, CECE_GRID_RECTILINEAR);
    set_out(lon_len_out, 0);
    set_out(lat_len_out, 0);

    std::vector<double> lon;
    std::vector<double> lat;
    if (lon_coords != nullptr && lon_len > 0) {
        lon.assign(lon_coords, lon_coords + lon_len);
    }
    if (lat_coords != nullptr && lat_len > 0) {
        lat.assign(lat_coords, lat_coords + lat_len);
    }

    cece::GridSpec spec;
    try {
        spec = cece::GridSpec::from_esmf(nx, ny, nz, is_rad != 0, lon, lat);
    } catch (const std::exception& e) {
        // Loud failure with a named diagnostic — no uniform fallback.
        cece::LogFatal(std::string("[SIM FATAL] cece_sim_grid_from_esmf rejected the grid: ") + e.what());
        set_out(rc, -1);
        return;
    }

    set_out(topology_out, to_c_topology(spec.topology));
    // Probe-then-fill: callers pass NULL/short buffers to learn the sizes.
    set_out(lon_len_out, static_cast<int>(spec.lon_coords.size()));
    set_out(lat_len_out, static_cast<int>(spec.lat_coords.size()));
    if (lon_out != nullptr && lon_out_max >= static_cast<int>(spec.lon_coords.size())) {
        std::memcpy(lon_out, spec.lon_coords.data(), spec.lon_coords.size() * sizeof(double));
    }
    if (lat_out != nullptr && lat_out_max >= static_cast<int>(spec.lat_coords.size())) {
        std::memcpy(lat_out, spec.lat_coords.data(), spec.lat_coords.size() * sizeof(double));
    }
}

void cece_sim_create_from_esmf(const char* config_path, int path_len, int nx, int ny, int nz, int is_rad, const double* lon_coords, int lon_len,
                               const double* lat_coords, int lat_len, int mpi_comm_f, CeceSimulation** out_sim, int* rc) {
    set_out(rc, 0);
    set_out(out_sim, nullptr);
    if (config_path == nullptr || out_sim == nullptr || rc == nullptr) {
        set_out(rc, -1);
        return;
    }

    // Coupled branch: the cap extracted the parent grid's global coordinates
    // (Fortran-only ESMF API); normalize + validate them here and build the
    // simulation with the exact same lifecycle as the standalone branch.
    std::vector<double> lon;
    std::vector<double> lat;
    if (lon_coords != nullptr && lon_len > 0) {
        lon.assign(lon_coords, lon_coords + lon_len);
    }
    if (lat_coords != nullptr && lat_len > 0) {
        lat.assign(lat_coords, lat_coords + lat_len);
    }

    try {
        const cece::GridSpec spec = cece::GridSpec::from_esmf(nx, ny, nz, is_rad != 0, lon, lat);
        const std::string path = cstr_to_string(config_path, path_len);

        CeceGridSpec c_grid{};
        c_grid.nx = spec.nx;
        c_grid.ny = spec.ny;
        c_grid.nz = spec.nz;
        c_grid.topology = to_c_topology(spec.topology);
        c_grid.lon_coords = spec.lon_coords.data();
        c_grid.lon_len = static_cast<int>(spec.lon_coords.size());
        c_grid.lat_coords = spec.lat_coords.data();
        c_grid.lat_len = static_cast<int>(spec.lat_coords.size());
        c_grid.gridspec_file = nullptr;
        c_grid.gridspec_file_len = 0;

        cece_sim_create(path.c_str(), static_cast<int>(path.length()), &c_grid, mpi_comm_f, out_sim, rc);
    } catch (const std::exception& e) {
        cece::LogFatal(std::string("[SIM FATAL] cece_sim_create_from_esmf rejected the grid: ") + e.what());
        set_out(rc, -1);
        set_out(out_sim, nullptr);
    }
}

void cece_sim_bind_export_field(CeceSimulation* sim, const char* species, int species_len, double* data_ptr, int nx, int ny_local, int nz, int* rc) {
    set_out(rc, 0);
    if (sim == nullptr || species == nullptr || rc == nullptr) {
        set_out(rc, -1);
        return;
    }

    cece::CeceSimulation& cpp_sim = *reinterpret_cast<cece::CeceSimulation*>(sim);
    const std::string name = cstr_to_string(species, species_len);
    cpp_sim.bind_export_field(name, data_ptr, nx, ny_local, nz, rc);
}

void cece_sim_set_import_field(CeceSimulation* sim, const char* field, int field_len, const double* data_ptr, int nx, int ny_local, int* rc) {
    set_out(rc, 0);
    if (sim == nullptr || field == nullptr || rc == nullptr) {
        set_out(rc, -1);
        return;
    }

    cece::CeceSimulation& cpp_sim = *reinterpret_cast<cece::CeceSimulation*>(sim);
    const std::string name = cstr_to_string(field, field_len);
    cpp_sim.set_import_field(name, data_ptr, nx, ny_local, rc);
}

void cece_sim_clock_info(const char* config_path, int path_len, int* timestep_seconds_out, int* step_count_out, int* rc) {
    set_out(rc, 0);
    if (config_path == nullptr || timestep_seconds_out == nullptr || step_count_out == nullptr || rc == nullptr) {
        set_out(rc, -1);
        return;
    }

    try {
        const std::string path = cstr_to_string(config_path, path_len);
        conf::Config config = conf::Config::from_file(path);
        const std::string start_str = config.get_string("driver.start_time");
        const std::string end_str = config.get_string("driver.end_time");
        const int timestep_seconds = config.get_int("driver.timestep_seconds");

        tick::Gregorian_Calendar cal;
        const tick::Time_Point start = cal.to_time_point(tick::parse_iso8601(start_str));
        const tick::Time_Point end = cal.to_time_point(tick::parse_iso8601(end_str));

        *timestep_seconds_out = timestep_seconds;
        if (timestep_seconds > 0 && end > start) {
            const double total_seconds = static_cast<double>((end - start).nanos()) / 1e9;
            *step_count_out = static_cast<int>(total_seconds / static_cast<double>(timestep_seconds));
        } else {
            *step_count_out = 0;
        }
    } catch (const std::exception& e) {
        cece::LogFatal(std::string("[SIM FATAL] cece_sim_clock_info failed: ") + e.what());
        set_out(rc, -1);
    }
}

}  // extern "C"
