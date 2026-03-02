#ifndef ACES_DATA_INGESTOR_HPP
#define ACES_DATA_INGESTOR_HPP

/**
 * @file aces_data_ingestor.hpp
 * @brief Hybrid data ingestion for ACES (ESMF + CDEPS).
 */

#include "ESMC.h"
#include "aces/aces_config.hpp"
#include "aces/aces_state.hpp"

namespace aces {

/**
 * @class AcesDataIngestor
 * @brief Handles ingestion of meteorology from ESMF and emissions from CDEPS.
 */
class AcesDataIngestor {
   public:
    AcesDataIngestor() = default;

    /**
     * @brief Ingests meteorological state from ESMF ImportState.
     * @param importState ESMF state containing meteorology fields.
     * @param field_names List of field names to extract from ESMF.
     * @param aces_state ACES state to be populated.
     * @param nx, ny, nz Grid dimensions.
     */
    void IngestMeteorology(ESMC_State importState, const std::vector<std::string>& field_names,
                           AcesImportState& aces_state, int nx, int ny, int nz);

    /**
     * @brief Initializes the DEMS-inline library.
     * Writes required .streams and namelist files.
     * @param config DEMS configuration.
     */
    void InitializeDEMS(const AcesDemsConfig& config);

    /**
     * @brief Finalizes the DEMS-inline library.
     */
    void FinalizeDEMS();

    /**
     * @brief Ingests emissions using DEMS-inline.
     * @param config DEMS configuration.
     * @param aces_state ACES state to be populated.
     * @param nx, ny, nz Grid dimensions.
     */
    void IngestEmissionsInline(const AcesDemsConfig& config, AcesImportState& aces_state, int nx,
                               int ny, int nz);
};

}  // namespace aces

#endif  // ACES_DATA_INGESTOR_HPP
