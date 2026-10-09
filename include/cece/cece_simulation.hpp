// SPDX-License-Identifier: Apache-2.0
// Copyright (c) HELM Project Contributors

#ifndef CECE_SIMULATION_HPP
#define CECE_SIMULATION_HPP

#include <mpi.h>

#include <conf/conf.hpp>
#include <memory>
#include <string>
#include <tick/tick.hpp>
#include <unordered_map>
#include <vector>

#include "cece/cece_band_decomposition.hpp"

namespace cece {

/// Topology of a resolved target grid. Mirrors the standalone writer's
/// GridType classification; the regrid path already consumes all three
/// (rectilinear 1-D, flattened curvilinear, unstructured node lists).
enum class GridTopology { Rectilinear, Curvilinear, Unstructured };

/**
 * @brief The single resolved description of the CECE target grid.
 *
 * This is the common currency both drivers hand to CeceSimulation: the C++
 * standalone driver builds it from YAML (named grids, gridspec files,
 * stream-inferred coordinates, uniform extents), and the NUOPC cap builds it
 * from an ESMF Grid/Mesh (or falls back to the same YAML path for standalone
 * runs). Everything downstream (cece_driver_create, the writer) consumes it
 * unchanged. Methods are snake_case to match the C ABI and driver layer.
 *
 * Coordinate arrays are degrees, CF-unpacked, radians converted, longitudes
 * wrapped to [-180, 180) — the producer normalizes; validate() checks shape.
 * Flattened curvilinear and unstructured grids use the existing convention:
 * ny == 1 with lon/lat arrays of length nx (the node/cell count).
 */
struct GridSpec {
    int nx = 0;
    int ny = 0;
    int nz = 1;
    GridTopology topology = GridTopology::Rectilinear;
    std::vector<double> lon_coords;
    std::vector<double> lat_coords;
    std::string gridspec_file;  ///< Optional; forwarded to the writer for bounds.

    /// Resolve the target grid from the CECE YAML configuration, exactly as
    /// the standalone driver has always done: named regular grids (F/R via
    /// axis::topology::NamedGridRegistry), an explicit or stream-inferred
    /// gridspec file read through AMIO (with CF unpacking and radian
    /// handling), or uniform extents from driver.grid. Throws
    /// std::invalid_argument with a named diagnostic on any failure —
    /// never a silent fallback.
    static GridSpec from_yaml(const std::string& config_file, conf::Config& config);

    /**
     * @brief Build the target grid from a parent NUOPC component's ESMF
     * grid/mesh, given the coordinate arrays the cap extracted from it.
     *
     * The cap (Fortran side) performs the ESMF extraction — ESMF_GridGetCoord
     * for 1-D (rectilinear) and 2-D (curvilinear) coordinates and
     * ESMF_MeshGet for unstructured node coordinates — assembles the GLOBAL
     * arrays across PETs, and applies the unit conversion implied by the
     * grid's coordinate system (degrees for SPH_DEG, radians -> degrees for
     * SPH_RAD, passed here as is_rad). Longitudes are wrapped to [-180, 180)
     * here, matching from_yaml exactly. The core consumes only coordinate
     * arrays (never an ESMF object), so rank classification follows the
     * array shapes:
     *   - lon.size()==nx, lat.size()==ny (ny>1)          -> Rectilinear
     *   - lon.size()==lat.size()==nx*ny (ny>1)           -> Curvilinear
     *   - ny==1, lon.size()==lat.size()==nx              -> Unstructured
     * Anything else fails loudly (no silent fallback).
     *
     * @param nx         Longitude count (rectilinear) or node/cell count.
     * @param ny         Latitude count; 1 for flattened curvilinear/unstructured.
     * @param nz         Vertical layers — ALWAYS from config, never from the grid.
     * @param is_rad     Coordinates arrive in radians (SPH_RAD) and are converted.
     * @param lon/lat    Extracted coordinate values (degrees, or radians with
     *                   is_rad set); lengths per the classification above.
     * @param gridspec_file  Optional GRIDSPEC/mesh file backing the ESMF object.
     */
    static GridSpec from_esmf(int nx, int ny, int nz, bool is_rad, const std::vector<double>& lon, const std::vector<double>& lat,
                              const std::string& gridspec_file = "");

    /// Human-readable identity (topology + dims + coordinate counts), used
    /// in the precedence warning so a run names the grid it ignored.
    std::string describe() const;

    /// Validate dimensions and coordinate-array shapes for the declared
    /// topology. Throws std::invalid_argument with a named diagnostic.
    void validate() const;
};

/**
 * @brief Result of one CeceSimulation::step.
 *
 * Named StepOutcome (not StepResult) because cece::StepResult is already
 * taken by CeceClock::Advance's due-component schedule; both headers are
 * included transitively in the simulation implementation.
 *
 * Mirrors cece_core_run's rc semantics: complete is true when the core clock
 * reached the configured end time (rc == 1); error is true on failure
 * (rc < 0). Hosts MUST stop stepping once complete is set — this is the
 * single completion signal both drivers honor, guaranteeing equal step
 * counts.
 */
struct StepOutcome {
    int rc = 0;
    bool complete = false;
    bool error = false;
};

/**
 * @brief Shared simulation lifecycle for both CECE drivers.
 *
 * Step-oriented (callbacks, not a blocking loop) so the ESMF-phase-driven
 * NUOPC cap and the C++ standalone main() drive it identically. Methods are
 * snake_case, matching the C ABI they back and the rest of the driver-layer
 * API. Owns everything that must not drift between drivers:
 *
 *  - create(): core p1 -> realize -> p2 -> export-field registration ->
 *    driver orchestrator -> writer initialization, in that fixed order.
 *  - step(): ingest at step_start, core run (the core clock owns
 *    hour/day-of-week/month), stamp output at step-end elapsed.
 *  - finalize(): driver teardown -> core finalize with warning-only failure.
 *
 * The two drivers differ ONLY in how they produce the GridSpec passed to
 * create().
 */
class CeceSimulation {
   public:
    /// Build the simulation. On success returns a non-null handle and sets
    /// *rc_out = 0. On failure returns nullptr and sets *rc_out < 0 after
    /// logging a named diagnostic (bad grid, core init failure, ...).
    /// mpi_comm is the real MPI communicator for band decomposition and the
    /// writer gather (MPI_COMM_WORLD in both drivers today).
    static std::unique_ptr<CeceSimulation> create(const std::string& config_path, const GridSpec& grid, MPI_Comm comm, int* rc_out);

    /// Advance one timestep: ingest at step_start_iso, run the compute core,
    /// write the output record stamped with elapsed time derived from
    /// step_end_iso. step_index is the monotonic 0-based output counter.
    StepOutcome step(const std::string& step_start_iso, const std::string& step_end_iso, int step_index);

    /// Tear down: driver orchestrator destroy, then core finalize. Sets
    /// rc_out to the teardown status (non-zero is a warning: output was
    /// already flushed). The handle must not be used afterwards.
    void finalize(int& rc_out);

    /// The resolved target grid the simulation was created with. Exposed for
    /// the C-ABI facade's grid-info entry so the NUOPC cap can set a matching
    /// ESMF grid on its component without re-deriving coordinates.
    const GridSpec& grid() const {
        return grid_;
    }

    /// The configuration file the simulation was created from.
    const std::string& config_path() const {
        return config_path_;
    }

    /// Bind a realized export field's ESMF-owned memory as the persistent
    /// write-back target for `species`. The coupling contract: each step the
    /// core deep-copies the managed host view into whatever
    /// `persistent_export_ptrs` points at, so rebinding this map is the only
    /// change needed to redirect write-back into ESMF field storage.
    ///
    /// The species MUST already exist in `export_state.fields` (created at
    /// config realization), and (nx, ny_local, nz) MUST equal this rank's
    /// band geometry — the facade owns the decomposition and verifies it,
    /// so a mismatch between the ESMF field extents and the core band fails
    /// loudly instead of writing back out of bounds.
    ///
    /// MUST NOT touch `export_state.fields`: the managed DualView there is
    /// the one the stacking engine bound its device view to at compile time,
    /// and replacing it would dangle that view. `data_ptr` is borrowed —
    /// ESMF owns the memory and the core never frees or reallocates it.
    ///
    /// @return true on success; false with *rc < 0 and a logged diagnostic
    ///         on unknown species, null/invalid extents, or a mismatch.
    bool bind_export_field(const std::string& species, double* data_ptr, int nx, int ny_local, int nz, int* rc);

    /// Copy a connected import field's ESMF-owned host memory into the core's
    /// import state for the current step (the NUOPC import binder). The
    /// configured input name is resolved through the meteorology/scale-factor/
    /// mask mappings to the import-state key, exactly as the compute-side
    /// resolver does, so the host value lands under the name the schemes read.
    ///
    /// The target managed DualView is created on first use when absent (a
    /// host-only field with no file stream); otherwise the values are copied
    /// into the existing view rather than replacing it, keeping the device
    /// buffer the schemes sync to stable across steps. The copy is host-side
    /// and then synced to device so the step's compute sees the host values.
    ///
    /// A 2-D surface field arrives as (nx, ny_local); it is stored with a
    /// single vertical layer, which the resolver reads in a 3-D context.
    /// (nx, ny_local) MUST equal this rank's band geometry — the facade owns
    /// the decomposition and verifies it, so a mismatch fails loudly instead
    /// of copying out of bounds. `data_ptr` is borrowed: ESMF owns the memory
    /// and the core never frees or reallocates it.
    ///
    /// @return true on success; false with *rc < 0 and a logged diagnostic on
    ///         a name that resolves to no configured input, null/invalid
    ///         extents, or a band mismatch.
    bool set_import_field(const std::string& field, const double* data_ptr, int nx, int ny_local, int* rc);

    /// Read the configured vertical layer count (driver.grid.nz, default 1)
    /// from a CECE YAML. Exposed so the C-ABI facade's ESMF entry can supply
    /// GridSpec.nz — which must always come from config, never from a 2-D
    /// ESMF grid object. Throws std::invalid_argument on a non-positive value.
    static int nz_from_config(const std::string& config_path);

    /// Reject a configured nz that conflicts with the input data's declared
    /// vertical levels. A stream variable may carry its own `levels:` count
    /// (defaulting to the grid nz); when any declared level count exceeds 1
    /// and differs from the configured nz, the input cannot be stacked onto
    /// the target layers, so creation fails loudly rather than silently
    /// reinterpreting the vertical dimension. Throws std::invalid_argument.
    static void validate_nz_against_streams(const std::string& config_path, int nz);

    ~CeceSimulation();

    CeceSimulation(const CeceSimulation&) = delete;
    CeceSimulation& operator=(const CeceSimulation&) = delete;
    CeceSimulation(CeceSimulation&&) = delete;
    CeceSimulation& operator=(CeceSimulation&&) = delete;

   private:
    /// Body of create(): every step that can throw. create() wraps this in a
    /// catch-all so no exception can cross the C ABI, and turns thrown
    /// diagnostics into the logged failure return. Reports status through
    /// rc_out exactly like create().
    static std::unique_ptr<CeceSimulation> create_internal(const std::string& config_path, const GridSpec& grid, MPI_Comm comm, int* rc_out);

    CeceSimulation() = default;

    // Test-only access, following the CeceDriverOrchestrator/
    // EndpointCacheTestAccess precedent: lets tests build an instance with a
    // known clock anchor and sentinel handles so Step's call sequence can be
    // observed through link-time interposed spies
    // (tests/test_driver_time_convention.cpp). Grants access only — it adds
    // no runtime path or indirection to production code.
    friend struct CeceSimulationTestAccess;

    void* core_data_ptr_ = nullptr;  ///< cece_core_* lifecycle handle.
    void* driver_ptr_ = nullptr;     ///< CeceDriverOrchestrator handle.
    GridSpec grid_;
    BandDecomposition band_;
    MPI_Comm comm_ = MPI_COMM_WORLD;
    std::string config_path_;
    /// Band-local persistent export buffers (nx x ny_local x nz) keyed by
    /// field name. Kept alive for the simulation lifetime; the core writes
    /// results back through these pointers (SyncAndCopyState).
    std::unordered_map<std::string, std::vector<double>> export_buffers_;
    /// Driver-side clock anchors: the configured start time (elapsed output
    /// stamps are measured from it) and the base timestep in seconds.
    tick::Time_Point start_time_{};
    double dt_seconds_ = 3600.0;
    bool finalized_ = false;
};

}  // namespace cece

#endif  // CECE_SIMULATION_HPP
