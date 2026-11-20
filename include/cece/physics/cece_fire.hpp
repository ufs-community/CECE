// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "cece/cece_fire_input.hpp"
#include "cece/physics_scheme.hpp"

namespace cece {

enum class FireMappingBasis { Mass, Molar, CarbonMolar, Number };
enum class FireSourceDispositionKind { Mapped, DiagnosticOnly, Excluded };
enum class FireOcOmPolicyKind { PreserveOc, ConvertWithFactor };

struct FireTargetSpecies {
    std::string name;
    double molecular_weight_g_mol;
};

struct FireTypedMapping {
    FireBulkSpecies source_species;
    std::string target_species;
    FireMappingBasis basis;
    double factor;
    std::string output_unit;
    std::optional<FireProfileIdentity> composition_profile;
};

struct FireConversionFactors {
    std::map<FireBulkSpecies, double> source_molecular_weights_g_mol;
    std::map<std::string, double> target_molecular_weights_g_mol;
    std::map<FireBulkSpecies, double> source_carbon_numbers;
    std::map<std::string, double> target_carbon_numbers;
    std::map<std::string, double> aerosol_factors;
};

struct FireNoxPartition {
    FireNoxReferenceBasis reference_basis;
    double no_mass_fraction;
    double no2_mass_fraction;
    std::string no_target_species;
    std::string no2_target_species;
};

struct FireOcOmPolicy {
    FireOcOmPolicyKind kind;
    double om_per_oc;
};

struct FireSourceDisposition {
    FireSourceDispositionKind kind;
    std::string reason;
};

struct FireBudgetTolerances {
    double relative;
    double absolute;
};

struct FireTargetProfile {
    FireProfileIdentity identity;
    std::string target_id;
    std::string target_version;
    bool production_eligible;
    std::map<std::string, std::string> asset_hashes;
    std::vector<FireTargetSpecies> mechanism_species;
    std::vector<FireTypedMapping> mappings;
    FireConversionFactors conversion_factors;
    std::optional<FireNoxPartition> nox_partition;
    FireOcOmPolicy oc_om_policy;
    std::map<FireBulkSpecies, FireSourceDisposition> dispositions;
    FireBudgetTolerances budget_tolerances;
};

struct FireMappedField {
    std::string target_species;
    std::string unit;
};

struct FireMappedQuantityBudget {
    std::string target_species;
    std::string unit;
    double output_quantity;
};

struct FireMappedInventory {
    FireInputIdentity source_identity;
    FireProfileIdentity target_profile_identity;
    std::vector<std::string> component_ids;
    std::map<std::string, FireComponentProfile> component_profiles;
    std::vector<FireMappedField> fields;
    // Component, target field, cell order; each field retains its declared unit.
    std::vector<double> values;
    std::vector<FireMappedQuantityBudget> budgets;
};

struct FireTargetSpecificInventory {
    FireInputIdentity source_identity;
    std::string target_id;
    std::string target_version;
    std::vector<FireMappedField> fields;
    // Target field, cell order. Values are extensive masses in kilograms.
    std::vector<double> mass_kg;
};

class FireScheme final : public BasePhysicsScheme {
   public:
    void Initialize(const conf::Value& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;

    std::vector<std::string> ExportFieldNames() const;
    void StageBulkInventory(FireBulkInventory inventory);
    void StageTargetSpecificInventory(FireTargetSpecificInventory inventory);
    void StageBorrowedBulkInventory(const FireBulkInventory& inventory);
    void StageBorrowedTargetSpecificInventory(const FireTargetSpecificInventory& inventory);
    void DiscardStagedInventory() noexcept;
    void Finalize() override {
        DiscardStagedInventory();
    }

   private:
    void ExportMassFlux(const FireInputIdentity& identity, const std::vector<FireMappedField>& fields, const std::vector<double>& mass_kg,
                        CeceExportState& export_state);

    std::string target_id_;
    std::string target_version_;
    std::optional<FireTargetProfile> profile_;
    std::vector<FireBulkSpecies> requested_species_;
    std::optional<FireBulkInventory> staged_bulk_;
    std::optional<FireTargetSpecificInventory> staged_target_specific_;
    const FireBulkInventory* staged_bulk_view_ = nullptr;
    const FireTargetSpecificInventory* staged_target_specific_view_ = nullptr;
};

/**
 * Load and structurally validate a hash-pinned target profile.
 *
 * This loader verifies every referenced asset's bytes but deliberately accepts
 * only test-only profiles. Scientific certification and production eligibility
 * require the separate reviewed asset registry and remain unavailable here.
 */
FireTargetProfile LoadFireTargetProfile(const std::string& path, const std::string& expected_target_id, const std::string& expected_target_version);

FireMappedInventory MapFireBulkInventory(const FireBulkInventory& inventory, const FireTargetProfile& profile,
                                         const std::vector<FireBulkSpecies>& required_species);

}  // namespace cece
