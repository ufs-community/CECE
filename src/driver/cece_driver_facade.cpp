#include "cece/cece_driver_facade.hpp"

#include <amio/amio.h>
#include <yaml-cpp/yaml.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <axis/axis.hpp>
#include <dagr/logging.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <tick/tick.hpp>
#include <vector>

#include "cece/cece_fatal.hpp"
#include "cece/cece_helm_graph.hpp"
#include "cece/cece_internal.hpp"
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
    int month = 0;         ///< 1-12
    int day = 0;           ///< 1-31
    int hour = 0;          ///< 0-23
    int day_of_week = 0;   ///< 0=Sunday .. 6=Saturday
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

/**
 * @brief A pair of file records that bracket the current simulation time, plus a
 *        blend weight for linear temporal interpolation.
 *
 * @c weight is the fraction toward @c i1: the interpolated field is
 * @f$ (1-w)\,\mathrm{rec}[i_0] + w\,\mathrm{rec}[i_1] @f$. When @c weight is 0
 * (or @c i0 == @c i1) a single read of @c i0 suffices.
 */
struct RecordBracket {
    int i0 = 0;
    int i1 = 0;
    double weight = 0.0;
    bool valid = false;  ///< false -> caller falls back to legacy step-index cycling.
};

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

}  // namespace

CeceDriverOrchestrator::CeceDriverOrchestrator(const std::string& config_file, int nx, int ny, int nz, const double* lon_coords,
                                               const double* lat_coords, MPI_Comm comm_c)
    : config_file_(config_file),
      nx_(nx),
      ny_(ny),
      nz_(nz),
      target_lons_(lon_coords, lon_coords + nx),
      target_lats_(lat_coords, lat_coords + ny),
      comm_c_(comm_c) {
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
        dagr::configure_logging(comm_c_ != MPI_COMM_NULL ? comm_c_ : MPI_COMM_WORLD,
                                rank == 0 ? dagr::Log_Level::info : dagr::Log_Level::error);
    }
}

CeceDriverOrchestrator::~CeceDriverOrchestrator() {
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

bool CeceDriverOrchestrator::AdvanceTime(const std::string& time_iso8601, void* cece_core_data_ptr) {
    if (!cece_core_data_ptr) return false;

    // A. Advance the pipeline step
    dagr_->advance_step();
    Kokkos::fence();

    // Load full config to parse streams
    YAML::Node config = YAML::LoadFile(config_file_);

    // Parse the current simulation datetime once. Streams that declare a
    // temporal cadence (hourly/weekly/monthly) use these calendar fields to
    // select the correct file record; streams without a cadence keep the
    // legacy step-index cycling behaviour and ignore this.
    const SimDateTime sim_dt = parse_sim_datetime(time_iso8601);

    // B. Push CeceIO's newly computed emission views into CECE's data ingestor
    for (const auto& var_name : cece_io_->GetOutputVarNames()) {
        auto tide_view = cece_io_->GetFieldView(var_name);

        // Parse input file path and variable name dynamically from YAML config cece_data block
        std::string input_file_path = "../scripts/data/MACCity_4x5.nc";  // default fallback
        std::string input_var_name = "MACCity";                          // default fallback
        std::string mapalgo = "consd";                                   // default fallback
        std::string stream_data_model = "enhanced";                      // default AMIO data model
        std::string cadence;                                             // temporal cadence: hourly|weekly|monthly ("" -> legacy cycling)
        std::string tintalgo = "nearest";                                // time-interp algorithm: linear|nearest
        bool stream_data_model_explicit = false;
        if (config["cece_data"] && config["cece_data"]["streams"]) {
            for (const auto& stream : config["cece_data"]["streams"]) {
                bool found_var = false;
                for (const auto& var : stream["variables"]) {
                    if (var["model"] && var["model"].as<std::string>() == var_name) {
                        if (stream["file"]) {
                            input_file_path = stream["file"].as<std::string>();
                        }
                        if (var["file"]) {
                            input_var_name = var["file"].as<std::string>();
                        }
                        if (stream["mapalgo"]) {
                            mapalgo = stream["mapalgo"].as<std::string>();
                        }
                        if (stream["cadence"]) {
                            cadence = stream["cadence"].as<std::string>();
                        }
                        if (stream["tintalgo"]) {
                            tintalgo = stream["tintalgo"].as<std::string>();
                        }
                        if (stream["data_model"]) {
                            std::string requested_model = stream["data_model"].as<std::string>();
                            std::transform(requested_model.begin(), requested_model.end(), requested_model.begin(),
                                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                            if (requested_model == "classic" || requested_model == "enhanced") {
                                stream_data_model = requested_model;
                                stream_data_model_explicit = true;
                            } else if (requested_model == "auto") {
                                stream_data_model = "enhanced";
                                stream_data_model_explicit = false;
                            } else {
                                std::cout << "[DRIVER WARNING] Invalid stream data_model='" << requested_model
                                          << "' for stream variable '" << var_name
                                          << "'; using default auto behavior (enhanced then classic fallback)." << std::endl;
                            }
                        }
                        found_var = true;
                        break;
                    }
                }
                if (found_var) break;
            }
        }

        // Verify if the input file path exists and is accessible from this compute/login node
        std::error_code fs_ec;
        if (!fs::exists(input_file_path, fs_ec)) {
            LogFatal("[DRIVER FATAL] File '" + input_file_path +
                     "' does not exist or is unreadable on this node! (System error: " + fs_ec.message() + ")");
        } else {
            std::cout << "[DRIVER DEBUG] Input file '" << input_file_path << "' successfully verified on local filesystem." << std::endl;
        }

        bool read_success = false;
        // Human-readable reason for the most recent read failure, propagated to
        // the fatal error message so the underlying AMIO status reaches CECE.
        std::string failure_detail;

        // Dynamically open and read using AMIO API
        std::string read_manifest_path = "amio_read_manifest_facade_" + var_name + ".yaml";

        int rank = 0;
        int mpi_initialized = 0;
        MPI_Initialized(&mpi_initialized);
        if (mpi_initialized && comm_c_ != MPI_COMM_NULL) {
            MPI_Comm_rank(comm_c_, &rank);
        }

        amio_core_handle read_core = nullptr;
        amio_dataset_handle read_dataset = nullptr;

        std::vector<std::string> data_models_to_try;
        if (stream_data_model_explicit) {
            data_models_to_try.push_back(stream_data_model);
        } else {
            data_models_to_try.push_back("enhanced");
            data_models_to_try.push_back("classic");
        }

        amio_status_t amio_rc = AMIO_ERR_BACKEND_FAILURE;
        std::string active_data_model = data_models_to_try.front();

        int amio_threads = 1;
        if (config["driver"] && config["driver"]["amio_worker_threads"]) {
            amio_threads = config["driver"]["amio_worker_threads"].as<int>();
            if (amio_threads < 1) {
                amio_threads = 1;
            }
        }

        for (const auto& candidate_model : data_models_to_try) {
            active_data_model = candidate_model;

            if (rank == 0) {
                // Write input manifest YAML (Rank 0 only to prevent parallel write conflicts)
                std::ofstream m_file(read_manifest_path);
                m_file << "backend: netcdf4\n"
                       << "path: " << input_file_path << "\n"
                       << "data_model: " << candidate_model << "\n"
                       << "staging_pool:\n"
                       << "  buffer_count: 8\n"
                       << "  buffer_capacity_bytes: 268435456\n"
                       << "worker_pool:\n"
                       << "  threads: " << amio_threads << "\n"
                       << "prefetch:\n"
                       << "  depth: 2\n"
                       << "  read_timeout_s: 120\n"
                       << "staging_timeout_ms: 30000\n";
                m_file.close();
            }

            // Wait for Rank 0 to finish writing the manifest before other ranks load it.
            if (mpi_initialized && comm_c_ != MPI_COMM_NULL) {
                MPI_Barrier(comm_c_);
            }

            // Temporarily force serial nc_open read fallback to improve portability.
            if (mpi_initialized) {
                amio_set_parent_communicator(MPI_Comm_c2f(MPI_COMM_SELF));
            }

            amio_rc = amio_init(read_manifest_path.c_str(), &read_core);
            if (amio_rc == AMIO_OK) {
                amio_rc = amio_open_dataset(read_core, read_manifest_path.c_str(), AMIO_MODE_READ, &read_dataset);
            }

            // Restore parent communicator for downstream operations.
            if (mpi_initialized && comm_c_ != MPI_COMM_NULL) {
                amio_set_parent_communicator(MPI_Comm_c2f(comm_c_));
            }

            if (amio_rc == AMIO_OK) {
                break;
            }

            std::cout << "[DRIVER DEBUG] AMIO open attempt failed (data_model='" << candidate_model << "') with rc = " << amio_rc << " ("
                      << amio_strerror(amio_rc) << ")" << std::endl;

            if (read_dataset) {
                amio_close(read_dataset);
                read_dataset = nullptr;
            }
            if (read_core) {
                amio_finalize(read_core);
                read_core = nullptr;
            }
        }

        if (amio_rc != AMIO_OK) {
            std::cout << "[DRIVER DEBUG] amio_open_dataset failed for " << input_file_path << " with rc = " << amio_rc << " ("
                      << amio_strerror(amio_rc) << ") after trying data_model='" << active_data_model << "'" << std::endl;
        } else {
            if (!stream_data_model_explicit && active_data_model != "enhanced") {
                std::cout << "[DRIVER INFO] AMIO read manifest auto-fell back to data_model='" << active_data_model << "' for " << input_file_path
                          << std::endl;
            }

            // Determine this rank's contiguous destination latitude band [j0, j1)
            // via a simple block decomposition of the ny_ destination rows.
            int mpi_size = 1;
            int mpi_rank = 0;
            if (mpi_initialized && comm_c_ != MPI_COMM_NULL) {
                MPI_Comm_size(comm_c_, &mpi_size);
                MPI_Comm_rank(comm_c_, &mpi_rank);
            }
            const int band_base = ny_ / mpi_size;
            const int band_rem = ny_ % mpi_size;
            auto band_start = [&](int r) { return r * band_base + std::min(r, band_rem); };
            const int j0 = band_start(mpi_rank);
            const int j1 = band_start(mpi_rank + 1);

            // 1. Determine total timesteps from the input variable.
            //    Since AMIO doesn't expose a public function to query total timesteps,
            //    we use a binary search with amio_read on the input variable to identify
            //    the actual record limit (since reads beyond the record limit return AMIO_ERR_INVALID_INPUT).
            //    We cache the result in file_nt_cache_ to avoid binary search overhead on subsequent steps.
            int file_nt = 1;
            auto nt_it = file_nt_cache_.find(var_name);
            if (nt_it != file_nt_cache_.end()) {
                file_nt = nt_it->second;
            } else {
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
                file_nt_cache_[var_name] = file_nt;
            }

            // 2. Build (or reuse cached) interpolation weights for this rank's band.
            //    Weights depend only on the grids, so they are generated once and
            //    reused for every timestep.
            auto plan_it = regrid_plans_.find(var_name);
            if (plan_it == regrid_plans_.end() || !plan_it->second.built) {
                cece::io::RegridPlan plan;
                if (!cece::io::build_regrid_plan(read_dataset, nx_, ny_, target_lons_, target_lats_, mapalgo, j0, j1, plan)) {
                    std::cout << "[DRIVER DEBUG] build_regrid_plan failed for '" << var_name << "'" << std::endl;
                    failure_detail = "regrid plan construction failed (could not read source grid coordinates)";
                } else {
                    plan_it = regrid_plans_.emplace(var_name, std::move(plan)).first;
                }
            }

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
            if (plan_it != regrid_plans_.end() && plan_it->second.built) {
                const cece::io::RegridPlan& plan = plan_it->second;

                RecordBracket bracket = cadence_record_bracket(cadence, tintalgo, sim_dt, file_nt);
                if (!bracket.valid) {
                    const int t_idx = (file_nt > 0) ? (step_index_ % file_nt) : 0;
                    bracket.i0 = bracket.i1 = t_idx;
                    bracket.weight = 0.0;
                }

                int file_nx = 0;
                int file_ny = 0;

                // Read a single record into a double buffer on the source grid. The
                // AMIO netCDF backend detects the CF time dimension and returns a
                // single [lat, lon] slab, so each read stays at ny*nx elements even
                // for long, high-resolution sub-daily datasets (e.g. CAMS-TEMPO).
                auto read_slab = [&](int t_idx, std::vector<double>& out) -> bool {
                    amio_view_handle slab_view = nullptr;
                    amio_status_t rc = amio_read(read_dataset, input_var_name.c_str(), t_idx, nullptr, &slab_view);
                    if (rc != AMIO_OK) {
                        amio_rc = rc;
                        std::cout << "[DRIVER DEBUG] amio_read('" << input_var_name << "', t=" << t_idx << ") failed with rc = " << rc << std::endl;
                        failure_detail = std::string("amio_read('") + input_var_name + "') failed: rc=" + std::to_string(rc) + " (" +
                                         amio_strerror(rc) + ")";
                        return false;
                    }
                    const void* view_data = nullptr;
                    size_t view_size = 0;
                    rc = amio_view_data(slab_view, &view_data, &view_size);
                    if (rc != AMIO_OK) {
                        amio_rc = rc;
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
                    const int fny = static_cast<int>(read_shape.extents[read_shape.rank - 2]);
                    const int fnx = static_cast<int>(read_shape.extents[read_shape.rank - 1]);
                    size_t total_elements = 1;
                    for (int d = 0; d < read_shape.rank; ++d) {
                        total_elements *= read_shape.extents[d];
                    }
                    const bool is_float = (view_size == total_elements * 4);
                    const size_t spatial = static_cast<size_t>(fny) * fnx;
                    // Normally the view holds a single slab (offset 0). Stay robust to
                    // a backend that returns the whole variable.
                    const size_t slices_in_view = (spatial > 0) ? (total_elements / spatial) : 1;
                    const size_t off = (slices_in_view > 1) ? static_cast<size_t>(t_idx) * spatial : 0;
                    out.resize(spatial);
                    if (is_float) {
                        const float* p = static_cast<const float*>(view_data) + off;
                        for (size_t k = 0; k < spatial; ++k) out[k] = static_cast<double>(p[k]);
                    } else {
                        const double* p = static_cast<const double*>(view_data) + off;
                        for (size_t k = 0; k < spatial; ++k) out[k] = p[k];
                    }
                    file_nx = fnx;
                    file_ny = fny;
                    amio_release_view(slab_view);
                    return true;
                };

                // Read the lower record and, when interpolating, the upper record;
                // blend on the source grid with the bracket weight.
                std::vector<double> src;
                bool have_data = read_slab(bracket.i0, src);
                if (have_data && bracket.i1 != bracket.i0 && bracket.weight > 0.0) {
                    std::vector<double> src1;
                    if (read_slab(bracket.i1, src1) && src1.size() == src.size()) {
                        const double w = bracket.weight;
                        for (size_t k = 0; k < src.size(); ++k) {
                            src[k] = (1.0 - w) * src[k] + w * src1[k];
                        }
                    } else {
                        have_data = false;
                    }
                }

                if (have_data) {
                    std::vector<double> local_dst;
                    if (cece::io::apply_regrid_plan(plan, /*time_offset=*/0, /*is_float=*/false, src.data(), file_nx, file_ny, nx_, local_dst)) {
                        // Gather each rank's destination band into the full [nx_*ny_] field.
                        std::vector<double> full_dst(static_cast<size_t>(nx_) * ny_, 0.0);
                        if (mpi_initialized && mpi_size > 1 && comm_c_ != MPI_COMM_NULL) {
                            std::vector<int> counts(mpi_size), displs(mpi_size);
                            for (int r = 0; r < mpi_size; ++r) {
                                counts[r] = (band_start(r + 1) - band_start(r)) * nx_;
                                displs[r] = band_start(r) * nx_;
                            }
                            MPI_Allgatherv(local_dst.data(), counts[mpi_rank], MPI_DOUBLE, full_dst.data(), counts.data(), displs.data(),
                                           MPI_DOUBLE, comm_c_);
                        } else {
                            std::copy(local_dst.begin(), local_dst.end(), full_dst.begin() + static_cast<size_t>(j0) * nx_);
                        }

                        // Populate the CECE field view (i, j, 0) from the full field.
                        auto h_view = Kokkos::create_mirror_view(tide_view);
                        for (int j = 0; j < ny_; ++j) {
                            for (int i = 0; i < nx_; ++i) {
                                h_view(i, j, 0) = full_dst[static_cast<size_t>(j) * nx_ + i];
                            }
                        }
                        Kokkos::deep_copy(tide_view, h_view);
                        read_success = true;
                    } else {
                        std::cout << "[DRIVER DEBUG] apply_regrid_plan returned false!" << std::endl;
                        failure_detail = "regrid weight application failed";
                    }
                }
            }
            amio_close(read_dataset);
        }
        amio_finalize(read_core);

        // Wait for all ranks to finalize their AMIO sessions before deleting the manifest file
        if (mpi_initialized && comm_c_ != MPI_COMM_NULL) {
            MPI_Barrier(comm_c_);
        }
        if (rank == 0) {
            std::remove(read_manifest_path.c_str());
        }

        // Throw a fatal error on AMIO read failures
        if (!read_success) {
            std::string detail = failure_detail.empty() ? ("open/init failed: rc=" + std::to_string(amio_rc) + " (" + amio_strerror(amio_rc) + ")")
                                                        : failure_detail;
            LogFatal("[FATAL ERROR] AMIO read failed for field '" + var_name + "' in file '" + input_file_path + "'. Reason: " + detail +
                     ". Idealized fallback is disabled!");
            return false;
        } else {
            std::cout << "[DRIVER DEBUG] AMIO read succeeded for field '" << var_name << "' - loaded real data from " << input_file_path << "!"
                      << std::endl;
        }

        // Ingest raw data pointer of Tide view into CECE's ingestor cache
        int bridge_rc = 0;
        cece_ingestor_set_field(cece_core_data_ptr, var_name.c_str(), static_cast<int>(var_name.length()), tide_view.data(),
                                nz_,        // n_lev
                                nx_ * ny_,  // n_elem
                                &bridge_rc);
    }

    step_index_++;
    return true;
}

}  // namespace cece

extern "C" {
void amio_set_parent_communicator(MPI_Fint comm);

void cece_driver_create(const char* yaml_path, int path_len, int nx, int ny, int nz, const double* lon_coords, const double* lat_coords,
                        int mpi_comm_f, void** driver_ptr_out, int* rc) {
    if (rc) *rc = 0;
    try {
        std::string path(yaml_path, path_len);

        // 1. Pass custom parent communicator to AMIO
        amio_set_parent_communicator(static_cast<MPI_Fint>(mpi_comm_f));

        // 2. Convert Fortran MPI handle to C MPI_Comm
        MPI_Comm comm_c = MPI_Comm_f2c(static_cast<MPI_Fint>(mpi_comm_f));

        // 3. Create orchestrator using the custom communicator
        auto* driver = new cece::CeceDriverOrchestrator(path, nx, ny, nz, lon_coords, lat_coords, comm_c);
        *driver_ptr_out = static_cast<void*>(driver);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: cece_driver_create: " << e.what() << std::endl;
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
        std::cerr << "ERROR: cece_driver_advance_time: " << e.what() << std::endl;
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
