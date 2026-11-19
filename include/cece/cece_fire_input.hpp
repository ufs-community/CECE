// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cece {

class FireUtcTime {
   public:
    explicit FireUtcTime(std::string_view utc);
    auto operator<=>(const FireUtcTime&) const = default;
    std::int64_t EpochSeconds() const;
    std::string ToString() const;

   private:
    std::chrono::sys_seconds value_;
};

enum class FireCutoffMode { Unspecified, Fixed, HostSupplied };

enum class FireLongitudeConvention { Unspecified, Minus180To180, ZeroTo360 };

struct FireGridDefinition {
    std::string grid_id;
    std::string grid_sha256;
    std::size_t nx = 0;
    std::size_t ny = 0;
    std::vector<double> longitude_deg;
    std::vector<double> latitude_deg;
    std::vector<std::array<double, 4>> longitude_bounds_deg;
    std::vector<std::array<double, 4>> latitude_bounds_deg;
    std::vector<double> area_m2;
    std::string crs;
    FireLongitudeConvention longitude_convention = FireLongitudeConvention::Unspecified;
};

class FireGrid {
   public:
    explicit FireGrid(FireGridDefinition definition);
    FireGrid(const FireGrid&) = default;
    FireGrid& operator=(const FireGrid&) = delete;
    const FireGridDefinition& Definition() const;
    std::size_t CellCount() const;

   private:
    FireGridDefinition definition_;
};

struct FireIdentityMetadata {
    int schema_version = 0;
    std::string source_id;
    std::string product_version;
    std::string algorithm_version;
    std::string delivery_id;
    std::string content_sha256;
    FireUtcTime interval_start;
    FireUtcTime interval_end;
    FireUtcTime acquired_at;
    FireUtcTime available_at;
    FireUtcTime availability_cutoff;
    FireCutoffMode cutoff_mode = FireCutoffMode::Unspecified;
    std::shared_ptr<const FireGrid> grid_definition;
};

class FireInputIdentity {
   public:
    explicit FireInputIdentity(FireIdentityMetadata metadata);
    const FireIdentityMetadata& Metadata() const;
    std::int64_t DurationSeconds() const;
    std::int64_t ObservationAgeSeconds() const;

   private:
    FireIdentityMetadata metadata_;
};

enum class FireBulkSpecies { CO2, CO, CH4, PM25, TPM, SO2, OC, BC, NOX, NH3, VOC };
enum class FireQualityState { Unknown, Observed, Reconstructed, ValidZero, Unavailable };
enum class FireNoxReferenceBasis { Unspecified, NO, NO2 };

FireBulkSpecies FireSpeciesFromName(std::string_view name);
std::string_view FireSpeciesName(FireBulkSpecies species);

struct FireProfileIdentity {
    std::string id;
    std::string version;
    std::string content_sha256;
};

struct FireComponentProfile {
    FireProfileIdentity biome;
    FireProfileIdentity composition;
};

struct FireParticulateOverlap {
    FireProfileIdentity definition;
    std::map<FireBulkSpecies, std::string> pool_definitions;
};

struct FireBulkInventoryData {
    FireInputIdentity identity;
    std::vector<FireBulkSpecies> species;
    std::vector<std::string> component_ids;
    std::map<std::string, FireComponentProfile> component_profiles;
    // Component, species, cell order; retain components until their coefficients apply.
    std::vector<double> mass_kg;
    std::vector<FireQualityState> quality_state;
    std::vector<double> coverage_fraction;
    std::vector<std::string> accepted_source_sha256;
    std::optional<FireNoxReferenceBasis> nox_reference_basis;
    std::optional<FireProfileIdentity> voc_profile;
    std::optional<FireParticulateOverlap> particulate_overlap;
    std::optional<std::vector<double>> fre_mj;
    std::optional<std::vector<double>> dry_matter_kg;
};

class FireBulkInventory {
   public:
    explicit FireBulkInventory(FireBulkInventoryData data);
    const FireBulkInventoryData& Data() const;
    double ComponentMassKg(std::size_t component, FireBulkSpecies species, std::size_t cell) const;
    double BulkMassKg(FireBulkSpecies species, std::size_t cell) const;
    double SpeciesBudgetKg(FireBulkSpecies species) const;

   private:
    FireBulkInventoryData data_;
};

}  // namespace cece
