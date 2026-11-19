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

namespace Kokkos {
class HostSpace;
}
namespace axis::solver {
template <class MemorySpace>
class InterpolationMatrix;
enum class NormType : std::uint8_t;
}  // namespace axis::solver

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
enum class FireInputMode { Product, DeriveFrp };
enum class FireInputProvider { Prepared, Coupled };
enum class FireTimeAllocation { Uniform, SourceResolved, Profile };

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

std::string FireContentSha256(std::string_view bytes);
std::string FireFileSha256(const std::string& path);

struct FireCacheContext {
    FireInputMode mode;
    FireProfileIdentity quality_rules;
    std::optional<FireProfileIdentity> scientific_profile;
    std::optional<FireProfileIdentity> scaling_profile;
    FireProfileIdentity target_profile;
    std::optional<std::string> source_manifest_sha256;
    std::optional<std::string> cutoff_schedule_sha256;
    std::optional<FireTimeAllocation> product_time_allocation;
    std::optional<FireProfileIdentity> product_time_profile;
};

struct FireProviderLimits {
    std::size_t slots;
    std::size_t cells_per_interval;
    std::size_t components_per_interval;
    std::size_t metadata_bytes_per_interval;
};

struct FireRevisionIdentity {
    std::string run;
    std::string revision;
    bool operator==(const FireRevisionIdentity&) const = default;
};

enum class FireIntervalState { Ready, Rejected, Consumed, Evicted };

struct FireIntervalStatus {
    FireIntervalState state = FireIntervalState::Evicted;
    std::string cache_identity;
    std::string rejection_reason;
    std::optional<FireUtcTime> interval_start;
    std::optional<FireUtcTime> interval_end;
    std::optional<FireUtcTime> availability_cutoff;
};

struct FireProviderCheckpoint {
    FireRevisionIdentity identity;
    std::optional<FireUtcTime> committed_end;
    std::optional<FireUtcTime> committed_cutoff;
};

/**
 * Host-side, rank-local normalized-input window, with fixed logical bounds.
 * The decoder/host must bind inventory values to the supplied payload bytes.
 * Borrowed Ready() references expire on restage, Consume() or Evict().
 * Call Consume() only after output succeeds; persist Checkpoint() with that
 * output and restore it on restart to retain replay protection.
 * This class is not thread-safe.
 */
class FireIntervalProvider {
   public:
    FireIntervalProvider(FireInputProvider provider, FireProviderLimits limits, FireRevisionIdentity identity,
                         std::optional<FireProviderCheckpoint> checkpoint = std::nullopt);
    FireIntervalProvider(const FireIntervalProvider&) = delete;
    FireIntervalProvider& operator=(const FireIntervalProvider&) = delete;
    // Prepared paths refer to normalized payloads, never the raw source archive.
    void StagePrepared(std::size_t slot, FireBulkInventory inventory, const FireCacheContext& context, const std::string& path);
    void StageCoupled(std::size_t slot, FireBulkInventory inventory, const FireCacheContext& context, std::string_view bytes);
    const FireBulkInventory& Ready(std::size_t slot) const;
    const FireIntervalStatus& Status(std::size_t slot) const;
    void Consume(std::size_t slot, std::string_view expected_cache_identity);
    void Evict(std::size_t slot);
    std::size_t SlotCount() const;
    FireProviderCheckpoint Checkpoint() const;

   private:
    struct Slot {
        FireIntervalStatus status;
        std::optional<FireBulkInventory> inventory;
    };
    void Stage(std::size_t slot, FireBulkInventory inventory, const FireCacheContext& context, FireInputProvider source, std::string_view content);
    std::string CacheIdentity(const FireBulkInventory& inventory, const FireCacheContext& context) const;
    void ValidateForward(const FireIdentityMetadata& metadata) const;
    FireInputProvider provider_;
    FireProviderLimits limits_;
    FireProviderCheckpoint checkpoint_;
    std::vector<Slot> slots_;
};

struct FireMassBudgetKg {
    FireBulkSpecies species;
    std::optional<std::string> component_id;
    double input_kg;
    double supported_input_kg;
    double output_kg;
};

struct FireEnergyBudgetMj {
    double input_mj;
    double supported_input_mj;
    double output_mj;
};

struct FireDryMatterBudgetKg {
    double input_kg;
    double supported_input_kg;
    double output_kg;
};

struct FireAllocatedMass {
    FireInputIdentity source_identity;
    std::shared_ptr<const FireGrid> target_grid;
    FireUtcTime requested_start;
    FireUtcTime requested_end;
    std::int64_t overlap_seconds;
    std::vector<FireBulkSpecies> species;
    std::vector<std::string> component_ids;
    std::map<std::string, FireComponentProfile> component_profiles;
    std::vector<double> mass_kg;
    std::vector<double> coverage_fraction;
    std::vector<FireQualityState> quality_state;
    std::vector<FireMassBudgetKg> component_budgets;
    std::vector<FireMassBudgetKg> species_budgets;
    std::vector<std::string> accepted_source_sha256;
    std::optional<FireNoxReferenceBasis> nox_reference_basis;
    std::optional<FireProfileIdentity> voc_profile;
    std::optional<FireParticulateOverlap> particulate_overlap;
    std::optional<std::vector<double>> fre_mj;
    std::optional<std::vector<double>> dry_matter_kg;
    std::optional<FireEnergyBudgetMj> fre_budget;
    std::optional<FireDryMatterBudgetKg> dry_matter_budget;
};

std::int64_t FireIntervalOverlapSeconds(const FireUtcTime& first_start, const FireUtcTime& first_end, const FireUtcTime& second_start,
                                        const FireUtcTime& second_end);

/**
 * Uniform allocation within one supplied source interval, retaining components.
 * The caller must generate the first-order AXIS operator from these exact grids;
 * source/destination physical areas must share its area metric up to one scale.
 * No temporal profile, coverage renormalization or geometric extrapolation is
 * applied. Zero support is unavailable, never evidence of valid zero fire.
 */
FireAllocatedMass FireAllocateConservativeMass(const FireBulkInventory& inventory, std::shared_ptr<const FireGrid> target_grid,
                                               const axis::solver::InterpolationMatrix<Kokkos::HostSpace>& matrix,
                                               axis::solver::NormType normalization, const FireUtcTime& requested_start,
                                               const FireUtcTime& requested_end);

}  // namespace cece
