/**
 * @file aces_core_writer_init.cpp
 * @brief Initialization of the standalone writer for output.
 */

#include <iostream>

#include "aces/aces_internal.hpp"

extern "C" {

/**
 * @brief Initialize the standalone writer with grid dimensions and start time.
 *
 * This function must be called during the Realize phase after grid dimensions
 * are known and before the Run phase begins.
 *
 * @param data_ptr Pointer to AcesInternalData structure.
 * @param nx Grid dimension in X.
 * @param ny Grid dimension in Y.
 * @param nz Grid dimension in Z.
 * @param start_time_iso8601 Start time in ISO 8601 format (e.g., "2020-01-01T00:00:00").
 * @param start_time_len Length of the start_time_iso8601 string.
 * @param rc Return code (0 on success).
 */
void aces_core_writer_initialize(void* data_ptr, int nx, int ny, int nz,
                                 const char* start_time_iso8601, int start_time_len, int* rc) {
    *rc = 0;

    if (data_ptr == nullptr) {
        std::cerr << "ERROR: aces_core_writer_initialize - null data pointer\n";
        *rc = -1;
        return;
    }

    if (start_time_iso8601 == nullptr) {
        std::cerr << "ERROR: aces_core_writer_initialize - null start_time_iso8601 pointer\n";
        *rc = -1;
        return;
    }

    try {
        auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr);

        if (!internal_data->standalone_writer) {
            std::cerr << "ERROR: aces_core_writer_initialize - standalone_writer not initialized\n";
            *rc = -1;
            return;
        }

        // Convert C string to std::string
        std::string start_time(start_time_iso8601, start_time_len);

        std::cout << "INFO: Initializing standalone writer with dimensions: " << nx << "x" << ny
                  << "x" << nz << " start_time=" << start_time << "\n";

        // Initialize the writer
        int writer_rc = internal_data->standalone_writer->Initialize(start_time, nx, ny, nz);

        if (writer_rc != 0) {
            std::cerr << "ERROR: aces_core_writer_initialize - writer initialization failed\n";
            *rc = -1;
            return;
        }

        std::cout << "INFO: Standalone writer initialized successfully\n";
        *rc = 0;

    } catch (const std::exception& e) {
        std::cerr << "ERROR: aces_core_writer_initialize - exception: " << e.what() << "\n";
        *rc = -1;
        return;
    } catch (...) {
        std::cerr << "ERROR: aces_core_writer_initialize - unknown exception\n";
        *rc = -1;
        return;
    }
}

}  // extern "C"
