#include "aces/aces_provenance.hpp"

#include <sstream>

namespace aces {

void ProvenanceTracker::RegisterSpecies(const std::string& species_name,
                                        std::vector<LayerContribution> contributions) {
    SpeciesProvenance prov;
    prov.species_name = species_name;
    prov.contributions = std::move(contributions);
    records_[species_name] = std::move(prov);
}

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

const SpeciesProvenance* ProvenanceTracker::GetProvenance(
    const std::string& species_name) const {
    auto it = records_.find(species_name);
    return (it != records_.end()) ? &it->second : nullptr;
}

std::string ProvenanceTracker::FormatReport() const {
    std::ostringstream oss;
    oss << "=== ACES Emission Provenance Report ===\n";
    for (const auto& [name, prov] : records_) {
        oss << "\nSpecies: " << name << "\n";
        oss << "  Time context: hour=" << prov.last_hour
            << " dow=" << prov.last_day_of_week
            << " month=" << prov.last_month << "\n";
        oss << "  Contributing layers (" << prov.contributions.size() << "):\n";
        for (size_t i = 0; i < prov.contributions.size(); ++i) {
            const auto& c = prov.contributions[i];
            oss << "    [" << i << "] field=" << c.field_name
                << " op=" << c.operation
                << " hier=" << c.hierarchy
                << " cat=" << c.category
                << " base_scale=" << c.base_scale
                << " eff_scale=" << c.effective_scale;
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
            if (!c.weekly_cycle.empty())  oss << " weekly=" << c.weekly_cycle;
            if (!c.seasonal_cycle.empty()) oss << " seasonal=" << c.seasonal_cycle;
            oss << "\n";
        }
    }
    return oss.str();
}

}  // namespace aces
