/**
 * @file cece_run_info.cpp
 * @brief Shared run-log setup and startup banner for CECE.
 *
 * Provides a single C-linkage entry point, cece_run_log_setup(), used by both
 * the standalone driver and the NUOPC cap so that logging behaves identically
 * regardless of how CECE is launched:
 *   - Optional run-log file (driver.log_file / output.log_file) tee'd from
 *     rank 0's stdout, so `> cece.log` redirection is no longer required.
 *   - Informational stdout suppressed on non-root MPI ranks to keep logs clean.
 *   - A descriptive startup banner (MPI tasks, threads, execution space, grid,
 *     simulation window, species, output fields, and input streams).
 *
 * Fatal errors continue to be routed through cece::LogFatal so they always
 * reach the log on every rank (see cece_fatal.hpp).
 */

#include <mpi.h>
#include <yaml-cpp/yaml.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "cece/cece_fatal.hpp"
#include "cece/cece_log_setup.hpp"

#if defined(__has_include)
#if __has_include(<Kokkos_Core.hpp>)
#include <Kokkos_Core.hpp>
#define CECE_HAVE_KOKKOS 1
#endif
#endif

namespace {

std::string RowLabel(const std::string& label) {
    std::string s = "  " + label;
    while (s.size() < 24) s += ' ';
    s += ": ";
    return s;
}

}  // namespace

extern "C" {

/**
 * @brief Configure run logging and print the startup banner.
 *
 * Safe to call once early in initialization. Determines MPI rank/size from
 * MPI_COMM_WORLD (if MPI is initialized) and reads logging/grid/stream options
 * from the YAML configuration file.
 *
 * @param config_path Path to the YAML control file.
 * @param path_len    Length of config_path (Fortran-friendly); pass 0 to use
 *                    the C string length.
 */
void cece_run_log_setup(const char* config_path, int path_len) {
    std::string config_file = (path_len > 0) ? std::string(config_path, static_cast<std::size_t>(path_len)) : std::string(config_path);
    // Trim any trailing null/whitespace that may arrive from Fortran.
    while (!config_file.empty() && (config_file.back() == '\0' || config_file.back() == ' ')) {
        config_file.pop_back();
    }

    int mpi_initialized = 0;
    MPI_Initialized(&mpi_initialized);
    int my_rank = 0;
    int num_ranks = 1;
    if (mpi_initialized) {
        MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
        MPI_Comm_size(MPI_COMM_WORLD, &num_ranks);
    }

    // Load configuration (best effort; a parse failure should not be fatal here).
    YAML::Node config;
    bool config_ok = false;
    try {
        config = YAML::LoadFile(config_file);
        config_ok = true;
    } catch (const std::exception&) {
        config_ok = false;
    }

    // Determine optional log file path.
    std::string log_file_path;
    if (config_ok) {
        if (config["driver"] && config["driver"]["log_file"]) {
            log_file_path = config["driver"]["log_file"].as<std::string>("");
        } else if (config["output"] && config["output"]["log_file"]) {
            log_file_path = config["output"]["log_file"].as<std::string>("");
        }
    }

    // On rank 0, tee stdout to the log file (if requested).
    //
    // The log stream, tee streambuf, and (non-root) /dev/null sink are
    // intentionally leaked (heap-allocated and never freed). They must outlive
    // the destruction of std::cout's standard stream objects (ios_base::Init)
    // at process exit: because we point std::cout.rdbuf() at the tee/null
    // streambuf, the final flush performed by ios_base::Init::~Init() would
    // otherwise dereference an already-destroyed streambuf and segfault. Since
    // these objects live for the entire process, leaking them is harmless and
    // is the standard idiom for redirecting a standard stream's buffer.
    static bool stdout_redirected = false;
    std::streambuf* const console_buf = std::cout.rdbuf();
    if (!stdout_redirected && my_rank == 0 && !log_file_path.empty()) {
        auto* log_file_stream = new std::ofstream(log_file_path);
        if (log_file_stream->is_open()) {
            auto* tee_buf = new cece::TeeStreambuf(console_buf, log_file_stream->rdbuf());
            std::cout.rdbuf(tee_buf);
            stdout_redirected = true;
        } else {
            std::cerr << "WARNING: Could not open log file '" << log_file_path << "' - continuing with console output only." << std::endl;
            delete log_file_stream;
        }
    }

    // Capture the real stdout (rank-0 tee or console) for fatal-error reporting.
    cece::SetRealStdoutBuf(std::cout.rdbuf());

    // Suppress routine stdout on non-root ranks.
    if (!stdout_redirected && my_rank != 0) {
        auto* cout_null_sink = new std::ofstream("/dev/null");
        std::cout.rdbuf(cout_null_sink->rdbuf());
        stdout_redirected = true;
    }

    // Only rank 0 renders the banner (others are suppressed anyway).
    if (my_rank != 0) {
        return;
    }

    // Gather thread / execution-space info.
    std::string omp_threads = "(all available)";
    if (const char* env = std::getenv("OMP_NUM_THREADS")) {
        omp_threads = env;
    }
    std::string exec_space = "(initialized during setup)";
#ifdef CECE_HAVE_KOKKOS
    if (Kokkos::is_initialized()) {
        exec_space = Kokkos::DefaultExecutionSpace::name();
    }
#endif

    std::cout << "========================================================================\n";
    std::cout << "  CECE - Community Emissions Computing Engine\n";
    std::cout << "========================================================================\n";
    std::cout << RowLabel("Config file") << config_file << "\n";
    if (!log_file_path.empty()) {
        std::cout << RowLabel("Log file") << log_file_path << "\n";
    }
    std::cout << RowLabel("MPI tasks") << num_ranks << "\n";
    std::cout << RowLabel("Threads/task") << omp_threads << "\n";
    std::cout << RowLabel("Execution space") << exec_space << "\n";

    if (config_ok) {
        // Grid.
        if (config["driver"] && config["driver"]["grid"]) {
            auto grid = config["driver"]["grid"];
            std::string grid_name = grid["grid_name"] ? grid["grid_name"].as<std::string>("") : "";
            std::cout << RowLabel("Grid");
            if (!grid_name.empty()) {
                std::cout << grid_name;
            } else {
                std::cout << grid["nx"].as<int>(0) << " x " << grid["ny"].as<int>(0);
            }
            if (grid["nz"]) {
                std::cout << " x " << grid["nz"].as<int>(1) << " (nz)";
            }
            std::cout << "\n";
        }

        // Simulation window.
        if (config["driver"]) {
            auto drv = config["driver"];
            std::string start = drv["start_time"] ? drv["start_time"].as<std::string>("") : "";
            std::string end = drv["end_time"] ? drv["end_time"].as<std::string>("") : "";
            int ts = drv["timestep_seconds"] ? drv["timestep_seconds"].as<int>(0) : 0;
            if (!start.empty() && !end.empty()) {
                std::cout << RowLabel("Simulation window") << start << "  ->  " << end << "\n";
            }
            if (ts > 0) {
                std::cout << RowLabel("Timestep") << ts << " s\n";
            }
        }

        // Species.
        if (config["species"]) {
            std::cout << RowLabel("Species");
            bool first = true;
            for (const auto& sp : config["species"]) {
                if (!first) std::cout << ", ";
                std::cout << sp.first.as<std::string>();
                first = false;
            }
            std::cout << "\n";
        }

        // Output fields. Entries are either a scalar field name or a map
        // with "name" (and optional "attributes").
        if (config["output"] && config["output"]["fields"]) {
            std::cout << RowLabel("Output fields");
            bool first = true;
            for (const auto& f : config["output"]["fields"]) {
                if (!first) std::cout << ", ";
                if (f.IsMap()) {
                    std::cout << (f["name"] ? f["name"].as<std::string>("") : "(unnamed)");
                } else {
                    std::cout << f.as<std::string>("");
                }
                first = false;
            }
            std::cout << "\n";
        }

        // Input streams.
        if (config["cece_data"] && config["cece_data"]["streams"]) {
            auto streams = config["cece_data"]["streams"];
            std::cout << RowLabel("Input streams") << streams.size() << "\n";
            for (const auto& s : streams) {
                std::string name = s["name"] ? s["name"].as<std::string>("") : "(unnamed)";
                std::string file = s["file"] ? s["file"].as<std::string>("") : "";
                std::cout << "      - " << name;
                if (!file.empty()) {
                    std::cout << "  <-  " << file;
                }
                std::cout << "\n";
            }
        }
    } else {
        std::cout << RowLabel("Config") << "(could not be parsed for banner details)\n";
    }

    std::cout << "========================================================================" << std::endl;
}

}  // extern "C"
