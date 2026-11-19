// SPDX-License-Identifier: Apache-2.0
#include "cece/cece_fire_input.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace cece {
namespace {

std::chrono::sys_seconds ParseUtc(std::string_view utc) {
    const auto invalid = [&]() {
        return std::invalid_argument("Fire UTC timestamp '" + std::string(utc) +
                                     "' must be a valid Gregorian YYYY-MM-DDTHH:MM:SSZ (no leap seconds)");
    };
    if (utc.size() != 20 || utc[4] != '-' || utc[7] != '-' || utc[10] != 'T' || utc[13] != ':' || utc[16] != ':' || utc[19] != 'Z') {
        throw invalid();
    }
    const auto number = [&](std::size_t offset, std::size_t length) {
        int value = 0;
        for (std::size_t i = offset; i < offset + length; ++i) {
            if (utc[i] < '0' || utc[i] > '9') {
                throw invalid();
            }
            value = value * 10 + (utc[i] - '0');
        }
        return value;
    };
    const int year = number(0, 4);
    const auto month = static_cast<unsigned>(number(5, 2));
    const auto day = static_cast<unsigned>(number(8, 2));
    const int hour = number(11, 2);
    const int minute = number(14, 2);
    const int second = number(17, 2);
    const std::chrono::year_month_day date{std::chrono::year(year), std::chrono::month(month), std::chrono::day(day)};
    if (year == 0 || !date.ok() || hour > 23 || minute > 59 || second > 59) {
        throw invalid();
    }
    return std::chrono::sys_days(date) + std::chrono::hours(hour) + std::chrono::minutes(minute) + std::chrono::seconds(second);
}

bool IsSha256(std::string_view digest) {
    return digest.size() == 64 && std::all_of(digest.begin(), digest.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool IsNonempty(std::string_view value) {
    return value.find_first_not_of(" \t\r\n") != std::string_view::npos;
}

void ValidateProfile(const FireProfileIdentity& profile, const std::string& context) {
    if (!IsNonempty(profile.id) || !IsNonempty(profile.version) || !IsSha256(profile.content_sha256)) {
        throw std::invalid_argument(context + ": profile requires nonempty id/version and lowercase SHA256");
    }
}

bool IsParticulate(FireBulkSpecies species) {
    return species == FireBulkSpecies::PM25 || species == FireBulkSpecies::TPM || species == FireBulkSpecies::OC || species == FireBulkSpecies::BC;
}

constexpr std::array<std::string_view, 11> kBulkSpeciesNames = {"CO2", "CO", "CH4", "PM25", "TPM", "SO2", "OC", "BC", "NOX", "NH3", "VOC"};

}  // namespace

FireUtcTime::FireUtcTime(std::string_view utc) : value_(ParseUtc(utc)) {}

std::int64_t FireUtcTime::EpochSeconds() const {
    return value_.time_since_epoch().count();
}

std::string FireUtcTime::ToString() const {
    const auto day = std::chrono::floor<std::chrono::days>(value_);
    const std::chrono::year_month_day date(day);
    const std::chrono::hh_mm_ss clock(value_ - day);
    std::ostringstream result;
    result << std::setfill('0') << std::setw(4) << int(date.year()) << '-' << std::setw(2) << unsigned(date.month()) << '-' << std::setw(2)
           << unsigned(date.day()) << 'T' << std::setw(2) << clock.hours().count() << ':' << std::setw(2) << clock.minutes().count() << ':'
           << std::setw(2) << clock.seconds().count() << 'Z';
    return result.str();
}

FireGrid::FireGrid(FireGridDefinition definition) : definition_(std::move(definition)) {
    const auto invalid = [&](std::string_view reason) {
        throw std::invalid_argument("Fire grid '" + definition_.grid_id + "': " + std::string(reason));
    };
    if (!IsNonempty(definition_.grid_id) || !IsSha256(definition_.grid_sha256)) {
        invalid("grid_id and a valid lowercase grid_sha256 are required");
    }
    if (definition_.nx == 0 || definition_.ny == 0 || definition_.nx > std::numeric_limits<std::size_t>::max() / definition_.ny) {
        invalid("dimensions must be nonzero and their product representable");
    }
    const std::size_t cells = CellCount();
    if (definition_.longitude_deg.size() != cells || definition_.latitude_deg.size() != cells || definition_.longitude_bounds_deg.size() != cells ||
        definition_.latitude_bounds_deg.size() != cells || definition_.area_m2.size() != cells) {
        invalid("each cell requires longitude, latitude, four corner bounds and area_m2");
    }
    if (definition_.crs != "EPSG:4326" && definition_.crs != "OGC:CRS84") {
        invalid("unsupported CRS; geographic degree coordinates require EPSG:4326 or OGC:CRS84");
    }
    if (definition_.longitude_convention != FireLongitudeConvention::Minus180To180 &&
        definition_.longitude_convention != FireLongitudeConvention::ZeroTo360) {
        invalid("longitude_convention must be explicitly selected");
    }
    const double lon_min = definition_.longitude_convention == FireLongitudeConvention::Minus180To180 ? -180.0 : 0.0;
    const double lon_max = lon_min + 360.0;
    const auto longitude_valid = [&](double value) { return std::isfinite(value) && value >= lon_min && value <= lon_max; };
    const auto latitude_valid = [](double value) { return std::isfinite(value) && value >= -90.0 && value <= 90.0; };
    for (std::size_t cell = 0; cell < cells; ++cell) {
        if (!longitude_valid(definition_.longitude_deg[cell]) || !latitude_valid(definition_.latitude_deg[cell]) ||
            !std::all_of(definition_.longitude_bounds_deg[cell].begin(), definition_.longitude_bounds_deg[cell].end(), longitude_valid) ||
            !std::all_of(definition_.latitude_bounds_deg[cell].begin(), definition_.latitude_bounds_deg[cell].end(), latitude_valid)) {
            invalid("cell " + std::to_string(cell) + " has nonfinite or out-of-range geographic coordinates/bounds");
        }
        if (!std::isfinite(definition_.area_m2[cell]) || definition_.area_m2[cell] <= 0.0) {
            invalid("cell " + std::to_string(cell) + " area_m2 must be finite and positive");
        }
    }
}

const FireGridDefinition& FireGrid::Definition() const {
    return definition_;
}

std::size_t FireGrid::CellCount() const {
    return definition_.nx * definition_.ny;
}

FireInputIdentity::FireInputIdentity(FireIdentityMetadata metadata) : metadata_(std::move(metadata)) {
    const auto invalid = [&](std::string_view field, std::string_view reason) {
        throw std::invalid_argument("Fire source '" + metadata_.source_id + "': " + std::string(field) + " " + std::string(reason));
    };
    if (metadata_.schema_version != 1) {
        invalid("schema_version", "must be 1");
    }
    for (const auto& [name, value] :
         {std::pair{"source_id", &metadata_.source_id}, std::pair{"product_version", &metadata_.product_version},
          std::pair{"algorithm_version", &metadata_.algorithm_version}, std::pair{"delivery_id", &metadata_.delivery_id}}) {
        if (value->find_first_not_of(" \t\r\n") == std::string::npos) {
            invalid(name, "must be nonempty");
        }
    }
    if (!IsSha256(metadata_.content_sha256)) {
        invalid("content_sha256", "must contain exactly 64 lowercase hexadecimal digits");
    }
    if (metadata_.interval_end <= metadata_.interval_start) {
        invalid("interval_end", "must be later than interval_start for a half-open interval");
    }
    if (metadata_.available_at < metadata_.acquired_at) {
        invalid("available_at", "must not precede acquired_at");
    }
    if (metadata_.acquired_at > metadata_.availability_cutoff || metadata_.available_at > metadata_.availability_cutoff) {
        invalid("availability_cutoff", "must not precede acquired_at or available_at");
    }
    if (metadata_.cutoff_mode != FireCutoffMode::Fixed && metadata_.cutoff_mode != FireCutoffMode::HostSupplied) {
        invalid("cutoff_mode", "must be fixed or host_supplied");
    }
    if (!metadata_.grid_definition) {
        invalid("grid_definition", "is required with validated grid_id, grid_sha256 and geometry");
    }
}

const FireIdentityMetadata& FireInputIdentity::Metadata() const {
    return metadata_;
}

std::int64_t FireInputIdentity::DurationSeconds() const {
    return metadata_.interval_end.EpochSeconds() - metadata_.interval_start.EpochSeconds();
}

std::int64_t FireInputIdentity::ObservationAgeSeconds() const {
    return metadata_.availability_cutoff.EpochSeconds() - metadata_.acquired_at.EpochSeconds();
}

FireBulkSpecies FireSpeciesFromName(std::string_view name) {
    const auto found = std::find(kBulkSpeciesNames.begin(), kBulkSpeciesNames.end(), name);
    if (found == kBulkSpeciesNames.end()) {
        throw std::invalid_argument("Unknown fire bulk species '" + std::string(name) + "'");
    }
    return static_cast<FireBulkSpecies>(std::distance(kBulkSpeciesNames.begin(), found));
}

std::string_view FireSpeciesName(FireBulkSpecies species) {
    const auto index = static_cast<std::size_t>(species);
    if (index >= kBulkSpeciesNames.size()) {
        throw std::invalid_argument("Invalid fire bulk species enum value");
    }
    return kBulkSpeciesNames[index];
}

FireBulkInventory::FireBulkInventory(FireBulkInventoryData data) : data_(std::move(data)) {
    const std::string context = "Fire inventory source '" + data_.identity.Metadata().source_id + "'";
    const auto invalid = [&](std::string_view reason) { throw std::invalid_argument(context + ": " + std::string(reason)); };
    const std::size_t cells = data_.identity.Metadata().grid_definition->CellCount();
    const std::size_t species_count = data_.species.size();
    const std::size_t components = data_.component_ids.size();
    if (species_count == 0 || components == 0 || species_count > kBulkSpeciesNames.size()) {
        invalid("species and component_ids must be nonempty");
    }
    std::set<FireBulkSpecies> unique_species;
    for (const auto species : data_.species) {
        FireSpeciesName(species);
        if (!unique_species.insert(species).second) {
            invalid("duplicate species '" + std::string(FireSpeciesName(species)) + "'");
        }
    }
    std::set<std::string> unique_components;
    if (data_.component_profiles.size() != components) {
        invalid("component_profiles must identify every component exactly once");
    }
    for (const auto& id : data_.component_ids) {
        if (!IsNonempty(id) || !unique_components.insert(id).second) {
            invalid("component_ids must be nonempty and unique");
        }
        const auto profile = data_.component_profiles.find(id);
        if (profile == data_.component_profiles.end()) {
            invalid("missing composition/biome profile for component '" + id + "'");
        }
        ValidateProfile(profile->second.biome, context + " component '" + id + "' biome");
        ValidateProfile(profile->second.composition, context + " component '" + id + "' composition");
    }
    if (species_count > std::numeric_limits<std::size_t>::max() / cells ||
        components > std::numeric_limits<std::size_t>::max() / (species_count * cells) ||
        data_.mass_kg.size() != components * species_count * cells) {
        invalid("mass_kg must have component, species, cell dimensions");
    }
    for (double mass : data_.mass_kg) {
        if (!std::isfinite(mass) || mass < 0.0) {
            invalid("mass_kg must be finite and nonnegative; missing mass is not zero");
        }
    }
    if (data_.quality_state.size() != cells || data_.coverage_fraction.size() != cells) {
        invalid("quality_state and coverage_fraction must describe every cell");
    }
    for (std::size_t cell = 0; cell < cells; ++cell) {
        if (!std::isfinite(data_.coverage_fraction[cell]) || data_.coverage_fraction[cell] <= 0.0 || data_.coverage_fraction[cell] > 1.0) {
            invalid("coverage_fraction must be finite in (0,1]; unavailable cells cannot form a complete inventory");
        }
        const auto state = data_.quality_state[cell];
        if (state != FireQualityState::Observed && state != FireQualityState::Reconstructed && state != FireQualityState::ValidZero) {
            invalid("unavailable/invalid quality_state cannot form a complete inventory");
        }
        for (std::size_t species = 0; species < species_count; ++species) {
            double mass = 0.0;
            for (std::size_t component = 0; component < components; ++component) {
                mass += data_.mass_kg[(component * species_count + species) * cells + cell];
            }
            if (!std::isfinite(mass) || (state == FireQualityState::ValidZero && mass != 0.0)) {
                invalid("component mass sum must be finite and valid_zero cells must contain only zero mass");
            }
        }
    }
    if (data_.accepted_source_sha256.empty()) {
        invalid("accepted_source_sha256 must record the accepted input identities");
    }
    std::set<std::string> hashes;
    for (const auto& hash : data_.accepted_source_sha256) {
        if (!IsSha256(hash) || !hashes.insert(hash).second) {
            invalid("accepted_source_sha256 must contain unique valid lowercase SHA256 values");
        }
    }
    if (unique_species.count(FireBulkSpecies::NOX) != 0 && !data_.nox_reference_basis) {
        invalid("NOX requires an explicit nox_reference_basis");
    }
    if (data_.nox_reference_basis && *data_.nox_reference_basis != FireNoxReferenceBasis::NO &&
        *data_.nox_reference_basis != FireNoxReferenceBasis::NO2) {
        invalid("nox_reference_basis must be NO or NO2 mass");
    }
    if (unique_species.count(FireBulkSpecies::VOC) != 0 && !data_.voc_profile) {
        invalid("VOC requires an explicit versioned/hashed voc_profile");
    }
    if (data_.voc_profile) {
        ValidateProfile(*data_.voc_profile, context + " voc_profile");
    }
    for (const auto species : data_.species) {
        if (IsParticulate(species) && (!data_.particulate_overlap || data_.particulate_overlap->pool_definitions.count(species) == 0 ||
                                       !IsNonempty(data_.particulate_overlap->pool_definitions.at(species)))) {
            invalid("particulate_overlap must explicitly define each selected particulate pool");
        }
    }
    if (data_.particulate_overlap) {
        ValidateProfile(data_.particulate_overlap->definition, context + " particulate_overlap");
        for (const auto& [species, definition] : data_.particulate_overlap->pool_definitions) {
            if (!IsParticulate(species) || !IsNonempty(definition)) {
                invalid("particulate_overlap pool definitions must describe particulate species");
            }
        }
    }
    for (const auto& [name, values] : {std::pair{"fre_mj", &data_.fre_mj}, std::pair{"dry_matter_kg", &data_.dry_matter_kg}}) {
        if (*values) {
            if ((*values)->size() != cells ||
                !std::all_of((*values)->begin(), (*values)->end(), [](double value) { return std::isfinite(value) && value >= 0.0; })) {
                invalid(std::string(name) + " must contain a finite nonnegative value for every cell when supplied");
            }
        }
    }
}

const FireBulkInventoryData& FireBulkInventory::Data() const {
    return data_;
}

double FireBulkInventory::ComponentMassKg(std::size_t component, FireBulkSpecies species, std::size_t cell) const {
    const std::size_t cells = data_.identity.Metadata().grid_definition->CellCount();
    if (component >= data_.component_ids.size() || cell >= cells) {
        throw std::out_of_range("Fire inventory component/cell index out of range for source '" + data_.identity.Metadata().source_id + "'");
    }
    const auto found = std::find(data_.species.begin(), data_.species.end(), species);
    if (found == data_.species.end()) {
        throw std::invalid_argument("Fire inventory source '" + data_.identity.Metadata().source_id + "' lacks species '" +
                                    std::string(FireSpeciesName(species)) + "'");
    }
    const auto index = static_cast<std::size_t>(std::distance(data_.species.begin(), found));
    return data_.mass_kg[(component * data_.species.size() + index) * cells + cell];
}

double FireBulkInventory::BulkMassKg(FireBulkSpecies species, std::size_t cell) const {
    double total = 0.0;
    for (std::size_t component = 0; component < data_.component_ids.size(); ++component) {
        total += ComponentMassKg(component, species, cell);
    }
    return total;
}

double FireBulkInventory::SpeciesBudgetKg(FireBulkSpecies species) const {
    double total = 0.0;
    for (std::size_t cell = 0; cell < data_.identity.Metadata().grid_definition->CellCount(); ++cell) {
        total += BulkMassKg(species, cell);
    }
    if (!std::isfinite(total)) {
        throw std::overflow_error("Fire inventory species budget overflow for source '" + data_.identity.Metadata().source_id + "'");
    }
    return total;
}

}  // namespace cece
