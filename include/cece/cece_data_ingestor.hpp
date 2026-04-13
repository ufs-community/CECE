#ifndef CECE_DATA_INGESTOR_HPP
#define CECE_DATA_INGESTOR_HPP

/**
 * @file cece_data_ingestor.hpp
 * @brief Data ingestion for CECE — field cache for TIDE-interp data.
 *
 * This header is free of ESMF includes. It maintains a cache of interpolated
 * field data received from the NUOPC layer via the C bridge.
 */

#include <Kokkos_Core.hpp>
#include <memory>
#include <string>
#include <unordered_map>

#include "cece/cece_config.hpp"
#include "cece/cece_state.hpp"

namespace cece {

/**
 * @class CeceDataIngestor
 * @brief Manages a cache of interpolated emission fields.
 *
 * The cache stores fields as rank-3 Kokkos::Views (n_lev, n_elem, 1).
 * 2D fields use n_lev=1.
 */
class CeceDataIngestor {
   public:
    CeceDataIngestor();
    ~CeceDataIngestor();

    // Non-copyable
    CeceDataIngestor(const CeceDataIngestor&) = delete;
    CeceDataIngestor& operator=(const CeceDataIngestor&) = delete;

    /**
     * @brief Stores field data in the cache.
     * Called from the C bridge once per time step per field.
     */
    void SetField(const std::string& name, const double* data, int n_lev, int n_elem, int nx,
                  int ny, int nz, int* rc);

    /**
     * @brief Ingests emissions from the cache into the import state.
     */
    void IngestEmissionsInline(const CeceDataConfig& config, CeceImportState& cece_state, int nx,
                               int ny, int nz);

    /**
     * @brief Resolves a field from the cache as an unmanaged device view.
     */
    Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace,
                 Kokkos::MemoryTraits<Kokkos::Unmanaged>>
    ResolveField(const std::string& name, int nx, int ny, int nz);

    /**
     * @brief Generates a TIDE-compatible YAML configuration string from the
     * CeceDataConfig.
     *
     * This serialized YAML is passed to the TIDE layer to initialize
     * data streams dynamically.
     *
     * @param config The data ingestion configuration.
     * @return A YAML formatted string describing the streams.
     */
    std::string SerializeTideYaml(const CeceDataConfig& config);

    /**
     * @brief Generates a TIDE-compatible ESMF configuration string from the
     * CeceDataConfig.
     *
     * This serialized configuration is passed to the TIDE layer for ESMF
     * initialization.
     *
     * @param config The data ingestion configuration.
     * @return An ESMF formatted configuration string.
     */
    std::string SerializeTideESMFConfig(const CeceDataConfig& config);

    /** @brief Returns true if the named field exists in the cache. */
    bool HasCachedField(const std::string& name) const;

    /** @brief Compatibility alias for HasCachedField. */
    bool HasDataIngesterField(const std::string& name) const {
        return HasCachedField(name);
    }

    /** @brief Clears all cached fields to release device memory. */
    void ClearCache();

   private:
    std::unordered_map<std::string,
                       Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>>
        field_cache_;
};

}  // namespace cece

#endif  // CECE_DATA_INGESTOR_HPP
