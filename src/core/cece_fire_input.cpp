// SPDX-License-Identifier: Apache-2.0
#include "cece/cece_fire_input.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <axis/solver/apply.hpp>
#include <axis/solver/regrid_config.hpp>
#include <cmath>
#include <fstream>
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

class Sha256 {
   public:
    Sha256() : context_(EVP_MD_CTX_new(), EVP_MD_CTX_free) {
        if (!context_ || EVP_DigestInit_ex(context_.get(), EVP_sha256(), nullptr) != 1) {
            throw std::runtime_error("Fire SHA256 initialization failed");
        }
    }
    void Update(std::string_view bytes) {
        if (!bytes.empty() && EVP_DigestUpdate(context_.get(), bytes.data(), bytes.size()) != 1) {
            throw std::runtime_error("Fire SHA256 update failed");
        }
    }
    std::string Finish() {
        std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
        unsigned int size = 0;
        if (EVP_DigestFinal_ex(context_.get(), digest.data(), &size) != 1 || size != 32) {
            throw std::runtime_error("Fire SHA256 finalization failed");
        }
        constexpr char hex[] = "0123456789abcdef";
        std::string result;
        result.reserve(64);
        for (unsigned int i = 0; i < size; ++i) {
            result.push_back(hex[digest[i] >> 4]);
            result.push_back(hex[digest[i] & 15]);
        }
        return result;
    }

   private:
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context_;
};

}  // namespace

FireUtcTime::FireUtcTime(std::string_view utc) : value_(ParseUtc(utc)) {}

FireUtcTime FireUtcTime::FromEpochSeconds(std::int64_t epoch_seconds) {
    const auto value = std::chrono::sys_seconds(std::chrono::seconds(epoch_seconds));
    const std::chrono::year_month_day date(std::chrono::floor<std::chrono::days>(value));
    if (!date.ok() || int(date.year()) < 1 || int(date.year()) > 9999) {
        throw std::invalid_argument("Fire UTC epoch seconds are outside the supported Gregorian timestamp range");
    }
    return FireUtcTime(value);
}

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

FireProductEmissionsInterval::FireProductEmissionsInterval(FireProductEmissionsIntervalData data) : data_(std::move(data)) {
    const auto invalid = [&](std::string_view reason) {
        throw std::invalid_argument("Fire product '" + data_.product_identity.product_id + "': " + std::string(reason));
    };
    const auto& identity = data_.product_identity;
    if (!IsNonempty(identity.product_id) || !IsNonempty(identity.version) || !IsNonempty(identity.variant)) {
        invalid("product_id, version and variant must be nonempty");
    }
    const auto& common = identity.common_identity.Metadata();
    if (identity.version != common.product_version) invalid("product version must match the common source identity");
    const std::size_t cells = common.grid_definition->CellCount();
    if (data_.quality_state.size() != cells || data_.coverage_fraction.size() != cells) {
        invalid("quality_state and coverage_fraction must describe every grid cell");
    }
    for (std::size_t cell = 0; cell < cells; ++cell) {
        if (!std::isfinite(data_.coverage_fraction[cell]) || data_.coverage_fraction[cell] <= 0.0 || data_.coverage_fraction[cell] > 1.0) {
            invalid("coverage_fraction must be finite and in (0,1]");
        }
        const auto state = data_.quality_state[cell];
        if (state != FireQualityState::Observed && state != FireQualityState::Reconstructed && state != FireQualityState::ValidZero) {
            invalid("quality_state must be observed, reconstructed or valid_zero");
        }
    }
    const auto validate_diagnostic = [&](const std::optional<std::vector<double>>& values, std::string_view name) {
        if (!values) return;
        if (values->size() != cells ||
            !std::all_of(values->begin(), values->end(), [](double value) { return std::isfinite(value) && value >= 0.0; })) {
            invalid(std::string(name) + " must contain one finite nonnegative value per cell");
        }
    };
    validate_diagnostic(data_.optional_diagnostics.frp_mw, "FRP diagnostics");
    validate_diagnostic(data_.optional_diagnostics.fre_mj, "FRE diagnostics");
    validate_diagnostic(data_.optional_diagnostics.dry_matter_kg, "dry-matter diagnostics");
    if (data_.scaling_state.kind == FireProductScalingKind::Applied) {
        if (!data_.scaling_state.profile) invalid("an applied scaling state requires its profile identity");
        ValidateProfile(*data_.scaling_state.profile, "Fire product scaling state");
    } else if (data_.scaling_state.kind == FireProductScalingKind::Unscaled) {
        if (data_.scaling_state.profile) invalid("an unscaled product cannot carry a scaling profile");
    } else {
        invalid("scaling_state is invalid");
    }

    if (data_.chemistry_state == FireProductChemistryState::Bulk) {
        if (data_.target_identity || !data_.target_fields.empty() || !data_.target_mass_kg.empty()) {
            invalid("bulk chemistry cannot carry target-specific identity or fields");
        }
        if (data_.species.empty() || data_.species.size() > kBulkSpeciesNames.size()) {
            invalid("bulk chemistry requires at least one recognized source species");
        }
        if (data_.species_availability.size() != kBulkSpeciesNames.size()) {
            invalid("species_availability must explicitly describe all eleven canonical species");
        }
        std::set<FireBulkSpecies> unique_species;
        for (const auto species : data_.species) {
            FireSpeciesName(species);
            if (!unique_species.insert(species).second) invalid("bulk species list contains a duplicate");
            const auto availability = data_.species_availability.find(species);
            if (availability == data_.species_availability.end() || availability->second != FireProductSpeciesAvailability::Available) {
                invalid("each declared source species must be marked available");
            }
        }
        for (std::size_t index = 0; index < kBulkSpeciesNames.size(); ++index) {
            const auto species = static_cast<FireBulkSpecies>(index);
            const auto found = data_.species_availability.find(species);
            if (found == data_.species_availability.end()) invalid("species_availability has a missing canonical species");
            if (found->second != FireProductSpeciesAvailability::Available && found->second != FireProductSpeciesAvailability::Unavailable) {
                invalid("species_availability contains an invalid state");
            }
            const bool declared = unique_species.contains(species);
            if ((found->second == FireProductSpeciesAvailability::Available) != declared) {
                invalid("species_availability must agree with the declared species list");
            }
        }
        if (data_.quantity_semantics.size() != data_.species.size()) {
            invalid("quantity_semantics must describe every declared species exactly once");
        }
        if (data_.species.size() > std::numeric_limits<std::size_t>::max() / cells ||
            data_.source_values.size() != data_.species.size() * cells) {
            invalid("source_values must have species, cell dimensions");
        }
        if (!data_.mass_kg.empty() && data_.mass_kg.size() != data_.source_values.size()) {
            invalid("mass_kg must be empty or have species, cell dimensions");
        }
        std::vector<double> normalized(data_.source_values.size());
        const auto& areas = common.grid_definition->Definition().area_m2;
        for (std::size_t species_index = 0; species_index < data_.species.size(); ++species_index) {
            const auto species = data_.species[species_index];
            const auto semantics = data_.quantity_semantics.find(species);
            if (semantics == data_.quantity_semantics.end()) invalid("quantity_semantics is missing a declared species");
            double multiplier = 1.0;
            switch (semantics->second.kind) {
                case FireProductQuantityKind::IntervalMass:
                    if (semantics->second.units != "kg/cell/interval") invalid("interval-mass units must be kg/cell/interval");
                    break;
                case FireProductQuantityKind::MeanRate:
                    if (semantics->second.units != "kg s-1") invalid("mean-rate units must be kg s-1");
                    multiplier = static_cast<double>(identity.common_identity.DurationSeconds());
                    break;
                case FireProductQuantityKind::Flux:
                    if (semantics->second.units != "kg m-2 s-1") invalid("flux units must be kg m-2 s-1");
                    break;
                default:
                    invalid("quantity_semantics contains an invalid quantity kind");
            }
            for (std::size_t cell = 0; cell < cells; ++cell) {
                const std::size_t offset = species_index * cells + cell;
                const double value = data_.source_values[offset];
                if (!std::isfinite(value) || value < 0.0) invalid("source quantities must be finite and nonnegative");
                double mass = value * multiplier;
                if (semantics->second.kind == FireProductQuantityKind::Flux) {
                    mass *= areas[cell] * static_cast<double>(identity.common_identity.DurationSeconds());
                }
                if (!std::isfinite(mass) || (data_.quality_state[cell] == FireQualityState::ValidZero && mass != 0.0)) {
                    invalid("normalized mass must be finite and valid_zero cells must contain only zero mass");
                }
                if (!data_.mass_kg.empty() &&
                    (!std::isfinite(data_.mass_kg[offset]) ||
                     std::abs(data_.mass_kg[offset] - mass) > 1e-12 * std::max({1.0, std::abs(data_.mass_kg[offset]), std::abs(mass)}))) {
                    invalid("provided mass_kg must match normalized source quantities within relative tolerance 1e-12");
                }
                normalized[offset] = mass;
            }
        }
        for (const auto& [species, semantics] : data_.quantity_semantics) {
            (void)semantics;
            if (!unique_species.contains(species)) invalid("quantity_semantics contains an undeclared species");
        }
        data_.mass_kg = std::move(normalized);
    } else if (data_.chemistry_state == FireProductChemistryState::TargetSpecific) {
        if (!data_.target_identity || !IsNonempty(data_.target_identity->id) || !IsNonempty(data_.target_identity->version)) {
            invalid("target-specific chemistry requires a nonempty target identity");
        }
        if (!data_.species.empty() || !data_.quantity_semantics.empty() || !data_.source_values.empty() || !data_.mass_kg.empty()) {
            invalid("target-specific chemistry must bypass bulk species and quantity data");
        }
        if (data_.species_availability.size() != kBulkSpeciesNames.size()) {
            invalid("species_availability must explicitly describe all eleven canonical species");
        }
        for (std::size_t index = 0; index < kBulkSpeciesNames.size(); ++index) {
            const auto found = data_.species_availability.find(static_cast<FireBulkSpecies>(index));
            if (found == data_.species_availability.end() || found->second != FireProductSpeciesAvailability::Unavailable) {
                invalid("target-specific chemistry cannot claim bulk species availability");
            }
        }
        if (data_.target_fields.empty() || data_.target_fields.size() > std::numeric_limits<std::size_t>::max() / cells ||
            data_.target_mass_kg.size() != data_.target_fields.size() * cells) {
            invalid("target-specific fields and mass_kg must have target-field, cell dimensions");
        }
        std::set<std::string> target_species;
        for (const auto& field : data_.target_fields) {
            if (!IsNonempty(field.target_species) || field.unit != "kg" || !target_species.insert(field.target_species).second) {
                invalid("target-specific fields must have unique nonempty species names and kg units");
            }
        }
        for (std::size_t offset = 0; offset < data_.target_mass_kg.size(); ++offset) {
            const double mass = data_.target_mass_kg[offset];
            const auto cell = offset % cells;
            if (!std::isfinite(mass) || mass < 0.0 ||
                (data_.quality_state[cell] == FireQualityState::ValidZero && mass != 0.0)) {
                invalid("target-specific mass_kg must be finite and nonnegative, with valid_zero cells containing only zero mass");
            }
        }
    } else {
        invalid("chemistry_state is invalid");
    }
}

const FireProductEmissionsIntervalData& FireProductEmissionsInterval::Data() const {
    return data_;
}

const std::vector<double>& FireProductEmissionsInterval::MassKg() const {
    return data_.mass_kg;
}

void FireProductEmissionsInterval::RequireSpecies(const std::vector<FireBulkSpecies>& species) const {
    if (data_.chemistry_state != FireProductChemistryState::Bulk) {
        throw std::invalid_argument("Bulk species cannot be requested from a target-specific fire product");
    }
    std::set<FireBulkSpecies> unique;
    for (const auto requested : species) {
        FireSpeciesName(requested);
        if (!unique.insert(requested).second) throw std::invalid_argument("Requested fire product species contain a duplicate");
        const auto found = data_.species_availability.find(requested);
        if (found == data_.species_availability.end() || found->second != FireProductSpeciesAvailability::Available) {
            throw std::invalid_argument("Fire product '" + data_.product_identity.product_id + "' does not provide requested species '" +
                                        std::string(FireSpeciesName(requested)) + "'");
        }
    }
}

void FireProductEmissionsInterval::ValidateScaling(const std::optional<FireProfileIdentity>& requested_profile) const {
    if (!requested_profile) return;
    ValidateProfile(*requested_profile, "Requested fire product scaling");
    if (data_.scaling_state.kind == FireProductScalingKind::Applied) {
        if (requested_profile->id == data_.scaling_state.profile->id && requested_profile->version == data_.scaling_state.profile->version &&
            requested_profile->content_sha256 == data_.scaling_state.profile->content_sha256) {
            throw std::invalid_argument("Fire product scaling profile '" + requested_profile->id + "' has already been applied");
        }
        throw std::invalid_argument("A scaled fire product cannot receive a second scaling profile");
    }
}

void ValidateFireProductIntervalSet(const std::vector<std::reference_wrapper<const FireProductEmissionsInterval>>& intervals) {
    for (std::size_t first = 0; first < intervals.size(); ++first) {
        const auto& lhs = intervals[first].get().Data();
        for (std::size_t second = first + 1; second < intervals.size(); ++second) {
            const auto& rhs = intervals[second].get().Data();
            if (lhs.product_identity.product_id != rhs.product_identity.product_id) continue;
            const auto& lhs_metadata = lhs.product_identity.common_identity.Metadata();
            const auto& rhs_metadata = rhs.product_identity.common_identity.Metadata();
            if (lhs_metadata.interval_start < rhs_metadata.interval_end && rhs_metadata.interval_start < lhs_metadata.interval_end) {
                throw std::invalid_argument("Overlapping intervals for fire product '" + lhs.product_identity.product_id +
                                            "' have competing versions or variants");
            }
        }
    }
}

std::string FireContentSha256(std::string_view bytes) {
    Sha256 digest;
    digest.Update(bytes);
    return digest.Finish();
}

std::string FireFileSha256(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open fire input for SHA256 verification: " + path);
    Sha256 digest;
    std::array<char, 65536> buffer{};
    while (input.read(buffer.data(), buffer.size()) || input.gcount() > 0) {
        digest.Update(std::string_view(buffer.data(), static_cast<std::size_t>(input.gcount())));
    }
    if (!input.eof() || input.bad()) throw std::runtime_error("Cannot read fire input for SHA256 verification: " + path);
    return digest.Finish();
}

FireIntervalProvider::FireIntervalProvider(FireInputProvider provider, FireProviderLimits limits, FireRevisionIdentity identity,
                                           std::optional<FireProviderCheckpoint> checkpoint)
    : provider_(provider), limits_(limits), checkpoint_{std::move(identity), std::nullopt, std::nullopt} {
    if (provider_ != FireInputProvider::Prepared && provider_ != FireInputProvider::Coupled) {
        throw std::invalid_argument("Fire provider must be prepared or coupled");
    }
    if (limits_.slots == 0 || limits_.cells_per_interval == 0 || limits_.components_per_interval == 0 || limits_.metadata_bytes_per_interval == 0) {
        throw std::invalid_argument("Fire provider slot, cell, component and metadata bounds must be positive");
    }
    if (!IsNonempty(checkpoint_.identity.run) || !IsNonempty(checkpoint_.identity.revision) ||
        checkpoint_.identity.run.size() > limits_.metadata_bytes_per_interval ||
        checkpoint_.identity.revision.size() > limits_.metadata_bytes_per_interval) {
        throw std::invalid_argument("Fire provider requires bounded, nonempty run and revision identities");
    }
    if (checkpoint) {
        if (checkpoint->identity != checkpoint_.identity || checkpoint->committed_end.has_value() != checkpoint->committed_cutoff.has_value()) {
            throw std::invalid_argument("Fire provider checkpoint requires matching run/revision and paired committed end/cutoff");
        }
        checkpoint_ = std::move(*checkpoint);
    }
    slots_.resize(limits_.slots);
}

std::string FireIntervalProvider::CacheIdentity(const FireBulkInventory& inventory, const FireCacheContext& context) const {
    const auto& data = inventory.Data();
    const auto& metadata = data.identity.Metadata();
    if (metadata.grid_definition->CellCount() > limits_.cells_per_interval || data.component_ids.size() > limits_.components_per_interval) {
        throw std::invalid_argument("Fire interval exceeds provider cell/component bounds");
    }
    std::string key;
    const auto append = [&](std::string_view value) {
        const auto length = std::to_string(value.size()) + ":";
        if (length.size() > limits_.metadata_bytes_per_interval - key.size() ||
            value.size() > limits_.metadata_bytes_per_interval - key.size() - length.size()) {
            throw std::invalid_argument("Fire interval exceeds provider metadata bound");
        }
        key.append(length).append(value);
    };
    const auto profile = [&](const FireProfileIdentity& value) {
        ValidateProfile(value, "Fire cache identity");
        append(value.id);
        append(value.version);
        append(value.content_sha256);
    };
    const auto optional_profile = [&](const std::optional<FireProfileIdentity>& value) {
        append(value ? "present" : "absent");
        if (value) profile(*value);
    };
    const auto optional_hash = [&](const std::optional<std::string>& value) {
        append(value ? "present" : "absent");
        if (value) {
            if (!IsSha256(*value)) throw std::invalid_argument("Fire cache identity requires lowercase SHA256 references");
            append(*value);
        }
    };
    if (context.mode != FireInputMode::Product && context.mode != FireInputMode::DeriveFrp) {
        throw std::invalid_argument("Fire cache identity requires product or derive_frp mode");
    }
    if (context.mode == FireInputMode::Product) {
        if (context.scientific_profile || !context.product_time_allocation) {
            throw std::invalid_argument("Fire product cache requires temporal policy and prohibits scientific derivation profile");
        }
        const auto allocation = *context.product_time_allocation;
        if (allocation != FireTimeAllocation::Uniform && allocation != FireTimeAllocation::SourceResolved &&
            allocation != FireTimeAllocation::Profile) {
            throw std::invalid_argument("Fire cache identity has unsupported temporal policy");
        }
        if ((allocation == FireTimeAllocation::Profile) != context.product_time_profile.has_value()) {
            throw std::invalid_argument("Fire temporal profile must be present only for profile allocation");
        }
    } else if (!context.scientific_profile || context.product_time_allocation || context.product_time_profile) {
        throw std::invalid_argument("Fire derivation cache requires scientific profile and prohibits product temporal options");
    }
    if ((metadata.cutoff_mode == FireCutoffMode::Fixed && context.cutoff_schedule_sha256) ||
        (metadata.cutoff_mode == FireCutoffMode::HostSupplied && provider_ == FireInputProvider::Prepared && !context.cutoff_schedule_sha256)) {
        throw std::invalid_argument("Fire cache cutoff schedule is incompatible with cutoff mode/provider");
    }
    append("cece-fire-cache-v1");
    append(checkpoint_.identity.run);
    append(checkpoint_.identity.revision);
    append(context.mode == FireInputMode::Product ? "product" : "derive_frp");
    append(std::to_string(metadata.schema_version));
    append(metadata.source_id);
    append(metadata.product_version);
    append(metadata.algorithm_version);
    append(metadata.delivery_id);
    append(metadata.content_sha256);
    append(metadata.interval_start.ToString());
    append(metadata.interval_end.ToString());
    append(metadata.acquired_at.ToString());
    append(metadata.available_at.ToString());
    append(metadata.availability_cutoff.ToString());
    append(metadata.cutoff_mode == FireCutoffMode::Fixed ? "fixed" : "host_supplied");
    append(metadata.grid_definition->Definition().grid_id);
    append(metadata.grid_definition->Definition().grid_sha256);
    profile(context.quality_rules);
    optional_profile(context.scientific_profile);
    optional_profile(context.scaling_profile);
    profile(context.target_profile);
    optional_hash(context.source_manifest_sha256);
    optional_hash(context.cutoff_schedule_sha256);
    append(context.product_time_allocation ? std::to_string(static_cast<int>(*context.product_time_allocation)) : "absent");
    optional_profile(context.product_time_profile);
    append(std::to_string(data.species.size()));
    for (const auto species : data.species) append(FireSpeciesName(species));
    append(std::to_string(data.component_ids.size()));
    for (const auto& id : data.component_ids) {
        append(id);
        profile(data.component_profiles.at(id).biome);
        profile(data.component_profiles.at(id).composition);
    }
    if (data.nox_reference_basis)
        append(*data.nox_reference_basis == FireNoxReferenceBasis::NO ? "NO" : "NO2");
    else
        append("absent");
    optional_profile(data.voc_profile);
    append(data.particulate_overlap ? "present" : "absent");
    if (data.particulate_overlap) {
        profile(data.particulate_overlap->definition);
        append(std::to_string(data.particulate_overlap->pool_definitions.size()));
        for (const auto& [species, definition] : data.particulate_overlap->pool_definitions) {
            append(FireSpeciesName(species));
            append(definition);
        }
    }
    append(std::to_string(data.accepted_source_sha256.size()));
    const auto source_offset = key.size();
    std::set<std::string_view> sources;
    for (const auto& hash : data.accepted_source_sha256) {
        append(hash);
        sources.insert(hash);
    }
    // Validate the metadata bound before canonicalizing provenance order.
    key.resize(source_offset);
    for (const auto hash : sources) append(hash);
    return FireContentSha256(key);
}

void FireIntervalProvider::ValidateForward(const FireIdentityMetadata& metadata) const {
    if (checkpoint_.committed_end && metadata.interval_start < *checkpoint_.committed_end) {
        throw std::invalid_argument(
            "Fire interval already committed or overlaps committed output; use an explicit new run/revision for reprocessing");
    }
    if (checkpoint_.committed_cutoff && metadata.availability_cutoff < *checkpoint_.committed_cutoff) {
        throw std::invalid_argument("Fire availability cutoff cannot move backward in a forward run");
    }
}

void FireIntervalProvider::StagePrepared(std::size_t slot, FireBulkInventory inventory, const FireCacheContext& context, const std::string& path) {
    Stage(slot, std::move(inventory), context, FireInputProvider::Prepared, path);
}

void FireIntervalProvider::StageCoupled(std::size_t slot, FireBulkInventory inventory, const FireCacheContext& context, std::string_view bytes) {
    Stage(slot, std::move(inventory), context, FireInputProvider::Coupled, bytes);
}

void FireIntervalProvider::Stage(std::size_t index, FireBulkInventory inventory, const FireCacheContext& context, FireInputProvider source,
                                 std::string_view content) {
    auto& slot = slots_.at(index);
    if (slot.status.state == FireIntervalState::Consumed) throw std::logic_error("Evict consumed fire slot before reuse");
    const auto previous = slot.status;
    slot.inventory.reset();
    slot.status = {};
    const auto& metadata = inventory.Data().identity.Metadata();
    slot.status.interval_start = metadata.interval_start;
    slot.status.interval_end = metadata.interval_end;
    slot.status.availability_cutoff = metadata.availability_cutoff;
    const auto reject = [&](const std::string& reason) {
        slot.status.state = FireIntervalState::Rejected;
        slot.status.rejection_reason = reason.substr(0, limits_.metadata_bytes_per_interval);
    };
    try {
        if (source != provider_) throw std::invalid_argument("Fire input source does not match configured provider");
        if (previous.state == FireIntervalState::Ready &&
            (previous.interval_start != metadata.interval_start || previous.interval_end != metadata.interval_end)) {
            throw std::invalid_argument("Cannot replace an unconsumed fire interval with different time support");
        }
        ValidateForward(metadata);
        for (std::size_t other = 0; other < slots_.size(); ++other) {
            const auto& status = slots_[other].status;
            if (other != index && status.state == FireIntervalState::Ready && *status.interval_start < metadata.interval_end &&
                metadata.interval_start < *status.interval_end) {
                throw std::invalid_argument("Fire interval overlaps another ready slot");
            }
        }
        slot.status.cache_identity = CacheIdentity(inventory, context);
        const auto digest = source == FireInputProvider::Prepared ? FireFileSha256(std::string(content)) : FireContentSha256(content);
        if (digest != metadata.content_sha256) throw std::runtime_error("Fire input content SHA256 mismatch");
        slot.inventory.emplace(std::move(inventory));
        slot.status.state = FireIntervalState::Ready;
    } catch (const std::invalid_argument& error) {
        reject(error.what());
        throw;
    } catch (const std::runtime_error& error) {
        reject(error.what());
        throw;
    }
}

const FireBulkInventory& FireIntervalProvider::Ready(std::size_t slot) const {
    const auto& entry = slots_.at(slot);
    if (entry.status.state != FireIntervalState::Ready || !entry.inventory) throw std::logic_error("Fire interval is not ready");
    return *entry.inventory;
}

const FireIntervalStatus& FireIntervalProvider::Status(std::size_t slot) const {
    return slots_.at(slot).status;
}

void FireIntervalProvider::Consume(std::size_t index, std::string_view expected_cache_identity) {
    auto& slot = slots_.at(index);
    const auto& metadata = Ready(index).Data().identity.Metadata();
    if (expected_cache_identity != slot.status.cache_identity) throw std::invalid_argument("Fire cache identity changed before commit");
    ValidateForward(metadata);
    for (std::size_t other = 0; other < slots_.size(); ++other) {
        if (other != index && slots_[other].status.state == FireIntervalState::Ready &&
            *slots_[other].status.interval_start < metadata.interval_start) {
            throw std::invalid_argument("Consume earlier ready fire intervals before committing later output");
        }
    }
    checkpoint_.committed_end = metadata.interval_end;
    checkpoint_.committed_cutoff = metadata.availability_cutoff;
    slot.inventory.reset();
    slot.status.state = FireIntervalState::Consumed;
}

void FireIntervalProvider::Evict(std::size_t slot) {
    auto& entry = slots_.at(slot);
    if (entry.status.state == FireIntervalState::Ready) throw std::logic_error("Cannot evict unconsumed ready fire interval");
    entry.inventory.reset();
    entry.status = {};
}

std::size_t FireIntervalProvider::SlotCount() const {
    return slots_.size();
}

FireProviderCheckpoint FireIntervalProvider::Checkpoint() const {
    return checkpoint_;
}

std::int64_t FireIntervalOverlapSeconds(const FireUtcTime& first_start, const FireUtcTime& first_end, const FireUtcTime& second_start,
                                        const FireUtcTime& second_end) {
    if (first_end <= first_start || second_end <= second_start) {
        throw std::invalid_argument("Fire overlap requires two positive half-open UTC intervals");
    }
    return std::max<std::int64_t>(0, std::min(first_end, second_end).EpochSeconds() - std::max(first_start, second_start).EpochSeconds());
}

FireAllocatedMass FireAllocateConservativeMass(const FireBulkInventory& inventory, std::shared_ptr<const FireGrid> target_grid,
                                               const axis::solver::InterpolationMatrix<Kokkos::HostSpace>& matrix,
                                               axis::solver::NormType normalization, const FireUtcTime& requested_start,
                                               const FireUtcTime& requested_end) {
    using axis::solver::NormType;
    using Policy = Kokkos::RangePolicy<Kokkos::DefaultHostExecutionSpace, Kokkos::IndexType<std::size_t>>;
    constexpr double relative_tolerance = 1e-6;
    constexpr double absolute_tolerance = 1e-12;
    const auto close = [&](double first, double second) {
        return std::isfinite(first) && std::isfinite(second) &&
               std::abs(first - second) <= absolute_tolerance + relative_tolerance * std::max(std::abs(first), std::abs(second));
    };
    const auto& data = inventory.Data();
    const auto& metadata = data.identity.Metadata();
    const auto overlap = FireIntervalOverlapSeconds(metadata.interval_start, metadata.interval_end, requested_start, requested_end);
    if (!target_grid || matrix.n_src() != metadata.grid_definition->CellCount() || matrix.n_dst() != target_grid->CellCount() ||
        (normalization != NormType::DstArea && normalization != NormType::FracArea)) {
        throw std::invalid_argument("Fire conservative operator dimensions/normalization must match source and target grids");
    }
    const auto ns = matrix.n_src(), nt = matrix.n_dst();
    const auto aa = matrix.area_a(), ab = matrix.area_b(), fa = matrix.frac_a(), fb = matrix.frac_b();
    const auto weights = matrix.factor_list();
    const auto rows = matrix.factor_row(), columns = matrix.factor_col();
    if (aa.extent(0) != ns || fa.extent(0) != ns || ab.extent(0) != nt || fb.extent(0) != nt || rows.extent(0) != matrix.nnz() ||
        columns.extent(0) != matrix.nnz()) {
        throw std::invalid_argument("Fire conservative operator lacks complete areas, fractions or indices");
    }
    const auto& source_area = metadata.grid_definition->Definition().area_m2;
    const auto& target_area = target_grid->Definition().area_m2;
    if (!std::isfinite(aa[0]) || aa[0] <= 0) throw std::invalid_argument("Fire source operator area must be finite and positive");
    const double area_scale = source_area[0] / aa[0];
    if (!std::isfinite(area_scale) || area_scale <= 0) throw std::invalid_argument("Fire operator physical-area scale is invalid");
    const auto validate_area = [&](auto areas, auto fractions, const std::vector<double>& physical) {
        for (std::size_t i = 0; i < physical.size(); ++i) {
            if (!std::isfinite(areas[i]) || areas[i] <= 0 || !std::isfinite(fractions[i]) || fractions[i] < 0 ||
                fractions[i] > 1 + absolute_tolerance || !close(physical[i] / areas[i], area_scale)) {
                throw std::invalid_argument("Fire conservative areas/fractions are invalid or inconsistent with physical grid areas");
            }
        }
    };
    validate_area(aa, fa, source_area);
    validate_area(ab, fb, target_area);
    std::vector<double> row_sum(nt, 0), covered_area(ns, 0);
    for (std::size_t k = 0; k < matrix.nnz(); ++k) {
        const auto row = rows[k], column = columns[k];
        if (row < 0 || column < 0 || static_cast<std::size_t>(row) >= nt || static_cast<std::size_t>(column) >= ns || !std::isfinite(weights[k]) ||
            weights[k] < 0) {
            throw std::invalid_argument("Fire conservative operator requires valid indices and finite nonnegative first-order weights");
        }
        row_sum[row] += weights[k];
        covered_area[column] += weights[k] * ab[row] * (normalization == NormType::FracArea ? fb[row] : 1);
    }
    for (std::size_t i = 0; i < ns; ++i) {
        if (!close(covered_area[i] / aa[i], fa[i])) throw std::invalid_argument("Fire conservative operator violates source coverage budget");
    }
    for (std::size_t j = 0; j < nt; ++j) {
        const double expected = normalization == NormType::DstArea ? fb[j] : (fb[j] > 0 ? 1 : 0);
        if (!close(row_sum[j], expected)) throw std::invalid_argument("Fire conservative operator violates destination normalization/coverage");
    }
    const auto fields = data.component_ids.size() * data.species.size();
    if (nt > std::numeric_limits<std::size_t>::max() / fields) {
        throw std::invalid_argument("Fire target mass dimensions overflow");
    }
    FireAllocatedMass result{data.identity,
                             target_grid,
                             requested_start,
                             requested_end,
                             overlap,
                             data.species,
                             data.component_ids,
                             data.component_profiles,
                             std::vector<double>(fields * nt, 0),
                             std::vector<double>(nt, 0),
                             std::vector<FireQualityState>(nt, FireQualityState::Unavailable),
                             {},
                             {},
                             data.accepted_source_sha256,
                             data.nox_reference_basis,
                             data.voc_profile,
                             data.particulate_overlap,
                             std::nullopt,
                             std::nullopt,
                             std::nullopt,
                             std::nullopt};
    const double mass_fraction = static_cast<double>(overlap) / data.identity.DurationSeconds();
    const double time_coverage = static_cast<double>(overlap) / (requested_end.EpochSeconds() - requested_start.EpochSeconds());
    std::vector<double> density(ns), destination_density(nt);
    const auto apply = [&]() {
        axis::solver::apply<Kokkos::HostSpace>(matrix, axis::field_view<const double, 1>(density.data(), ns),
                                               axis::field_view<double, 1>(destination_density.data(), nt));
    };
    const auto allocate = [&](const double* source_values, double* target_values) {
        auto* source_density = density.data();
        const auto* physical_source_area = source_area.data();
        const auto* physical_target_area = target_area.data();
        const auto* target_density = destination_density.data();
        Kokkos::parallel_for("fire::extensive_to_density", Policy(0, ns),
                             [=](std::size_t i) { source_density[i] = source_values[i] * mass_fraction / physical_source_area[i]; });
        Kokkos::fence("fire::source_density_ready");
        apply();
        Kokkos::parallel_for("fire::density_to_extensive", Policy(0, nt), [=](std::size_t j) {
            target_values[j] = target_density[j] * physical_target_area[j] * (normalization == NormType::FracArea ? fb[j] : 1);
        });
        double input = 0, supported = 0, output = 0;
        Kokkos::parallel_reduce("fire::input_budget", Policy(0, ns), [=](std::size_t i, double& sum) { sum += source_values[i]; }, input);
        Kokkos::parallel_reduce(
            "fire::supported_budget", Policy(0, ns), [=](std::size_t i, double& sum) { sum += source_values[i] * mass_fraction * fa[i]; }, supported);
        Kokkos::parallel_reduce("fire::output_budget", Policy(0, nt), [=](std::size_t j, double& sum) { sum += target_values[j]; }, output);
        if (!std::isfinite(input) || !close(supported, output)) {
            throw std::runtime_error("Fire extensive quantity budget is nonfinite or fails conservative allocation tolerance");
        }
        return std::array<double, 3>{input, supported, output};
    };
    for (std::size_t s = 0; s < data.species.size(); ++s) {
        result.species_budgets.push_back({data.species[s], std::nullopt, 0, 0, 0});
    }
    for (std::size_t component = 0; component < data.component_ids.size(); ++component) {
        for (std::size_t s = 0; s < data.species.size(); ++s) {
            const auto offset = (component * data.species.size() + s) * ns;
            const auto target_offset = (component * data.species.size() + s) * nt;
            const auto [input, supported, output] = allocate(data.mass_kg.data() + offset, result.mass_kg.data() + target_offset);
            result.component_budgets.push_back({data.species[s], data.component_ids[component], input, supported, output});
            auto& total = result.species_budgets[s];
            total.input_kg += input;
            total.supported_input_kg += supported;
            total.output_kg += output;
        }
    }
    for (const auto& budget : result.species_budgets) {
        if (!std::isfinite(budget.input_kg) || !close(budget.supported_input_kg, budget.output_kg)) {
            throw std::runtime_error("Fire species kg budget is nonfinite or fails conservation tolerance");
        }
        if (data.fre_mj) {
            result.fre_mj.emplace(nt, 0);
            const auto [input, supported, output] = allocate(data.fre_mj->data(), result.fre_mj->data());
            result.fre_budget = FireEnergyBudgetMj{input, supported, output};
        }
        if (data.dry_matter_kg) {
            result.dry_matter_kg.emplace(nt, 0);
            const auto [input, supported, output] = allocate(data.dry_matter_kg->data(), result.dry_matter_kg->data());
            result.dry_matter_budget = FireDryMatterBudgetKg{input, supported, output};
        }
    }
    // Coverage is diagnostic support, not another multiplier on supported mass.
    const auto* source_coverage = data.coverage_fraction.data();
    auto* coverage_density = density.data();
    Kokkos::parallel_for("fire::source_coverage", Policy(0, ns), [=](std::size_t i) { coverage_density[i] = source_coverage[i] * time_coverage; });
    Kokkos::fence("fire::coverage_ready");
    apply();
    for (std::size_t j = 0; j < nt; ++j) {
        const double coverage = destination_density[j] * (normalization == NormType::FracArea ? fb[j] : 1);
        if (!std::isfinite(coverage) || coverage < 0 || coverage > 1 + relative_tolerance) {
            throw std::runtime_error("Fire target coverage is invalid after conservative allocation");
        }
        result.coverage_fraction[j] = std::min(1.0, coverage);
        if (coverage > 0) result.quality_state[j] = FireQualityState::ValidZero;
    }
    for (std::size_t k = 0; k < matrix.nnz(); ++k) {
        const auto row = rows[k], column = columns[k];
        if (result.coverage_fraction[row] <= 0 || weights[k] <= 0) continue;
        const auto quality = data.quality_state[column];
        auto& target_quality = result.quality_state[row];
        if (quality == FireQualityState::Reconstructed)
            target_quality = FireQualityState::Reconstructed;
        else if (quality == FireQualityState::Observed && target_quality != FireQualityState::Reconstructed)
            target_quality = FireQualityState::Observed;
    }
    return result;
}

}  // namespace cece
