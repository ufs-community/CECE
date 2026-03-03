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
     * @brief Initializes the CDEPS-inline library.
     * Writes required .streams and namelist files.
     * @param gcomp ESMF GridComp.
     * @param clock ESMF Clock.
     * @param mesh ESMF Mesh.
     * @param config CDEPS configuration.
     */
    void InitializeCDEPS(ESMC_GridComp gcomp, ESMC_Clock clock, ESMC_Mesh mesh,
                         const AcesCdepsConfig& config);

    /**
     * @brief Advances the CDEPS-inline library to the current time.
     * @param clock ESMF Clock.
     */
    void AdvanceCDEPS(ESMC_Clock clock);

    /**
     * @brief Finalizes the CDEPS-inline library.
     */
    void FinalizeCDEPS();

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
