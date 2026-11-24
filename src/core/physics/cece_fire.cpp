// SPDX-License-Identifier: Apache-2.0
#include "cece/physics/cece_fire.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <set>
#include <stdexcept>

#include "cece/cece_physics_factory.hpp"
#include "conf/config.hpp"
#include "conf/value.hpp"

namespace cece {
namespace {

bool IsSha256(std::string_view digest) {
    return digest.size() == 64 &&
           std::all_of(digest.begin(), digest.end(), [](char value) { return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'); });
}

[[noreturn]] void Invalid(std::string_view field, std::string_view message) {
    throw std::invalid_argument("Fire target profile." + std::string(field) + ": " + std::string(message));
}

void OnlyKeys(const conf::Value& node, const std::set<std::string>& allowed, std::string_view field) {
    if (node.kind() != conf::Node_Kind::Map) Invalid(field, "must be a mapping");
    for (const auto& key : node.keys()) {
        if (!allowed.contains(key)) Invalid(field, "unknown key '" + key + "'");
    }
}

std::string Text(const conf::Value& node, std::string_view field) {
    const auto value = node.try_string();
    if (!value || value->find_first_not_of(" \t\r\n") == std::string::npos) Invalid(field, "must be a nonempty string");
    return *value;
}

double Positive(const conf::Value& node, std::string_view field, bool allow_zero = false) {
    const auto value = node.try_double();
    if (!value || !std::isfinite(*value) || (allow_zero ? *value < 0 : *value <= 0)) {
        Invalid(field, allow_zero ? "must be finite and nonnegative" : "must be finite and positive");
    }
    return *value;
}

void Require(const conf::Value& node, std::string_view key, std::string_view field) {
    if (!node[std::string(key)]) Invalid(field, "missing required key '" + std::string(key) + "'");
}

std::map<std::string, double> PositiveStringMap(const conf::Value& node, std::string_view field) {
    if (node.kind() != conf::Node_Kind::Map) Invalid(field, "must be a mapping");
    std::map<std::string, double> result;
    for (const auto& key : node.keys()) result.emplace(key, Positive(node[key], std::string(field) + "." + key));
    return result;
}

std::map<FireBulkSpecies, double> PositiveBulkMap(const conf::Value& node, std::string_view field) {
    if (node.kind() != conf::Node_Kind::Map) Invalid(field, "must be a mapping");
    std::map<FireBulkSpecies, double> result;
    for (const auto& key : node.keys()) {
        FireBulkSpecies species;
        try {
            species = FireSpeciesFromName(key);
        } catch (const std::invalid_argument&) {
            Invalid(field, "unknown canonical fire species '" + key + "'");
        }
        result.emplace(species, Positive(node[key], std::string(field) + "." + key));
    }
    return result;
}

FireMappingBasis ParseBasis(std::string_view basis) {
    if (basis == "mass") return FireMappingBasis::Mass;
    if (basis == "molar") return FireMappingBasis::Molar;
    if (basis == "carbon_molar") return FireMappingBasis::CarbonMolar;
    if (basis == "number") return FireMappingBasis::Number;
    Invalid("typed_mappings.basis", "must be mass, molar, carbon_molar or number");
}

FireBulkSpecies ParseSpecies(std::string_view text, std::string_view field) {
    try {
        return FireSpeciesFromName(text);
    } catch (const std::invalid_argument&) {
        Invalid(field, "must name a canonical bulk fire species");
    }
}

bool Close(double lhs, double rhs, double tolerance = 1e-12) {
    return std::abs(lhs - rhs) <= tolerance;
}

}  // namespace

static PhysicsRegistration<FireScheme> register_fire_scheme("fire");

void FireScheme::Initialize(const conf::Value& config, CeceDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);
    staged_bulk_.reset();
    staged_target_specific_.reset();
    staged_bulk_view_ = nullptr;
    staged_target_specific_view_ = nullptr;
    profile_.reset();
    requested_species_.clear();
    native_diagnostic_species_.clear();
    target_id_ = config["target_id"].string_or("");
    target_version_ = config["target_version"].string_or("");
    if (target_id_.empty() || target_version_.empty()) {
        throw std::invalid_argument("Fire scheme requires nonempty target_id and target_version");
    }

    const auto chemistry_state = config["chemistry_state"].string_or("bulk");
    if (chemistry_state == "bulk") {
        const auto mapping_file = config["fire_mapping_file"].string_or("");
        if (mapping_file.empty()) throw std::invalid_argument("Fire bulk chemistry requires fire_mapping_file");
        const auto requested = config["requested_species"];
        if (requested.kind() != conf::Node_Kind::Sequence || requested.size() == 0) {
            throw std::invalid_argument("Fire bulk chemistry requires a nonempty requested_species list");
        }
        for (const auto& species_name : requested.as_string_list()) {
            requested_species_.push_back(FireSpeciesFromName(species_name));
        }
        if (config["native_diagnostic_species"]) {
            for (const auto& species_name : config["native_diagnostic_species"].as_string_list()) {
                native_diagnostic_species_.push_back(FireSpeciesFromName(species_name));
            }
        }
        profile_ = LoadFireTargetProfile(mapping_file, target_id_, target_version_);
    } else if (chemistry_state == "target_specific") {
        if (config["fire_mapping_file"]) {
            throw std::invalid_argument("Fire target_specific chemistry must bypass fire_mapping_file");
        }
    } else {
        throw std::invalid_argument("Fire chemistry_state must be bulk or target_specific");
    }
}

std::vector<std::string> FireScheme::ExportFieldNames() const {
    std::vector<std::string> names;
    if (!profile_) return names;
    std::set<FireBulkSpecies> requested(requested_species_.begin(), requested_species_.end());
    std::map<std::string, std::string> targets;
    for (const auto& mapping : profile_->mappings) {
        if (!requested.contains(mapping.source_species)) continue;
        if (mapping.output_unit != "kg") {
            throw std::invalid_argument("FireScheme currently exports only mass-based target fields in kg");
        }
        targets.emplace(mapping.target_species, mapping.output_unit);
    }
    if (targets.empty()) throw std::invalid_argument("Fire target profile produces no requested target fields");
    std::set<std::string> output_names;
    for (const auto& target : profile_->mechanism_species) {
        if (!targets.contains(target.name)) continue;
        const auto name = target.name.starts_with("FIRE_") ? target.name : "FIRE_" + target.name;
        auto output_name = MapOutput(name);
        if (!output_names.insert(output_name).second) {
            throw std::invalid_argument("Fire target fields map to duplicate output '" + output_name + "'");
        }
        names.push_back(std::move(output_name));
    }
    for (const auto species : native_diagnostic_species_) {
        auto name = MapOutput("FIRE_NATIVE_" + std::string(FireSpeciesName(species)));
        if (!output_names.insert(name).second) {
            throw std::invalid_argument("Fire diagnostic fields map to duplicate output '" + name + "'");
        }
        names.push_back(std::move(name));
    }
    return names;
}

void FireScheme::StageBulkInventory(FireBulkInventory inventory) {
    if (!profile_) throw std::logic_error("Fire bulk inventory cannot be staged unless bulk chemistry is configured");
    if (staged_bulk_view_ || staged_target_specific_view_) throw std::logic_error("Fire scheme already has an unconsumed interval staged");
    staged_bulk_.emplace(std::move(inventory));
    staged_bulk_view_ = &*staged_bulk_;
}

void FireScheme::StageTargetSpecificInventory(FireTargetSpecificInventory inventory) {
    if (profile_) throw std::logic_error("Fire target-specific inventory cannot be staged when bulk chemistry is configured");
    if (staged_bulk_view_ || staged_target_specific_view_) throw std::logic_error("Fire scheme already has an unconsumed interval staged");
    if (inventory.target_id != target_id_ || inventory.target_version != target_version_) {
        throw std::invalid_argument("Fire target-specific inventory target id/version does not match the configured target");
    }
    staged_target_specific_.emplace(std::move(inventory));
    staged_target_specific_view_ = &*staged_target_specific_;
}

void FireScheme::StageBorrowedBulkInventory(const FireBulkInventory& inventory) {
    if (!profile_) throw std::logic_error("Fire bulk inventory cannot be staged unless bulk chemistry is configured");
    if (staged_bulk_view_ || staged_target_specific_view_) throw std::logic_error("Fire scheme already has an unconsumed interval staged");
    staged_bulk_view_ = &inventory;
}

void FireScheme::StageBorrowedTargetSpecificInventory(const FireTargetSpecificInventory& inventory) {
    if (profile_) throw std::logic_error("Fire target-specific inventory cannot be staged when bulk chemistry is configured");
    if (staged_bulk_view_ || staged_target_specific_view_) throw std::logic_error("Fire scheme already has an unconsumed interval staged");
    if (inventory.target_id != target_id_ || inventory.target_version != target_version_) {
        throw std::invalid_argument("Fire target-specific inventory target id/version does not match the configured target");
    }
    staged_target_specific_view_ = &inventory;
}

void FireScheme::DiscardStagedInventory() noexcept {
    staged_bulk_view_ = nullptr;
    staged_target_specific_view_ = nullptr;
    staged_bulk_.reset();
    staged_target_specific_.reset();
}

void FireScheme::ExportMassFlux(const FireInputIdentity& identity, const std::vector<FireMappedField>& fields, const std::vector<double>& mass_kg,
                                CeceExportState& export_state) {
    const auto& metadata = identity.Metadata();
    if (!metadata.grid_definition) throw std::invalid_argument("Fire output identity has no grid definition");
    const auto& grid = metadata.grid_definition->Definition();
    const auto cells = metadata.grid_definition->CellCount();
    const auto duration_seconds = identity.DurationSeconds();
    if (duration_seconds <= 0 || grid.area_m2.size() != cells) throw std::invalid_argument("Fire output grid/time dimensions are invalid");
    if (fields.empty() || cells > std::numeric_limits<std::size_t>::max() / fields.size() || mass_kg.size() != fields.size() * cells) {
        throw std::invalid_argument("Fire output mass dimensions do not match fields and grid");
    }
    for (std::size_t cell = 0; cell < cells; ++cell) {
        if (!std::isfinite(grid.area_m2[cell]) || grid.area_m2[cell] <= 0) {
            throw std::invalid_argument("Fire output cell area must be finite and positive");
        }
    }
    for (const auto mass : mass_kg) {
        if (!std::isfinite(mass) || mass < 0) throw std::invalid_argument("Fire output mass must be finite and nonnegative");
    }

    std::vector<std::pair<std::string, std::unordered_map<std::string, DualView3D>::iterator>> outputs;
    std::set<std::string> output_names;
    for (std::size_t field = 0; field < fields.size(); ++field) {
        if (fields[field].unit != "kg") throw std::invalid_argument("FireScheme currently exports only mass-based target fields in kg");
        const auto name = fields[field].target_species.starts_with("FIRE_") ? fields[field].target_species : "FIRE_" + fields[field].target_species;
        const auto mapped_name = MapOutput(name);
        if (!output_names.insert(mapped_name).second) throw std::invalid_argument("Fire target fields map to duplicate output '" + mapped_name + "'");
        const auto output = export_state.fields.find(mapped_name);
        if (output == export_state.fields.end()) throw std::runtime_error("Fire export state is missing required field '" + mapped_name + "'");
        if (output->second.extent(0) != grid.nx || output->second.extent(1) != grid.ny || output->second.extent(2) != 1) {
            throw std::invalid_argument("Fire export field '" + mapped_name + "' dimensions do not match the surface fire grid");
        }
        outputs.emplace_back(mapped_name, output);
    }

    for (std::size_t field = 0; field < fields.size(); ++field) {
        auto& output = outputs[field].second->second;
        output.sync_host();
        auto host = output.view_host();
        for (std::size_t cell = 0; cell < cells; ++cell) {
            const auto mass = mass_kg[field * cells + cell];
            const auto area = grid.area_m2[cell];
            const auto i = cell % grid.nx;
            const auto j = cell / grid.nx;
            host(i, j, 0) = mass / (area * static_cast<double>(duration_seconds));
        }
        output.modify_host();
        output.sync_device();
    }
}

void FireScheme::Run(CeceImportState& import_state, CeceExportState& export_state) {
    (void)import_state;
    if (staged_bulk_view_) {
        if (!profile_) throw std::logic_error("Fire scheme has no initialized target profile");
        const auto mapped = MapFireBulkInventory(*staged_bulk_view_, profile_.value(), requested_species_);
        const auto cells = mapped.source_identity.Metadata().grid_definition->CellCount();
        const auto component_count = mapped.component_ids.size();
        std::vector<double> mass_kg(mapped.fields.size() * cells, 0);
        for (std::size_t component = 0; component < component_count; ++component) {
            for (std::size_t field = 0; field < mapped.fields.size(); ++field) {
                for (std::size_t cell = 0; cell < cells; ++cell) {
                    mass_kg[field * cells + cell] += mapped.values[(component * mapped.fields.size() + field) * cells + cell];
                }
            }
        }
        auto fields = mapped.fields;
        for (const auto species : native_diagnostic_species_) {
            const auto& diagnostics = staged_bulk_view_->Data().native_diagnostic_species;
            if (std::find(diagnostics.begin(), diagnostics.end(), species) == diagnostics.end()) {
                throw std::invalid_argument("Fire inventory lacks requested native diagnostic '" + std::string(FireSpeciesName(species)) + "'");
            }
            fields.push_back({"FIRE_NATIVE_" + std::string(FireSpeciesName(species)), "kg"});
            for (std::size_t cell = 0; cell < cells; ++cell) {
                mass_kg.push_back(staged_bulk_view_->BulkMassKg(species, cell));
            }
        }
        ExportMassFlux(mapped.source_identity, fields, mass_kg, export_state);
        staged_bulk_view_ = nullptr;
        staged_bulk_.reset();
        return;
    }
    if (staged_target_specific_view_) {
        ExportMassFlux(staged_target_specific_view_->source_identity, staged_target_specific_view_->fields, staged_target_specific_view_->mass_kg,
                       export_state);
        staged_target_specific_view_ = nullptr;
        staged_target_specific_.reset();
        return;
    }
    throw std::runtime_error("Fire scheme has no staged interval inventory; core provider integration is required");
}

FireTargetProfile LoadFireTargetProfile(const std::string& path, const std::string& expected_target_id, const std::string& expected_target_version) {
    if (expected_target_id.empty() || expected_target_version.empty()) Invalid("identity", "expected target id/version are required");
    if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("Fire target profile file not found: " + path);
    const auto profile_path = std::filesystem::canonical(path);
    conf::Config file = conf::Config::from_file(path);
    const auto& root = file.root();
    OnlyKeys(root,
             {"schema_version", "production_eligible", "mapping_factor_semantics", "identity", "assets", "mechanism_species", "typed_mappings",
              "conversion_factors", "nox_partition", "oc_om_policy", "source_disposition", "budgets_tolerances"},
             "root");
    Require(root, "schema_version", "schema_version");
    if (root["schema_version"].try_int() != std::optional<int>{1}) Invalid("schema_version", "only version 1 is supported");
    Require(root, "production_eligible", "production_eligible");
    if (root["production_eligible"].try_bool() != std::optional<bool>{false}) {
        Invalid("production_eligible", "production certification is not available through this loader");
    }

    FireTargetProfile profile{};
    const auto identity = root["identity"];
    OnlyKeys(identity, {"id", "version", "target_id", "target_version"}, "identity");
    for (const auto key : {"id", "version", "target_id", "target_version"}) Require(identity, key, "identity");
    profile.identity.id = Text(identity["id"], "identity.id");
    profile.identity.version = Text(identity["version"], "identity.version");
    profile.target_id = Text(identity["target_id"], "identity.target_id");
    profile.target_version = Text(identity["target_version"], "identity.target_version");
    if (profile.target_id != expected_target_id || profile.target_version != expected_target_version) {
        Invalid("identity", "profile target id/version does not match the requested configuration");
    }
    profile.identity.content_sha256 = FireFileSha256(profile_path.string());
    profile.production_eligible = false;
    const auto factor_semantics = root["mapping_factor_semantics"].string_or("partition");
    if (factor_semantics == "partition") {
        profile.mapping_factor_semantics = FireMappingFactorSemantics::Partition;
    } else if (factor_semantics == "independent_coefficients") {
        profile.mapping_factor_semantics = FireMappingFactorSemantics::IndependentCoefficients;
    } else {
        Invalid("mapping_factor_semantics", "must be partition or independent_coefficients");
    }

    const auto assets = root["assets"];
    if (assets.kind() != conf::Node_Kind::Sequence || assets.size() == 0) Invalid("assets", "must be a nonempty list of hash-pinned assets");
    for (std::size_t i = 0; i < assets.size(); ++i) {
        const auto asset = assets[i];
        const auto field = "assets[" + std::to_string(i) + "]";
        OnlyKeys(asset, {"name", "path", "sha256"}, field);
        for (const auto key : {"name", "path", "sha256"}) Require(asset, key, field);
        const auto name = Text(asset["name"], field + ".name");
        const auto asset_path = std::filesystem::weakly_canonical(profile_path.parent_path() / Text(asset["path"], field + ".path"));
        const auto expected_hash = Text(asset["sha256"], field + ".sha256");
        if (!IsSha256(expected_hash)) Invalid(field + ".sha256", "must contain lowercase SHA256");
        if (!profile.asset_hashes.emplace(name, expected_hash).second) Invalid(field + ".name", "must be unique");
        if (FireFileSha256(asset_path.string()) != expected_hash) Invalid(field, "asset content SHA256 does not match the declared digest");
    }

    const auto mechanisms = root["mechanism_species"];
    if (mechanisms.kind() != conf::Node_Kind::Sequence || mechanisms.size() == 0) Invalid("mechanism_species", "must be a nonempty list");
    std::set<std::string> mechanism_names;
    for (std::size_t i = 0; i < mechanisms.size(); ++i) {
        const auto species = mechanisms[i];
        const auto field = "mechanism_species[" + std::to_string(i) + "]";
        OnlyKeys(species, {"name", "molecular_weight_g_mol"}, field);
        Require(species, "name", field);
        std::optional<double> molecular_weight;
        if (species["molecular_weight_g_mol"]) {
            molecular_weight = Positive(species["molecular_weight_g_mol"], field + ".molecular_weight_g_mol");
        }
        FireTargetSpecies target{Text(species["name"], field + ".name"), molecular_weight};
        if (!mechanism_names.insert(target.name).second) Invalid(field + ".name", "target species names must be unique");
        profile.mechanism_species.push_back(std::move(target));
    }

    const auto conversions = root["conversion_factors"];
    OnlyKeys(
        conversions,
        {"source_molecular_weights_g_mol", "target_molecular_weights_g_mol", "source_carbon_numbers", "target_carbon_numbers", "aerosol_factors"},
        "conversion_factors");
    for (const auto key :
         {"source_molecular_weights_g_mol", "target_molecular_weights_g_mol", "source_carbon_numbers", "target_carbon_numbers", "aerosol_factors"})
        Require(conversions, key, "conversion_factors");
    profile.conversion_factors.source_molecular_weights_g_mol =
        PositiveBulkMap(conversions["source_molecular_weights_g_mol"], "conversion_factors.source_molecular_weights_g_mol");
    profile.conversion_factors.target_molecular_weights_g_mol =
        PositiveStringMap(conversions["target_molecular_weights_g_mol"], "conversion_factors.target_molecular_weights_g_mol");
    profile.conversion_factors.source_carbon_numbers =
        PositiveBulkMap(conversions["source_carbon_numbers"], "conversion_factors.source_carbon_numbers");
    profile.conversion_factors.target_carbon_numbers =
        PositiveStringMap(conversions["target_carbon_numbers"], "conversion_factors.target_carbon_numbers");
    if (conversions["aerosol_factors"].kind() != conf::Node_Kind::Map) Invalid("conversion_factors.aerosol_factors", "must be a mapping");
    if (conversions["aerosol_factors"].size() != 0) {
        profile.conversion_factors.aerosol_factors = PositiveStringMap(conversions["aerosol_factors"], "conversion_factors.aerosol_factors");
    }
    for (const auto& target : profile.mechanism_species) {
        if (const auto found = profile.conversion_factors.target_molecular_weights_g_mol.find(target.name);
            found != profile.conversion_factors.target_molecular_weights_g_mol.end() && target.molecular_weight_g_mol &&
            !Close(found->second, *target.molecular_weight_g_mol)) {
            Invalid("conversion_factors.target_molecular_weights_g_mol", "must agree with mechanism_species molecular weights");
        }
    }

    const auto mappings = root["typed_mappings"];
    if (mappings.kind() != conf::Node_Kind::Sequence || mappings.size() == 0) Invalid("typed_mappings", "must be a nonempty list");
    for (std::size_t i = 0; i < mappings.size(); ++i) {
        const auto mapping = mappings[i];
        const auto field = "typed_mappings[" + std::to_string(i) + "]";
        OnlyKeys(mapping, {"source_species", "target_species", "basis", "factor", "output_unit", "composition_profile"}, field);
        for (const auto key : {"source_species", "target_species", "basis", "factor", "output_unit"}) Require(mapping, key, field);
        FireTypedMapping typed{ParseSpecies(Text(mapping["source_species"], field + ".source_species"), field + ".source_species"),
                               Text(mapping["target_species"], field + ".target_species"),
                               ParseBasis(Text(mapping["basis"], field + ".basis")),
                               Positive(mapping["factor"], field + ".factor", true),
                               Text(mapping["output_unit"], field + ".output_unit"),
                               std::nullopt};
        if (mapping["composition_profile"]) {
            const auto composition = mapping["composition_profile"];
            OnlyKeys(composition, {"id", "version", "content_sha256"}, field + ".composition_profile");
            for (const auto key : {"id", "version", "content_sha256"}) Require(composition, key, field + ".composition_profile");
            FireProfileIdentity identity{Text(composition["id"], field + ".composition_profile.id"),
                                         Text(composition["version"], field + ".composition_profile.version"),
                                         Text(composition["content_sha256"], field + ".composition_profile.content_sha256")};
            if (!IsSha256(identity.content_sha256)) Invalid(field + ".composition_profile.content_sha256", "must contain lowercase SHA256");
            typed.composition_profile = std::move(identity);
        }
        if (!mechanism_names.contains(typed.target_species)) Invalid(field + ".target_species", "is absent from mechanism_species");
        const auto basis_unit = typed.basis == FireMappingBasis::Mass          ? "kg"
                                : typed.basis == FireMappingBasis::Molar       ? "kmol"
                                : typed.basis == FireMappingBasis::CarbonMolar ? "kmol-C"
                                                                               : "count";
        if (typed.output_unit != basis_unit) Invalid(field + ".output_unit", "does not match the declared mapping basis");
        const auto source = typed.source_species;
        const auto& factors = profile.conversion_factors;
        if (typed.basis == FireMappingBasis::Molar || typed.basis == FireMappingBasis::CarbonMolar) {
            if (!factors.source_molecular_weights_g_mol.contains(source) || !factors.target_molecular_weights_g_mol.contains(typed.target_species)) {
                Invalid(field + ".basis", "requires source and target molecular weights");
            }
        }
        if (typed.basis == FireMappingBasis::CarbonMolar &&
            (!factors.source_carbon_numbers.contains(source) || !factors.target_carbon_numbers.contains(typed.target_species))) {
            Invalid(field + ".basis", "requires source and target carbon numbers");
        }
        if (typed.basis == FireMappingBasis::Number && !factors.aerosol_factors.contains(typed.target_species)) {
            Invalid(field + ".basis", "requires a named target aerosol factor");
        }
        profile.mappings.push_back(std::move(typed));
    }

    const auto nox = root["nox_partition"];
    if (nox) {
        OnlyKeys(nox, {"reference_basis", "no_mass_fraction", "no2_mass_fraction", "no_target_species", "no2_target_species", "partition_basis"},
                 "nox_partition");
        for (const auto key : {"reference_basis", "no_mass_fraction", "no2_mass_fraction", "no_target_species", "no2_target_species"})
            Require(nox, key, "nox_partition");
        const auto basis = Text(nox["reference_basis"], "nox_partition.reference_basis");
        if (basis != "NO" && basis != "NO2") Invalid("nox_partition.reference_basis", "must be NO or NO2");
        FireNoxPartition partition{basis == "NO" ? FireNoxReferenceBasis::NO : FireNoxReferenceBasis::NO2,
                                   Positive(nox["no_mass_fraction"], "nox_partition.no_mass_fraction", true),
                                   Positive(nox["no2_mass_fraction"], "nox_partition.no2_mass_fraction", true),
                                   Text(nox["no_target_species"], "nox_partition.no_target_species"),
                                   Text(nox["no2_target_species"], "nox_partition.no2_target_species")};
        if (!mechanism_names.contains(partition.no_target_species) || !mechanism_names.contains(partition.no2_target_species) ||
            partition.no_target_species == partition.no2_target_species) {
            Invalid("nox_partition", "distinct target species must exist in the mechanism");
        }
        const auto partition_basis = nox["partition_basis"].string_or("mass");
        double total = partition.no_mass_fraction + partition.no2_mass_fraction;
        if (partition_basis == "molar_equivalent") {
            if (partition.reference_basis != FireNoxReferenceBasis::NO2 ||
                !profile.conversion_factors.source_molecular_weights_g_mol.contains(FireBulkSpecies::NOX) ||
                !profile.conversion_factors.target_molecular_weights_g_mol.contains(partition.no_target_species) ||
                !profile.conversion_factors.target_molecular_weights_g_mol.contains(partition.no2_target_species)) {
                Invalid("nox_partition", "molar_equivalent requires NO2 reference and source, NO and NO2 molecular weights");
            }
            const auto reference_weight = profile.conversion_factors.source_molecular_weights_g_mol.at(FireBulkSpecies::NOX);
            const auto no_weight = profile.conversion_factors.target_molecular_weights_g_mol.at(partition.no_target_species);
            const auto no2_weight = profile.conversion_factors.target_molecular_weights_g_mol.at(partition.no2_target_species);
            if (!Close(reference_weight, no2_weight)) {
                Invalid("nox_partition", "NO2 target molecular weight must match the NOX reference weight");
            }
            total = partition.no_mass_fraction * reference_weight / no_weight + partition.no2_mass_fraction * reference_weight / no2_weight;
            partition.partition_basis = FireNoxPartitionBasis::MolarEquivalent;
        } else if (partition_basis != "mass") {
            Invalid("nox_partition.partition_basis", "must be mass or molar_equivalent");
        }
        if (!Close(total, 1)) {
            Invalid("nox_partition", "partition must conserve the declared mass or molar-equivalent basis");
        }
        profile.nox_partition = std::move(partition);
    }

    const auto oc = root["oc_om_policy"];
    OnlyKeys(oc, {"kind", "om_per_oc"}, "oc_om_policy");
    Require(oc, "kind", "oc_om_policy");
    const auto oc_kind = Text(oc["kind"], "oc_om_policy.kind");
    if (oc_kind == "preserve_oc") {
        if (oc["om_per_oc"]) Invalid("oc_om_policy.om_per_oc", "is prohibited with preserve_oc");
        profile.oc_om_policy = FireOcOmPolicy{FireOcOmPolicyKind::PreserveOc, 1};
    } else if (oc_kind == "convert_with_factor") {
        Require(oc, "om_per_oc", "oc_om_policy");
        profile.oc_om_policy = FireOcOmPolicy{FireOcOmPolicyKind::ConvertWithFactor, Positive(oc["om_per_oc"], "oc_om_policy.om_per_oc")};
    } else {
        Invalid("oc_om_policy.kind", "must be preserve_oc or convert_with_factor");
    }

    const auto dispositions = root["source_disposition"];
    OnlyKeys(dispositions, {"CO2", "CO", "CH4", "PM25", "TPM", "SO2", "OC", "BC", "NOX", "NH3", "VOC"}, "source_disposition");
    std::map<FireBulkSpecies, std::size_t> mapping_counts;
    std::map<FireBulkSpecies, FireMappingBasis> mapping_basis;
    std::map<FireBulkSpecies, double> factor_totals;
    std::map<std::pair<FireBulkSpecies, std::string>, double> component_factor_totals;
    std::map<FireBulkSpecies, std::size_t> invariant_mapping_counts;
    std::map<FireBulkSpecies, std::set<std::string>> invariant_targets;
    std::map<FireBulkSpecies, std::set<std::string>> component_targets;
    for (const auto& mapping : profile.mappings) {
        ++mapping_counts[mapping.source_species];
        if (mapping.composition_profile) {
            component_factor_totals[{mapping.source_species, mapping.composition_profile->id + "\n" + mapping.composition_profile->version + "\n" +
                                                                 mapping.composition_profile->content_sha256}] += mapping.factor;
            component_targets[mapping.source_species].insert(mapping.target_species);
        } else {
            ++invariant_mapping_counts[mapping.source_species];
            invariant_targets[mapping.source_species].insert(mapping.target_species);
        }
    }
    for (const auto& mapping : profile.mappings) {
        const auto [it, inserted] = mapping_basis.emplace(mapping.source_species, mapping.basis);
        if (!inserted && it->second != mapping.basis) {
            Invalid("typed_mappings", "one bulk source cannot mix quantity bases");
        }
        if (!mapping.composition_profile) factor_totals[mapping.source_species] += mapping.factor;
    }
    for (const auto& [key, total] : component_factor_totals) {
        const auto species = key.first;
        if (invariant_mapping_counts[species] != 0 && profile.mapping_factor_semantics != FireMappingFactorSemantics::IndependentCoefficients) {
            Invalid("typed_mappings", "mixed invariant and composition-specific mappings require independent coefficients");
        }
        if (profile.mapping_factor_semantics == FireMappingFactorSemantics::Partition && !Close(total, 1)) {
            Invalid("typed_mappings", "composition-specific mapping factors must form a unit-sum partition for each composition profile");
        }
        for (const auto& [species, targets] : component_targets) {
            for (const auto& target : targets) {
                if (invariant_targets[species].contains(target)) {
                    Invalid("typed_mappings", "invariant and composition-specific mappings for a source must use distinct targets");
                }
            }
        }
    }
    for (const auto species_name : {"CO2", "CO", "CH4", "PM25", "TPM", "SO2", "OC", "BC", "NOX", "NH3", "VOC"}) {
        const auto species = FireSpeciesFromName(species_name);
        if (!dispositions[species_name]) Invalid("source_disposition", "must declare every canonical bulk species");
        const auto entry = dispositions[species_name];
        const auto field = "source_disposition." + std::string(species_name);
        OnlyKeys(entry, {"kind", "reason"}, field);
        Require(entry, "kind", field);
        const auto kind = Text(entry["kind"], field + ".kind");
        FireSourceDisposition disposition;
        if (kind == "mapped")
            disposition.kind = FireSourceDispositionKind::Mapped;
        else if (kind == "diagnostic_only")
            disposition.kind = FireSourceDispositionKind::DiagnosticOnly;
        else if (kind == "excluded")
            disposition.kind = FireSourceDispositionKind::Excluded;
        else
            Invalid(field + ".kind", "must be mapped, diagnostic_only or excluded");
        if (entry["reason"]) disposition.reason = Text(entry["reason"], field + ".reason");
        if (disposition.kind == FireSourceDispositionKind::Mapped && mapping_counts[species] == 0) {
            Invalid(field, "mapped sources require at least one typed mapping");
        }
        if (profile.mapping_factor_semantics == FireMappingFactorSemantics::Partition && disposition.kind == FireSourceDispositionKind::Mapped &&
            invariant_mapping_counts[species] > 0 &&
            !(species == FireBulkSpecies::NOX && profile.nox_partition &&
              profile.nox_partition->partition_basis == FireNoxPartitionBasis::MolarEquivalent) &&
            !Close(factor_totals[species], 1)) {
            Invalid(field, "mapping factors for each source must form an explicit unit-sum partition");
        }
        if (disposition.kind != FireSourceDispositionKind::Mapped && mapping_counts[species] != 0) {
            Invalid(field, "diagnostic-only/excluded sources cannot have target mappings");
        }
        if (disposition.kind != FireSourceDispositionKind::Mapped && disposition.reason.empty()) {
            Invalid(field + ".reason", "non-mapped disposition requires an explicit reason");
        }
        profile.dispositions.emplace(species, std::move(disposition));
    }
    for (const auto& mapping : profile.mappings) {
        if (profile.dispositions.at(mapping.source_species).kind != FireSourceDispositionKind::Mapped) {
            Invalid("typed_mappings", "source species disposition must be mapped");
        }
    }
    if (mapping_counts[FireBulkSpecies::NOX] > 0) {
        if (!profile.nox_partition) Invalid("nox_partition", "is required when NOX is mapped");
        double mapped_no = 0, mapped_no2 = 0;
        for (const auto& mapping : profile.mappings) {
            if (mapping.source_species != FireBulkSpecies::NOX) continue;
            if (mapping.basis != FireMappingBasis::Mass) Invalid("typed_mappings", "NOX partition mappings must retain a mass basis");
            if (mapping.target_species == profile.nox_partition->no_target_species)
                mapped_no += mapping.factor;
            else if (mapping.target_species == profile.nox_partition->no2_target_species)
                mapped_no2 += mapping.factor;
            else
                Invalid("typed_mappings", "NOX mappings must use the declared NO/NO2 partition targets");
        }
        if (!Close(mapped_no, profile.nox_partition->no_mass_fraction) || !Close(mapped_no2, profile.nox_partition->no2_mass_fraction)) {
            Invalid("nox_partition", "fractions must agree with the explicit NOX mass mappings");
        }
    }

    const auto tolerances = root["budgets_tolerances"];
    OnlyKeys(tolerances, {"relative", "absolute"}, "budgets_tolerances");
    Require(tolerances, "relative", "budgets_tolerances");
    Require(tolerances, "absolute", "budgets_tolerances");
    profile.budget_tolerances = FireBudgetTolerances{Positive(tolerances["relative"], "budgets_tolerances.relative"),
                                                     Positive(tolerances["absolute"], "budgets_tolerances.absolute", true)};
    if (profile.budget_tolerances.relative > 1) Invalid("budgets_tolerances.relative", "must not exceed 1");
    return profile;
}

FireMappedInventory MapFireBulkInventory(const FireBulkInventory& inventory, const FireTargetProfile& profile,
                                         const std::vector<FireBulkSpecies>& required_species) {
    if (profile.production_eligible) {
        throw std::invalid_argument("Fire mapper cannot consume production-eligible profiles without registry certification");
    }
    if (required_species.empty()) throw std::invalid_argument("Fire mapping requires an explicit nonempty source species set");
    std::set<FireBulkSpecies> required;
    for (const auto species : required_species) {
        if (!required.insert(species).second) throw std::invalid_argument("Fire mapping source species requests must be unique");
        if (std::find(inventory.Data().native_diagnostic_species.begin(), inventory.Data().native_diagnostic_species.end(), species) !=
            inventory.Data().native_diagnostic_species.end()) {
            throw std::invalid_argument("Fire native diagnostics cannot be used as mapped chemistry inputs");
        }
        if (std::find(inventory.Data().species.begin(), inventory.Data().species.end(), species) == inventory.Data().species.end()) {
            throw std::invalid_argument("Fire mapping input lacks requested bulk species '" + std::string(FireSpeciesName(species)) + "'");
        }
        const auto disposition = profile.dispositions.find(species);
        if (disposition == profile.dispositions.end() || disposition->second.kind != FireSourceDispositionKind::Mapped) {
            throw std::invalid_argument("Fire target profile does not map requested species '" + std::string(FireSpeciesName(species)) + "'");
        }
    }
    if (required.contains(FireBulkSpecies::NOX) && (!inventory.Data().nox_reference_basis || !profile.nox_partition ||
                                                    *inventory.Data().nox_reference_basis != profile.nox_partition->reference_basis)) {
        throw std::invalid_argument("Fire NOX input reference mass does not match the target profile partition");
    }

    for (const auto species : required) {
        const bool composition_specific = std::any_of(profile.mappings.begin(), profile.mappings.end(), [&](const auto& mapping) {
            return mapping.source_species == species && mapping.composition_profile.has_value();
        });
        if (!composition_specific) continue;
        for (const auto& component_id : inventory.Data().component_ids) {
            const auto component_profile = inventory.Data().component_profiles.find(component_id);
            if (component_profile == inventory.Data().component_profiles.end()) {
                throw std::invalid_argument("Fire target profile requires composition identity for source species '" +
                                            std::string(FireSpeciesName(species)) + "' but component '" + component_id +
                                            "' has no biome/composition profile");
            }
            const auto& composition = component_profile->second.composition;
            const bool found = std::any_of(profile.mappings.begin(), profile.mappings.end(), [&](const auto& mapping) {
                return mapping.source_species == species && mapping.composition_profile == composition;
            });
            if (!found) {
                throw std::invalid_argument("Fire target profile has no composition-specific mapping for source species '" +
                                            std::string(FireSpeciesName(species)) + "' and component '" + component_id + "'");
            }
        }
    }

    std::map<std::string, std::string> target_units;
    for (const auto& mapping : profile.mappings) {
        if (!required.contains(mapping.source_species)) continue;
        const auto [found, inserted] = target_units.emplace(mapping.target_species, mapping.output_unit);
        if (!inserted && found->second != mapping.output_unit) {
            throw std::invalid_argument("Fire target species '" + mapping.target_species + "' is mapped in incompatible units");
        }
    }
    FireMappedInventory result{inventory.Data().identity,
                               profile.identity,
                               inventory.Data().component_ids,
                               inventory.Data().component_profiles,
                               {},
                               {},
                               {},
                               inventory.Data().unprofiled_component_ids};
    for (const auto& target : profile.mechanism_species) {
        const auto found = target_units.find(target.name);
        if (found != target_units.end()) result.fields.push_back({target.name, found->second});
    }
    if (result.fields.empty()) throw std::invalid_argument("Fire target profile produces no requested target fields");
    const auto cells = inventory.Data().identity.Metadata().grid_definition->CellCount();
    const auto component_count = result.component_ids.size();
    if (result.fields.size() > std::numeric_limits<std::size_t>::max() / component_count ||
        result.fields.size() * component_count > std::numeric_limits<std::size_t>::max() / cells) {
        throw std::invalid_argument("Fire mapped output dimensions overflow");
    }
    result.values.assign(component_count * result.fields.size() * cells, 0);
    std::map<std::string, std::size_t> target_indexes;
    for (std::size_t index = 0; index < result.fields.size(); ++index) target_indexes.emplace(result.fields[index].target_species, index);
    std::map<std::pair<std::string, std::string>, double> budgets;
    const auto& conversions = profile.conversion_factors;
    for (std::size_t component = 0; component < component_count; ++component) {
        for (const auto& mapping : profile.mappings) {
            if (!required.contains(mapping.source_species)) continue;
            if (mapping.composition_profile) {
                const auto component_profile = inventory.Data().component_profiles.find(result.component_ids[component]);
                if (component_profile == inventory.Data().component_profiles.end()) continue;
                if (*mapping.composition_profile != component_profile->second.composition) continue;
            }
            const auto target_index = target_indexes.at(mapping.target_species);
            for (std::size_t cell = 0; cell < cells; ++cell) {
                const double source_mass = inventory.ComponentMassKg(component, mapping.source_species, cell);
                double quantity = 0;
                switch (mapping.basis) {
                    case FireMappingBasis::Mass:
                        quantity = source_mass * mapping.factor;
                        break;
                    case FireMappingBasis::Molar:
                        quantity = source_mass / conversions.source_molecular_weights_g_mol.at(mapping.source_species) * mapping.factor;
                        break;
                    case FireMappingBasis::CarbonMolar:
                        quantity = source_mass / conversions.source_molecular_weights_g_mol.at(mapping.source_species) *
                                   conversions.source_carbon_numbers.at(mapping.source_species) * mapping.factor;
                        break;
                    case FireMappingBasis::Number:
                        quantity = source_mass * conversions.aerosol_factors.at(mapping.target_species) * mapping.factor;
                        break;
                }
                if (!std::isfinite(quantity) || quantity < 0) {
                    throw std::runtime_error("Fire mapping generated nonfinite or negative target quantity");
                }
                result.values[(component * result.fields.size() + target_index) * cells + cell] += quantity;
                budgets[{mapping.target_species, mapping.output_unit}] += quantity;
            }
        }
    }
    for (const auto& field : result.fields) result.budgets.push_back({field.target_species, field.unit, budgets[{field.target_species, field.unit}]});
    return result;
}

FireTargetSpecificInventory FireProductEmissionsInterval::ToTargetSpecificInventory(std::string_view target_id,
                                                                                    std::string_view target_version) const {
    const auto& data = Data();
    if (data.chemistry_state != FireProductChemistryState::TargetSpecific || !data.target_identity) {
        throw std::invalid_argument("Bulk fire products cannot be converted to a target-specific inventory");
    }
    if (data.target_identity->id != target_id || data.target_identity->version != target_version) {
        throw std::invalid_argument("Fire product target identity does not match the requested target id/version");
    }
    std::vector<FireMappedField> fields;
    fields.reserve(data.target_fields.size());
    for (const auto& field : data.target_fields) fields.push_back({field.target_species, field.unit});
    return {data.product_identity.common_identity, data.target_identity->id, data.target_identity->version, std::move(fields), data.target_mass_kg};
}

}  // namespace cece
