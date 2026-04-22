#include <cstring>
#include <iostream>
#include <string>

#include "cece/cece_internal.hpp"
#include "cece/cece_logger.hpp"

extern "C" {

/**
 * @brief Foreign function interface to ingest a field from TIDE.
 *
 * @param data_ptr Pointer to the C++ CeceInternalData structure (opaque).
 * @param field_name Name of the field being ingested.
 * @param name_len Length of the field name string.
 * @param field_data Pointer to the raw field data (Fortran array/C pointer).
 * @param n_lev Number of vertical levels.
 * @param n_elem Number of horizontal elements (columns).
 * @param rc Return code (0 = success, -1 = error).
 */
void cece_ingestor_set_field(void* data_ptr, const char* field_name, int name_len, const double* field_data, int n_lev, int n_elem, int* rc) {
    if (!data_ptr) {
        CECE_LOG_ERROR("[CECE] cece_ingestor_set_field: Null data pointer");
        if (rc) *rc = -1;
        return;
    }

    if (!field_data) {
        CECE_LOG_ERROR("[CECE] cece_ingestor_set_field: Null field data pointer");
        if (rc) *rc = -1;
        return;
    }

    if (!field_name || name_len <= 0) {
        CECE_LOG_ERROR("[CECE] cece_ingestor_set_field: Invalid field name");
        if (rc) *rc = -1;
        return;
    }

    // Cast the opaque pointer to the internal data structure
    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);

    // Construct the string from the char pointer and length
    std::string name(field_name, name_len);

    // Call the ingestor's SetField method
    // We pass the cached grid dimensions (nx, ny, nz) for validation
    internal_data->ingestor.SetField(name, field_data, n_lev, n_elem, internal_data->nx, internal_data->ny, internal_data->nz, rc);
}

}  // extern "C"
