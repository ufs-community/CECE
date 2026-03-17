#ifndef ACES_DATA_INGESTOR_HPP
#define ACES_DATA_INGESTOR_HPP

/**
 * @file aces_data_ingestor.hpp
 * @brief Hybrid data ingestion for ACES (ESMF + CDEPS).
 */

#include <unordered_map>

#include "ESMC.h"
#include "aces/aces_config.hpp"
#include "aces/aces_state.hpp"

namespace aces {

/**
 * @class AcesDataIngestor
 * @brief Handles ingestion of meteorology from ESMF and emissions from CDEPS.
 *
 * Implements hybrid field resolution with priority: CDEPS first, then ESMF ImportState.
 * Caches field pointers to avoid redundant queries.
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
     * @brief Initializes the CDEPS-inline library with automatic mesh discovery.
     * @details Attempts to extract mesh from exportState fields if mesh is null.
     *          Falls back to provided mesh if extraction fails.
     *          Implements Requirement 1.5: Automatic mesh discovery from ESMF fields.
     * @param gcomp ESMF GridComp.
     * @param clock ESMF Clock.
     * @param exportState ESMF ExportState for mesh discovery (can be null).
     * @param mesh ESMF Mesh (can be null for automatic discovery).
     * @param config CDEPS configuration.
     */
    void InitializeCDEPS(ESMC_GridComp gcomp, ESMC_Clock clock, ESMC_State exportState,
                         ESMC_Mesh mesh, const AcesCdepsConfig& config);

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

    /**
     * @brief Resolves a field with priority: CDEPS first, then ESMF ImportState.
     * @details Implements field resolution priority as per Requirements 1.5, 1.6.
     *          When a field exists in both CDEPS and ESMF, returns CDEPS version.
     *          Wraps CDEPS data pointers in Kokkos::View with Unmanaged trait.
     *          Caches results to avoid redundant queries.
     * @param name Field name to resolve.
     * @param importState ESMF ImportState to query if not in CDEPS.
     * @param nx, ny, nz Grid dimensions.
     * @return Kokkos::View with Unmanaged trait pointing to field data.
     */
    Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace,
                 Kokkos::MemoryTraits<Kokkos::Unmanaged>>
    ResolveField(const std::string& name, ESMC_State importState, int nx, int ny, int nz);

    /**
     * @brief Checks if a field exists in CDEPS cache.
     * @param name Field name to check.
     * @return True if field exists in CDEPS, false otherwise.
     */
    bool HasCDEPSField(const std::string& name) const;

    /**
     * @brief Checks if a field exists in ESMF State.
     * @param name Field name to check.
     * @param state ESMF State to query.
     * @return True if field exists in ESMF State, false otherwise.
     */
    bool HasESMFField(const std::string& name, ESMC_State state) const;

   private:
    bool cdeps_initialized_ = false;

    /// Cache of CDEPS field pointers (field name -> raw pointer)
    std::unordered_map<std::string, void*> cdeps_field_cache_;

    /// Cache of ESMF fields (field name -> DualView3D)
    std::unordered_map<std::string, DualView3D> esmf_field_cache_;
};

}  // namespace aces

#endif  // ACES_DATA_INGESTOR_HPP
