/**
 * @file cece_core_writer_init.cpp
 * @brief Initialization of the standalone writer for output.
 */

#include <mpi.h>

#include <cstdlib>
#include <iostream>

#include "cece/cece_internal.hpp"
#include "cece/cece_standalone_writer.hpp"

namespace cece {
class CeceStandaloneWriter;
}
extern std::unique_ptr<cece::CeceStandaloneWriter> g_standalone_writer;

extern "C" {

/**
 * @brief Initialize the standalone writer with grid dimensions and coordinate arrays.
 *
 * @param data_ptr Pointer to CeceInternalData structure.
 * @param nx Grid dimension in X.
 * @param ny Grid dimension in Y.
 * @param nz Grid dimension in Z.
 * @param lon_coords Array of longitude coordinates (size nx).
 * @param lat_coords Array of latitude coordinates (size ny).
 * @param start_time_iso8601 Start time in ISO 8601 format.
 * @param start_time_len Length of the start_time_iso8601 string.
 * @param mpi_comm_f Fortran MPI communicator handle.
 * @param rc Return code (0 on success).
 */
void cece_core_writer_initialize_with_coords(void* data_ptr, int nx, int ny, int nz, const double* lon_coords, int lon_len, const double* lat_coords,
                                             int lat_len, const char* start_time_iso8601, int start_time_len, int mpi_comm_f, int* rc) {
    *rc = 0;

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: cece_core_writer_initialize_with_coords - null data pointer\n";
        *rc = -1;
        return;
    }

    if (lon_coords == nullptr || lat_coords == nullptr) {
        std::cerr << "ERROR: cece_core_writer_initialize_with_coords - null coordinate arrays\n";
        *rc = -1;
        return;
    }

    if (start_time_iso8601 == nullptr) {
        std::cerr << "ERROR: cece_core_writer_initialize_with_coords - null start_time_iso8601 pointer\n";
        *rc = -1;
        return;
    }

    try {
        auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);

        if (!g_standalone_writer) {
            auto output_config = internal_data->config.output_config;
            if (output_config.amio_worker_threads == -1) {
                output_config.amio_worker_threads = internal_data->config.driver_config.amio_worker_threads;
            }
            MPI_Comm comm = MPI_COMM_SELF;
            int mpi_initialized = 0;
            MPI_Initialized(&mpi_initialized);
            if (mpi_initialized) {
                MPI_Comm temp_comm = MPI_Comm_f2c(static_cast<MPI_Fint>(mpi_comm_f));
                if (temp_comm != MPI_COMM_NULL) {
                    comm = temp_comm;
                }
            }
            g_standalone_writer = std::make_unique<cece::CeceStandaloneWriter>(output_config, comm);
            std::atexit([]() { g_standalone_writer.reset(); });
        }

        // Convert C string to std::string
        std::string start_time(start_time_iso8601, start_time_len);

        // Convert C arrays to std::vectors
        std::vector<double> lon_vec(lon_coords, lon_coords + lon_len);
        std::vector<double> lat_vec(lat_coords, lat_coords + lat_len);

        std::cout << "INFO: Initializing standalone writer with coordinates: " << nx << "x" << ny << "x" << nz << " start_time=" << start_time
                  << "\n";
        std::cout << "INFO: Longitude range: " << lon_vec[0] << " to " << lon_vec[lon_len - 1] << "\n";
        std::cout << "INFO: Latitude range: " << lat_vec[0] << " to " << lat_vec[lat_len - 1] << "\n";

        // Initialize the writer with coordinates and gridspec file path
        int writer_rc =
            g_standalone_writer->InitializeWithCoords(start_time, nx, ny, nz, lon_vec, lat_vec, internal_data->config.driver_config.gridspec_file);

        if (writer_rc != 0) {
            std::cerr << "ERROR: cece_core_writer_initialize_with_coords - writer initialization "
                         "failed\n";
            *rc = -1;
            return;
        }

        std::cout << "INFO: Standalone writer initialized successfully\n";
    } catch (const std::exception& e) {
        std::cerr << "ERROR: cece_core_writer_initialize_with_coords - " << e.what() << std::endl;
        *rc = -1;
    }
}

/**
 * @brief Initialize the standalone writer with grid dimensions and start time (legacy).
 *
 * This function must be called during the Realize phase after grid dimensions
 * are known and before the Run phase begins.
 *
 * @param data_ptr Pointer to CeceInternalData structure.
 * @param nx Grid dimension in X.
 * @param ny Grid dimension in Y.
 * @param nz Grid dimension in Z.
 * @param start_time_iso8601 Start time in ISO 8601 format (e.g., "2020-01-01T00:00:00").
 * @param start_time_len Length of the start_time_iso8601 string.
 * @param mpi_comm_f Fortran MPI communicator handle.
 * @param rc Return code (0 on success).
 */
void cece_core_writer_initialize(void* data_ptr, int nx, int ny, int nz, const char* start_time_iso8601, int start_time_len, int mpi_comm_f,
                                 int* rc) {
    *rc = 0;

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: cece_core_writer_initialize - null data pointer\n";
        *rc = -1;
        return;
    }

    if (start_time_iso8601 == nullptr) {
        std::cerr << "ERROR: cece_core_writer_initialize - null start_time_iso8601 pointer\n";
        *rc = -1;
        return;
    }

    try {
        auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);

        if (!g_standalone_writer) {
            auto output_config = internal_data->config.output_config;
            if (output_config.amio_worker_threads == -1) {
                output_config.amio_worker_threads = internal_data->config.driver_config.amio_worker_threads;
            }
            MPI_Comm comm = MPI_COMM_SELF;
            int mpi_initialized = 0;
            MPI_Initialized(&mpi_initialized);
            if (mpi_initialized) {
                MPI_Comm temp_comm = MPI_Comm_f2c(static_cast<MPI_Fint>(mpi_comm_f));
                if (temp_comm != MPI_COMM_NULL) {
                    comm = temp_comm;
                }
            }
            g_standalone_writer = std::make_unique<cece::CeceStandaloneWriter>(output_config, comm);
            std::atexit([]() { g_standalone_writer.reset(); });
        }

        // Convert C string to std::string
        std::string start_time(start_time_iso8601, start_time_len);

        std::cout << "INFO: Initializing standalone writer with dimensions: " << nx << "x" << ny << "x" << nz << " start_time=" << start_time << "\n";

        // Initialize the writer
        int writer_rc = g_standalone_writer->Initialize(start_time, nx, ny, nz);

        if (writer_rc != 0) {
            std::cerr << "ERROR: cece_core_writer_initialize - writer initialization failed\n";
            *rc = -1;
            return;
        }

        std::cout << "INFO: Standalone writer initialized successfully\n";
        *rc = 0;

    } catch (const std::exception& e) {
        std::cerr << "ERROR: cece_core_writer_initialize - exception: " << e.what() << "\n";
        *rc = -1;
        return;
    } catch (...) {
        std::cerr << "ERROR: cece_core_writer_initialize - unknown exception\n";
        *rc = -1;
        return;
    }
}

}  // extern "C"
