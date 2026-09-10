#include "cece/cece_driver_facade.hpp"

#include <amio/amio.h>
#include <yaml-cpp/yaml.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <axis/axis.hpp>
#include <cmath>
#include <dagr/logging.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tick/tick.hpp>
#include <vector>

#include "cece/cece_helm_graph.hpp"
#include "cece/cece_internal.hpp"
#include "cece/cece_logger.hpp"
#include "cece/cece_regridder_utils.hpp"
#include "cece/cece_standalone_writer.hpp"

namespace fs = std::filesystem;

extern "C" {
void cece_ingestor_set_field(void* data_ptr, const char* field_name, int name_len, const double* field_data, int n_lev, int n_elem, int* rc);
void amio_set_parent_communicator(MPI_Fint comm);
}

namespace cece {

namespace {

/**
 * @brief Simulation datetime fields derived from an ISO-8601 timestamp.
 *
 * Used by the per-stream temporal-cadence mechanism to map the current
 * simulation time onto a record index within an input file.
 */
struct SimDateTime {
    int year = 0;
    int month = 0;        ///< 1-12
    int day = 0;          ///< 1-31
    int hour = 0;         ///< 0-23
    int day_of_week = 0;  ///< 0=Sunday .. 6=Saturday
    bool valid = false;
};

/**
 * @brief Parse an ISO-8601 timestamp ("YYYY-MM-DDThh:mm:ss") into calendar fields.
 *
 * Parsing and calendar arithmetic use the HELM TICK library (tick::parse_iso8601
 * and tick::Gregorian_Calendar) rather than std::chrono, keeping time handling
 * consistent with the rest of CECE. The day-of-week is derived from TICK's
 * proleptic-Gregorian day count (TICK's epoch 2026-01-01 is a Thursday), so it
 * is correct for any date.
 */
SimDateTime parse_sim_datetime(const std::string& iso8601) {
    SimDateTime dt;
    try {
        const tick::Date_Time tdt = tick::parse_iso8601(iso8601);
        dt.year = tdt.year;
        dt.month = tdt.month;
        dt.day = tdt.day;
        dt.hour = tdt.hour;

        // Whole days since TICK's epoch (2026-01-01T00:00:00), floored so dates
        // before the epoch map correctly. 2026-01-01 is a Thursday, i.e. index 4
        // in a 0=Sunday..6=Saturday week; offset by that to anchor the cycle.
        const std::int64_t nanos = tick::Gregorian_Calendar::to_time_point(tdt).nanos();
        std::int64_t days = nanos / tick::nanos_per_day;
        if (nanos < 0 && nanos % tick::nanos_per_day != 0) --days;  // floor toward -inf
        dt.day_of_week = static_cast<int>(((days + 4) % 7 + 7) % 7);
        dt.valid = true;
    } catch (const std::exception&) {
        // Malformed timestamp: use explicit default values so callers fall back
        // to legacy step-index cycling.
        dt = SimDateTime{};
    }
    return dt;
}

// RecordBracket is defined in cece/cece_driver_facade.hpp so it can be used as
// SliceCacheEntry::last_bracket; the temporal-cadence helpers below use that
// shared definition (resolved as cece::RecordBracket).

/**
 * @brief Map a simulation datetime onto a record bracket for a given cadence.
 *
 * @param cadence  One of "hourly", "weekly", "monthly" (case-insensitive).
 *                 Any other value (including empty) returns an invalid bracket,
 *                 signalling the caller to fall back to legacy step-index cycling.
 * @param tintalgo Time-interpolation algorithm: "linear" enables interpolation
 *                 for the (continuous) monthly cadence; anything else -> nearest.
 * @param dt       Parsed simulation datetime.
 * @param file_nt  Number of records available in the file (for clamping).
 *
 * Hourly and weekly cadences select discrete profile records (hour-of-day,
 * day-of-week) and are always nearest-neighbour: interpolating between, say,
 * two day-type weights is not physically meaningful. Only the monthly cadence
 * honours @c tintalgo, using the mid-month convention so that, e.g., Jan 1 is
 * interpolated between the December and January climatological records.
 */
RecordBracket cadence_record_bracket(const std::string& cadence, const std::string& tintalgo, const SimDateTime& dt, int file_nt) {
    RecordBracket br;
    if (cadence.empty() || !dt.valid) return br;

    std::string c = cadence;
    std::transform(c.begin(), c.end(), c.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    std::string algo = tintalgo;
    std::transform(algo.begin(), algo.end(), algo.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    const bool linear = (algo == "linear");

    auto clamp_idx = [&](int idx) {
        if (file_nt > 0 && idx >= file_nt) idx = file_nt - 1;
        if (idx < 0) idx = 0;
        return idx;
    };

    if (c == "hourly") {
        br.i0 = br.i1 = clamp_idx(dt.hour);  // 0-23, discrete of-day profile
        br.valid = true;
    } else if (c == "weekly") {
        br.i0 = br.i1 = clamp_idx(dt.day_of_week);  // 0=Sunday..6=Saturday, discrete day-type
        br.valid = true;
    } else if (c == "monthly") {
        const int m = dt.month - 1;  // 0-11
        if (!linear) {
            br.i0 = br.i1 = clamp_idx(m);
            br.valid = true;
            return br;
        }
        // Mid-month convention: each monthly record is valid at the midpoint of
        // its month. Interpolate between the two records whose anchors bracket
        // the current instant, cycling across the Dec<->Jan boundary.
        const int dim = tick::Gregorian_Calendar::days_in_month(dt.year, dt.month);
        const double frac = (static_cast<double>(dt.day - 1) + dt.hour / 24.0) / static_cast<double>(dim);  // [0,1)
        const int nrec = (file_nt > 0) ? file_nt : 12;
        if (frac >= 0.5) {
            br.i0 = m % nrec;
            br.i1 = (m + 1) % nrec;
            br.weight = frac - 0.5;  // 0 at mid-month, ->0.5 approaching next anchor
        } else {
            br.i0 = (m - 1 + nrec) % nrec;
            br.i1 = m % nrec;
            br.weight = frac + 0.5;  // ->1 at mid-month, 0.5 just after previous anchor
        }
        br.valid = true;
    }
    return br;
}

// Reimplemented on halo::allreduce<int> (Decision C): the size>1 branch reduces
// the single-element 0/1 readiness flag with MPI_MIN through the orchestrator's
// long-lived halo_comm_ wrapper (passed in as halo_comm) rather than a
// hand-rolled MPI_Allreduce. The signature adds the halo::Communicator* so this
// free function can reach the wrapper the orchestrator already owns; every call
// site passes `halo_comm_ ? &*halo_comm_ : nullptr`. The short-circuits
// (uninitialized MPI / MPI_COMM_NULL / mpi_size <= 1 return the local readiness
// value with the same context-based failure_detail discipline) and the
// not-ready message are preserved verbatim. HALO's throwing error policy
// replaces the former rc != MPI_SUCCESS branch; the try/catch maps any throw to
// a failure_detail and returns false (Req 4.1-4.4).
bool collective_all_ready(halo::Communicator* halo_comm, MPI_Comm comm, bool local_ready, const std::string& context, std::string& failure_detail) {
    int mpi_initialized = 0;
    MPI_Initialized(&mpi_initialized);
    if (!mpi_initialized || comm == MPI_COMM_NULL) {
        if (!local_ready && failure_detail.empty()) failure_detail = context;
        return local_ready;
    }

    int mpi_size = 1;
    MPI_Comm_size(comm, &mpi_size);
    if (mpi_size <= 1) {
        if (!local_ready && failure_detail.empty()) failure_detail = context;
        return local_ready;
    }

    // size > 1: reduce the 0/1 readiness flag with MPI_MIN via halo::allreduce.
    try {
        const std::vector<int> out = halo::allreduce<int>(*halo_comm, std::vector<int>{local_ready ? 1 : 0}, MPI_MIN);
        if (out[0] != 1) {
            if (failure_detail.empty()) failure_detail = context + " failed on one or more ranks";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        failure_detail = context + " failed: " + e.what();
        return false;
    }
}

// Reimplemented on halo::allreduce<int> (Req 5). Reduces the single-element
// local_value with MPI_MIN and then MPI_MAX through the orchestrator's
// long-lived halo_comm_ wrapper (passed in as halo_comm) rather than two
// hand-rolled MPI_Allreduce calls. The signature adds the halo::Communicator*
// so this free function can reach the wrapper the orchestrator already owns;
// every call site passes `halo_comm_ ? &*halo_comm_ : nullptr`. The
// short-circuits (uninitialized MPI / MPI_COMM_NULL / mpi_size <= 1 return true
// without any collective) and the mismatch message are preserved verbatim. The
// two-reduce (MIN then MAX) op sequence is kept. HALO's throwing error policy
// replaces the former rc != MPI_SUCCESS branch; the try/catch maps any throw to
// a failure_detail and returns false (Req 5.1-5.4).
bool collective_int_matches(halo::Communicator* halo_comm, MPI_Comm comm, int local_value, const std::string& name, std::string& failure_detail) {
    int mpi_initialized = 0;
    MPI_Initialized(&mpi_initialized);
    if (!mpi_initialized || comm == MPI_COMM_NULL) return true;

    int mpi_size = 1;
    MPI_Comm_size(comm, &mpi_size);
    if (mpi_size <= 1) return true;

    // size > 1: reduce local_value with MPI_MIN then MPI_MAX via halo::allreduce,
    // preserving the two-reduce op sequence; flag a mismatch when min != max.
    try {
        const std::vector<int> mins = halo::allreduce<int>(*halo_comm, std::vector<int>{local_value}, MPI_MIN);
        const std::vector<int> maxs = halo::allreduce<int>(*halo_comm, std::vector<int>{local_value}, MPI_MAX);
        const int minimum = mins[0];
        const int maximum = maxs[0];
        if (minimum != maximum) {
            failure_detail = name + " differs across ranks (minimum " + std::to_string(minimum) + ", maximum " + std::to_string(maximum) + ")";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        failure_detail = "collective comparison of " + name + " failed: " + e.what();
        return false;
    }
}

}  // namespace

bool CeceDriverOrchestrator::CallCollectiveAllReady(MPI_Comm comm, bool local_ready, const std::string& context, std::string& failure_detail) {
    // Test-only forwarder (Task 10.2, P2). Builds a short-lived halo::Communicator
    // wrapping `comm` exactly as src/driver/cece_helm_graph.cpp does (duplicating a
    // non-predefined handle so the caller's handle is never freed; wrapping
    // WORLD/SELF directly) only on the size>1 branch, then delegates to the REAL
    // anonymous-namespace collective_all_ready helper. The short-circuit branches
    // (uninitialized MPI / MPI_COMM_NULL / mpi_size <= 1) never touch the wrapper
    // and are handled inside the helper. This changes no production signature and
    // is never called by production code.
    int mpi_initialized = 0;
    MPI_Initialized(&mpi_initialized);
    int mpi_size = 1;
    if (mpi_initialized && comm != MPI_COMM_NULL) MPI_Comm_size(comm, &mpi_size);
    if (!mpi_initialized || comm == MPI_COMM_NULL || mpi_size <= 1) {
        return collective_all_ready(nullptr, comm, local_ready, context, failure_detail);
    }
    MPI_Comm comm_to_wrap = comm;
    if (comm != MPI_COMM_WORLD && comm != MPI_COMM_SELF) {
        MPI_Comm_dup(comm, &comm_to_wrap);
    }
    halo::Communicator wrapper(comm_to_wrap);
    return collective_all_ready(&wrapper, comm, local_ready, context, failure_detail);
}

bool CeceDriverOrchestrator::CallCollectiveIntMatches(MPI_Comm comm, int local_value, const std::string& name, std::string& failure_detail) {
    // Test-only forwarder (Task 10.2, P3). Same wrapping discipline as
    // CallCollectiveAllReady; delegates to the REAL anonymous-namespace
    // collective_int_matches helper. No production signature change; never called
    // by production code.
    int mpi_initialized = 0;
    MPI_Initialized(&mpi_initialized);
    int mpi_size = 1;
    if (mpi_initialized && comm != MPI_COMM_NULL) MPI_Comm_size(comm, &mpi_size);
    if (!mpi_initialized || comm == MPI_COMM_NULL || mpi_size <= 1) {
        return collective_int_matches(nullptr, comm, local_value, name, failure_detail);
    }
    MPI_Comm comm_to_wrap = comm;
    if (comm != MPI_COMM_WORLD && comm != MPI_COMM_SELF) {
        MPI_Comm_dup(comm, &comm_to_wrap);
    }
    halo::Communicator wrapper(comm_to_wrap);
    return collective_int_matches(&wrapper, comm, local_value, name, failure_detail);
}

void CeceDriverOrchestrator::ResolveStreamConfigs() {
    // Thin wrapper: run the pure YAML->StreamConfig resolution against
    // config_file_ and store the results into this orchestrator's members.
    // The actual resolution lives in the static ResolveStreamConfigsFromFile so
    // it can be exercised in isolation by tests (Property 4) via the SAME code
    // path without constructing a full orchestrator (which requires MPI/DAGR/
    // CeceIO setup). Production behavior is unchanged.
    ResolveStreamConfigsFromFile(config_file_, stream_configs_, gridspec_file_);
}

void CeceDriverOrchestrator::ResolveStreamConfigsFromFile(const std::string& config_file, std::unordered_map<std::string, StreamConfig>& out_configs,
                                                          std::string& out_gridspec_file) {
    // Parse config_file exactly once. On a YAML error keep the orchestrator
    // robust (out_gridspec_file = "", out_configs left empty) so the existing
    // collective gates in AdvanceTime surface any downstream problem, matching
    // the legacy inline parse which simply found no matching variable.
    YAML::Node config;
    try {
        config = YAML::LoadFile(config_file);
    } catch (const YAML::Exception& e) {
        out_gridspec_file = "";
        return;
    }

    // Driver-level values shared by every StreamConfig. Preserve the existing
    // constructor validation: a < 1 value is rejected with std::invalid_argument
    // and the same message.
    int amio_worker_threads = 1;
    int amio_staging_buffer_count = 8;
    int amio_staging_buffer_capacity_bytes = 33554432;  // 32 MiB per buffer
    int amio_prefetch_depth = 2;
    if (config["driver"]) {
        if (config["driver"]["gridspec_file"]) {
            out_gridspec_file = config["driver"]["gridspec_file"].as<std::string>();
        }
        if (config["driver"]["amio_worker_threads"]) {
            amio_worker_threads = config["driver"]["amio_worker_threads"].as<int>();
            if (amio_worker_threads < 1) {
                throw std::invalid_argument("driver.amio_worker_threads must be >= 1; got " + std::to_string(amio_worker_threads) + ".");
            }
        }
        if (config["driver"]["amio_staging_buffer_count"]) {
            amio_staging_buffer_count = config["driver"]["amio_staging_buffer_count"].as<int>();
            if (amio_staging_buffer_count < 1) {
                throw std::invalid_argument("driver.amio_staging_buffer_count must be >= 1; got " + std::to_string(amio_staging_buffer_count) + ".");
            }
        }
        if (config["driver"]["amio_staging_buffer_capacity_bytes"]) {
            amio_staging_buffer_capacity_bytes = config["driver"]["amio_staging_buffer_capacity_bytes"].as<int>();
            if (amio_staging_buffer_capacity_bytes < 1) {
                throw std::invalid_argument("driver.amio_staging_buffer_capacity_bytes must be >= 1; got " +
                                            std::to_string(amio_staging_buffer_capacity_bytes) + ".");
            }
        }
        if (config["driver"]["amio_prefetch_depth"]) {
            amio_prefetch_depth = config["driver"]["amio_prefetch_depth"].as<int>();
            if (amio_prefetch_depth < 1) {
                throw std::invalid_argument("driver.amio_prefetch_depth must be >= 1; got " + std::to_string(amio_prefetch_depth) + ".");
            }
        }
    }

    if (!config["cece_data"] || !config["cece_data"]["streams"]) {
        return;
    }

    // Walk every stream and populate a StreamConfig for each model variable,
    // keyed by model name. Field resolution + defaults mirror the legacy inline
    // AdvanceTime parse exactly.
    for (const auto& stream : config["cece_data"]["streams"]) {
        for (const auto& var : stream["variables"]) {
            std::string model_name;
            std::string file_name;
            if (var.IsScalar()) {
                model_name = var.as<std::string>();
                file_name = model_name;
            } else if (var.IsMap() && var["model"]) {
                model_name = var["model"].as<std::string>();
                file_name = var["file"] ? var["file"].as<std::string>() : model_name;
            } else {
                continue;
            }

            StreamConfig cfg;
            // Missing file path is recorded as empty (not thrown); the existing
            // collective gate in AdvanceTime surfaces it later (Req 1.4).
            if (stream["file"]) {
                cfg.input_file_path = stream["file"].as<std::string>();
            }
            cfg.input_var_name = file_name;
            if (stream["mapalgo"]) {
                cfg.mapalgo = stream["mapalgo"].as<std::string>();
            }
            if (stream["cadence"]) {
                cfg.cadence = stream["cadence"].as<std::string>();
            }
            if (stream["tintalgo"]) {
                cfg.tintalgo = stream["tintalgo"].as<std::string>();
            }
            if (stream["data_model"]) {
                std::string requested_model = stream["data_model"].as<std::string>();
                std::transform(requested_model.begin(), requested_model.end(), requested_model.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (requested_model == "classic" || requested_model == "enhanced") {
                    cfg.data_model = requested_model;
                    cfg.data_model_explicit = true;
                } else if (requested_model == "auto") {
                    cfg.data_model = "enhanced";
                    cfg.data_model_explicit = false;
                } else {
                    CECE_LOG_WARNING("[DRIVER] Invalid stream data_model='" + requested_model + "' for stream variable '" + model_name +
                                     "'; using default auto behavior (enhanced then classic fallback).");
                    cfg.data_model = "enhanced";
                    cfg.data_model_explicit = false;
                }
            }
            cfg.amio_worker_threads = amio_worker_threads;
            cfg.amio_staging_buffer_count = amio_staging_buffer_count;
            cfg.amio_staging_buffer_capacity_bytes = amio_staging_buffer_capacity_bytes;
            cfg.amio_prefetch_depth = amio_prefetch_depth;

            // Match the legacy inline parse's first-match-wins behavior: it
            // stopped at the first stream/variable matching the model name.
            out_configs.emplace(model_name, std::move(cfg));
        }
    }
}

std::string CeceDriverOrchestrator::BuildManifestContent(const StreamConfig& cfg, const std::string& data_model) const {
    // Mirror the exact keys, order, values, newlines, and indentation the
    // legacy inline AdvanceTime writer produced (the `m_file << "backend: ..."`
    // block), just to a string instead of a file, so the resulting manifest is
    // byte-for-byte identical (Req 2.3, 2.5, 2.6).
    std::ostringstream m_content;
    // The staging pool auto-grows its slot count on demand (AMIO lazy
    // allocation + grow-to-required + grow-on-exhaustion), so buffer_count is
    // a provisioning hint, not a hard limit a run can crash on.  We emit an
    // explicit ceiling = 8x the requested count (clamped to [count, 4096]) so
    // a genuine view-leak / stuck-IO is still bounded: past the ceiling the
    // pool reverts to the staging-timeout tripwire (AMIO_ERR_STAGING_BACKPRESSURE)
    // instead of growing without limit.  The ceiling is a pure function of
    // amio_staging_buffer_count (already part of HandleKey), so the
    // key <-> manifest-identity invariant is preserved.
    const long long ceiling_raw = static_cast<long long>(cfg.amio_staging_buffer_count) * 8;
    const int staging_ceiling = static_cast<int>(std::min<long long>(4096, std::max<long long>(cfg.amio_staging_buffer_count, ceiling_raw)));
    m_content << "backend: netcdf4\n"
              << "path: " << cfg.input_file_path << "\n"
              << "data_model: " << data_model << "\n"
              << "staging_pool:\n"
              << "  buffer_count: " << cfg.amio_staging_buffer_count << "\n"
              << "  buffer_capacity_bytes: " << cfg.amio_staging_buffer_capacity_bytes << "\n"
              << "  max_buffer_count: " << staging_ceiling << "\n"
              << "worker_pool:\n"
              << "  threads: " << cfg.amio_worker_threads << "\n"
              << "prefetch:\n"
              << "  depth: " << cfg.amio_prefetch_depth << "\n"
              << "  read_timeout_s: 120\n"
              << "staging_timeout_ms: 30000\n";
    return m_content.str();
}

std::string CeceDriverOrchestrator::StreamKey(const StreamConfig& cfg) {
    // Stream identity key = HandleKey extended by mapalgo. It keys the regrid
    // plan cache (regrid_plans_): variables sharing a HandleKey but requesting
    // a different mapalgo get distinct StreamKeys and therefore distinct plans,
    // while everything else is shared at the coarser HandleKey level (Req 11.5).
    return HandleKey(cfg) + "|" + cfg.mapalgo;
}

void CeceDriverOrchestrator::InvalidateEndpointCachesForStream(const std::string& stream_key) {
    // Plan (re)build invalidation (Req 6.2): endpoint entries are keyed by
    // var_name while regrid_plans_ is keyed by stream_key, so when the plan for
    // stream_key is (re)built we must invalidate the endpoint entry of every
    // variable whose computed StreamKey matches. Only touch entries that
    // already exist so we do not fabricate empty (default-invalid) entries.
    for (const auto& [var_name, cfg] : stream_configs_) {
        if (StreamKey(cfg) != stream_key) {
            continue;
        }
        auto it = endpoint_caches_.find(var_name);
        if (it != endpoint_caches_.end()) {
            it->second.valid = false;
        }
    }
}

std::string CeceDriverOrchestrator::HandleKey(const StreamConfig& cfg) {
    // File/manifest-scoped identity key: it concatenates exactly the
    // StreamConfig fields that BuildManifestContent consumes
    // (input_file_path, data_model, amio_worker_threads,
    // amio_staging_buffer_count, amio_staging_buffer_capacity_bytes,
    // amio_prefetch_depth), so two configs share a HandleKey iff they
    // would produce a byte-identical AMIO manifest. data_model here is the
    // resolved pre-open value (identical across ranks). The "|" separator
    // cannot appear in NetCDF file paths, so the concatenation is unambiguous
    // (Req 11.1). Variables reading the same file/manifest share one open AMIO
    // handle set and one file record count even when mapalgo differs.
    return cfg.input_file_path + "|" + cfg.data_model + "|" + std::to_string(cfg.amio_worker_threads) + "|" +
           std::to_string(cfg.amio_staging_buffer_count) + "|" + std::to_string(cfg.amio_staging_buffer_capacity_bytes) + "|" +
           std::to_string(cfg.amio_prefetch_depth);
}

bool CeceDriverOrchestrator::bracket_equal(const RecordBracket& a, const RecordBracket& b) {
    // Two resolved brackets are equal when they select the same record indices
    // and blend weight (within tolerance). The `valid` field is intentionally
    // NOT compared: bracket_equal answers "does b select the same slice as a"
    // for two already-resolved brackets (Req 3.5, 6.3).
    return a.i0 == b.i0 && a.i1 == b.i1 && std::fabs(a.weight - b.weight) <= kBracketWeightTol;
}

bool CeceDriverOrchestrator::FusedGateDecision(const std::vector<int>& mn, const std::vector<int>& mx, std::string& failure_detail) {
    // Pure decision over the elementwise MIN/MAX reductions of the packed
    // 5-entry front-half gate vector [readiness, file_nx, file_ny, field_nlev,
    // plan.identity]. Because elementwise allreduce is independent per position,
    // mn[k]/mx[k] equal exactly what the standalone legacy gate for value k
    // would have computed: mn[0] == the legacy collective_all_ready MIN over
    // readiness, and (mn[k], mx[k]) == the legacy collective_int_matches
    // (MIN, MAX) for k in {1..4}. This maps those reductions to the SAME
    // accept/reject decision and the SAME failure_detail under the SAME
    // precedence the five separate gates used (Req 6.1, 6.3, 6.4, 8.3). It
    // issues no collective itself, so it is unit/property testable off-MPI.

    // Index 0 — readiness (MIN semantics): ready iff every rank contributed 1.
    // Message written only when failure_detail is still empty, preserving the
    // local not-ready branch's "source buffer or regrid metadata changed after
    // AMIO validation" detail, exactly as the legacy collective_all_ready did
    // with context "source and regrid metadata readiness".
    if (mn[0] != 1) {
        if (failure_detail.empty()) failure_detail = "source and regrid metadata readiness failed on one or more ranks";
        return false;
    }

    // Indices 1..4 — metadata match values: entry k agrees iff mn[k] == mx[k].
    // Precedence and messages match the legacy collective_int_matches order:
    // file_nx, then file_ny, then field_nlev, then plan.identity.
    if (mn[1] != mx[1]) {
        failure_detail = "source longitude count differs across ranks (minimum " + std::to_string(mn[1]) + ", maximum " + std::to_string(mx[1]) + ")";
        return false;
    }
    if (mn[2] != mx[2]) {
        failure_detail = "source latitude count differs across ranks (minimum " + std::to_string(mn[2]) + ", maximum " + std::to_string(mx[2]) + ")";
        return false;
    }
    if (mn[3] != mx[3]) {
        failure_detail = "source level count differs across ranks (minimum " + std::to_string(mn[3]) + ", maximum " + std::to_string(mx[3]) + ")";
        return false;
    }
    if (mn[4] != mx[4]) {
        failure_detail =
            "regrid-plan identity mode differs across ranks (minimum " + std::to_string(mn[4]) + ", maximum " + std::to_string(mx[4]) + ")";
        return false;
    }

    return true;
}

AmioHandleSet* CeceDriverOrchestrator::GetOrOpenHandleSet(const std::string& handle_key, const StreamConfig& cfg, std::string& failure_detail) {
    // Lazy-open-once: if the handle set already exists for this
    // Handle_Identity_Key, reuse it without any re-open. Variables that read the
    // same file/manifest share this single open handle set even when their
    // mapalgo differs (Req 2.2, 3.1, 3.2, 7.1, 11.2, 11.7).
    auto existing = amio_handles_.find(handle_key);
    if (existing != amio_handles_.end()) {
        return &existing->second;
    }

    int mpi_initialized = 0;
    MPI_Initialized(&mpi_initialized);

    // Candidate data models, mirroring the legacy AdvanceTime fallback ordering
    // (Req 2.5, 2.6).
    std::vector<std::string> data_models_to_try;
    if (cfg.data_model_explicit) {
        data_models_to_try.push_back(cfg.data_model);
    } else {
        data_models_to_try.push_back("enhanced");
        data_models_to_try.push_back("classic");
    }

    for (const auto& candidate_model : data_models_to_try) {
        // Build the manifest in memory once per candidate; no file is written
        // to disk (Req 9.2).
        const std::string manifest_content = BuildManifestContent(cfg, candidate_model);

        amio_core_handle read_core = nullptr;
        amio_dataset_handle read_dataset = nullptr;

        // Force serial I/O fallback for reading offline datasets to prevent MPI
        // multithreading deadlocks. Only the open is wrapped in the swap.
        if (mpi_initialized) {
            amio_set_parent_communicator(MPI_Comm_c2f(MPI_COMM_SELF));
        }

        amio_status_t amio_rc = amio_init_from_string(manifest_content.c_str(), "yaml", &read_core);
        if (amio_rc != AMIO_OK) {
            failure_detail = std::string("amio_init_from_string failed for handle '") + handle_key + "': rc=" + std::to_string(amio_rc) + " (" +
                             amio_strerror(amio_rc) + ")";
        } else {
            amio_rc = amio_open_dataset_from_string(read_core, manifest_content.c_str(), "yaml", AMIO_MODE_READ, &read_dataset);
            if (amio_rc != AMIO_OK) {
                failure_detail = std::string("amio_open_dataset_from_string failed for '") + cfg.input_file_path +
                                 "': rc=" + std::to_string(amio_rc) + " (" + amio_strerror(amio_rc) + ")";
            }
        }

        // Restore parent communicator for downstream operations (match the
        // legacy guards exactly).
        if (mpi_initialized && comm_c_ != MPI_COMM_NULL) {
            amio_set_parent_communicator(MPI_Comm_c2f(comm_c_));
        }

        if (amio_rc == AMIO_OK && read_core != nullptr && read_dataset != nullptr) {
            // Emit the same auto-fallback INFO log the legacy code emitted when
            // a non-explicit stream fell back to a non-"enhanced" model.
            if (!cfg.data_model_explicit && candidate_model != "enhanced") {
                CECE_LOG_INFO("[DRIVER] AMIO read manifest auto-fell back to data_model='" + candidate_model + "' for " + cfg.input_file_path);
            }

            failure_detail.clear();
            AmioHandleSet set;
            set.core = read_core;
            set.dataset = read_dataset;
            set.active_data_model = candidate_model;
            set.manifest_content = manifest_content;
            auto inserted = amio_handles_.emplace(handle_key, std::move(set));
            return &inserted.first->second;
        }

        CECE_LOG_DEBUG("[DRIVER] AMIO open attempt failed (data_model='" + candidate_model + "') with rc = " + std::to_string(amio_rc) + " (" +
                       amio_strerror(amio_rc) + ")");

        // Close/finalize any partially-opened handle for this attempt before
        // trying the next candidate. Nothing is cached on failure (Req 8.1).
        if (read_dataset) {
            amio_close(read_dataset);
            read_dataset = nullptr;
        }
        if (read_core) {
            amio_finalize(read_core);
            read_core = nullptr;
        }
    }

    // All candidates failed: leave failure_detail set, cache nothing.
    CECE_LOG_DEBUG("[DRIVER] amio open failed for " + cfg.input_file_path + " after trying all candidate data models");
    return nullptr;
}

CeceDriverOrchestrator::CeceDriverOrchestrator(const std::string& config_file, int nx, int ny, int nz, const double* lon_coords, int lon_len,
                                               const double* lat_coords, int lat_len, MPI_Comm comm_c)
    : config_file_(config_file),
      nx_(nx),
      ny_(ny),
      nz_(nz),
      target_lons_(lon_coords, lon_coords + lon_len),
      target_lats_(lat_coords, lat_coords + lat_len),
      comm_c_(comm_c) {
    // Build the long-lived HALO wrapper over comm_c_ now that it is set, so the
    // fused gate and cached gather plans have a valid Communicator to reference
    // (Req 7.1-7.3). Absent for single-rank / uninitialized MPI / MPI_COMM_NULL.
    RefreshHaloCommunicator();

    // Compute this rank's destination latitude band once from ny_ and comm_c_.
    // This is the single source of truth for the band geometry, replacing the
    // inline band_start j0/j1 computations. Recomputed wherever comm_c_ is
    // reassigned (Req 1.1, 1.2, 1.5).
    band_ = BandDecomposition::compute(ny_, comm_c_);

    // Parse the YAML once and resolve the StreamConfig for every stream
    // variable, plus driver-level values and gridspec_file_ (Req 1.1). This
    // preserves the previous constructor's driver-block validation (a < 1
    // amio_worker_threads / amio_staging_buffer_count throws) and its
    // YAML::Exception robustness (gridspec_file_ = "" on a bad/missing file).
    ResolveStreamConfigs();

    cece_io_ = std::make_unique<io::CeceIO>();
    cece_io_->Initialize(config_file_, nx_, ny_, nz_);
    CompileHelmGraph(config_file_, dagr_, *cece_io_, comm_c_);

    // Route DAGR's diagnostics through its shared LOGS logger with the same
    // MPI communicator CECE uses, and quiet non-root ranks (they still emit
    // FATAL). Without this, DAGR's logger is unconfigured and every rank prints
    // identical "GraphOrchestrator: shutdown initiated" lines with a [RANK:----]
    // sentinel stamp.
    {
        int mpi_initialized = 0;
        MPI_Initialized(&mpi_initialized);
        int rank = 0;
        if (mpi_initialized && comm_c_ != MPI_COMM_NULL) {
            MPI_Comm_rank(comm_c_, &rank);
        }
        dagr::configure_logging(comm_c_ != MPI_COMM_NULL ? comm_c_ : MPI_COMM_WORLD, rank == 0 ? dagr::Log_Level::info : dagr::Log_Level::error);
    }
}

void CeceDriverOrchestrator::RefreshHaloCommunicator() {
    // Drop any existing wrapper first so nothing references a stale handle while
    // we rebuild. The wrapper is used by the fused front-half gate and the
    // pre-gather readiness allreduce.
    halo_comm_.reset();

    // Preserve the existing single-rank short-circuit: build a HALO Communicator
    // ONLY on the distributed branch (MPI initialized, valid comm, size > 1).
    // This mirrors RegridToBandBuffer's `distributed` predicate and
    // guarantees we never wrap MPI_COMM_NULL or an uninitialized environment
    // (Req 7.2). halo::Environment::initialize() is assumed already run in
    // main.cpp after MPI init, so we do not re-init here.
    int mpi_initialized = 0;
    MPI_Initialized(&mpi_initialized);
    if (!mpi_initialized || comm_c_ == MPI_COMM_NULL) {
        return;
    }

    int mpi_size = 1;
    MPI_Comm_size(comm_c_, &mpi_size);
    if (mpi_size <= 1) {
        return;
    }

    // Wrap comm_c_ in a halo::Communicator following cece_helm_graph.cpp's
    // convention: duplicate a non-predefined handle (so the wrapper's RAII
    // MPI_Comm_free does not destroy the caller's communicator), and wrap the
    // predefined WORLD/SELF handles directly (the wrapper never frees those).
    MPI_Comm comm_to_wrap = comm_c_;
    if (comm_c_ != MPI_COMM_WORLD && comm_c_ != MPI_COMM_SELF) {
        MPI_Comm_dup(comm_c_, &comm_to_wrap);
    }
    halo_comm_.emplace(comm_to_wrap);
}

void CeceDriverOrchestrator::TeardownHandles() {
    // Release every retained AMIO handle set. Close the dataset first, then
    // finalize the core, each wrapped in its own best-effort try/catch so one
    // failing handle does not prevent the rest from tearing down (Req 7.2, 7.4).
    // amio_handles_ is now keyed by Handle_Identity_Key, so this generic loop
    // closes each shared handle set exactly once regardless of how many
    // variables shared it (Req 3.6, 7.4).
    for (auto& entry : amio_handles_) {
        AmioHandleSet& set = entry.second;
        if (set.dataset != nullptr) {
            try {
                amio_close(set.dataset);
            } catch (const std::exception& e) {
                CECE_LOG_DEBUG("[DRIVER] amio_close threw during teardown for '" + entry.first + "': " + e.what());
            } catch (...) {
                CECE_LOG_DEBUG("[DRIVER] amio_close threw an unknown exception during teardown for '" + entry.first + "'");
            }
            set.dataset = nullptr;
        }
        if (set.core != nullptr) {
            try {
                amio_finalize(set.core);
            } catch (const std::exception& e) {
                CECE_LOG_DEBUG("[DRIVER] amio_finalize threw during teardown for '" + entry.first + "': " + e.what());
            } catch (...) {
                CECE_LOG_DEBUG("[DRIVER] amio_finalize threw an unknown exception during teardown for '" + entry.first + "'");
            }
            set.core = nullptr;
        }
    }

    // No manifest files to delete: manifests are in-memory strings, never
    // written to disk (Req 7.3). Drop the loop-invariant caches (Req 7.5).
    amio_handles_.clear();
    stream_configs_.clear();
    slice_caches_.clear();
    endpoint_caches_.clear();
}

CeceDriverOrchestrator::~CeceDriverOrchestrator() {
    // Release retained AMIO handle sets first, before tearing down the pipeline
    // (Req 7.2-7.5).
    TeardownHandles();

    // Cleanly drain any in-flight pipeline tasks and release hijacked ranks
    // before destroying the graph. Without this, tearing down the DAGR
    // GraphOrchestrator while a task is still in flight races with the
    // Event_Loop worker(s) and can segfault at teardown. shutdown() is
    // idempotent and safe to call here.
    if (dagr_) {
        dagr_->shutdown();
    }
    dagr_.reset();
    cece_io_.reset();
}

bool CeceDriverOrchestrator::RegridToBandBuffer(const std::string& var_name, const io::RegridPlan& plan, const std::vector<double>& source_record,
                                                int file_nx, int file_ny, int field_nlev, std::vector<double>& out_buffer,
                                                std::string& failure_detail) {
    (void)var_name;
    const std::vector<double>& source = source_record;
    int mpi_initialized = 0;
    MPI_Initialized(&mpi_initialized);
    int mpi_size = 1;
    if (mpi_initialized && comm_c_ != MPI_COMM_NULL) {
        MPI_Comm_size(comm_c_, &mpi_size);
    }
    const bool distributed_regrid = mpi_initialized && mpi_size > 1 && comm_c_ != MPI_COMM_NULL;
    // This rank's own band comes from the stored decomposition (single source
    // of truth), preserving the identical values the inline band_start(mpi_rank)
    // /band_start(mpi_rank + 1) produced (Req 1.1, 1.2, 1.5). The per-rank
    // row_counts/row_displs the removed per-level MPI_Allgatherv used are no
    // longer needed here — the Output_Gather (task 8) uses
    // band_.row_counts/row_displs.
    const int expected_j0 = band_.j0;
    const int expected_j1 = band_.j1;

    // This must be the helper's first distributed gate. Every caller reaches
    // it before any rank-local return, so a bad source buffer or inconsistent
    // AMIO metadata cannot leave peer ranks waiting in the layer collectives.
    // This front half does NOT touch import_state or stream_view, so the
    // readiness check omits the cece_core_data_ptr / stream_view extent
    // conditions the full AssembleBandField applies; those are enforced
    // by the write-back half (WriteBandToImport).
    const bool positive_dimensions = file_nx > 0 && file_ny > 0 && field_nlev > 0 && nx_ > 0 && ny_ > 0;
    const size_t source_spatial = positive_dimensions ? static_cast<size_t>(file_nx) * file_ny : 0;
    const size_t expected_source_size = positive_dimensions ? static_cast<size_t>(field_nlev) * source_spatial : 0;
    const bool local_source_ready = positive_dimensions && plan.built && plan.file_nx == file_nx && plan.file_ny == file_ny &&
                                    plan.j0 == expected_j0 && plan.j1 == expected_j1 && source.size() == expected_source_size;
    if (!local_source_ready && failure_detail.empty()) {
        failure_detail = "source buffer or regrid metadata changed after AMIO validation";
    }

    // Fused front-half gate (Req 6.1-6.4, 8.3): replace the former
    // collective_all_ready + four collective_int_matches sequence (~9 reductions)
    // with ONE packed 5-entry vector reduced by one MIN and one MAX (2
    // reductions), then apply the pure FusedGateDecision helper. The packed
    // order is fixed: [readiness, file_nx, file_ny, field_nlev, identity].
    const std::vector<int> gate_vec{local_source_ready ? 1 : 0, file_nx, file_ny, field_nlev, plan.identity ? 1 : 0};
    if (!distributed_regrid) {
        // Short-circuit (uninitialized MPI / MPI_COMM_NULL / mpi_size <= 1): issue
        // NO collective. With a single participant, elementwise min == max ==
        // local, so passing the local vector as both mn and mx reproduces the
        // legacy short-circuit outcome exactly: collective_all_ready returned the
        // local readiness (mn[0] == local_source_ready ? 1 : 0), and each
        // collective_int_matches returned true (mn[k] == mx[k] for k in 1..4).
        // FusedGateDecision(gate_vec, gate_vec, ...) therefore returns
        // local_source_ready and preserves the already-set not-ready detail.
        if (!FusedGateDecision(gate_vec, gate_vec, failure_detail)) return false;
    } else if (!halo_comm_) {
        // Defensive: RefreshHaloCommunicator engages halo_comm_ under exactly
        // this distributed predicate, so this branch is unreachable; the
        // explicit check keeps the dereferences below clang-tidy-clean
        // (bugprone-unchecked-optional-access).
        failure_detail = "front-half readiness gate: HALO communicator unavailable";
        return false;
    } else {
        // Distributed (size > 1): every rank issues the same two allreduce
        // collectives in the same order (the predicate is rank-invariant), so no
        // rank skips a collective a peer enters (Req 6.2, 8.3, 8.4). Any HALO
        // throw maps to failure_detail and returns false.
        try {
            const std::vector<int> mn = halo::allreduce<int>(*halo_comm_, gate_vec, MPI_MIN);
            const std::vector<int> mx = halo::allreduce<int>(*halo_comm_, gate_vec, MPI_MAX);
            if (!FusedGateDecision(mn, mx, failure_detail)) return false;
        } catch (const std::exception& e) {
            failure_detail = "front-half readiness gate failed: " + std::string(e.what());
            return false;
        }
    }

    // Band decomposition rework (Req 3.1, 3.3, 3.4, 3.5): the per-level
    // MPI_Allgatherv that re-replicated every rank's band into a global
    // field_nlev * nx_ * ny_ buffer is REMOVED. apply_regrid_plan already emits
    // exactly this rank's band slice (nx_ * ny_local), so we keep that slice as
    // the final per-rank result. out_buffer becomes the band buffer of size
    // field_nlev * nx_ * ny_local, laid out [level][jrel][i] with element
    // (level, jrel, i) at level*(nx_*ny_local) + jrel*nx_ + i. Downstream
    // (WriteBandToImport, task 3.3) transposes only [0, ny_local) rows and the
    // Output_Gather (task 8) reassembles the global field at write time.
    //
    // On the single-rank / no-MPI path ny_local == ny_, so the band buffer IS
    // the global buffer: the outcome is unchanged from the former single-rank
    // fast path (each level's band is placed contiguously at jrel 0..ny_local).
    //
    // band_elems is the per-level band element count nx_ * ny_local, where
    // ny_local == band_.ny_local == expected_j1 - expected_j0.
    const size_t band_elems = static_cast<size_t>(expected_j1 - expected_j0) * nx_;

    // band_buffer holds this rank's band for every level: field_nlev per-level
    // band slices of band_elems doubles each, concatenated so (level, jrel, i)
    // lives at level*band_elems + jrel*nx_ + i. This is precisely the former
    // distributed send_buf layout, now promoted to the returned buffer.
    std::vector<double> band_buffer(static_cast<size_t>(field_nlev) * band_elems, 0.0);

    // Per-level regrid directly into the contiguous band buffer. Preserve
    // lock-step: a per-level regrid failure sets local_ok = false but does NOT
    // early-return before the collective — the single pre-gather
    // allreduce(MIN) over local_ok (retained below) lets a failing rank still
    // enter the collective so no peer is stranded (Req 3.5, 8.4).
    std::vector<double> band_scratch;  // reused across levels
    bool local_ok = true;
    for (int level = 0; level < field_nlev; ++level) {
        const double* source_layer = source.data() + static_cast<size_t>(level) * source_spatial;
        bool local_regrid_succeeded = false;
        try {
            local_regrid_succeeded =
                cece::io::apply_regrid_plan(plan, /*time_offset=*/0, /*is_float=*/false, source_layer, file_nx, file_ny, nx_, band_scratch);
        } catch (const std::exception& error) {
            failure_detail = "regrid weight application threw an exception: " + std::string(error.what());
        } catch (...) {
            failure_detail = "regrid weight application threw an unknown exception";
        }
        const size_t expected_local_size = static_cast<size_t>(plan.j1 - plan.j0) * nx_;
        const bool band_size_matches = expected_local_size == band_elems;
        const bool local_layer_ready = local_regrid_succeeded && band_scratch.size() == expected_local_size && band_size_matches;
        if (!local_layer_ready) {
            local_ok = false;
            continue;  // Do NOT early-return: preserve lock-step for the collective.
        }

        // Copy this level's band into the contiguous [level][band] band buffer
        // at offset level*band_elems (Req 3.1, 3.3). No global placement / no
        // gather: the band slice is the final per-rank result.
        std::copy(band_scratch.begin(), band_scratch.end(), band_buffer.begin() + static_cast<size_t>(level) * band_elems);
    }

    if (distributed_regrid) {
        // Single pre-gather readiness reduction over local_ok, RETAINED from the
        // former gather path (Req 3.4, 3.5, 8.4): every rank enters this
        // collective, so a rank whose per-level regrid failed still participates
        // and no peer is stranded. This is the sole collective the band regrid
        // now issues — the per-level MPI_Allgatherv assembly is gone. All ranks
        // still agree the band regrid succeeded before proceeding.
        if (!halo_comm_) {
            // Defensive, unreachable (see front-half gate above).
            failure_detail = "pre-gather readiness gate: HALO communicator unavailable";
            return false;
        }
        const std::vector<int> ready_vec{local_ok ? 1 : 0};
        const std::vector<int> reduced = halo::allreduce<int>(*halo_comm_, ready_vec, MPI_MIN);
        if (reduced.empty() || reduced[0] != 1) {
            if (failure_detail.empty()) {
                failure_detail = "rank-local regrid failed or produced an unexpected destination-band size";
            }
            CECE_LOG_DEBUG("[DRIVER] rank-local band regrid failed!");
            return false;
        }
    } else if (!local_ok) {
        // Single-rank path: no collective, but still honor the readiness result
        // so the single-rank outcome matches the distributed all-ranks decision.
        if (failure_detail.empty()) {
            failure_detail = "rank-local regrid failed or produced an unexpected destination-band size";
        }
        CECE_LOG_DEBUG("[DRIVER] rank-local band regrid failed!");
        return false;
    }

    out_buffer = std::move(band_buffer);
    return true;
}

bool CeceDriverOrchestrator::WriteBandToImport(const std::string& var_name, const std::vector<double>& dest_buffer, int field_nlev,
                                               void* cece_core_data_ptr, std::string& failure_detail) {
    // Band decomposition rework (Req 2.1, 2.4, 2.5, 3.2, 3.4): the incoming
    // dest_buffer is now the rank-local BAND buffer produced by
    // RegridToBandBuffer, laid out [level][jrel][i] with element (level, jrel, i)
    // at level*(nx_*ny_local) + jrel*nx_ + i, sized field_nlev * nx_ * ny_local
    // (NOT the former global field_nlev * nx_ * ny_). This method transposes only
    // the [0, ny_local) band rows into the LayoutLeft (i, jrel, level) core
    // import DualView and allocates it band-tall (nx_ x ny_local x field_nlev).
    // The global field is reassembled later by the Output_Gather (task 8).
    //
    // ny_local comes from the stored band decomposition (single source of truth,
    // identical to expected_j1 - expected_j0 in RegridToBandBuffer). On the
    // single-rank / no-MPI path ny_local == ny_, so this is byte-for-byte the
    // former global transpose and the outcome is unchanged.
    const std::vector<double>& band_destination = dest_buffer;
    const int ny_local = band_.ny_local;
    const size_t band_spatial = static_cast<size_t>(nx_) * ny_local;

    // Transpose the [level][jrel][i] band buffer into the LayoutLeft
    // (i, jrel, level) DualView layout exactly ONCE, into a single host mirror
    // buffer sized to the band. The index math mirrors the former global
    // transpose but is bounded by ny_local rows:
    //   host(i, jrel, level) = band_destination[level*nx*ny_local + jrel*nx + i].
    // Surplus ranks (ny_local == 0) allocate a zero-row host mirror and the copy
    // loop iterates zero times, so no band data is touched (Req 2.5, 3.4).
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> transposed_host("assembled_field_host", nx_, ny_local, field_nlev);
    for (int level = 0; level < field_nlev; ++level) {
        for (int jrel = 0; jrel < ny_local; ++jrel) {
            for (int i = 0; i < nx_; ++i) {
                transposed_host(i, jrel, level) = band_destination[static_cast<size_t>(level) * band_spatial + static_cast<size_t>(jrel) * nx_ + i];
            }
        }
    }

    // Populate the Core import state with the band-local field. Surplus ranks
    // (ny_local == 0) allocate a zero-row DualView3D(nx_, 0, field_nlev) without
    // error (Req 2.5).
    auto* data = static_cast<cece::CeceInternalData*>(cece_core_data_ptr);
    // Publish this rank's band geometry to the core so band-local compute
    // (task 6.2) sizes its work over ny_local rows instead of the global ny_.
    // This runs during AdvanceTime, before cece_core_run consumes d.ny_local,
    // and is idempotent — band_ is fixed for the life of the orchestrator, so
    // writing the same values every step is safe. On the single-rank / no-MPI
    // path band_.ny_local == ny_ and band_.j0 == 0, matching the global grid
    // (Req 1.5, 2.3).
    data->ny_local = band_.ny_local;
    data->j0 = band_.j0;
    auto core_it = data->import_state.fields.find(var_name);
    if (core_it == data->import_state.fields.end()) {
        cece::DualView3D field(var_name, nx_, ny_local, field_nlev);
        data->import_state.fields[var_name] = field;
        core_it = data->import_state.fields.find(var_name);
    }

    auto& core_field = core_it->second;
    auto core_view = core_field.view_device();
    // Validate the core import field extents against ny_local (Req 2.4, 3.4):
    // extent(1) must be ny_local, not the global ny_. The collective_all_ready
    // gate below keeps every rank — including surplus ranks (ny_local == 0) —
    // in lock-step (Req 2.5, 3.4).
    const bool local_core_shape_ready = core_view.extent(0) == static_cast<size_t>(nx_) && core_view.extent(1) == static_cast<size_t>(ny_local) &&
                                        core_view.extent(2) == static_cast<size_t>(field_nlev);
    if (!local_core_shape_ready) {
        failure_detail = "core import field shape mismatch for '" + var_name + "': expected " + std::to_string(nx_) + "x" + std::to_string(ny_local) +
                         "x" + std::to_string(field_nlev) + ", found " + std::to_string(core_view.extent(0)) + "x" +
                         std::to_string(core_view.extent(1)) + "x" + std::to_string(core_view.extent(2));
    }
    if (!collective_all_ready(halo_comm_ ? &*halo_comm_ : nullptr, comm_c_, local_core_shape_ready, "core import field shape validation",
                              failure_detail))
        return false;

    // Authoritative write of the core import field from the single host buffer.
    // This is now the SOLE authoritative write of the assembled field. The
    // former stream_view populate (Tier 2, task 9.1) has been removed: the
    // Finding-B read-site enumeration proved no consumer reads the data stored
    // in CeceIO::field_views_ / stream_view within a step (it is used only for
    // .extent(...) shape/metadata queries and the readiness gate above, both of
    // which are preserved). Dropping the stream_view deep_copy removes one
    // per-field-per-step copy without changing any value seen by a live
    // consumer, any layout, or any collective gate. Surplus ranks (ny_local == 0)
    // deep_copy a zero-row view (a no-op) but still issue modify/sync so the
    // DualView invariants hold.
    Kokkos::deep_copy(core_view, transposed_host);
    core_field.modify_device();
    core_field.sync_host();

    return true;
}

bool CeceDriverOrchestrator::AssembleBandField(const std::string& var_name, const io::RegridPlan& plan, const std::vector<double>& source,
                                               int file_nx, int file_ny, int field_nlev, DeviceView3D stream_view, void* cece_core_data_ptr,
                                               std::vector<double>& ingest_buffer, std::string& failure_detail) {
    // Band composition (Req 3.1, 3.2, 7.5): the front half (RegridToBandBuffer)
    // runs the readiness gates + per-level apply_regrid_plan into this rank's
    // contiguous [level][jrel][i] LATITUDE-BAND buffer sized
    // field_nlev * nx_ * ny_local (the former per-level MPI_Allgatherv that
    // re-replicated every band into a global field_nlev * nx_ * ny_ buffer was
    // removed in task 3.2). The returned ingest_buffer IS that band buffer. The
    // back half (WriteBandToImport) transposes only [0, ny_local) rows into a
    // band-tall LayoutLeft (i, jrel, level) DualView, runs the core-import-shape
    // collective gate (validated against ny_local), and deep_copy +
    // modify_device + sync_host. The composed collective sequence is identical
    // across ranks: the front half's source/metadata readiness, nx/ny/nlev/
    // identity int matches, and the retained pre-gather allreduce(MIN) run
    // first, then the back half's core-import-field-shape gate, in exactly the
    // same order. On the single-rank / no-MPI path ny_local == ny_, so the band
    // buffer IS the former global buffer and the outcome is unchanged.
    //
    // stream_view is now unused: the former stream_view readiness condition and
    // populate were dropped when the sole authoritative write became the core
    // import field (see WriteBandToImport). The signature is kept
    // unchanged for every existing caller and the slice-cache path.
    (void)stream_view;

    // Front half: produce this rank's latitude-band buffer (task 3.2). The
    // per-level MPI_Allgatherv re-replication was removed, so band_buffer is
    // sized field_nlev * nx_ * band_.ny_local (was field_nlev * nx_ * ny_) and
    // is the returned ingest_buffer.
    std::vector<double> band_buffer;
    if (!RegridToBandBuffer(var_name, plan, source, file_nx, file_ny, field_nlev, band_buffer, failure_detail)) {
        return false;
    }

    // Surface the band buffer to the caller/slice-cache verbatim, then reuse it
    // for the core import write.
    ingest_buffer = std::move(band_buffer);

    // Back half: authoritative write of the core import field (task 3.3).
    // WriteBandToImport transposes only the [0, ny_local) band rows into a
    // band-tall DualView3D(nx_, band_.ny_local, field_nlev) and validates the
    // core-import shape against ny_local. On the single-rank path
    // (ny_local == ny_) the band field IS the global field and the outcome is
    // unchanged; on the multi-rank path the write side now matches the band
    // buffer RegridToBandBuffer produced.
    if (!WriteBandToImport(var_name, ingest_buffer, field_nlev, cece_core_data_ptr, failure_detail)) {
        return false;
    }

    return true;
}

bool CeceDriverOrchestrator::AdvanceTime(const std::string& time_iso8601, void* cece_core_data_ptr) {
    std::string core_readiness_detail;
    if (!collective_all_ready(halo_comm_ ? &*halo_comm_ : nullptr, comm_c_, cece_core_data_ptr != nullptr, "CECE core-data readiness",
                              core_readiness_detail)) {
        CECE_LOG_ERROR("[DRIVER FATAL] " + core_readiness_detail);
        return false;
    }

    // A. Advance the pipeline step
    dagr_->advance_step();
    Kokkos::fence();

    // Configuration is parsed exactly once at construction (Req 9.3); AdvanceTime
    // consults the resolved StreamConfig via stream_configs_ and never re-reads
    // the YAML file from disk.

    // Parse the current simulation datetime once. Streams that declare a
    // temporal cadence (hourly/weekly/monthly) use these calendar fields to
    // select the correct file record; streams without a cadence keep the
    // legacy step-index cycling behaviour and ignore this.
    const SimDateTime sim_dt = parse_sim_datetime(time_iso8601);

    // B. Push CeceIO's newly computed emission views into CECE's data ingestor
    for (const auto& var_name : cece_io_->GetOutputVarNames()) {
        auto stream_view = cece_io_->GetFieldView(var_name);
        const int field_nlev = static_cast<int>(stream_view.extent(2));
        // Human-readable reason for the most recent read failure, propagated to
        // the fatal error message so the underlying AMIO status reaches CECE.
        std::string failure_detail;
        if (field_nlev < 1) {
            CECE_LOG_ERROR("[DRIVER FATAL] Field '" + var_name + "' has no configured levels");
            failure_detail = "field has no configured levels";
        }
        if (!collective_all_ready(halo_comm_ ? &*halo_comm_ : nullptr, comm_c_, field_nlev >= 1,
                                  "field-level metadata readiness for '" + var_name + "'", failure_detail))
            return false;
        std::vector<double> ingest_buffer;
        bool read_success = false;

        // Read the resolved StreamConfig for this variable from the map filled
        // once at construction (Req 1.2, 9.3). A variable with no StreamConfig
        // entry is treated exactly like a stream whose input_file_path is empty:
        // the missing-configuration error is surfaced through the same
        // collective gate the empty-path case used, so all ranks agree (Req 1.4).
        auto cfg_it = stream_configs_.find(var_name);
        const bool has_stream_config = cfg_it != stream_configs_.end();
        const StreamConfig cfg = has_stream_config ? cfg_it->second : StreamConfig{};

        std::string input_file_path = cfg.input_file_path;
        std::string input_var_name = cfg.input_var_name;
        const std::string& mapalgo = cfg.mapalgo;
        const std::string& cadence = cfg.cadence;
        const std::string& tintalgo = cfg.tintalgo;

        // TWO-LEVEL keying for the shared caches:
        //   - handle_key (file/manifest-scoped) keys amio_handles_ and
        //     file_nt_cache_. Variables reading the same file/manifest share
        //     one open AMIO handle set and one record-count search even when
        //     their mapalgo differs (Req 11.2, 11.3).
        //   - stream_key (= handle_key + "|" + mapalgo) keys regrid_plans_.
        //     The plan cache splits only when mapalgo differs; everything else
        //     stays shared at the coarser handle_key level (Req 11.5, 11.6).
        //   - slice_caches_ and cece_ingestor_set_field remain keyed by
        //     var_name (Req 5, 12.1).
        // Both keys are identical on every rank because StreamConfig is
        // resolved identically from the same YAML (Req 8.1, 11.8).
        const std::string handle_key = HandleKey(cfg);
        const std::string stream_key = StreamKey(cfg);

        if (input_file_path.empty()) {
            CECE_LOG_ERROR("[DRIVER FATAL] Input file path not specified for stream variable '" + var_name + "' in configuration!");
            failure_detail = "input file path is not configured";
        }
        if (input_var_name.empty()) {
            input_var_name = var_name;
        }
        if (!collective_all_ready(halo_comm_ ? &*halo_comm_ : nullptr, comm_c_, !input_file_path.empty(),
                                  "stream configuration readiness for '" + var_name + "'", failure_detail)) {
            return false;
        }

        // Verify if the input file path exists and is accessible from this compute/login node
        std::error_code fs_ec;
        const bool local_file_ready = fs::exists(input_file_path, fs_ec);
        if (!local_file_ready) {
            CECE_LOG_ERROR("[DRIVER FATAL] File '" + input_file_path +
                           "' does not exist or is unreadable on this node! (System error: " + fs_ec.message() + ")");
            failure_detail = "input file does not exist or is unreadable: " + fs_ec.message();
        } else {
            CECE_LOG_DEBUG("[DRIVER] Input file '" + input_file_path + "' successfully verified on local filesystem.");
        }
        if (!collective_all_ready(halo_comm_ ? &*halo_comm_ : nullptr, comm_c_, local_file_ready, "input-file readiness for '" + var_name + "'",
                                  failure_detail))
            return false;

        // Dynamically open and read using AMIO API
        // First-touch open (once per variable) via GetOrOpenHandleSet, which
        // builds the in-memory manifest, performs the MPI_COMM_SELF swap, and
        // tries the candidate data models. On subsequent steps it returns the
        // cached handle set with no re-open, no manifest write, and no barrier
        // (Req 2.1, 2.2, 9.1, 9.2). The dataset-open readiness collective still
        // runs every step so ranks agree that a usable handle set exists; the
        // actual open work only happens on the first touch.
        AmioHandleSet* handle_set = GetOrOpenHandleSet(handle_key, cfg, failure_detail);
        const bool local_open_ready = handle_set != nullptr && handle_set->dataset != nullptr && handle_set->core != nullptr;
        const bool amio_open_ready = collective_all_ready(halo_comm_ ? &*halo_comm_ : nullptr, comm_c_, local_open_ready,
                                                          "AMIO dataset open for '" + var_name + "'", failure_detail);

        if (!amio_open_ready) {
            CECE_LOG_DEBUG("[DRIVER] amio open failed for " + input_file_path + ": " + failure_detail);
        } else {
            failure_detail.clear();
            // Use the retained dataset handle for record discovery, plan
            // building, and reads. It persists in amio_handles_ across timesteps
            // and is torn down in the destructor (task 10.1).
            amio_dataset_handle read_dataset = handle_set->dataset;

            // This rank's contiguous destination latitude band [j0, j1) comes
            // from the stored decomposition (single source of truth),
            // preserving the identical values the inline block decomposition
            // produced (Req 1.1, 1.2, 1.5).
            const int j0 = band_.j0;
            const int j1 = band_.j1;

            // 1. Determine total timesteps (and the per-timestep shape) for the
            //    input variable. amio_describe answers both from the file's
            //    metadata without staging any payload, replacing the old
            //    binary search that probed amio_read at ~20 record indices --
            //    each probe a full-record read, which is exactly the traffic
            //    band-scoped reads exist to avoid. Results are cached per
            //    handle_key (record count) and per (handle_key, variable)
            //    (shape) so the query runs at most once per file/variable.
            int file_nt = 1;
            amio_shape_t var_shape{};
            bool have_shape = false;
            const std::string shape_key = handle_key + "|" + input_var_name;
            auto shape_it = var_shape_cache_.find(shape_key);
            auto nt_it = file_nt_cache_.find(handle_key);
            if (shape_it != var_shape_cache_.end()) {
                var_shape = shape_it->second;
                have_shape = true;
            }
            if (nt_it != file_nt_cache_.end()) {
                file_nt = nt_it->second;
            }
            if (nt_it == file_nt_cache_.end() || !have_shape) {
                int64_t nt64 = 0;
                amio_shape_t desc_shape{};
                amio_status_t desc_rc = amio_describe(read_dataset, input_var_name.c_str(), &desc_shape, &nt64);
                if (desc_rc == AMIO_OK && nt64 > 0) {
                    file_nt = static_cast<int>(nt64);
                    var_shape = desc_shape;
                    have_shape = true;
                    file_nt_cache_[handle_key] = file_nt;
                    var_shape_cache_[shape_key] = var_shape;
                } else {
                    // Metadata unavailable (older driver, absent variable, ...):
                    // fall back to the historical binary search so record
                    // discovery still works. The bbox path stays disabled
                    // (have_shape == false -> full-record reads).
                    if (!input_var_name.empty()) {
                        int low = 1;
                        int high = 1000000;
                        int found_nt = 1;
                        while (low <= high) {
                            int mid = low + (high - low) / 2;
                            amio_view_handle v = nullptr;
                            amio_status_t rc = amio_read(read_dataset, input_var_name.c_str(), mid, nullptr, &v);
                            if (rc == AMIO_OK) {
                                amio_release_view(v);
                                found_nt = mid + 1;
                                low = mid + 1;
                            } else {
                                high = mid - 1;
                            }
                        }
                        file_nt = found_nt;
                    }
                    file_nt_cache_[handle_key] = file_nt;
                }
            }
            bool file_records_ready = collective_all_ready(halo_comm_ ? &*halo_comm_ : nullptr, comm_c_, file_nt > 0,
                                                           "AMIO record-count readiness for '" + var_name + "'", failure_detail);
            if (file_records_ready) {
                file_records_ready = collective_int_matches(halo_comm_ ? &*halo_comm_ : nullptr, comm_c_, file_nt,
                                                            "AMIO record count for '" + var_name + "'", failure_detail);
            }

            // 2. Build (or reuse cached) interpolation weights for this rank's band.
            //    Weights depend only on the grids, so they are generated once and
            //    reused for every timestep.
            auto plan_it = regrid_plans_.find(stream_key);
            if (file_records_ready && (plan_it == regrid_plans_.end() || !plan_it->second.built)) {
                cece::io::RegridPlan plan;
                // An explicit passthrough is safe without reopening coordinate
                // variables only when the stream and gridspec resolve to the
                // exact same file.  main.cpp has already loaded and validated
                // the target coordinates from that file; read_slab below still
                // verifies every field's horizontal size before copying.
                std::error_code equivalent_ec;
                const bool exact_source_target_file = mapalgo == "passthrough" && !gridspec_file_.empty() &&
                                                      fs::equivalent(fs::path(input_file_path), fs::path(gridspec_file_), equivalent_ec) &&
                                                      !equivalent_ec;

                if (exact_source_target_file) {
                    plan.j0 = j0;
                    plan.j1 = j1;
                    plan.file_nx = nx_;
                    plan.file_ny = ny_;
                    plan.identity = true;
                    // Identity apply reads source rows [j0, j1) verbatim, so
                    // the exact window is the band itself (same rule as
                    // build_regrid_plan's passthrough branch).
                    plan.src_j0 = j0;
                    plan.src_rows = j1 - j0;
                    plan.built = true;
                    CECE_LOG_INFO("[DRIVER] passthrough verified stream file equals explicit gridspec file for '" + var_name +
                                  "'; using exact cell copy");
                    plan_it = regrid_plans_.emplace(stream_key, std::move(plan)).first;
                    // Plan (re)build invalidates the endpoint cache of every
                    // variable bound to this stream_key: cached endpoints depend
                    // on the plan, so a new/rebuilt plan must force a rebuild
                    // before reuse (Req 6.2).
                    InvalidateEndpointCachesForStream(stream_key);
                } else {
                    bool local_plan_built = false;
                    try {
                        local_plan_built =
                            cece::io::build_regrid_plan(read_dataset, nx_, ny_, target_lons_, target_lats_, mapalgo, j0, j1, gridspec_file_, plan);
                    } catch (const std::exception& error) {
                        failure_detail = "regrid plan construction threw an exception: " + std::string(error.what());
                    } catch (...) {
                        failure_detail = "regrid plan construction threw an unknown exception";
                    }
                    if (!local_plan_built) {
                        CECE_LOG_DEBUG("[DRIVER] build_regrid_plan failed for '" + var_name + "'");
                        if (failure_detail.empty()) {
                            failure_detail = "regrid plan construction failed (could not read source grid coordinates)";
                        }
                    } else {
                        plan_it = regrid_plans_.emplace(stream_key, std::move(plan)).first;
                        // Plan (re)build invalidates the endpoint cache of every
                        // variable bound to this stream_key: cached endpoints
                        // depend on the plan, so a new/rebuilt plan must force a
                        // rebuild before reuse (Req 6.2).
                        InvalidateEndpointCachesForStream(stream_key);
                    }
                }
            }
            const bool local_plan_ready = file_records_ready && plan_it != regrid_plans_.end() && plan_it->second.built;
            const bool all_plans_ready = collective_all_ready(halo_comm_ ? &*halo_comm_ : nullptr, comm_c_, local_plan_ready,
                                                              "regrid-plan readiness for '" + var_name + "'", failure_detail);

            // 3. Read the bracketing record(s) for this timestep, blend in time on
            //    the SOURCE grid, then regrid ONCE. Because regridding is a linear
            //    operator, interpolating in time before space is mathematically
            //    identical to the reverse, but it costs a single regrid apply (not
            //    two) and keeps fill-value handling on the native grid.
            //
            //    The record bracket comes from the stream's temporal cadence:
            //      - no cadence declared  -> legacy step-index cycling (single read)
            //      - hourly / weekly      -> nearest discrete profile record
            //      - monthly + tintalgo=linear -> mid-month linear interpolation
            //        between the two bracketing climatological records.
            if (all_plans_ready) {
                const cece::io::RegridPlan& plan = plan_it->second;

                RecordBracket bracket = cadence_record_bracket(cadence, tintalgo, sim_dt, file_nt);
                if (!bracket.valid) {
                    const int t_idx = (file_nt > 0) ? (step_index_ % file_nt) : 0;
                    bracket.i0 = bracket.i1 = t_idx;
                    bracket.weight = 0.0;
                }
                const bool needs_upper_record = bracket.i1 != bracket.i0 && bracket.weight > 0.0;
                // Run the record-index / interp-mode collectives EVERY step so
                // all ranks resolve an identical bracket and therefore make the
                // same hit/miss decision (Req 6.3, 6.4).
                const bool bracket_ready =
                    collective_int_matches(halo_comm_ ? &*halo_comm_ : nullptr, comm_c_, bracket.i0, "lower AMIO record index", failure_detail) &&
                    collective_int_matches(halo_comm_ ? &*halo_comm_ : nullptr, comm_c_, bracket.i1, "upper AMIO record index", failure_detail) &&
                    collective_int_matches(halo_comm_ ? &*halo_comm_ : nullptr, comm_c_, needs_upper_record ? 1 : 0, "AMIO interpolation mode",
                                           failure_detail);

                // Cache decision (Req 3, 5, 9.4): if the previously computed
                // result was for the same bracket, reuse its ingest buffer and
                // skip the disk read AND the regrid apply entirely. Because the
                // resolved bracket is identical across ranks, every rank makes
                // the same hit/miss decision, so no rank reads while another
                // skips (Req 6.3).
                auto& slice_cache = slice_caches_[var_name];
                auto& endpoint_cache = endpoint_caches_[var_name];

                // Tier 1: exact slice-cache hit (indices AND weight) — reuse the
                // cached ingest buffer with no read, no regrid, no blend (Req
                // 2.4, 9.3). This is the pre-existing short-circuit, unchanged.
                const bool tier1_slice_hit = bracket_ready && slice_cache.valid && bracket_equal(bracket, slice_cache.last_bracket);

                // Tier 2: endpoint-cache hit (same indices, different weight) —
                // reuse the two cached band-grid endpoints regrid(i0)/regrid(i1)
                // and recompute only the cheap blend, skipping both reads and
                // both regrids (Req 2.1, 2.2). The gate is a pure function of
                // collective-agreed (i0,i1) + rank-invariant nx_/field_nlev and
                // the rank's band rows band_.ny_local (equal to the built_ny the
                // endpoints were stored with), so every rank makes the same
                // decision (Req 4.2, 4.3, 4.5).
                const bool tier2_endpoint_hit = bracket_ready && needs_upper_record && endpoint_cache.valid &&
                                                endpoint_cache.cached_i0 == bracket.i0 && endpoint_cache.cached_i1 == bracket.i1 &&
                                                endpoint_cache.built_nx == nx_ && endpoint_cache.built_ny == band_.ny_local &&
                                                endpoint_cache.built_field_nlev == field_nlev;

                if (tier1_slice_hit) {
                    // ---- Tier 1 ----
                    CECE_LOG_DEBUG("[DRIVER] Reusing cached time slice " + std::to_string(bracket.i0) + " for field '" + var_name +
                                   "' (bracket unchanged; no read, no regrid)");
                    ingest_buffer = slice_cache.ingest_buffer;
                    read_success = true;
                } else if (tier2_endpoint_hit) {
                    // ---- Tier 2: endpoint-cache hit (same indices, different
                    // weight) ----
                    // NO read_slab and NO apply_regrid_plan on this step. Blend
                    // the two cached destination-grid endpoints with the current
                    // bracket weight and write the result back through the same
                    // core-import shape gate every other tier uses (Req 2.1,
                    // 2.2, 4.5, 5.1, 5.4, 6.5). No rank-local early return: all
                    // ranks reach WriteBandToImport, whose single collective
                    // shape gate keeps them in lock-step.
                    //
                    // The cached endpoints are band buffers of size
                    // field_nlev * nx_ * band_.ny_local (task 3.2), and
                    // WriteBandToImport consumes a band buffer indexing only
                    // [0, ny_local) rows. blend_size and the refreshed
                    // slice_cache.ingest_size are therefore band-sized. On the
                    // single-rank path (ny_local == ny_) this is byte-for-byte
                    // the former global blend (Req 3.1, 3.2, 7.5).
                    CECE_LOG_DEBUG("[DRIVER] Reusing cached regrid endpoints " + std::to_string(bracket.i0) + " & " + std::to_string(bracket.i1) +
                                   " for field '" + var_name + "' (w=" + std::to_string(bracket.weight) + "; no read, no regrid)");
                    const double w = bracket.weight;
                    const size_t blend_size = static_cast<size_t>(field_nlev) * nx_ * band_.ny_local;
                    std::vector<double> blended(blend_size);
                    for (size_t k = 0; k < blend_size; ++k) {
                        blended[k] = (1.0 - w) * endpoint_cache.endpoint_i0[k] + w * endpoint_cache.endpoint_i1[k];
                    }
                    // Surface the band buffer to the caller/slice cache first
                    // (mirror AssembleBandField), then write back from the same
                    // buffer so it stays valid.
                    ingest_buffer = std::move(blended);
                    read_success = WriteBandToImport(var_name, ingest_buffer, field_nlev, cece_core_data_ptr, failure_detail);
                    if (read_success) {
                        // Refresh the slice cache so an immediate exact repeat
                        // (same indices AND weight) re-hits Tier 1 (Req 6.5).
                        slice_cache.last_bracket = bracket;
                        slice_cache.ingest_buffer = ingest_buffer;
                        slice_cache.ingest_size = static_cast<size_t>(field_nlev) * nx_ * band_.ny_local;
                        slice_cache.valid = true;
                    }
                } else {
                    // ---- Tier 3: interpolation miss / rollover / single record ----
                    // Diagnostic: report which time slice(s) are being read from the file.
                    if (bracket.i0 == bracket.i1 || bracket.weight == 0.0) {
                        CECE_LOG_INFO("[DRIVER] Reading time slice " + std::to_string(bracket.i0) + "/" + std::to_string(file_nt - 1) + " from '" +
                                      input_file_path + "' for field '" + var_name + "'" +
                                      (cadence.empty() ? " (cycling, step=" + std::to_string(step_index_) + ")"
                                                       : " (cadence=" + cadence + ", time=" + time_iso8601 + ")"));
                    } else {
                        CECE_LOG_INFO("[DRIVER] Interpolating time slices " + std::to_string(bracket.i0) + " & " + std::to_string(bracket.i1) + "/" +
                                      std::to_string(file_nt - 1) + " (w=" + std::to_string(bracket.weight) + ") from '" + input_file_path +
                                      "' for field '" + var_name + "' (cadence=" + cadence + ", tintalgo=" + tintalgo + ", time=" + time_iso8601 +
                                      ")");
                    }

                    // Read one time record into a double buffer on the source grid.
                    // AMIO removes the CF time dimension, but any remaining dimensions
                    // before [lat, lon] are preserved as per-variable levels.
                    //
                    // Band-scoped read: when the plan carries an exact source-row
                    // window (build_regrid_plan derives it from the weight matrix's
                    // column range) and the variable's per-timestep shape is known
                    // (amio_describe), the on-disk fetch is restricted to rows
                    // [src_j0, src_j0 + src_rows). Without this every rank pulls the
                    // WHOLE global record from shared storage and discards all but
                    // its band's footprint -- nranks-fold replicated IO that made
                    // the 16-rank run slower than serial. The windowed payload is
                    // scattered into a GLOBAL-geometry buffer below, so
                    // RegridToBandBuffer / apply_regrid_plan (which index the source
                    // globally) and their size gates are unchanged; rows outside the
                    // window are provably never referenced (the window spans
                    // min..max matrix column) and stay zero-filled.
                    auto read_slab = [&](int t_idx, std::vector<double>& out, int& slab_nx, int& slab_ny) -> bool {
                        amio_view_handle slab_view = nullptr;
                        amio_bbox_t bbox{};
                        const int fny_global_full = have_shape ? static_cast<int>(var_shape.extents[var_shape.rank - 2]) : 0;
                        const bool use_window = have_shape && var_shape.rank >= 2 && plan.src_rows > 0 && plan.src_j0 >= 0 && fny_global_full > 0 &&
                                                plan.src_j0 + plan.src_rows <= fny_global_full;
                        if (use_window) {
                            bbox.rank = var_shape.rank;
                            for (int d = 0; d < var_shape.rank; ++d) {
                                bbox.offsets[d] = 0;
                                bbox.extents[d] = var_shape.extents[d];
                                bbox.strides[d] = 1;
                            }
                            bbox.offsets[var_shape.rank - 2] = plan.src_j0;
                            bbox.extents[var_shape.rank - 2] = plan.src_rows;
                        }
                        amio_status_t rc = amio_read(read_dataset, input_var_name.c_str(), t_idx, use_window ? &bbox : nullptr, &slab_view);
                        if (rc != AMIO_OK) {
                            CECE_LOG_DEBUG("[DRIVER] amio_read('" + input_var_name + "', t=" + std::to_string(t_idx) +
                                           ") failed with rc = " + std::to_string(rc));
                            failure_detail =
                                std::string("amio_read('") + input_var_name + "') failed: rc=" + std::to_string(rc) + " (" + amio_strerror(rc) + ")";
                            return false;
                        }
                        const void* view_data = nullptr;
                        size_t view_size = 0;
                        rc = amio_view_data(slab_view, &view_data, &view_size);
                        if (rc != AMIO_OK) {
                            failure_detail = std::string("amio_view_data failed: rc=") + std::to_string(rc) + " (" + amio_strerror(rc) + ")";
                            amio_release_view(slab_view);
                            return false;
                        }
                        amio_shape_t read_shape{};
                        if (amio_view_shape(slab_view, &read_shape) != AMIO_OK) {
                            failure_detail = "amio_view_shape failed";
                            amio_release_view(slab_view);
                            return false;
                        }
                        if (read_shape.rank < 2) {
                            failure_detail = "AMIO field rank is less than two for '" + input_var_name + "'";
                            amio_release_view(slab_view);
                            return false;
                        }
                        const int fny_view = static_cast<int>(read_shape.extents[read_shape.rank - 2]);
                        const int fnx = static_cast<int>(read_shape.extents[read_shape.rank - 1]);
                        size_t total_elements = 1;
                        for (int d = 0; d < read_shape.rank; ++d) {
                            total_elements *= read_shape.extents[d];
                        }
                        // Global source latitude count: the full grid the
                        // downstream regrid indexes against. For a windowed read
                        // this is the describe shape's lat extent; for a full read
                        // it equals the view's lat extent.
                        const int fny_global = use_window ? fny_global_full : fny_view;
                        if (fny_global <= 0 || fnx <= 0) {
                            failure_detail = "AMIO view has non-positive source geometry for '" + input_var_name + "'";
                            amio_release_view(slab_view);
                            return false;
                        }
                        // View record geometry (windowed or full) and global record
                        // geometry (what `out` is sized to).
                        const size_t view_spatial = static_cast<size_t>(fny_view) * fnx;
                        const size_t view_record_elements = static_cast<size_t>(field_nlev) * view_spatial;
                        const size_t global_spatial = static_cast<size_t>(fny_global) * fnx;
                        const size_t global_record_elements = static_cast<size_t>(field_nlev) * global_spatial;
                        if (view_spatial == 0 || view_record_elements == 0 || total_elements % view_record_elements != 0) {
                            failure_detail = "AMIO field shape is incompatible with configured levels=" + std::to_string(field_nlev) + " for '" +
                                             input_var_name + "'";
                            amio_release_view(slab_view);
                            return false;
                        }
                        const size_t records_in_view = total_elements / view_record_elements;
                        const size_t record_index = records_in_view > 1 ? static_cast<size_t>(t_idx) : 0;
                        if (record_index >= records_in_view) {
                            failure_detail = "AMIO view does not contain requested record " + std::to_string(t_idx) + " for '" + input_var_name + "'";
                            amio_release_view(slab_view);
                            return false;
                        }
                        const size_t view_offset = record_index * view_record_elements;

                        const bool is_float = (view_size == total_elements * sizeof(float));
                        const bool is_double = (view_size == total_elements * sizeof(double));
                        if (!is_float && !is_double) {
                            failure_detail = "AMIO returned an unsupported element size for '" + input_var_name + "'";
                            amio_release_view(slab_view);
                            return false;
                        }

                        // Scatter into global geometry. Full read: window covers
                        // every row, so this reduces to a straight widened copy.
                        // Windowed read: place rows [src_j0, src_j0+src_rows) at
                        // their global positions; untouched rows stay zero (and are
                        // provably never referenced by the regrid).
                        const int win_j0 = use_window ? plan.src_j0 : 0;
                        const int win_rows = use_window ? plan.src_rows : fny_view;
                        out.assign(global_record_elements, 0.0);
                        for (int level = 0; level < field_nlev; ++level) {
                            const size_t view_level_base = view_offset + static_cast<size_t>(level) * view_spatial;
                            const size_t global_level_base = static_cast<size_t>(level) * global_spatial;
                            for (int wr = 0; wr < win_rows; ++wr) {
                                const int gr = win_j0 + wr;
                                if (gr < 0 || gr >= fny_global) {
                                    continue;
                                }
                                const size_t src_row = view_level_base + static_cast<size_t>(wr) * fnx;
                                const size_t dst_row = global_level_base + static_cast<size_t>(gr) * fnx;
                                if (is_float) {
                                    const float* p = static_cast<const float*>(view_data);
                                    for (int i = 0; i < fnx; ++i) out[dst_row + i] = static_cast<double>(p[src_row + i]);
                                } else {
                                    const double* p = static_cast<const double*>(view_data);
                                    for (int i = 0; i < fnx; ++i) out[dst_row + i] = p[src_row + i];
                                }
                            }
                        }
                        slab_nx = fnx;
                        slab_ny = fny_global;
                        amio_release_view(slab_view);
                        CECE_LOG_DEBUG("[DRIVER] Read slab t=" + std::to_string(t_idx) + " for '" + input_var_name + "': window rows [" +
                                       std::to_string(win_j0) + "," + std::to_string(win_j0 + win_rows) + ") of " + std::to_string(fny_global) + "x" +
                                       std::to_string(fnx) + "x" + std::to_string(field_nlev) + " (global buffer " +
                                       std::to_string(global_record_elements) + " elements, view " + std::to_string(view_record_elements) +
                                       ", records_in_view=" + std::to_string(records_in_view) + ", " + (is_float ? "float32" : "float64") + ")");
                        return true;
                    };

                    // Read the lower record (record i0) on the source grid. This
                    // guard is identical to the pre-cache path; every rank agrees on
                    // readiness before either sub-case proceeds.
                    std::vector<double> src;
                    int file_nx = 0;
                    int file_ny = 0;
                    const bool local_lower_ready = bracket_ready && read_slab(bracket.i0, src, file_nx, file_ny);
                    bool have_data = collective_all_ready(halo_comm_ ? &*halo_comm_ : nullptr, comm_c_, local_lower_ready,
                                                          "lower AMIO slab readiness for '" + var_name + "'", failure_detail);

                    if (needs_upper_record) {
                        // ---- Tier 3 interpolation sub-case: rebuild endpoints ----
                        // Read the upper record (record i1) into srcB with the same
                        // readiness guard the pre-cache path used, then regrid EACH
                        // endpoint separately on the destination grid instead of
                        // blending on the source grid and regridding once. Because
                        // regrid is linear, (1-w)*R(A)+w*R(B) == R((1-w)A+w*B), and
                        // caching R(A)/R(B) lets same-index/different-weight steps
                        // skip both reads and both regrids (Req 1.4, 2.3, 3.1, 3.2,
                        // 5.3, 5.4).
                        std::vector<double> srcB;
                        if (have_data) {
                            int upper_nx = 0;
                            int upper_ny = 0;
                            const bool local_upper_ready = read_slab(bracket.i1, srcB, upper_nx, upper_ny) && srcB.size() == src.size() &&
                                                           upper_nx == file_nx && upper_ny == file_ny;
                            have_data = collective_all_ready(halo_comm_ ? &*halo_comm_ : nullptr, comm_c_, local_upper_ready,
                                                             "upper AMIO slab readiness for '" + var_name + "'", failure_detail);
                        }

                        if (have_data) {
                            // Regrid each endpoint into this rank's latitude band.
                            // RegridToBandBuffer owns its own source/metadata
                            // readiness gate, int-match collectives, and the retained
                            // pre-gather allreduce(MIN), so both calls run in
                            // lock-step across ranks. All ranks reach BOTH calls
                            // (they are inside the collective-agreed have_data branch,
                            // not behind any rank-local condition), keeping the
                            // collective sequence identical on every rank
                            // (Req 4.2, 4.3, 4.5).
                            //
                            // epA/epB are band buffers of size
                            // field_nlev * nx_ * band_.ny_local (the per-level
                            // MPI_Allgatherv was removed in task 3.2), so the blend,
                            // the endpoint cache, and the refreshed slice cache below
                            // are all band-sized. built_ny stores band_.ny_local so
                            // the Tier-2 gate compares like-for-like. On the
                            // single-rank path (ny_local == ny_) this is byte-for-byte
                            // the former global blend/cache (Req 3.1, 3.2, 7.5).
                            std::vector<double> epA;
                            std::vector<double> epB;
                            const bool regridA_ok = RegridToBandBuffer(var_name, plan, src, file_nx, file_ny, field_nlev, epA, failure_detail);
                            const bool regridB_ok = RegridToBandBuffer(var_name, plan, srcB, file_nx, file_ny, field_nlev, epB, failure_detail);

                            if (regridA_ok && regridB_ok) {
                                // Store the endpoint cache keyed on (i0, i1) only; the
                                // weight is intentionally excluded so later same-index
                                // steps re-hit Tier 2 (Req 1.1, 1.2, 1.3). Blend from
                                // epA/epB BEFORE moving them into the cache so both the
                                // cache and the blend see the same values.
                                const double w = bracket.weight;
                                const size_t blend_size = static_cast<size_t>(field_nlev) * nx_ * band_.ny_local;
                                std::vector<double> blended(blend_size);
                                for (size_t k = 0; k < blend_size; ++k) {
                                    blended[k] = (1.0 - w) * epA[k] + w * epB[k];
                                }

                                endpoint_cache.cached_i0 = bracket.i0;
                                endpoint_cache.cached_i1 = bracket.i1;
                                endpoint_cache.endpoint_i0 = std::move(epA);
                                endpoint_cache.endpoint_i1 = std::move(epB);
                                endpoint_cache.built_field_nlev = field_nlev;
                                endpoint_cache.built_nx = nx_;
                                endpoint_cache.built_ny = band_.ny_local;
                                endpoint_cache.valid = true;

                                // Surface the destination-grid blend to the caller,
                                // then write it back through the same core-import
                                // shape gate the other tiers use.
                                ingest_buffer = std::move(blended);
                                read_success = WriteBandToImport(var_name, ingest_buffer, field_nlev, cece_core_data_ptr, failure_detail);
                                if (read_success) {
                                    // Refresh the slice cache so an immediate exact
                                    // repeat (same indices AND weight) re-hits Tier 1
                                    // (Req 3.3, 9.4). Band-sized.
                                    slice_cache.last_bracket = bracket;
                                    slice_cache.ingest_buffer = ingest_buffer;
                                    slice_cache.ingest_size = static_cast<size_t>(field_nlev) * nx_ * band_.ny_local;
                                    slice_cache.valid = true;
                                }
                            } else {
                                // A regrid failed on this refresh: invalidate the
                                // endpoint entry so no stale endpoint is reused and
                                // leave read_success false. The failure surfaces via
                                // failure_detail and the trailing collective_all_ready
                                // on read_success (Req 6.4).
                                endpoint_cache.valid = false;
                            }
                        }
                    } else if (have_data) {
                        // ---- Tier 3 single-record sub-case (unchanged) ----
                        // Reached when !needs_upper_record and the collective-agreed
                        // "lower AMIO slab readiness" guard read record bracket.i0
                        // into src. No temporal interpolation, so there is no upper
                        // endpoint and nothing to cache for interpolation reuse: the
                        // endpoint cache is intentionally NOT populated here. Regrid
                        // the single record through the band assembly path exactly as
                        // the pre-cache code did (Req 2.5, 5.2).
                        read_success = AssembleBandField(var_name, plan, src, file_nx, file_ny, field_nlev, stream_view, cece_core_data_ptr,
                                                         ingest_buffer, failure_detail);
                        if (read_success) {
                            // Refresh the slice cache with the freshly computed
                            // ingest buffer and the bracket that produced it, so a
                            // later step resolving the same bracket can reuse it
                            // (Req 3.3, 9.4). The band buffer is sized
                            // field_nlev * nx_ * ny_local.
                            slice_cache.last_bracket = bracket;
                            slice_cache.ingest_buffer = ingest_buffer;
                            slice_cache.ingest_size = static_cast<size_t>(field_nlev) * nx_ * band_.ny_local;
                            slice_cache.valid = true;
                        }
                    }
                }  // end Tier 3 (cache-miss / rollover / single-record) branch
            }
            read_success = collective_all_ready(halo_comm_ ? &*halo_comm_ : nullptr, comm_c_, read_success,
                                                "band field assembly for '" + var_name + "'", failure_detail);
            // The AMIO handle set persists in amio_handles_ across timesteps
            // (Req 2.2, 9.1); it is closed/finalized only in the destructor
            // (task 10.1). No per-step amio_close/amio_finalize, no manifest
            // write/delete, and no per-step MPI_Barrier occur here (Req 9.1, 9.2).
        }

        // Throw a fatal error on AMIO read failures
        if (!read_success) {
            std::string detail = failure_detail.empty() ? "AMIO open/read failed (no detail)" : failure_detail;
            CECE_LOG_ERROR("[FATAL ERROR] AMIO read failed for field '" + var_name + "' in file '" + input_file_path + "'. Reason: " + detail +
                           ". Idealized fallback is disabled!");
            return false;
        } else {
            CECE_LOG_INFO("[DRIVER] AMIO read succeeded for field '" + var_name + "' - loaded real data from " + input_file_path);
        }

        const size_t expected_ingest_size = static_cast<size_t>(field_nlev) * nx_ * band_.ny_local;
        const bool local_ingest_size_ready = ingest_buffer.size() == expected_ingest_size;
        if (!local_ingest_size_ready) {
            failure_detail = "internal ingest buffer size mismatch for field '" + var_name + "'";
        }
        if (!collective_all_ready(halo_comm_ ? &*halo_comm_ : nullptr, comm_c_, local_ingest_size_ready,
                                  "ingest-buffer readiness for '" + var_name + "'", failure_detail)) {
            CECE_LOG_ERROR("[DRIVER FATAL] " + failure_detail);
            return false;
        }

        // Ingest-copy consolidation (Req 3.1, 3.2, 3.4, 5.3): the driver facade
        // already wrote import_state.fields[var_name] directly and authoritatively
        // in AssembleBandField, so the legacy ingestor round-trip here is
        // redundant. The `cece_ingestor_set_field` call (which populated the
        // separate field_cache_ for AMIO variables) and its guarding
        // `CECE ingestor readiness` collective gate were removed together as a
        // UNIT — the gate only guarded the now-deleted call, and it is removed
        // identically on every rank so no rank waits on a collective a peer
        // skipped. The surrounding `ingest-buffer readiness` gate above is
        // retained: it validates the assembled buffer and must still be reached
        // by every rank in the same order. `ingest_buffer` is still produced
        // above because the slice cache stores its own copy in AdvanceTime;
        // only the `cece_ingestor_set_field` consumer is removed here.
        //
        // Removing the SetField population of field_cache_ for AMIO variables
        // also naturally neutralizes IngestEmissionsInline's copy-back for those
        // fields: its HasCachedField(...) check now returns false, so it skips
        // them. No edit to IngestEmissionsInline itself is required.
        CECE_LOG_INFO("[DRIVER] Ingested field '" + var_name + "' with band shape " + std::to_string(nx_) + "x" + std::to_string(band_.ny_local) +
                      "x" + std::to_string(field_nlev));
    }

    step_index_++;
    return true;
}

}  // namespace cece

extern "C" {
void amio_set_parent_communicator(MPI_Fint comm);

void cece_driver_create(const char* yaml_path, int path_len, int nx, int ny, int nz, const double* lon_coords, int lon_len, const double* lat_coords,
                        int lat_len, int mpi_comm_f, void** driver_ptr_out, int* rc) {
    if (rc) *rc = 0;
    try {
        std::string path(yaml_path, path_len);

        // 1. Pass custom parent communicator to AMIO
        amio_set_parent_communicator(static_cast<MPI_Fint>(mpi_comm_f));

        // 2. Convert Fortran MPI handle to C MPI_Comm
        MPI_Comm comm_c = MPI_Comm_f2c(static_cast<MPI_Fint>(mpi_comm_f));

        // 3. Create orchestrator using the custom communicator
        auto* driver = new cece::CeceDriverOrchestrator(path, nx, ny, nz, lon_coords, lon_len, lat_coords, lat_len, comm_c);
        *driver_ptr_out = static_cast<void*>(driver);
    } catch (const std::exception& e) {
        CECE_LOG_ERROR(std::string("cece_driver_create: ") + e.what());
        if (rc) *rc = -1;
    }
}

void cece_driver_advance_time(void* driver_ptr, const char* time_iso8601, int time_len, void* cece_core_data_ptr, int* rc) {
    if (rc) *rc = 0;
    try {
        auto* driver = static_cast<cece::CeceDriverOrchestrator*>(driver_ptr);
        std::string t_iso(time_iso8601, time_len);
        bool ok = driver->AdvanceTime(t_iso, cece_core_data_ptr);
        if (!ok && rc) *rc = -1;
    } catch (const std::exception& e) {
        CECE_LOG_ERROR(std::string("cece_driver_advance_time: ") + e.what());
        if (rc) *rc = -1;
    }
}

extern std::unique_ptr<cece::CeceStandaloneWriter> g_standalone_writer;

void cece_driver_destroy(void* driver_ptr) {
    if (driver_ptr) {
        delete static_cast<cece::CeceDriverOrchestrator*>(driver_ptr);
    }
    g_standalone_writer.reset();
}

}  // extern "C"
