#ifndef CECE_DRIVER_FACADE_HPP
#define CECE_DRIVER_FACADE_HPP

#include <amio/amio.h>
#include <mpi.h>

#include <cstddef>
#include <dagr/dagr.hpp>
#include <halo/collectives.hpp>
#include <halo/communicator.hpp>
#include <halo/environment.hpp>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "cece/cece_band_decomposition.hpp"
#include "cece/cece_io.hpp"
#include "cece/cece_regridder_utils.hpp"

// Forward declaration only: the header never touches yaml-cpp; the member
// taking a const YAML::Node& is defined in the .cpp, which includes yaml.h.
namespace YAML {
class Node;
}

namespace cece {

/**
 * @brief Resolved input-record selection for a given simulation time.
 *
 * @c i0 / @c i1 are the lower/upper record indices and @c weight is the
 * fraction toward @c i1: the interpolated field is
 * @f$ (1-w)\,\mathrm{rec}[i_0] + w\,\mathrm{rec}[i_1] @f$. When @c weight is 0
 * (or @c i0 == @c i1) a single read of @c i0 suffices. @c valid is false when
 * the caller should fall back to legacy step-index cycling.
 *
 * This struct is shared between the facade header (for use as
 * SliceCacheEntry::last_bracket) and the facade translation unit's temporal
 * cadence helpers.
 */
struct RecordBracket {
    int i0 = 0;
    int i1 = 0;
    double weight = 0.0;
    bool valid = false;  ///< false -> caller falls back to legacy step-index cycling.
};

/**
 * @brief Resolved per-stream-variable configuration.
 *
 * Resolved once per stream variable at construction; depends only on
 * configuration + file, never on simulation time.
 */
struct StreamConfig {
    std::string input_file_path;       ///< stream["file"]; "" => missing (Req 1.4)
    std::string input_var_name;        ///< resolved file var name (falls back to model name)
    std::string mapalgo = "consd";     ///< default matches current AdvanceTime
    std::string cadence;               ///< "" => legacy step-index cycling
    std::string tintalgo = "nearest";  ///< "linear" | "nearest"
    std::string data_model = "enhanced";
    bool data_model_explicit = false;                   ///< true => open with only data_model
    int amio_worker_threads = 1;                        ///< driver-level, validated >= 1
    int amio_staging_buffer_count = 8;                  ///< driver-level, validated >= 1
    int amio_staging_buffer_capacity_bytes = 33554432;  ///< driver-level, validated >= 1 (32 MiB)
    int amio_prefetch_depth = 2;                        ///< driver-level, validated >= 1
};

/**
 * @brief Retained AMIO resources for one stream variable, opened at most once.
 */
struct AmioHandleSet {
    amio_core_handle core = nullptr;        ///< from amio_init_from_string
    amio_dataset_handle dataset = nullptr;  ///< from amio_open_dataset_from_string
    std::string active_data_model;          ///< model that actually opened
    std::string manifest_content;           ///< in-memory manifest (never a file)
};

/**
 * @brief Most-recent read+regrid result plus the bracket that produced it.
 */
struct SliceCacheEntry {
    RecordBracket last_bracket;         ///< bracket that produced ingest_buffer
    bool valid = false;                 ///< false until first successful compute
    std::vector<double> ingest_buffer;  ///< field_nlev * nx_ * ny_local, this rank's band
    size_t ingest_size = 0;             ///< == field_nlev * nx_ * ny_local
};

/**
 * @brief Two destination-grid regridded endpoint fields for one variable,
 *        keyed on the bracket indices (i0, i1) only (NOT the weight).
 *
 * endpoint_i0 == regrid(record i0), endpoint_i1 == regrid(record i1), each a
 * band buffer sized field_nlev * nx_ * ny_local laid out [level][jrel][i] — the
 * SAME layout AssembleBandField's ingest_buffer uses. On a same-indices step
 * the driver blends these as (1-w)*endpoint_i0 + w*endpoint_i1 without any read
 * or regrid (Req 1.1-1.3, 8.1). On the single-rank / no-MPI path ny_local ==
 * ny_ so the band buffer IS the former global buffer.
 */
struct EndpointCacheEntry {
    int cached_i0 = -1;               ///< bracket index that produced endpoint_i0
    int cached_i1 = -1;               ///< bracket index that produced endpoint_i1
    bool valid = false;               ///< false until both endpoints built for (cached_i0,cached_i1)
    std::vector<double> endpoint_i0;  ///< regrid(record i0), field_nlev*nx*ny_local
    std::vector<double> endpoint_i1;  ///< regrid(record i1), field_nlev*nx*ny_local
    int built_field_nlev = 0;         ///< shape at build time (shape-change invalidation)
    int built_nx = 0;
    int built_ny = 0;  ///< stores ny_local (this rank's band rows), compared against band_.ny_local
};

class CeceDriverOrchestrator {
   public:
    CeceDriverOrchestrator(const std::string& config_file, int nx, int ny, int nz, const double* lon_coords, int lon_len, const double* lat_coords,
                           int lat_len, MPI_Comm comm_c);
    ~CeceDriverOrchestrator();

    // Prevent copy/move construction and assignment (Rule of Five)
    CeceDriverOrchestrator(const CeceDriverOrchestrator&) = delete;
    CeceDriverOrchestrator& operator=(const CeceDriverOrchestrator&) = delete;
    CeceDriverOrchestrator(CeceDriverOrchestrator&&) = delete;
    CeceDriverOrchestrator& operator=(CeceDriverOrchestrator&&) = delete;

    bool AdvanceTime(const std::string& time_iso8601, void* cece_core_data_ptr);

    // Release every retained AMIO handle set at shutdown. Iterates amio_handles_
    // and for each set closes the dataset (amio_close) then finalizes the core
    // (amio_finalize), each wrapped in its own best-effort try/catch so one
    // failing handle does not prevent the rest from tearing down; null
    // dataset/core are skipped. After the loop it clears amio_handles_,
    // stream_configs_, and slice_caches_. No manifest files are deleted because
    // none are ever created (all manifests are in-memory strings) (Req 7.2-7.5).
    // Returns false if any close/finalize failed or threw (failures are logged
    // at ERROR, never thrown: the destructor path must stay noexcept). Public
    // so the cece_driver_destroy C wrapper can invoke it explicitly and report
    // teardown failures through its rc out-param; the destructor also calls it,
    // where a second invocation is a safe no-op.
    bool TeardownHandles();

   private:
    using DeviceView3D = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>;

    // Recompose one source record into this rank's LATITUDE-BAND import field:
    // the front half (RegridToBandBuffer) produces the band buffer sized
    // field_nlev * nx_ * ny_local laid out [level][jrel][i]; the returned
    // ingest_buffer IS that band buffer; the back half (WriteBandToImport)
    // transposes only [0, ny_local) rows into a band-tall
    // DualView3D(nx_, ny_local, field_nlev). On the single-rank / no-MPI path
    // ny_local == ny_, so the band field IS the former global field and the
    // outcome is unchanged.
    bool AssembleBandField(const std::string& var_name, const io::RegridPlan& plan, const std::vector<double>& source, int file_nx, int file_ny,
                           int field_nlev, DeviceView3D stream_view, void* cece_core_data_ptr, std::vector<double>& ingest_buffer,
                           std::string& failure_detail);

    // Front half of AssembleBandField: regrid ONE source record into this
    // rank's LATITUDE-BAND buffer via per-level apply_regrid_plan. The former
    // per-level MPI_Allgatherv that re-replicated every band into a global
    // field_nlev * nx_ * ny_ buffer is REMOVED (Req 3.1): apply_regrid_plan
    // already emits exactly this rank's band slice (nx_ * ny_local), so that
    // slice is kept as the final per-rank result. Produces the [level][jrel][i]
    // band buffer (sized field_nlev * nx_ * band_.ny_local, element
    // (level, jrel, i) at level*nx_*ny_local + jrel*nx_ + i) into out_buffer.
    // Runs the identical source/metadata readiness + fused nx/ny/nlev/identity
    // gate, and RETAINS the single pre-gather MPI_Allreduce(MIN) over local_ok
    // so all ranks still agree the band regrid succeeded before proceeding
    // (Req 3.4, 3.5). Does NOT touch import_state or stream_view and takes no
    // cece_core_data_ptr. Single-rank (ny_local == ny_) outcome is unchanged:
    // the band buffer IS the global buffer.
    bool RegridToBandBuffer(const std::string& var_name, const io::RegridPlan& plan, const std::vector<double>& source_record, int file_nx,
                            int file_ny, int field_nlev, std::vector<double>& out_buffer, std::string& failure_detail);

    // Back half of AssembleBandField: transpose the rank-local [level][jrel][i]
    // BAND buffer (sized field_nlev * nx_ * band_.ny_local) into the
    // (i, jrel, level) core import DualView and write it authoritatively.
    // Iterates only [0, ny_local) rows and allocates the import field band-tall
    // DualView3D(nx_, band_.ny_local, field_nlev) (Req 2.1, 2.4, 3.2). Runs the
    // core-import-field-shape collective_all_ready gate validated against
    // ny_local, and the deep_copy + modify_device() + sync_host(). Surplus ranks
    // (ny_local == 0) allocate a zero-row field and skip the copy loop but still
    // enter the collective gate so ranks stay in lock-step (Req 2.5, 3.4).
    // Single-rank (ny_local == ny_) outcome is unchanged: the band field IS the
    // global field. Local work plus the single core-import-shape collective gate.
    bool WriteBandToImport(const std::string& var_name, const std::vector<double>& dest_buffer, int field_nlev, void* cece_core_data_ptr,
                           std::string& failure_detail);

    // Build or rebuild halo_comm_ to wrap the current comm_c_. Duplicates a
    // non-predefined handle (comm != WORLD/SELF/NULL) mirroring
    // src/driver/cece_helm_graph.cpp's convention, and wraps MPI_COMM_WORLD /
    // MPI_COMM_SELF directly. Leaves halo_comm_ absent
    // when MPI is uninitialized, comm_c_ is MPI_COMM_NULL, or mpi_size <= 1,
    // preserving the single-rank short-circuit rather than constructing a HALO
    // Communicator over an invalid comm (Req 7.1, 7.2). Assumes
    // halo::Environment::initialize() has already run (invoked in main.cpp after
    // MPI init); does not re-init here. Must be called wherever comm_c_ is
    // (re)assigned so the wrapper never lags the communicator (Req 7.3).
    void RefreshHaloCommunicator();

    // Parse config_file_ once at construction and populate stream_configs_ for
    // every model variable, resolving the same fields and defaults the legacy
    // inline AdvanceTime parse produced. Also resolves the driver-level
    // amio_worker_threads / amio_staging_buffer_count (validated >= 1) and
    // gridspec_file_ (Req 1.1-1.5, 4.3).
    void ResolveStreamConfigs();

    // Pure YAML->StreamConfig resolution shared by ResolveStreamConfigs() and
    // the Property 4 test. Parses config_file, fills out_configs (keyed by model
    // variable name) and out_gridspec_file using the exact same field resolution
    // and defaults as the legacy inline AdvanceTime parse. Static so it can be
    // exercised in isolation without constructing a full orchestrator (which
    // requires MPI/DAGR/CeceIO). This is the SAME code path production uses; it
    // is not a copy (Req 1.1-1.5, 4.3).
    static void ResolveStreamConfigsFromFile(const std::string& config_file, std::unordered_map<std::string, StreamConfig>& out_configs,
                                             std::string& out_gridspec_file);

    // Resolve one stream variable's YAML node into a StreamConfig: field
    // resolution + defaults for file/mapalgo/cadence/tintalgo/data_model plus
    // the four driver-level AMIO tuning defaults. Extracted from the
    // ResolveStreamConfigsFromFile stream loop, which had grown to ~40 lines
    // of per-field if/else logic; the loop now keeps only the iteration and
    // first-match-wins bookkeeping. Behavior is unchanged.
    static StreamConfig BuildStreamConfig(const YAML::Node& stream, const std::string& model_name, const std::string& file_name,
                                          int amio_worker_threads, int amio_staging_buffer_count, int amio_staging_buffer_capacity_bytes,
                                          int amio_prefetch_depth);

    // Build the in-memory AMIO manifest YAML for one stream, byte-for-byte
    // identical to the manifest the legacy inline AdvanceTime code wrote to
    // disk (same keys, same order, same values), just returned as a string
    // instead of written to a file (Req 2.3, 2.5, 2.6).
    std::string BuildManifestContent(const StreamConfig& cfg, const std::string& data_model) const;

    // Compute the file/manifest-scoped identity key for a given StreamConfig.
    // The key concatenates exactly the StreamConfig fields that
    // BuildManifestContent consumes (input_file_path, data_model,
    // amio_worker_threads, amio_staging_buffer_count), so two configs share a
    // HandleKey iff they would produce a byte-identical AMIO manifest. Note
    // data_model here is the resolved pre-open value (identical across ranks).
    // Variables reading the same file/manifest share one open AMIO handle set
    // and one file record count even when their mapalgo differs.
    static std::string HandleKey(const StreamConfig& cfg);

    // Compute the mesh/weight-scoped stream identity key for a given
    // StreamConfig. It is HandleKey extended by mapalgo
    // (HandleKey(cfg) + "|" + cfg.mapalgo), and keys the regrid plan cache so
    // the plan splits only when mapalgo differs. Variables sharing the same
    // HandleKey and mapalgo produce the same key and share one regrid plan.
    static std::string StreamKey(const StreamConfig& cfg);

    // Invalidate (valid = false) the endpoint cache entry of every variable
    // whose computed StreamKey matches stream_key. Called when a regrid_plans_
    // entry is built or rebuilt for that stream, because cached endpoints depend
    // on the plan and must be rebuilt before reuse (Req 6.2). endpoint_caches_
    // is keyed by var_name while regrid_plans_ is keyed by stream_key, so the
    // var_name->stream_key mapping is derived from stream_configs_ + StreamKey.
    void InvalidateEndpointCachesForStream(const std::string& stream_key);

    // Tolerance for comparing two resolved bracket weights for equality.
    static constexpr double kBracketWeightTol = 1e-12;

    // Return true iff two resolved brackets have identical record indices and
    // weights within kBracketWeightTol. Compares i0, i1, and weight only; the
    // `valid` field is intentionally NOT compared (Req 3.5, 6.3).
    static bool bracket_equal(const RecordBracket& a, const RecordBracket& b);

    // Pure fused front-half gate DECISION helper (Req 6.1, 6.3, 6.4, 8.3).
    //
    // Given the two elementwise-reduced vectors produced by the fused
    // halo::allreduce pair over the packed 5-entry vector
    // [readiness, file_nx, file_ny, field_nlev, plan.identity]:
    //   mn[k] = MIN over ranks of entry k,
    //   mx[k] = MAX over ranks of entry k,
    // returns the accept/reject decision that is IDENTICAL to the conjunction
    // of the five legacy gates: accept iff `mn[0] == 1` (readiness, MIN
    // semantics — any not-ready rank contributes 0) AND `mn[k] == mx[k]` for
    // k in {1,2,3,4} (each metadata value agrees across ranks). On rejection it
    // sets failure_detail to the SAME message the legacy per-check gate would
    // report, under the SAME precedence (readiness, then file_nx, file_ny,
    // field_nlev, plan.identity). The readiness message is only written when
    // failure_detail is empty, preserving the local not-ready branch's
    // "source buffer or regrid metadata changed after AMIO validation" detail.
    //
    // Pure and free of any MPI/collective call so it is unit/property testable
    // off-MPI (Task 10.3, P4). The reduce itself is issued by the caller
    // (RegridToDestinationBuffer, Task 6.2); this helper only maps (mn, mx) to
    // (decision, failure_detail). Requires mn.size() == mx.size() == 5.
    static bool FusedGateDecision(const std::vector<int>& mn, const std::vector<int>& mx, std::string& failure_detail);

    // Test-only forwarding seams to the reworked HALO-backed collective helpers
    // (Task 10.2, P2/P3). The helpers collective_all_ready / collective_int_matches
    // live in the facade translation unit's anonymous namespace, so they have
    // internal linkage and cannot be reached from a test TU nor by a friend
    // declaration alone. These thin static forwarders are defined in the same
    // translation unit (after the helpers), so they can call them, and they are
    // reachable off the CollectiveGateTestAccess friend shim. Production code
    // never calls them and they change no existing signature or visibility, so
    // production behavior is unchanged; they merely expose the REAL production
    // helpers (not a copy) to the gate-equivalence property test.
    static bool CallCollectiveAllReady(MPI_Comm comm, bool local_ready, const std::string& context, std::string& failure_detail);
    static bool CallCollectiveIntMatches(MPI_Comm comm, int local_value, const std::string& name, std::string& failure_detail);

    // Return the retained AMIO handle set for the given Handle_Identity_Key
    // (input_file_path + data_model + worker_threads + staging_buffer_count),
    // opening it lazily on first touch and reusing it on every subsequent call.
    // Variables that read the same file/manifest share a single open
    // core+dataset handle even when their mapalgo differs (Req 2.1, 2.2, 3.1,
    // 3.2, 7.1, 11.2, 11.7).
    //
    // On first touch this builds the in-memory manifest via BuildManifestContent
    // and opens via the STRING-based AMIO entry points (amio_init_from_string /
    // amio_open_dataset_from_string) — no manifest file is written to disk and
    // no per-step MPI_Barrier is issued. Only the open is wrapped in the
    // MPI_COMM_SELF parent-communicator swap; the communicator is restored to
    // comm_c_ afterward. Candidate data models are tried in order: {cfg.data_model}
    // when cfg.data_model_explicit, else {"enhanced", "classic"} (Req 2.5, 2.6).
    //
    // On success it caches {core, dataset, active_data_model, manifest_content}
    // in amio_handles_ and returns the pointer. On failure of a candidate it
    // closes/finalizes any partial handle and tries the next; if all candidates
    // fail it caches nothing, sets failure_detail (with amio_strerror), and
    // returns nullptr (Req 8.1). This helper only opens; it does not read or
    // regrid.
    AmioHandleSet* GetOrOpenHandleSet(const std::string& handle_key, const StreamConfig& cfg, std::string& failure_detail);

    std::string config_file_;
    int nx_{0}, ny_{0}, nz_{0};
    std::vector<double> target_lons_;
    std::vector<double> target_lats_;
    int step_index_{0};
    MPI_Comm comm_c_{MPI_COMM_NULL};

    // Long-lived HALO wrapper over comm_c_. Built once when comm_c_ is set to a
    // valid, size>1 communicator (duplicating a non-predefined handle exactly as
    // cece_helm_graph.cpp does; wrapping WORLD/SELF directly). Used by the fused
    // front-half gate and the pre-gather readiness allreduce. Absent when MPI is
    // uninitialized, comm_c_ is MPI_COMM_NULL, or mpi_size <= 1 (Req 7.2).
    std::optional<halo::Communicator> halo_comm_;

    // Single source of truth for this rank's destination latitude band,
    // computed from ny_ and comm_c_ (recomputed wherever comm_c_ is
    // reassigned, alongside RefreshHaloCommunicator). Replaces the two inline
    // `band_start` j0/j1 computations that previously lived in
    // RegridToDestinationBuffer and the regrid-plan build within AdvanceTime;
    // band_.j0 / band_.j1 preserve those identical values (Req 1.1, 1.2, 1.5).
    BandDecomposition band_;

    // Cached regridding plans keyed by Stream_Identity_Key, now
    // = HandleKey + "|" + mapalgo. Variables that share a HandleKey but differ
    // in mapalgo get distinct plans; the plan splits only when mapalgo differs.
    // The interpolation weights depend only on source grid, target grid, and
    // mapping algorithm — not on the variable name.
    std::unordered_map<std::string, io::RegridPlan> regrid_plans_;

    // File record counts keyed by Handle_Identity_Key (input_file_path +
    // data_model + worker_threads + staging_buffer_count). The record-count
    // search runs at most once per file/manifest.
    std::unordered_map<std::string, int> file_nt_cache_;

    // Per-variable source shape (rank + per-timestep extents, CF time
    // stripped) keyed by Handle_Identity_Key + "|" + var_name. Populated once
    // per variable via amio_describe (metadata only, no payload staged) and
    // reused to size the band-scoped read bounding box in read_slab. Kept
    // separate from file_nt_cache_ because two variables in one file may have
    // different ranks/extents.
    std::unordered_map<std::string, amio_shape_t> var_shape_cache_;

    // Loop-invariant work moved out of AdvanceTime:
    //  - stream_configs_ : YAML resolved once at construction, keyed by model
    //                      variable name (Req 1).
    //  - amio_handles_   : AMIO core/dataset opened lazily once, retained
    //                      across timesteps, keyed by Handle_Identity_Key
    //                      (input_file_path + data_model + worker_threads +
    //                      staging_buffer_count). Variables reading the same
    //                      file/manifest share one open core+dataset handle
    //                      even when mapalgo differs (Req 2, 7, 11).
    //  - slice_caches_   : most-recent read+regrid result, reused when the
    //                      resolved time bracket is unchanged. Keyed by model
    //                      variable name (NOT Stream_Identity_Key) because
    //                      each variable carries different data even within
    //                      the same stream (Req 3, 5).
    std::unordered_map<std::string, StreamConfig> stream_configs_;
    std::unordered_map<std::string, AmioHandleSet> amio_handles_;
    std::unordered_map<std::string, SliceCacheEntry> slice_caches_;

    // Per-variable endpoint cache holding regrid(A)/regrid(B) for the current
    // Bracket_Indices. Keyed by model variable name (like slice_caches_) so
    // variables sharing one Regrid_Plan still get distinct endpoint pairs
    // (Req 1, 7). Cleared in TeardownHandles (Req 6.3).
    std::unordered_map<std::string, EndpointCacheEntry> endpoint_caches_;

    // HELM Orchestration and pipeline components
    std::unique_ptr<dagr::GraphOrchestrator> dagr_;
    std::unique_ptr<io::CeceIO> cece_io_;
    std::string gridspec_file_;

    // Test-only access to the private static StreamKey helper. Grants the
    // property test (tests/test_stream_key_properties.cpp) permission to
    // invoke StreamKey without changing its signature/visibility. Has no
    // effect on production behavior. (Task 6.1)
    friend struct StreamKeyTestAccess;

    // Test-only access to the private static HandleKey helper. Grants the
    // Amendment 1 property tests (tests/test_handle_key_properties.cpp,
    // tests/test_stream_key_extends_handle_key.cpp) permission to invoke
    // HandleKey without changing its signature/visibility. Has no effect on
    // production behavior. (Task 8.1)
    friend struct HandleKeyTestAccess;

    // Test-only access to the private static HandleKey and StreamKey helpers.
    // Grants the Amendment 1 Property 5 test
    // (tests/test_stream_key_extends_handle_key.cpp) permission to invoke both
    // private statics without changing their signature/visibility. A distinct
    // shim name (vs StreamKeyTestAccess / HandleKeyTestAccess) keeps that
    // translation unit collision-free. Has no effect on production behavior.
    // (Task 11.2)
    friend struct StreamKeyExtendsAccess;

    // Test-only access to the private static bracket_equal helper. Grants the
    // property test (tests/test_bracket_equal_properties.cpp) permission to
    // invoke bracket_equal without changing its signature/visibility. Has no
    // effect on production behavior. (Task 8.5)
    friend struct BracketEqualTestAccess;

    // Test-only access to the private static ResolveStreamConfigsFromFile
    // helper. Grants the Property 4 test
    // (tests/test_stream_config_resolution.cpp) permission to invoke the real
    // YAML->StreamConfig resolution against a chosen config path without
    // constructing a full orchestrator. Has no effect on production behavior.
    // (Task 8.2)
    friend struct StreamConfigTestAccess;

    // Test-only access to the private static bracket_equal helper for the
    // Property 6 slice-cache-equivalence test
    // (tests/test_slice_cache_equivalence.cpp). SliceCacheEntry / RecordBracket
    // are public structs in the cece namespace, so only bracket_equal needs
    // friend access. Has no effect on production behavior. (Task 9.2)
    friend struct SliceCacheTestAccess;

    // Test-only access to the private static bracket_equal helper for the
    // complementary "cache HIT skips read+regrid work" property test
    // (tests/test_cache_hit_skips_work.cpp). SliceCacheEntry / RecordBracket are
    // public structs in the cece namespace, so only bracket_equal needs friend
    // access. Has no effect on production behavior. (Task 13.3)
    friend struct CacheHitSkipTestAccess;

    // Test-only access to the private static bracket_equal helper and the
    // endpoint-cache decision logic for the temporal-endpoint-regrid-cache
    // property tests (endpoint-blend equivalence, work reduction, tier
    // precedence). EndpointCacheEntry / RecordBracket are public structs in the
    // cece namespace, so only bracket_equal and the endpoint decision helper
    // need friend access. Has no effect on production behavior. (Task 4.3)
    friend struct EndpointCacheTestAccess;

    // Test-only access to the private static bracket_equal helper for the
    // cross-rank reuse-decision MPI integration test
    // (tests/test_mpi_reuse_decision.cpp, Property 8). Grants that test
    // permission to invoke the REAL production bracket_equal so the per-rank
    // hit/miss decision is derived from production logic, not a copy.
    // RecordBracket is a public struct in the cece namespace, so only
    // bracket_equal needs friend access. Has no effect on production behavior.
    // (Task 12.3)
    friend struct CrossRankReuseTestAccess;

    // Test-only access to the pure fused front-half gate decision helper
    // (FusedGateDecision) and the reworked HALO-backed collective helpers, so
    // the fused-gate-equivalence property test
    // (tests/test_fused_gate_equivalence.cpp, Property 4) and the collective
    // gate-equivalence tests can drive the REAL production decision logic
    // off-MPI, without changing any production signature or visibility. Has no
    // effect on production behavior. (Task 6.1)
    friend struct CollectiveGateTestAccess;
};

}  // namespace cece

extern "C" {
void cece_driver_create(const char* yaml_path, int path_len, int nx, int ny, int nz, const double* lon_coords, int lon_len, const double* lat_coords,
                        int lat_len, int mpi_comm_f, void** driver_ptr_out, int* rc);

void cece_driver_advance_time(void* driver_ptr, const char* time_iso8601, int time_len, void* cece_core_data_ptr, int* rc);

// Destroys the driver. On return, *rc (when non-NULL) is 0 on clean shutdown
// or -1 if the final AMIO close/finalize teardown reported failures (the
// destruction itself always runs; see TeardownHandles).
void cece_driver_destroy(void* driver_ptr, int* rc);
}

#endif  // CECE_DRIVER_FACADE_HPP
