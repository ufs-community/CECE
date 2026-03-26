#ifndef ACES_DATA_INGESTOR_HPP
#define ACES_DATA_INGESTOR_HPP

/**
 * @file aces_data_ingestor.hpp
 * @brief Data ingestion for ACES — field cache for TIDE-interp data.
 *
 * This header is free of ESMF includes. It maintains a cache of interpolated
 * field data received from the NUOPC layer via the C bridge.
 */

#include <memory>
#include <string>
#include <unordered_map>

#include <Kokkos_Core.hpp>

#include "aces/aces_config.hpp"
#include "aces/aces_state.hpp"

namespace aces {

/**
 * @class AcesDataIngestor
 * @brief Manages a cache of interpolated emission fields.
 *
 * The cache stores fields as rank-3 Kokkos::Views (n_lev, n_elem, 1).
 * 2D fields use n_lev=1.
 */
class AcesDataIngestor {
   public:
    AcesDataIngestor();
    ~AcesDataIngestor();

    // Non-copyable
    AcesDataIngestor(const AcesDataIngestor&) = delete;
    AcesDataIngestor& operator=(const AcesDataIngestor&) = delete;

    /**
     * @brief Stores field data in the cache.
     * Called from the C bridge once per time step per field.
     */
    void SetField(const std::string& name, const double* data, int n_lev, int n_elem,
                  int nx, int ny, int nz, int* rc);

    /**
     * @brief Ingests emissions from the cache into the import state.
     */
    void IngestEmissionsInline(const AcesDataConfig& config, AcesImportState& aces_state,
                               int nx, int ny, int nz);

    /**
     * @brief Resolves a field from the cache as an unmanaged device view.
     */
    Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace,
                 Kokkos::MemoryTraits<Kokkos::Unmanaged>>
    ResolveField(const std::string& name, int nx, int ny, int nz);

    /**
     * @brief Generates a TIDE-compatible YAML configuration string from the
     * AcesDataConfig.
     *
     * This serialized YAML is passed to the TIDE layer (CDEPS) to initialize
     * data streams dynamically.
    /**
     * @brief Generates a TIDE-compatible ESMF Config resource string from the
     * AcesDataConfig.
     *
     * This serialized string is written to a file and passed to the TIDE layer
     * via shr_strdata_init_from_config.
     *
     * @param config The data ingestion configuration.
     * @return An ESMF Config formatted string describing the streams.
     */
    std::string SerializeTideESMFConfig(const AcesDataConfig& config);

    /** @brief Returns true if the named field exists in the cache. */
    bool HasCachedField(const std::string& name) const;

    /** @brief Compatibility alias for HasCachedField. */
    bool HasDataIngesterField(const std::string& name) const { return HasCachedField(name); }

   private:
    std::unordered_map<std::string, Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>>
        field_cache_;
};

}  // namespace aces

#endif  // ACES_DATA_INGESTOR_HPP
