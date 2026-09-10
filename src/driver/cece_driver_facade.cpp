#include "cece/cece_driver_facade.hpp"

#include <amio/amio.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <axis/axis.hpp>
#include <cmath>
#include <conf/conf.hpp>
#include <cstdint>
#include <dagr/logging.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cece/cece_amio_utils.hpp"
#include "cece/cece_fatal.hpp"
#include "cece/cece_helm_graph.hpp"
#include "cece/cece_internal.hpp"
#include "cece/cece_logger.hpp"
#include "cece/cece_regridder_utils.hpp"
#include "cece/cece_standalone_writer.hpp"
#include "cece/cece_string_utils.hpp"
#include "cece/cece_time_indexing.hpp"

namespace fs = std::filesystem;

extern "C" {
void cece_ingestor_set_field(void* data_ptr, const char* field_name, int name_len, const double* field_data, int n_lev, int n_elem, int* rc);
void amio_set_parent_communicator(MPI_Fint comm);
}

namespace cece {

using namespace detail;

CeceDriverOrchestrator::CeceDriverOrchestrator(const std::string& config_file, int nx, int ny, int nz, const double* lon_coords, int lon_len,
                                               const double* lat_coords, int lat_len, MPI_Comm comm_c)
    : config_file_(config_file),
      nx_(nx),
      ny_(ny),
      nz_(nz),
      target_lons_(lon_coords, lon_coords + lon_len),
      target_lats_(lat_coords, lat_coords + lat_len),
      comm_c_(comm_c) {
    // Configure the CECE logger with this driver's communicator so rank
    // filtering is correct when running under a non-COMM_WORLD split.
    {
        int mpi_init = 0;
        MPI_Initialized(&mpi_init);
        if (mpi_init && comm_c_ != MPI_COMM_NULL) {
            cece::CeceLogger::GetInstance().ConfigureCommunicator(comm_c_);
        }
    }

    // Parse config once at construction using HELM CONF
    try {
        conf::Config cfg = conf::Config::from_file(config_file_);
        gridspec_file_ = cfg.get_or<std::string>("driver.gridspec_file", "");

        int amio_threads = cfg.get_or("driver.amio_worker_threads", 1);
        if (amio_threads < 1) {
            throw std::invalid_argument("driver.amio_worker_threads must be >= 1; got " + std::to_string(amio_threads) + ".");
        }
        int amio_staging_buffer_count = cfg.get_or("driver.amio_staging_buffer_count", 8);
        if (amio_staging_buffer_count < 1) {
            throw std::invalid_argument("driver.amio_staging_buffer_count must be >= 1; got " + std::to_string(amio_staging_buffer_count) + ".");
        }

        // Cache per-variable stream configuration
        if (cfg.has("cece_data.streams")) {
            conf::Value streams = cfg.at("cece_data.streams");
            for (std::size_t si = 0; si < streams.size(); ++si) {
                conf::Value stream = streams[si];
                std::string stream_file = stream["file"].string_or("");
                std::string stream_mapalgo = stream["mapalgo"].string_or("consd");
                std::string stream_cadence = stream["cadence"].string_or("");
                int stream_year_first = stream["yearFirst"].int_or(0);
                int stream_year_last = stream["yearLast"].int_or(0);
                int stream_year_align = stream["yearAlign"].int_or(0);
                std::string stream_taxmode = stream["taxmode"].string_or("");
                std::string stream_tintalgo = stream["tintalgo"].string_or("nearest");
                std::string stream_time_var = stream["time_var"].string_or("time");
                std::string stream_time_units = stream["time_units"].string_or("");
                std::string stream_calendar = stream["calendar"].string_or("");

                // Validate the cadence and warn on knobs that the chosen cadence ignores.
                validate_stream_temporal_config(stream_cadence, stream_taxmode, stream_tintalgo, stream_year_first, stream_year_last,
                                                stream_year_align, " (stream file '" + stream_file + "')");

                // Parse data_model
                std::string data_model = "enhanced";
                bool data_model_explicit = false;
                conf::Value dm_val = stream["data_model"];
                if (dm_val.is_defined()) {
                    const std::string requested_model = to_lower(dm_val.as_string());
                    if (requested_model == "classic" || requested_model == "enhanced") {
                        data_model = requested_model;
                        data_model_explicit = true;
                    } else if (requested_model != "auto") {
                        CECE_LOG_WARNING("[DRIVER] Invalid stream data_model='" + requested_model +
                                         "'; using default auto behavior (enhanced then classic fallback).");
                    }
                }

                // Map each variable in this stream to its config
                conf::Value variables = stream["variables"];
                for (std::size_t vi = 0; vi < variables.size(); ++vi) {
                    conf::Value var = variables[vi];
                    std::string model_name = var["model"].string_or("");
                    if (model_name.empty()) continue;

                    StreamVarConfig svc;
                    svc.input_file_path = stream_file;
                    svc.input_var_name = var["file"].string_or(model_name);
                    svc.mapalgo = stream_mapalgo;
                    svc.cadence = stream_cadence;
                    svc.yearFirst = stream_year_first;
                    svc.yearLast = stream_year_last;
                    svc.yearAlign = stream_year_align;
                    svc.taxmode = stream_taxmode;
                    svc.tintalgo = stream_tintalgo;
                    svc.time_var = stream_time_var;
                    svc.time_units = stream_time_units;
                    svc.calendar = stream_calendar;
                    svc.data_model = data_model;
                    svc.data_model_explicit = data_model_explicit;
                    svc.amio_threads = amio_threads;
                    svc.amio_staging_buffer_count = amio_staging_buffer_count;
                    stream_var_configs_[model_name] = svc;
                }
            }
        }
    } catch (const conf::Conf_Error& e) {
        CECE_LOG_ERROR("[DRIVER] Failed to parse config file '" + config_file_ + "': " + e.what());
        throw;
    }

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
    // Parse the current simulation datetime once. Every cadence except
    // 'stepwise' uses these calendar fields to select the correct file record.
    const SimDateTime sim_dt = parse_sim_datetime(time_iso8601);

    // B. Push CeceIO's newly computed emission views into CECE's data ingestor
    for (const auto& var_name : cece_io_->GetOutputVarNames()) {
        auto stream_view = cece_io_->GetFieldView(var_name);

        // Use cached stream configuration (parsed once at construction)
        std::string input_file_path = "";
        std::string input_var_name = "";
        std::string mapalgo = "consd";
        std::string stream_data_model = "enhanced";
        std::string cadence;
        int yearFirst = 0;
        int yearLast = 0;
        int yearAlign = 0;
        std::string taxmode;
        std::string tintalgo = "nearest";
        std::string time_var = "time";
        std::string time_units;
        std::string calendar;
        bool stream_data_model_explicit = false;
        int amio_threads = 1;
        int amio_staging_buffer_count = 8;

        auto cfg_it = stream_var_configs_.find(var_name);
        if (cfg_it != stream_var_configs_.end()) {
            const StreamVarConfig& svc = cfg_it->second;
            input_file_path = svc.input_file_path;
            input_var_name = svc.input_var_name;
            mapalgo = svc.mapalgo;
            cadence = svc.cadence;
            yearFirst = svc.yearFirst;
            yearLast = svc.yearLast;
            yearAlign = svc.yearAlign;
            taxmode = svc.taxmode;
            tintalgo = svc.tintalgo;
            time_var = svc.time_var;
            time_units = svc.time_units;
            calendar = svc.calendar;
            stream_data_model = svc.data_model;
            stream_data_model_explicit = svc.data_model_explicit;
            amio_threads = svc.amio_threads;
            amio_staging_buffer_count = svc.amio_staging_buffer_count;
        }

        if (input_file_path.empty()) {
            LogFatal("[DRIVER FATAL] Input file path not specified for stream variable '" + var_name + "' in configuration!");
            return false;
        }
        if (input_var_name.empty()) {
            input_var_name = var_name;
        }

        // Verify if the input file path exists and is accessible from this compute/login node
        std::error_code fs_ec;
        if (!fs::exists(input_file_path, fs_ec)) {
            LogFatal("[DRIVER FATAL] File '" + input_file_path + "' does not exist or is unreadable on this node! (System error: " + fs_ec.message() +
                     ")");
        } else {
            CECE_LOG_DEBUG("[DRIVER] Input file '" + input_file_path + "' successfully verified on local filesystem.");
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
        for (const auto& candidate_model : data_models_to_try) {
            active_data_model = candidate_model;

            if (rank == 0) {
                // Write input manifest YAML (Rank 0 only to prevent parallel write conflicts)
                std::ofstream m_file(read_manifest_path);
                if (!m_file) {
                    LogFatal("[DRIVER FATAL] Failed to create AMIO manifest YAML file '" + read_manifest_path + "'");
                    return false;
                }
                m_file << "backend: netcdf4\n"
                       << "path: " << input_file_path << "\n"
                       << "data_model: " << candidate_model << "\n"
                       << "staging_pool:\n"
                       << "  buffer_count: " << amio_staging_buffer_count << "\n"
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
                int barrier_rc = MPI_Barrier(comm_c_);
                if (barrier_rc != MPI_SUCCESS) {
                    CECE_LOG_WARNING("[DRIVER] MPI_Barrier failed with error code " + std::to_string(barrier_rc));
                }
            }

            // Force serial I/O fallback for reading offline datasets to prevent MPI multithreading deadlocks.
            if (mpi_initialized) {
                amio_set_parent_communicator(MPI_Comm_c2f(MPI_COMM_SELF));
            }

            amio_rc = amio_init(read_manifest_path.c_str(), &read_core);
            if (amio_rc != AMIO_OK) {
                failure_detail = std::string("amio_init failed for manifest '") + read_manifest_path + "': rc=" + std::to_string(amio_rc) + " (" +
                                 amio_strerror(amio_rc) + ")";
            } else {
                amio_rc = amio_open_dataset(read_core, read_manifest_path.c_str(), AMIO_MODE_READ, &read_dataset);
                if (amio_rc != AMIO_OK) {
                    failure_detail = std::string("amio_open_dataset failed for '") + input_file_path + "': rc=" + std::to_string(amio_rc) + " (" +
                                     amio_strerror(amio_rc) + ")";
                }
            }

            // Restore parent communicator for downstream operations.
            if (mpi_initialized && comm_c_ != MPI_COMM_NULL) {
                amio_set_parent_communicator(MPI_Comm_c2f(comm_c_));
            }

            if (amio_rc == AMIO_OK) {
                break;
            }

            CECE_LOG_DEBUG("[DRIVER] AMIO open attempt failed (data_model='" + candidate_model + "') with rc = " + std::to_string(amio_rc) + " (" +
                           amio_strerror(amio_rc) + ")");

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
            CECE_LOG_DEBUG("[DRIVER] amio_open_dataset failed for " + input_file_path + " with rc = " + std::to_string(amio_rc) + " (" +
                           amio_strerror(amio_rc) + ") after trying data_model='" + active_data_model + "'");
        } else {
            if (!stream_data_model_explicit && active_data_model != "enhanced") {
                CECE_LOG_INFO("[DRIVER] AMIO read manifest auto-fell back to data_model='" + active_data_model + "' for " + input_file_path);
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
                if (!cece::io::build_regrid_plan(read_dataset, nx_, ny_, target_lons_, target_lats_, mapalgo, j0, j1, gridspec_file_, plan)) {
                    CECE_LOG_DEBUG("[DRIVER] build_regrid_plan failed for '" + var_name + "'");
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
            //    The record bracket comes from the stream's cadence kind:
            //      - series (default) -> decode the file's time axis; degrade to
            //        arithmetic for daily/monthly when it can't be decoded
            //      - hourly / weekly  -> nearest discrete profile record
            //      - stepwise         -> opt-in step-index cycling (ignores time)
            if (plan_it != regrid_plans_.end() && plan_it->second.built) {
                const cece::io::RegridPlan& plan = plan_it->second;

                RecordBracket bracket;

                // Dispatch on cadence kind: Series decodes the file's time axis
                // (degrading to arithmetic only for daily/monthly), Profile indexes a
                // calendar field, Stepwise walks the record index (ignores time).
                const CadenceKind kind = classify_cadence(cadence);
                const std::string c_lower = to_lower(cadence);
                std::string bracket_note;

                if (kind == CadenceKind::Stepwise) {
                    const int t_idx = (file_nt > 0) ? (step_index_ % file_nt) : 0;
                    bracket.i0 = bracket.i1 = t_idx;
                    bracket.weight = 0.0;
                    bracket.valid = true;
                    bracket_note = "stepwise (time ignored)";
                } else if (kind == CadenceKind::Profile) {
                    bracket = bracket_from_cadence(cadence, tintalgo, sim_dt, file_nt, yearFirst, yearLast, yearAlign, taxmode);
                    bracket_note = "profile:" + c_lower;
                } else {  // Series
                    if (file_nt > 1) {
                        bracket = bracket_from_dataset(read_dataset, time_var, sim_dt, file_nt, tintalgo, yearAlign, taxmode, time_units, calendar);
                    }
                    if (bracket.valid) {
                        bracket_note = "decoded axis";
                    } else if (bracket.out_of_range) {
                        // The axis decoded; taxmode 'limit' rejected the time. Degrading
                        // here would quietly hand back a climatology record instead.
                        bracket_note = "decoded axis, out of range";
                    } else if (c_lower == "daily" || c_lower == "monthly") {
                        // Undecodable axis but the cadence carries a granularity: degrade.
                        bracket = bracket_from_cadence(cadence, tintalgo, sim_dt, file_nt, yearFirst, yearLast, yearAlign, taxmode);
                        bracket_note = "degraded arithmetic:" + c_lower;
                    } else if (file_nt == 1) {
                        bracket.i0 = bracket.i1 = 0;
                        bracket.weight = 0.0;
                        bracket.valid = true;
                        bracket_note = "single record";
                    }
                }

                if (!bracket.valid) {
                    amio_close(read_dataset);
                    amio_finalize(read_core);
                    const std::string cadence_note = (cadence.empty() ? std::string("series (default)") : cadence);
                    if (bracket.out_of_range) {
                        LogFatal("[DRIVER FATAL] Simulation time " + time_iso8601 + " is outside the coverage of '" + input_file_path +
                                 "' for field '" + var_name + "' (cadence='" + cadence_note +
                                 "', taxmode='limit'). Use taxmode 'extend' to hold the nearest end or 'cycle' to repeat the file.");
                    } else {
                        LogFatal("[DRIVER FATAL] Could not resolve a time record for field '" + var_name + "' in '" + input_file_path +
                                 "' (cadence='" + cadence_note +
                                 "'): the time axis is not usable (missing/non-fixed units, records out of ascending order, or a degenerate span) "
                                 "and there is no cadence granularity to fall back on. "
                                 "Set 'cadence: stepwise' to ignore time, use 'cadence: daily'/'monthly', or provide 'time_units'.");
                    }
                    return false;
                }

                // Diagnostic: report which time slice(s) are being read and via which path.
                if (bracket.i0 == bracket.i1 || bracket.weight == 0.0) {
                    CECE_LOG_INFO("[DRIVER] Reading time slice " + std::to_string(bracket.i0 + 1) + "/" + std::to_string(file_nt) + " from '" +
                                  input_file_path + "' for field '" + var_name + "' (" + bracket_note + ", time=" + time_iso8601 + ")");
                } else {
                    CECE_LOG_INFO("[DRIVER] Interpolating time slices " + std::to_string(bracket.i0 + 1) + " & " + std::to_string(bracket.i1 + 1) +
                                  "/" + std::to_string(file_nt) + " (w=" + std::to_string(bracket.weight) + ") from '" + input_file_path +
                                  "' for field '" + var_name + "' (" + bracket_note + ", tintalgo=" + tintalgo + ", time=" + time_iso8601 + ")");
                }

                int file_nx = 0;
                int file_ny = 0;

                // CF packing for this variable, read once per step.
                double var_scale = 1.0;
                double var_offset = 0.0;
                read_cf_packing(read_dataset, input_var_name, var_scale, var_offset);

                // Read a single record into a double buffer on the source grid. The
                // AMIO netCDF backend detects the CF time dimension and returns a
                // single [lat, lon] slab, so each read stays at ny*nx elements even
                // for long, high-resolution sub-daily datasets (e.g. CAMS-TEMPO).
                auto read_slab = [&](int t_idx, std::vector<double>& out) -> bool {
                    amio_view_handle slab_view = nullptr;
                    amio_status_t rc = amio_read(read_dataset, input_var_name.c_str(), t_idx, nullptr, &slab_view);
                    if (rc != AMIO_OK) {
                        amio_rc = rc;
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
                    amio_dtype_t slab_dtype = AMIO_DTYPE_F32;
                    if (amio_view_dtype(slab_view, &slab_dtype) != AMIO_OK) {
                        failure_detail = "amio_view_dtype failed";
                        amio_release_view(slab_view);
                        return false;
                    }
                    const std::size_t elem_size = amio_dtype_size(slab_dtype);
                    if (elem_size == 0) {
                        failure_detail = "unsupported element type on variable '" + input_var_name + "'";
                        amio_release_view(slab_view);
                        return false;
                    }
                    const int fny = static_cast<int>(read_shape.extents[read_shape.rank - 2]);
                    const int fnx = static_cast<int>(read_shape.extents[read_shape.rank - 1]);
                    size_t total_elements = 1;
                    for (int d = 0; d < read_shape.rank; ++d) {
                        total_elements *= read_shape.extents[d];
                    }
                    const size_t spatial = static_cast<size_t>(fny) * fnx;
                    // Normally the view holds a single slab (offset 0). Stay robust to
                    // a backend that returns the whole variable.
                    const size_t slices_in_view = (spatial > 0) ? (total_elements / spatial) : 1;
                    const size_t off = (slices_in_view > 1) ? static_cast<size_t>(t_idx) * spatial : 0;
                    if (view_size < (off + spatial) * elem_size) {
                        failure_detail = "view payload smaller than the requested slab";
                        amio_release_view(slab_view);
                        return false;
                    }
                    const void* slab_start = static_cast<const char*>(view_data) + off * elem_size;
                    if (!widen_amio_elements(slab_start, slab_dtype, spatial, var_scale, var_offset, out)) {
                        failure_detail = "could not widen element type of variable '" + input_var_name + "'";
                        amio_release_view(slab_view);
                        return false;
                    }
                    file_nx = fnx;
                    file_ny = fny;
                    amio_release_view(slab_view);
                    CECE_LOG_DEBUG("[DRIVER] Read slab t=" + std::to_string(t_idx) + " for '" + input_var_name + "': " + std::to_string(fny) + "x" +
                                   std::to_string(fnx) + " (" + std::to_string(spatial) + " elements, " + std::to_string(elem_size) + "-byte dtype " +
                                   std::to_string(static_cast<int>(slab_dtype)) + ")");
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
                            MPI_Allgatherv(local_dst.data(), counts[mpi_rank], MPI_DOUBLE, full_dst.data(), counts.data(), displs.data(), MPI_DOUBLE,
                                           comm_c_);
                        } else {
                            std::copy(local_dst.begin(), local_dst.end(), full_dst.begin() + static_cast<size_t>(j0) * nx_);
                        }

                        // Populate the CECE field view (i, j, 0) from the full field.
                        auto h_view = Kokkos::create_mirror_view(stream_view);
                        for (int j = 0; j < ny_; ++j) {
                            for (int i = 0; i < nx_; ++i) {
                                h_view(i, j, 0) = full_dst[static_cast<size_t>(j) * nx_ + i];
                            }
                        }
                        Kokkos::deep_copy(stream_view, h_view);

                        // Also directly populate the C++ Core's import state fields to guarantee
                        // parallel-safe and synchronized import states across the driver facade and compute core!
                        auto* d = static_cast<cece::CeceInternalData*>(cece_core_data_ptr);
                        auto it_core = d->import_state.fields.find(var_name);
                        if (it_core == d->import_state.fields.end()) {
                            // Dynamically allocate the import field DualView inside the core
                            cece::DualView3D dv(var_name, nx_, ny_, nz_);
                            d->import_state.fields[var_name] = dv;
                            it_core = d->import_state.fields.find(var_name);
                        }

                        if (it_core != d->import_state.fields.end()) {
                            auto& core_field = it_core->second;
                            auto h_view_core = Kokkos::create_mirror_view(core_field.view_device());
                            for (int j = 0; j < ny_; ++j) {
                                for (int i = 0; i < nx_; ++i) {
                                    h_view_core(i, j, 0) = full_dst[static_cast<size_t>(j) * nx_ + i];
                                }
                            }
                            Kokkos::deep_copy(core_field.view_device(), h_view_core);
                            core_field.modify_device();
                            core_field.sync_host();
                        }

                        read_success = true;
                    } else {
                        CECE_LOG_DEBUG("[DRIVER] apply_regrid_plan returned false!");
                        failure_detail = "regrid weight application failed";
                    }
                }
            }
            amio_close(read_dataset);
        }
        amio_finalize(read_core);

        // Wait for all ranks to finalize their AMIO sessions before deleting the manifest file
        if (mpi_initialized && comm_c_ != MPI_COMM_NULL) {
            int barrier_rc = MPI_Barrier(comm_c_);
            if (barrier_rc != MPI_SUCCESS) {
                CECE_LOG_WARNING("[DRIVER] MPI_Barrier failed with error code " + std::to_string(barrier_rc));
            }
        }
        if (rank == 0) {
            std::error_code rm_ec;
            fs::remove(read_manifest_path, rm_ec);
            if (rm_ec) {
                CECE_LOG_WARNING("[DRIVER] Failed to remove manifest file '" + read_manifest_path + "': " + rm_ec.message());
            }
        }

        // Throw a fatal error on AMIO read failures
        if (!read_success) {
            std::string detail =
                failure_detail.empty() ? ("open/init failed: rc=" + std::to_string(amio_rc) + " (" + amio_strerror(amio_rc) + ")") : failure_detail;
            LogFatal("[FATAL ERROR] AMIO read failed for field '" + var_name + "' in file '" + input_file_path + "'. Reason: " + detail +
                     ". Idealized fallback is disabled!");
            return false;
        } else {
            CECE_LOG_INFO("[DRIVER] AMIO read succeeded for field '" + var_name + "' - loaded real data from " + input_file_path);
        }

        // Ingest raw data pointer of stream view into CECE's ingestor cache
        int bridge_rc = 0;
        cece_ingestor_set_field(cece_core_data_ptr, var_name.c_str(), static_cast<int>(var_name.length()), stream_view.data(),
                                nz_,        // n_lev
                                nx_ * ny_,  // n_elem
                                &bridge_rc);
        if (bridge_rc != 0) {
            LogFatal("[DRIVER FATAL] cece_ingestor_set_field failed for variable '" + var_name + "' with rc=" + std::to_string(bridge_rc));
            return false;
        }
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
