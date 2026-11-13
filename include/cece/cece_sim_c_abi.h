// SPDX-License-Identifier: Apache-2.0
// Copyright (c) HELM Project Contributors

#ifndef CECE_SIM_C_ABI_H
#define CECE_SIM_C_ABI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file cece_sim_c_abi.h
 * @brief Shared simulation-lifecycle facade used by BOTH CECE drivers.
 *
 * This is the parity boundary between the two CECE launch paths. The C++
 * standalone driver and the NUOPC cap each
 * become thin adapters that (1) resolve a target grid into a CeceGridSpec and
 * (2) drive a simulation through these calls. Everything that must be
 * identical between the drivers — lifecycle ordering, export-field
 * registration, the ingest instant, the output-stamp convention, and the
 * completion signal — lives behind this interface, so it cannot drift.
 *
 * The granular C-ABI symbols (cece_core_*, cece_driver_*,
 * cece_core_writer_*, cece_core_set_export_field) remain present and
 * unchanged; this facade sequences them.
 */

// This header is a C/C++ polyglot ABI boundary: it is included by plain C
// translation units (the NetCDF test helpers) as well as C++ and via
// bind(C) from Fortran. `typedef` is mandatory here — a C++ `using` alias
// for a struct/enum elaborated type specifier is not valid C, so the
// modernize-use-using check is a false positive across this block.
// NOLINTBEGIN(modernize-use-using)

/// Opaque simulation handle. Produced by cece_sim_create, consumed by
/// cece_sim_step / cece_sim_finalize. Not dereferenced by callers.
typedef struct CeceSimulation CeceSimulation;

/// Topology of the resolved target grid. Mirrors the writer's GridType
/// classification (Rectilinear / Curvilinear / Unstructured).
typedef enum CeceGridTopology {
    CECE_GRID_RECTILINEAR = 0,  ///< 1-D lon[nx] x lat[ny] structured grid.
    CECE_GRID_CURVILINEAR = 1,  ///< 2-D coordinates flattened to length nx*ny.
    CECE_GRID_UNSTRUCTURED = 2  ///< Mesh nodes; ny == 1, coords length == nx.
} CeceGridTopology;

/**
 * @brief Resolved target grid handed to the simulation.
 *
 * Coordinate arrays are in degrees, CF-unpacked (scale_factor/add_offset
 * applied), radians converted to degrees, and longitudes wrapped to
 * [-180, 180). These normalizations are performed by the producer
 * (GridSpec::from_yaml / GridSpec::from_esmf); the facade validates shape.
 *
 * Flattened curvilinear and unstructured grids use the existing convention
 * ny == 1 with lon/lat arrays of length nx (the node/cell count).
 */
typedef struct CeceGridSpec {
    int nx;                     ///< Longitude count (rectilinear) or node count.
    int ny;                     ///< Latitude count; 1 for flattened/unstructured.
    int nz;                     ///< Vertical layers — ALWAYS from config.
    int topology;               ///< CeceGridTopology value.
    const double* lon_coords;   ///< Length nx (rect) or nx*ny (curv/unstructured).
    int lon_len;                ///< Number of elements in lon_coords.
    const double* lat_coords;   ///< Length ny (rect) or nx*ny (curv/unstructured).
    int lat_len;                ///< Number of elements in lat_coords.
    const char* gridspec_file;  ///< Optional path, may be NULL or empty.
    int gridspec_file_len;      ///< Length of gridspec_file (0 if NULL).
} CeceGridSpec;

// NOLINTEND(modernize-use-using)

/**
 * @brief Build the simulation: initialize the core, realize config, bind the
 * grid, register export fields, create the driver orchestrator, and initialize
 * the output writer.
 *
 * Ordering (fixed, identical for every caller):
 *   set_config_path -> run_log_setup -> core_p1 -> core_realize -> core_p2 ->
 *   register export fields (band-local) -> driver_create -> writer_initialize.
 *
 * @param config_path  Path to the CECE YAML configuration.
 * @param path_len     Length of config_path (may exclude a trailing NUL; the
 *                     facade treats it as the string length, matching the
 *                     existing cece_core_* C-ABI convention).
 * @param grid         Resolved target grid. Must be non-NULL and valid.
 * @param mpi_comm_f   Fortran MPI communicator handle (MPI_Comm_c2f), or 0 for
 *                     MPI_COMM_WORLD when MPI is already initialized.
 * @param out_sim      Receives the created handle on success; set to NULL on
 *                     failure.
 * @param rc           0 on success, < 0 on failure (a named diagnostic is
 *                     logged). Never a silent fallback.
 */
void cece_sim_create(const char* config_path, int path_len, const CeceGridSpec* grid, int mpi_comm_f, CeceSimulation** out_sim, int* rc);

/**
 * @brief Advance one timestep.
 *
 * Ingests offline data at step_start_iso, runs the compute core (whose own
 * clock owns hour / day-of-week / month), and stamps the output record with
 * elapsed time derived from step_end_iso. On the step where the core clock
 * reaches the configured end time, complete_out is set to 1 and the caller
 * MUST stop stepping.
 *
 * @param sim              Handle from cece_sim_create.
 * @param step_start_iso   ISO8601 time at the start of the step (ingest).
 * @param step_start_len   Length of step_start_iso.
 * @param step_end_iso     ISO8601 time at the end of the step (output stamp).
 * @param step_end_len     Length of step_end_iso.
 * @param step_index       Monotonic 0-based output step counter.
 * @param complete_out     1 when the simulation is complete, else 0.
 * @param rc               0 on success, < 0 on failure.
 */
void cece_sim_step(CeceSimulation* sim, const char* step_start_iso, int step_start_len, const char* step_end_iso, int step_end_len, int step_index,
                   int* complete_out, int* rc);

/**
 * @brief Tear down the simulation: destroy the driver orchestrator, then
 * finalize the core.
 *
 * @param sim  Handle from cece_sim_create. Ownership is released; the handle
 *             must not be used afterwards.
 * @param rc   0 on success. Non-zero means a teardown resource reported a
 *             failure (output was already flushed); this is a warning, not an
 *             abort — matching the existing driver teardown semantics.
 */
void cece_sim_finalize(CeceSimulation* sim, int* rc);

/**
 * @brief Read the base timestep and total step count from the configuration,
 * so both drivers derive step_start/step_end instants identically without each
 * re-parsing the YAML clock.
 *
 * @param config_path        Path to the CECE YAML configuration.
 * @param path_len           Length of config_path.
 * @param timestep_seconds_out Receives the base timestep in seconds.
 * @param step_count_out     Receives the number of complete steps in
 *                           [start_time, end_time).
 * @param rc                 0 on success, < 0 on failure.
 */
void cece_sim_clock_info(const char* config_path, int path_len, int* timestep_seconds_out, int* step_count_out, int* rc);

/**
 * @brief Resolve the grid from the CECE YAML and build the simulation in one
 * call — the NUOPC cap's config-built (standalone) branch.
 *
 * This is literally `GridSpec::from_yaml(...)` followed by `cece_sim_create`,
 * so the standalone cap runs the exact same grid-resolution + lifecycle code as
 * the C++ driver (parity by construction). The cap never touches
 * coordinate buffers. On success *out_sim is set and *rc = 0; on failure
 * *out_sim is NULL and *rc < 0 with a logged diagnostic.
 */
void cece_sim_create_from_yaml(const char* config_path, int path_len, int mpi_comm_f, CeceSimulation** out_sim, int* rc);

/**
 * @brief Read back the resolved grid from a live simulation.
 *
 * The cap uses this in InitializeRealize to associate a matching ESMF grid on
 * its component without re-deriving coordinates (the coordinates already live
 * in the simulation). lon/lat extents are the bounding box of the resolved
 * coordinate arrays. Only the requested outputs are written (NULL allowed).
 *
 * @param sim        Handle from cece_sim_create / cece_sim_create_from_yaml.
 * @param nx,ny,nz   Grid dimensions (ny == 1 for flattened/unstructured).
 * @param topology_out  CeceGridTopology value of the resolved grid.
 * @param lon_min,lon_max,lat_min,lat_max  Bounding box of the coordinates.
 * @param rc         0 on success, < 0 on failure (NULL sim).
 */
void cece_sim_grid_info(const CeceSimulation* sim, int* nx, int* ny, int* nz, int* topology_out, double* lon_min, double* lon_max, double* lat_min,
                        double* lat_max, int* rc);

/**
 * @brief Read the configured vertical layer count (driver.grid.nz) from a
 * CECE YAML. The cap must populate CeceGridSpec.nz from config — never from
 * a (2-D) ESMF grid object — so this is the single shared source.
 *
 * @param config_path Path to the CECE YAML configuration.
 * @param path_len    Length of config_path.
 * @param nz_out      Receives the configured layer count.
 * @param rc          0 on success, < 0 on failure.
 */
void cece_sim_nz_from_config(const char* config_path, int path_len, int* nz_out, int* rc);

/**
 * @brief Describe the grid that GridSpec::from_yaml would resolve for this
 * configuration, as a human-readable string. Used by the cap's precedence
 * warning so an ignored YAML grid is named, not silently dropped.
 *
 * Two-call pattern: pass buf == NULL (or too small) to probe the needed
 * length via *buf_len_out, then call again with a buffer of that size.
 *
 * @param config_path   Path to the CECE YAML configuration.
 * @param path_len      Length of config_path.
 * @param buf           Output buffer for the description (may be NULL).
 * @param buf_max       Capacity of buf.
 * @param buf_len_out   Receives the description length (excluding NUL).
 * @param rc            0 on success, < 0 on failure (grid resolution error).
 */
void cece_sim_describe_yaml_grid(const char* config_path, int path_len, char* buf, int buf_max, int* buf_len_out, int* rc);

/**
 * @brief Validate/normalize an ESMF-extracted grid without building a
 * simulation (the cap's parent-grid unit path).
 *
 * Wraps longitudes to [-180,180), converts radians when is_rad != 0,
 * classifies topology from the array shapes, and validates. On success
 * *topology_out receives the CeceGridTopology and the normalized
 * coordinates are written (probe-then-fill: NULL/short buffers return
 * required lengths via *lon_len_out / *lat_len_out with rc == 0). On an
 * unsupported shape or bad dims *rc < 0 with a logged diagnostic — never a
 * silent fallback.
 */
void cece_sim_grid_from_esmf(int nx, int ny, int nz, int is_rad, const double* lon_coords, int lon_len, const double* lat_coords, int lat_len,
                             int* topology_out, double* lon_out, int lon_out_max, int* lon_len_out, double* lat_out, int lat_out_max,
                             int* lat_len_out, int* rc);

/**
 * @brief Build the simulation from a parent NUOPC component's ESMF grid/mesh
 * (the coupled branch): GridSpec::from_esmf + cece_sim_create in one call.
 *
 * The cap performs the ESMF extraction (Fortran-only API) and passes the
 * assembled GLOBAL coordinate arrays here. nz must come from the YAML
 * (cece_sim_nz_from_config), never from the grid object. On success
 * *out_sim is set and *rc = 0; on failure *out_sim is NULL and *rc < 0.
 */
void cece_sim_create_from_esmf(const char* config_path, int path_len, int nx, int ny, int nz, int is_rad, const double* lon_coords, int lon_len,
                               const double* lat_coords, int lat_len, int mpi_comm_f, CeceSimulation** out_sim, int* rc);

/**
 * @brief Bind a realized export field's ESMF-owned memory as the persistent
 * write-back target for `species` (the NUOPC coupling binder).
 *
 * Called from the cap's InitializeRealize for each CONNECTED export field,
 * after NUOPC_Realize and ESMF_FieldGet(farrayPtr). This is a pointer-map
 * update only: each step the core's write-back deep-copies the managed host
 * view into whatever the persistent pointer map points at, so rebinding it
 * redirects output into ESMF field storage. The managed DualView in the
 * export state is never replaced (the stacking engine's device view is bound
 * to it at compile time). `data_ptr` is borrowed — ESMF owns the memory and
 * the core MUST NOT free or reallocate it.
 *
 * (nx, ny_local, nz) are the ESMF field's own per-PET extents; the facade
 * verifies they match the species' managed view and this rank's band
 * geometry. Unknown species or a dimension mismatch fails with rc < 0 and a
 * logged diagnostic — never a silent fallback.
 *
 * @param sim          Handle from cece_sim_create*.
 * @param species      Species key as configured under `nuopc: export_fields`.
 * @param species_len  Length of species (existing cece_core_* convention).
 * @param data_ptr     The field's farrayPtr (ESMF-owned, band-shaped).
 * @param nx           Field longitude extent (must match the core grid).
 * @param ny_local     Field latitude extent (must match this rank's band).
 * @param nz           Field vertical extent (must match the core grid).
 * @param rc           0 on success, < 0 on failure.
 */
void cece_sim_bind_export_field(CeceSimulation* sim, const char* species, int species_len, double* data_ptr, int nx, int ny_local, int nz, int* rc);

/**
 * @brief Copy a connected import field's ESMF-owned host memory into the
 * core's import state for the current step (the NUOPC import binder).
 *
 * Called from the cap's Run for each CONNECTED import field, before the
 * simulation steps. The configured internal input name is resolved through
 * the meteorology/scale-factor/mask mappings to the import-state key, and the
 * values are copied into that key's managed DualView (created on first use
 * for a host-only field), then synced to device so the step's compute sees the
 * host values. The managed view is reused, never replaced, so the device
 * buffer the schemes sync to stays stable across steps. `data_ptr` is
 * borrowed — ESMF owns the memory and the core MUST NOT free or reallocate
 * it.
 *
 * A 2-D surface field arrives as (nx, ny_local) and is stored with a single
 * vertical layer. The facade verifies (nx, ny_local) match this rank's band;
 * a mismatch or an unusable existing view fails with rc < 0 and a logged
 * diagnostic — never a silent fallback.
 *
 * @param sim         Handle from cece_sim_create*.
 * @param field       Configured input name as under `nuopc: import_fields`.
 * @param field_len   Length of field (existing cece_core_* convention).
 * @param data_ptr    The field's farrayPtr (ESMF-owned, band-shaped, const).
 * @param nx          Field longitude extent (must match the core grid).
 * @param ny_local    Field latitude extent (must match this rank's band).
 * @param rc          0 on success, < 0 on failure.
 */
void cece_sim_set_import_field(CeceSimulation* sim, const char* field, int field_len, const double* data_ptr, int nx, int ny_local, int* rc);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CECE_SIM_C_ABI_H
