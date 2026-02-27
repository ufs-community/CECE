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
     * @brief Ingests meteorology from ESMF ImportState.
     * @param importState ESMF state containing meteorology fields.
     * @param aces_state ACES state to be populated.
     * @param nx, ny, nz Grid dimensions.
     */
    void IngestMeteorology(ESMC_State importState, AcesImportState& aces_state, int nx, int ny,
                           int nz);

    /**
     * @brief Ingests emissions using CDEPS-inline.
     * @param config CDEPS configuration.
     * @param aces_state ACES state to be populated.
     * @param nx, ny, nz Grid dimensions.
     */
    void IngestEmissionsInline(const AcesCdepsConfig& config, AcesImportState& aces_state, int nx,
                               int ny, int nz);
};

}  // namespace aces

#endif  // ACES_DATA_INGESTOR_HPP
