#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "cece/cece_driver_facade.hpp"
#include "cece/cece_logger.hpp"
#include "cece/cece_mpi_env.hpp"

namespace fs = std::filesystem;

namespace cece {

using namespace detail;

namespace {

/** Write a grid-coordinate vector in the text format consumed by the helper. */
bool write_vector_file(const fs::path& path, const std::vector<double>& values) {
    std::ofstream out(path);
    if (!out) return false;
    out.precision(17);
    for (double value : values) {
        out << value << '\n';
    }
    return true;
}

/** Run the EarthAccess Python helper and return its process status. */
int run_process(const std::vector<std::string>& args) {
    if (args.empty()) return 127;

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const std::string& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    const pid_t child = fork();
    if (child < 0) return errno;
    if (child == 0) {
        execvp(argv.front(), argv.data());
        _exit(127);
    }

    int status = 0;
    pid_t waited = 0;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);

    if (waited < 0) return errno;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return status;
}

/** Resolve the configured EarthAccess helper or a repository-relative default. */
fs::path resolve_earthaccess_helper(const std::string& config_file) {
    if (const char* configured = std::getenv("CECE_EARTHACCESS_HELPER")) {
        if (*configured != '\0') return fs::path(configured);
    }

    const fs::path config_path = fs::absolute(fs::path(config_file));
    try {
        YAML::Node config = YAML::LoadFile(config_file);
        const YAML::Node configured = config["driver"]["earthaccess_helper"];
        if (configured && configured.IsScalar()) {
            fs::path helper_path(configured.as<std::string>());
            if (helper_path.is_relative()) {
                helper_path = config_path.parent_path() / helper_path;
            }
            return helper_path;
        }
    } catch (const YAML::Exception& ex) {
        // The main driver parse reports malformed configuration elsewhere.
        // Continue to conventional helper locations for a useful diagnostic.
        CECE_LOG_DEBUG(std::string("[DRIVER] Could not read earthaccess_helper override: ") + ex.what());
    }

    const fs::path cwd_helper = fs::current_path() / "scripts" / "cece_earthaccess_standalone_ingest.py";
    if (fs::exists(cwd_helper)) return cwd_helper;

    for (fs::path dir = config_path.parent_path(); !dir.empty(); dir = dir.parent_path()) {
        const fs::path candidate = dir / "scripts" / "cece_earthaccess_standalone_ingest.py";
        if (fs::exists(candidate)) return candidate;
        if (dir == dir.root_path()) break;
    }

    return cwd_helper;
}

}  // namespace

/** Return whether the config includes any streams handled by EarthAccess. */
bool CeceDriverOrchestrator::HasEarthAccessStreams(const std::string& config_file) {
    try {
        YAML::Node config = YAML::LoadFile(config_file);
        if (!config["cece_data"] || !config["cece_data"]["streams"]) return false;
        for (const auto& stream : config["cece_data"]["streams"]) {
            if (stream["source"] && stream["source"].as<std::string>() == "earthaccess") {
                return true;
            }
        }
    } catch (const YAML::Exception&) {
        return false;
    }
    return false;
}

/**
 * Ingest EarthAccess fields for one timestep.
 *
 * Rank zero invokes the Python helper unless data has already been staged.
 * Every rank reads the manifest, extracts its latitude band, and writes each
 * imported field through the same CECE import-state path used by AMIO streams.
 */
bool CeceDriverOrchestrator::IngestEarthAccessStreams(const std::string& time_iso8601, void* cece_core_data_ptr) {
    if (!has_earthaccess_streams_) return true;

    int rank = 0;
    if (comm_is_distributed(comm_c_)) {
        MPI_Comm_rank(comm_c_, &rank);
    }

    const char* staged_dir_env = std::getenv("CECE_EARTHACCESS_STAGE_DIR");
    const bool use_staged_earthaccess = staged_dir_env != nullptr && *staged_dir_env != '\0';
    const fs::path work_dir = fs::absolute((use_staged_earthaccess ? fs::path(staged_dir_env) : fs::path(".cece_earthaccess_cache")) /
                                           ("step_" + std::to_string(step_index_)));
    const fs::path lon_file = work_dir / "target_lons.txt";
    const fs::path lat_file = work_dir / "target_lats.txt";
    const fs::path manifest_file = work_dir / "manifest.txt";

    int helper_status = 0;
    if (rank == 0) {
        std::error_code ec;
        if (use_staged_earthaccess) {
            if (!fs::exists(manifest_file)) {
                CECE_LOG_ERROR("[DRIVER] Pre-staged earthaccess manifest not found: " + manifest_file.string());
                helper_status = 1;
            } else {
                CECE_LOG_INFO("[DRIVER] Using pre-staged earthaccess streams from " + work_dir.string());
            }
        } else {
            fs::remove_all(work_dir, ec);
            fs::create_directories(work_dir, ec);
        }
        if (ec) {
            CECE_LOG_ERROR("[DRIVER] Failed to create earthaccess work directory '" + work_dir.string() + "': " + ec.message());
            helper_status = 1;
        } else if (!use_staged_earthaccess && (!write_vector_file(lon_file, target_lons_) || !write_vector_file(lat_file, target_lats_))) {
            CECE_LOG_ERROR("[DRIVER] Failed to write earthaccess target grid coordinate files in '" + work_dir.string() + "'");
            helper_status = 1;
        } else if (!use_staged_earthaccess) {
            const char* python_env = std::getenv("CECE_PYTHON");
            const std::string python = (python_env && *python_env != '\0') ? python_env : "python3";
            const fs::path helper = resolve_earthaccess_helper(config_file_);
            if (!fs::exists(helper)) {
                CECE_LOG_ERROR("[DRIVER] Earthaccess helper not found at '" + helper.string() +
                               "'. Set CECE_EARTHACCESS_HELPER or run from the CECE repository root.");
                helper_status = 1;
            } else {
                CECE_LOG_INFO("[DRIVER] Fetching earthaccess streams for " + time_iso8601);
                helper_status = run_process({python, helper.string(), "--config", config_file_, "--time", time_iso8601, "--output-dir",
                                             work_dir.string(), "--lon-file", lon_file.string(), "--lat-file", lat_file.string()});
                if (helper_status != 0) {
                    CECE_LOG_ERROR("[DRIVER] Earthaccess ingestion helper failed with status " + std::to_string(helper_status));
                }
            }
        }
    }

    if (comm_is_distributed(comm_c_)) {
        MPI_Bcast(&helper_status, 1, MPI_INT, 0, comm_c_);
        MPI_Barrier(comm_c_);
    }
    if (helper_status != 0) return false;

    std::ifstream manifest(manifest_file);
    if (!manifest) {
        CECE_LOG_ERROR("[DRIVER] Earthaccess manifest not found: " + manifest_file.string());
        return false;
    }

    std::string field_name;
    int field_nx = 0;
    int field_ny = 0;
    int field_nz = 0;
    std::string binary_name;
    double min_value = 0.0;
    double max_value = 0.0;
    while (manifest >> field_name >> field_nx >> field_ny >> field_nz >> binary_name >> min_value >> max_value) {
        if (field_nx != nx_ || field_ny != ny_ || field_nz != 1) {
            CECE_LOG_ERROR("[DRIVER] Earthaccess field '" + field_name + "' has shape " + std::to_string(field_nx) + "x" + std::to_string(field_ny) +
                           "x" + std::to_string(field_nz) + ", expected " + std::to_string(nx_) + "x" + std::to_string(ny_) + "x1");
            return false;
        }

        const fs::path binary_path = work_dir / binary_name;
        std::ifstream data_file(binary_path, std::ios::binary);
        if (!data_file) {
            CECE_LOG_ERROR("[DRIVER] Earthaccess field data file not found: " + binary_path.string());
            return false;
        }

        std::vector<double> global_values(static_cast<std::size_t>(nx_) * ny_);
        data_file.read(reinterpret_cast<char*>(global_values.data()), static_cast<std::streamsize>(global_values.size() * sizeof(double)));
        if (data_file.gcount() != static_cast<std::streamsize>(global_values.size() * sizeof(double))) {
            CECE_LOG_ERROR("[DRIVER] Short read from earthaccess field data file: " + binary_path.string());
            return false;
        }

        std::vector<double> band_values(static_cast<std::size_t>(nx_) * band_.ny_local);
        for (int jrel = 0; jrel < band_.ny_local; ++jrel) {
            const int j = band_.j0 + jrel;
            std::copy(global_values.begin() + static_cast<std::size_t>(j) * nx_, global_values.begin() + static_cast<std::size_t>(j + 1) * nx_,
                      band_values.begin() + static_cast<std::size_t>(jrel) * nx_);
        }

        std::string failure_detail;
        if (!WriteBandToImport(field_name, band_values, 1, cece_core_data_ptr, failure_detail)) {
            CECE_LOG_ERROR("[DRIVER] Failed to write earthaccess field '" + field_name + "' into CECE import state: " + failure_detail);
            return false;
        }

        CECE_LOG_INFO("[DRIVER] Earthaccess field '" + field_name + "' ingested for " + time_iso8601 + " (min=" + std::to_string(min_value) +
                      ", max=" + std::to_string(max_value) + ")");
    }

    return true;
}

}  // namespace cece
