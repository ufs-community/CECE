/**
 * @file cece_core_local_time_init.cpp
 * @brief Initialization of the local-time service (feature 001).
 *
 * Mirrors the cece_core_writer_initialize_* precedent: a C-linkage entry point
 * called once from the standalone driver (src/main.cpp) and the NUOPC facade
 * path after grid coordinates exist. It is a no-op when local_time is disabled,
 * so the default (disabled) behavior is byte-identical to a pre-feature run
 * (SC-001). When enabled it decodes the RLE UTC-offset grid exactly once, maps
 * it onto the native band grid, and attaches a read-only LocalTimeService to
 * CeceInternalData. Any decode failure warns once and falls back to a UTC
 * service rather than aborting the run (FR-008, SC-004).
 */

#include <mpi.h>

#include <memory>
#include <string>
#include <vector>

#include "cece/cece_band_decomposition.hpp"
#include "cece/cece_internal.hpp"
#include "cece/cece_local_time.hpp"
#include "cece/cece_logger.hpp"
#include "cece/cece_mpi_env.hpp"
#include "cece/utc_grid.hpp"

namespace {

/// Repository default grid path used when local_time.grid_file is empty.
constexpr const char* kDefaultUtcGridFile = "data/utc_grid_f720r.rle";

}  // namespace

extern "C" {

/**
 * @brief Initialize the local-time service on the CECE core data.
 *
 * @param data_ptr    Pointer to CeceInternalData.
 * @param nx          Grid dimension in X (== lon_len).
 * @param ny          Global grid dimension in Y (== lat_len).
 * @param nz          Grid dimension in Z (unused; kept for writer-precedent symmetry).
 * @param lon_coords  Array of longitude coordinates (size nx), degrees.
 * @param lon_len     Length of lon_coords.
 * @param lat_coords  Array of latitude coordinates (size ny, GLOBAL rows), degrees.
 * @param lat_len     Length of lat_coords.
 * @param mpi_comm_f  Fortran MPI communicator handle (band decomposition).
 * @param rc          0 on success (including disabled / UTC-fallback), -1 on bad args.
 */
void cece_core_local_time_init(void* data_ptr, int nx, int ny, int nz, const double* lon_coords, int lon_len, const double* lat_coords, int lat_len,
                               int mpi_comm_f, int* rc) {
    *rc = 0;
    (void)nz;

    if (data_ptr == nullptr) {
        CECE_LOG_ERROR("cece_core_local_time_init: null data pointer");
        *rc = -1;
        return;
    }
    if (lon_coords == nullptr || lat_coords == nullptr) {
        CECE_LOG_ERROR("cece_core_local_time_init: null coordinate arrays");
        *rc = -1;
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);

    // Disabled (default): no file opened, no allocation, service stays null.
    if (!internal_data->config.local_time.enabled) {
        return;
    }

    // Resolve the MPI communicator for the band decomposition (writer precedent).
    MPI_Comm comm = MPI_COMM_WORLD;
    if (cece::mpi_environment_ready()) {
        MPI_Comm temp = MPI_Comm_f2c(static_cast<MPI_Fint>(mpi_comm_f));
        if (temp != MPI_COMM_NULL) {
            comm = temp;
        }
    }
    const cece::BandDecomposition band = cece::BandDecomposition::compute(ny, comm);

    std::vector<double> lon_vec(lon_coords, lon_coords + lon_len);
    std::vector<double> lat_vec(lat_coords, lat_coords + lat_len);
    const std::int64_t start_epoch = internal_data->clock ? internal_data->clock->StartEpochSeconds() : 0;
    const std::string grid_file =
        internal_data->config.local_time.grid_file.empty() ? kDefaultUtcGridFile : internal_data->config.local_time.grid_file;

    try {
        auto dense = cece::DecodeUtcGridRle(grid_file);
        auto provider = std::make_unique<cece::StaticGridOffsetProvider>(std::move(dense));
        internal_data->local_time = cece::LocalTimeService::Create(std::move(provider), lon_vec, lat_vec, nx, band.j0, band.ny_local, start_epoch);
        CECE_LOG_INFO("[LOCAL_TIME] Local-time service initialized from '" + grid_file + "' (nx=" + std::to_string(nx) +
                      ", band j0=" + std::to_string(band.j0) + ", ny_local=" + std::to_string(band.ny_local) + ")");
    } catch (const std::exception& e) {
        // FR-008 / SC-004: warn ONCE, fall back to UTC everywhere, keep running.
        CECE_LOG_WARNING(std::string("[LOCAL_TIME] Failed to load UTC-offset grid '") + grid_file + "': " + e.what() +
                         " — continuing in UTC mode (offset 0 everywhere).");
        internal_data->local_time = cece::LocalTimeService::CreateUtcFallback(nx, band.ny_local);
    }
}

}  // extern "C"
