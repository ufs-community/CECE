/**
 * @file aces_provenance.cpp
 * @brief Implementation of emission provenance tracking system.
 *
 * The provenance tracking system provides comprehensive logging and reporting
 * capabilities for ACES emission calculations. It tracks the contribution of
 * each emission layer, temporal scaling factors, and field operations to
 * enable reproducible science and debugging.
 *
 * Key capabilities:
 * - Layer contribution tracking with hierarchy information
 * - Temporal scaling factor recording (diurnal, weekly, seasonal)
 * - Field operation provenance (add, multiply, set, mask operations)
 * - Formatted reporting for scientific reproducibility
 * - Performance-optimized data structures for runtime efficiency
 *
 * This system is essential for scientific traceability and model validation,
 * allowing researchers to understand exactly how final emission fields are
 * constructed from individual components.
 *
 * @author Barry Baker
 * @date 2024
 * @version 1.0
 */

#include "aces/aces_provenance.hpp"

#include <sstream>

namespace aces {

/**
 * @brief Register a species and its contributing emission layers.
 *
 * This method initializes provenance tracking for a species by recording
 * all configured emission layers that will contribute to the final field.
 * The layer information includes field names, operations, hierarchies,
 * and scaling factors.
 *
 * @param species_name Name of the chemical species (e.g., "CO", "NOx")
 * @param contributions Vector of layer contributions for this species
 */
void ProvenanceTracker::RegisterSpecies(const std::string& species_name,
                                        std::vector<LayerContribution> contributions) {
    SpeciesProvenance prov;
    prov.species_name = species_name;
    prov.contributions = std::move(contributions);
    records_[species_name] = std::move(prov);
}

/**
 * @brief Update temporal scaling factors for a species.
 *
 * Records the effective scaling factors applied during a specific timestep.
 * This information is crucial for understanding how temporal cycles (diurnal,
 * weekly, seasonal) modify base emission rates.
 *
 * @param species_name Name of chemical species to update
 * @param hour Current hour of day (0-23)
 * @param day_of_week Current day of week (0=Sunday, 6=Saturday)
 * @param month Current month (1-12)
 * @param effective_scales Vector of scaling factors applied to each layer
 */
void ProvenanceTracker::UpdateTemporalScales(const std::string& species_name, int hour,
                                             int day_of_week, int month,
                                             const std::vector<double>& effective_scales) {
    auto it = records_.find(species_name);
    if (it == records_.end()) {
        return;
    }
    auto& prov = it->second;
    prov.last_hour = hour;
    prov.last_day_of_week = day_of_week;
    prov.last_month = month;
    for (size_t i = 0; i < effective_scales.size() && i < prov.contributions.size(); ++i) {
        prov.contributions[i].effective_scale = effective_scales[i];
    }
}

/**
 * @brief Retrieve provenance information for a species.
 *
 * Returns a read-only pointer to the complete provenance record for the
 * specified species, including all layer contributions and temporal information.
 *
 * @param species_name Name of chemical species to query
 * @return Constant pointer to provenance record, or nullptr if not found
 */
const SpeciesProvenance* ProvenanceTracker::GetProvenance(const std::string& species_name) const {
    auto it = records_.find(species_name);
    return (it != records_.end()) ? &it->second : nullptr;
}

/**
 * @brief Generate a formatted provenance report for all tracked species.
 *
 * Creates a comprehensive text report showing the emission construction
 * process for all species. The report includes layer contributions,
 * temporal scaling factors, field operations, and masking information.
 *
 * This report is essential for:
 * - Scientific reproducibility and validation
 * - Model debugging and development
 * - Publication documentation
 * - Regulatory compliance reporting
 *
 * @return Formatted string containing complete provenance information
 */
std::string ProvenanceTracker::FormatReport() const {
    std::ostringstream oss;
    oss << "=== ACES Emission Provenance Report ===\n";
    for (const auto& [name, prov] : records_) {
        oss << "\nSpecies: " << name << "\n";
        oss << "  Time context: hour=" << prov.last_hour << " dow=" << prov.last_day_of_week
            << " month=" << prov.last_month << "\n";
        oss << "  Contributing layers (" << prov.contributions.size() << "):\n";
        for (size_t i = 0; i < prov.contributions.size(); ++i) {
            const auto& c = prov.contributions[i];
            oss << "    [" << i << "] field=" << c.field_name << " op=" << c.operation
                << " hier=" << c.hierarchy << " cat=" << c.category
                << " base_scale=" << c.base_scale << " eff_scale=" << c.effective_scale;
            if (!c.masks.empty()) {
                oss << " masks=[";
                for (size_t m = 0; m < c.masks.size(); ++m) {
                    if (m > 0) oss << ",";
                    oss << c.masks[m];
                }
                oss << "]";
            }
            if (!c.scale_fields.empty()) {
                oss << " scale_fields=[";
                for (size_t s = 0; s < c.scale_fields.size(); ++s) {
                    if (s > 0) oss << ",";
                    oss << c.scale_fields[s];
                }
                oss << "]";
            }
            if (!c.diurnal_cycle.empty()) oss << " diurnal=" << c.diurnal_cycle;
            if (!c.weekly_cycle.empty()) oss << " weekly=" << c.weekly_cycle;
            if (!c.seasonal_cycle.empty()) oss << " seasonal=" << c.seasonal_cycle;
            oss << "\n";
        }
    }
    return oss.str();
}

}  // namespace aces
