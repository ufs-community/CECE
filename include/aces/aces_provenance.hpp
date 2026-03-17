#ifndef ACES_PROVENANCE_HPP
#define ACES_PROVENANCE_HPP

/**
 * @file aces_provenance.hpp
 * @brief Emission provenance tracking for ACES.
 *
 * Records which emission layers contributed to each species field,
 * including hierarchy levels, scale factors, and masks applied.
 */

#include <string>
#include <unordered_map>
#include <vector>

namespace aces {

/**
 * @struct LayerContribution
 * @brief Records the contribution of a single emission layer to a species.
 */
struct LayerContribution {
    std::string field_name;                 ///< Source field name.
    std::string operation;                  ///< "add" or "replace".
    int hierarchy = 0;                      ///< Hierarchy level.
    std::string category;                   ///< Emission category label.
    double base_scale = 1.0;                ///< Base scale factor applied.
    std::vector<std::string> masks;         ///< Mask field names applied.
    std::vector<std::string> scale_fields;  ///< Additional scale field names.
    std::string diurnal_cycle;              ///< Diurnal cycle name (if any).
    std::string weekly_cycle;               ///< Weekly cycle name (if any).
    std::string seasonal_cycle;             ///< Seasonal cycle name (if any).
    double effective_scale = 1.0;           ///< Combined temporal scale at last execution.
};

/**
 * @struct SpeciesProvenance
 * @brief Provenance record for a single emission species.
 */
struct SpeciesProvenance {
    std::string species_name;                      ///< Species identifier.
    std::vector<LayerContribution> contributions;  ///< Ordered list of contributing layers.
    int last_hour = -1;                            ///< Hour at last execution.
    int last_day_of_week = -1;                     ///< Day-of-week at last execution.
    int last_month = -1;                           ///< Month at last execution.
};

/**
 * @class ProvenanceTracker
 * @brief Tracks which emission layers contributed to each species field.
 *
 * Records hierarchy levels, scale factors, and masks applied during
 * StackingEngine execution. Provenance can be written to diagnostic output.
 */
class ProvenanceTracker {
   public:
    ProvenanceTracker() = default;

    /**
     * @brief Registers the layer structure for a species from config.
     * @param species_name Species identifier.
     * @param contributions Ordered list of layer contributions.
     */
    void RegisterSpecies(const std::string& species_name,
                         std::vector<LayerContribution> contributions);

    /**
     * @brief Updates effective temporal scales for a species after execution.
     * @param species_name Species identifier.
     * @param hour Current hour (0-23).
     * @param day_of_week Current day of week (0-6).
     * @param month Current month (0-11).
     * @param effective_scales Per-layer effective scales after temporal application.
     */
    void UpdateTemporalScales(const std::string& species_name, int hour, int day_of_week, int month,
                              const std::vector<double>& effective_scales);

    /**
     * @brief Returns the provenance record for a species.
     * @param species_name Species identifier.
     * @return Pointer to SpeciesProvenance, or nullptr if not found.
     */
    [[nodiscard]] const SpeciesProvenance* GetProvenance(const std::string& species_name) const;

    /**
     * @brief Returns all provenance records.
     */
    [[nodiscard]] const std::unordered_map<std::string, SpeciesProvenance>& GetAllProvenance()
        const {
        return records_;
    }

    /**
     * @brief Serializes provenance to a human-readable string for diagnostic output.
     * @return Multi-line provenance report.
     */
    [[nodiscard]] std::string FormatReport() const;

   private:
    std::unordered_map<std::string, SpeciesProvenance> records_;
};

}  // namespace aces

#endif  // ACES_PROVENANCE_HPP
