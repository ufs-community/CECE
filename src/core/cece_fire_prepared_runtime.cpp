// SPDX-License-Identifier: Apache-2.0
#include "cece/cece_fire_runtime.hpp"

#include <Kokkos_Core.hpp>
#include <axis/solver/regrid_config.hpp>
#include <axis/solver/weight_generator.hpp>
#include <axis/topology/structured_grid.hpp>
#include <netcdf.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <numbers>
#include <set>
#include <stdexcept>
#include <string_view>

namespace cece {
namespace {

void NetcdfCheck(int status, std::string_view operation, const std::string& path) {
    if (status != NC_NOERR) throw std::runtime_error(std::string(operation) + " in '" + path + "': " + nc_strerror(status));
}

std::string Text(const YAML::Node& node, std::string_view field) {
    if (!node || !node.IsScalar()) throw std::invalid_argument(std::string(field) + " must be a nonempty string");
    const auto value = node.as<std::string>();
    if (value.find_first_not_of(" \t\r\n") == std::string::npos) throw std::invalid_argument(std::string(field) + " must be a nonempty string");
    return value;
}

FireUtcTime Time(const YAML::Node& node, std::string_view field) {
    try {
        return FireUtcTime(Text(node, field));
    } catch (const std::exception& error) {
        throw std::invalid_argument(std::string(field) + ": " + error.what());
    }
}

FireProfileIdentity ProfileIdentity(const YAML::Node& node, std::string_view field) {
    if (!node || !node.IsMap()) throw std::invalid_argument(std::string(field) + " must be a profile identity mapping");
    FireProfileIdentity result{Text(node["id"], std::string(field) + ".id"), Text(node["version"], std::string(field) + ".version"),
                               Text(node["content_sha256"], std::string(field) + ".content_sha256")};
    if (result.content_sha256.size() != 64 ||
        !std::all_of(result.content_sha256.begin(), result.content_sha256.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) {
        throw std::invalid_argument(std::string(field) + ".content_sha256 must be lowercase SHA256");
    }
    return result;
}

std::vector<double> ReadDoubles(int file, const std::string& path, std::string_view variable, std::size_t count) {
    int id = -1;
    NetcdfCheck(nc_inq_varid(file, std::string(variable).c_str(), &id), "missing variable '" + std::string(variable) + "'", path);
    std::vector<double> values(count);
    NetcdfCheck(nc_get_var_double(file, id, values.data()), "cannot read variable '" + std::string(variable) + "'", path);
    if (!std::all_of(values.begin(), values.end(), [](double value) { return std::isfinite(value); })) {
        throw std::invalid_argument("nonfinite values in '" + std::string(variable) + "' in '" + path + "'");
    }
    return values;
}

std::string GlobalText(int file, const std::string& path, std::string_view attribute) {
    std::size_t length = 0;
    NetcdfCheck(nc_inq_attlen(file, NC_GLOBAL, std::string(attribute).c_str(), &length),
                "missing attribute '" + std::string(attribute) + "'", path);
    std::string value(length, '\0');
    NetcdfCheck(nc_get_att_text(file, NC_GLOBAL, std::string(attribute).c_str(), value.data()),
                "cannot read attribute '" + std::string(attribute) + "'", path);
    while (!value.empty() && value.back() == '\0') value.pop_back();
    return value;
}

std::filesystem::path ManifestPath(const std::filesystem::path& manifest, const YAML::Node& node) {
    const auto relative = std::filesystem::path(Text(node, "interval.path"));
    if (relative.is_absolute()) throw std::invalid_argument("prepared interval paths must be relative to input_manifest");
    const auto root = std::filesystem::canonical(manifest.parent_path());
    const auto file = std::filesystem::canonical(root / relative);
    const auto from_root = file.lexically_relative(root);
    if (from_root.empty() || *from_root.begin() == ".." || !std::filesystem::is_regular_file(file)) {
        throw std::invalid_argument("prepared interval path escapes input_manifest directory or is not a regular file");
    }
    return file;
}

template <typename T>
Kokkos::View<T*, Kokkos::HostSpace> HostView(const std::vector<T>& values) {
    Kokkos::View<T*, Kokkos::HostSpace> view("fire_prepared_grid", values.size());
    for (std::size_t i = 0; i < values.size(); ++i) view(i) = values[i];
    return view;
}

axis::topology::UnstructuredMesh<Kokkos::HostSpace> AxisMesh(const FireGridDefinition& definition) {
    axis::topology::StructuredGrid<Kokkos::HostSpace> grid(definition.nx, definition.ny, HostView(definition.longitude_deg),
                                                           HostView(definition.latitude_deg), axis::topology::CoordinateSystem::SphericalDeg);
    std::vector<double> lons;
    std::vector<double> lats;
    lons.reserve((definition.nx + 1) * (definition.ny + 1));
    lats.reserve((definition.nx + 1) * (definition.ny + 1));
    for (std::size_t j = 0; j <= definition.ny; ++j) {
        for (std::size_t i = 0; i <= definition.nx; ++i) {
            const auto cell = std::min(j, definition.ny - 1) * definition.nx + std::min(i, definition.nx - 1);
            const auto corner = j == definition.ny ? (i == definition.nx ? 2 : 3) : (i == definition.nx ? 1 : 0);
            lons.push_back(definition.longitude_bounds_deg[cell][corner]);
            lats.push_back(definition.latitude_bounds_deg[cell][corner]);
        }
    }
    grid.set_corners(HostView(lons), HostView(lats));
    return grid.to_unstructured();
}

axis::solver::InterpolationMatrix<Kokkos::HostSpace> ConservativeMatrix(const FireGrid& source, const FireGrid& target) {
    axis::solver::RegridConfig config;
    config.method = axis::solver::InterpolationMethod::Conservative1stOrder;
    config.norm_type = axis::solver::NormType::DstArea;
    config.extrap_method = axis::solver::ExtrapolationAction::None;
    return axis::solver::WeightGenerator::generate<Kokkos::HostSpace>(AxisMesh(source.Definition()), AxisMesh(target.Definition()), config);
}

struct PreparedInterval {
    YAML::Node record;
    FireUtcTime start;
    FireUtcTime end;
    std::string source_id;
    FireUtcTime acquired_at;
    FireUtcTime available_at;
    std::string source_sha256;
};

struct LoadedInterval {
    FireBulkInventory inventory;
    std::string normalized_sha256;
};

class PreparedProductAdapter final : public FireRuntimeAdapter {
   public:
    void SetTargetGrid(std::shared_ptr<const FireGrid> target_grid) override {
        if (!target_grid) throw std::invalid_argument("prepared fire adapter requires a nonnull target grid");
        target_grid_ = std::move(target_grid);
    }

    void Initialize(const FireConfig& config, int nx, int ny, int nz) override {
        if (initialized_) throw std::logic_error("prepared fire adapter is already initialized");
        if (config.input_provider != FireInputProvider::Prepared || config.mode != FireInputMode::Product ||
            config.chemistry_state != FireChemistryState::Bulk) {
            throw std::invalid_argument("prepared product adapter supports prepared bulk-product mode only");
        }
        if (!target_grid_) throw std::invalid_argument("prepared product adapter has no rectilinear CECE target grid");
        if (target_grid_->Definition().nx != static_cast<std::size_t>(nx) || target_grid_->Definition().ny != static_cast<std::size_t>(ny) ||
            nz < 1) {
            throw std::invalid_argument("prepared product target grid dimensions do not match CECE's surface export grid");
        }
        if (!config.product_time_allocation || config.requested_species.empty()) {
            throw std::invalid_argument("prepared product adapter requires a temporal policy and requested species");
        }
        config_ = config;
        config_.requested_species.insert(config_.requested_species.end(), config_.native_diagnostic_species.begin(),
                                         config_.native_diagnostic_species.end());
        if (*config.product_time_allocation == FireTimeAllocation::Profile) {
            temporal_profile_ = LoadFireTemporalAllocationProfile(config.product_time_profile);
        }
        manifest_path_ = std::filesystem::canonical(config.input_manifest);
        manifest_ = YAML::LoadFile(manifest_path_.string());
        ValidateManifest();
        LoadCutoffSchedule();
        ReadIntervals();
        initialized_ = true;
    }

    FireRuntimeInventoryView PrepareInterval(const FireRuntimeIntervalRequest& request) override {
        if (!initialized_ || pending_) throw std::logic_error("prepared fire adapter is uninitialized or has an uncommitted interval");
        if (request.interval_end <= request.interval_start) throw std::invalid_argument("prepared fire request interval must be positive");
        const auto cutoff = ResolveCutoff(request);
        std::vector<const PreparedInterval*> selected;
        for (const auto& interval : intervals_) {
            if (FireIntervalOverlapSeconds(interval.start, interval.end, request.interval_start, request.interval_end) > 0) selected.push_back(&interval);
        }
        if (selected.empty()) throw std::runtime_error("prepared fire manifest has no product support for the requested interval");
        std::sort(selected.begin(), selected.end(), [](const auto* lhs, const auto* rhs) { return lhs->start < rhs->start; });

        std::int64_t supported_seconds = 0;
        auto cursor = request.interval_start;
        for (const auto* interval : selected) {
            const auto overlap_start = std::max(interval->start, request.interval_start);
            const auto overlap_end = std::min(interval->end, request.interval_end);
            if (overlap_start != cursor) throw std::runtime_error("prepared fire intervals contain a gap or overlap in requested temporal support");
            if (interval->available_at > cutoff || interval->acquired_at > cutoff) {
                throw std::invalid_argument("prepared fire product was acquired or available after the requested cutoff");
            }
            const auto prepared_cutoff = Time(interval->record["availability_cutoff"], "interval.availability_cutoff");
            if (prepared_cutoff > cutoff) throw std::invalid_argument("prepared fire product was normalized using a later cutoff than the runtime request");
            if (prepared_cutoff < interval->acquired_at || prepared_cutoff < interval->available_at) {
                throw std::invalid_argument("prepared fire product was not available at its recorded normalization cutoff");
            }
            supported_seconds += overlap_end.EpochSeconds() - overlap_start.EpochSeconds();
            cursor = overlap_end;
        }
        if (cursor != request.interval_end || supported_seconds != request.interval_end.EpochSeconds() - request.interval_start.EpochSeconds()) {
            throw std::runtime_error("prepared fire source intervals do not completely cover the requested model interval");
        }

        const auto target_cells = target_grid_->CellCount();
        std::map<std::string, FireComponentProfile> component_profiles;
        std::set<std::string> unprofiled_component_ids;
        std::map<std::string, std::vector<double>> component_masses;
        std::vector<double> coverage(target_cells, 0.0);
        std::vector<FireQualityState> quality(target_cells, FireQualityState::Unavailable);
        std::vector<std::string> accepted_hashes;
        std::optional<FireNoxReferenceBasis> nox_basis;
        std::optional<FireProfileIdentity> voc_profile;
        FireUtcTime latest_acquired = selected.front()->acquired_at;
        FireUtcTime latest_available = selected.front()->available_at;

        for (const auto* interval : selected) {
            auto loaded = LoadInterval(*interval);
            const auto matrix = ConservativeMatrix(*loaded.inventory.Data().identity.Metadata().grid_definition, *target_grid_);
            const auto allocated = FireAllocateConservativeMass(loaded.inventory, target_grid_, matrix, axis::solver::NormType::DstArea,
                                                                request.interval_start, request.interval_end, *config_.product_time_allocation,
                                                                temporal_profile_);
            for (std::size_t component = 0; component < allocated.component_ids.size(); ++component) {
                const auto& id = allocated.component_ids[component];
                const auto profile = allocated.component_profiles.find(id);
                if (profile != allocated.component_profiles.end()) {
                    if (unprofiled_component_ids.contains(id)) {
                        throw std::invalid_argument("prepared component profile availability changes across intervals");
                    }
                    const auto [found, inserted] = component_profiles.emplace(id, profile->second);
                    if (!inserted && (found->second.biome != profile->second.biome || found->second.composition != profile->second.composition)) {
                        throw std::invalid_argument("prepared component identity changes biome/composition profile across intervals");
                    }
                } else {
                    if (component_profiles.contains(id)) {
                        throw std::invalid_argument("prepared component profile availability changes across intervals");
                    }
                    unprofiled_component_ids.insert(id);
                }
                auto& mass = component_masses[id];
                if (mass.empty()) mass.assign(config_.requested_species.size() * target_cells, 0.0);
                for (std::size_t species = 0; species < allocated.species.size(); ++species) {
                    const auto destination_species = std::find(config_.requested_species.begin(), config_.requested_species.end(), allocated.species[species]);
                    if (destination_species == config_.requested_species.end()) continue;
                    const auto destination_index = static_cast<std::size_t>(std::distance(config_.requested_species.begin(), destination_species));
                    const auto offset = (component * allocated.species.size() + species) * target_cells;
                    for (std::size_t cell = 0; cell < target_cells; ++cell) {
                        mass[destination_index * target_cells + cell] += allocated.mass_kg[offset + cell];
                    }
                }
            }
            for (std::size_t cell = 0; cell < target_cells; ++cell) {
                coverage[cell] = std::min(1.0, coverage[cell] + allocated.coverage_fraction[cell]);
                const auto state = allocated.quality_state[cell];
                if (state == FireQualityState::Reconstructed ||
                    (state == FireQualityState::Observed && quality[cell] != FireQualityState::Reconstructed) ||
                    (state == FireQualityState::ValidZero && quality[cell] == FireQualityState::Unavailable)) {
                    quality[cell] = state;
                }
            }
            const auto& input_data = loaded.inventory.Data();
            if (input_data.nox_reference_basis) {
                if (nox_basis && nox_basis != input_data.nox_reference_basis) throw std::invalid_argument("prepared fire intervals disagree on NOX reference basis");
                nox_basis = input_data.nox_reference_basis;
            }
            if (input_data.voc_profile) {
                if (voc_profile && voc_profile != input_data.voc_profile) throw std::invalid_argument("prepared fire intervals disagree on VOC composition profile");
                voc_profile = input_data.voc_profile;
            }
            accepted_hashes.push_back(interval->source_sha256);
            latest_acquired = std::max(latest_acquired, interval->acquired_at);
            latest_available = std::max(latest_available, interval->available_at);
        }

        std::vector<std::string> component_ids;
        std::vector<double> mass;
        for (const auto& [id, values] : component_masses) {
            component_ids.push_back(id);
            mass.insert(mass.end(), values.begin(), values.end());
        }
        std::sort(accepted_hashes.begin(), accepted_hashes.end());
        accepted_hashes.erase(std::unique(accepted_hashes.begin(), accepted_hashes.end()), accepted_hashes.end());
        const auto manifest_sha256 = FireFileSha256(manifest_path_.string());
        FireIdentityMetadata metadata{1,
                                      "prepared-product-runtime",
                                      "normalized-v1",
                                      "cece-fire-product-decoder-v1",
                                      "prepared-" + manifest_sha256.substr(0, 12),
                                      manifest_sha256,
                                      request.interval_start,
                                      request.interval_end,
                                      latest_acquired,
                                      latest_available,
                                      cutoff,
                                      config_.cutoff_mode,
                                      target_grid_};
        auto identity = FireInputIdentity(std::move(metadata), temporal_profile_ ? std::optional<FireProfileIdentity>(temporal_profile_->identity)
                                                                                 : std::nullopt);
        FireBulkInventoryData inventory_data{std::move(identity), config_.requested_species, std::move(component_ids), std::move(component_profiles),
                                             std::move(mass), std::move(quality), std::move(coverage), std::move(accepted_hashes), nox_basis,
                                             voc_profile, std::nullopt, std::nullopt, std::nullopt};
        inventory_data.unprofiled_component_ids.assign(unprofiled_component_ids.begin(), unprofiled_component_ids.end());
        inventory_data.native_diagnostic_species = config_.native_diagnostic_species;
        pending_inventory_.emplace(std::move(inventory_data));
        pending_ = true;
        return std::cref(*pending_inventory_);
    }

    void CommitInterval() override {
        if (!pending_) throw std::logic_error("prepared fire adapter has no pending interval to commit");
        pending_inventory_.reset();
        pending_ = false;
    }

    void AbortInterval() noexcept override {
        pending_inventory_.reset();
        pending_ = false;
    }

    void Finalize() override {
        AbortInterval();
        intervals_.clear();
        manifest_ = {};
        initialized_ = false;
    }

   private:
    void ValidateManifest() {
        if (!manifest_.IsMap() || manifest_["schema_version"].as<int>(-1) != 1 || Text(manifest_["mode"], "manifest.mode") != "product") {
            throw std::invalid_argument("prepared fire input_manifest must be a schema-v1 product manifest");
        }
        const auto expected_cutoff_mode = config_.cutoff_mode == FireCutoffMode::Fixed ? "fixed" : "host_supplied";
        if (Text(manifest_["product_adapter"], "manifest.product_adapter") != config_.product_adapter ||
            Text(manifest_["product_time_allocation"], "manifest.product_time_allocation") !=
                (*config_.product_time_allocation == FireTimeAllocation::Uniform     ? "uniform"
                 : *config_.product_time_allocation == FireTimeAllocation::SourceResolved ? "source_resolved"
                                                                                          : "profile") ||
            Text(manifest_["cutoff_mode"], "manifest.cutoff_mode") != expected_cutoff_mode) {
            throw std::invalid_argument("prepared fire manifest adapter, temporal policy or cutoff mode does not match configuration");
        }
        if (Text(manifest_["chemistry_state"], "manifest.chemistry_state") != "bulk") {
            throw std::invalid_argument("prepared product runtime currently accepts bulk chemistry intervals only");
        }
        const auto profile_node = manifest_["product_time_profile"];
        if (temporal_profile_) {
            if (!profile_node || Text(profile_node["profile_id"], "manifest.product_time_profile.profile_id") != temporal_profile_->identity.id ||
                Text(profile_node["version"], "manifest.product_time_profile.version") != temporal_profile_->identity.version ||
                Text(profile_node["content_sha256"], "manifest.product_time_profile.content_sha256") != temporal_profile_->identity.content_sha256) {
                throw std::invalid_argument("prepared fire temporal profile identity/hash does not match the configured profile file");
            }
        } else if (profile_node && profile_node.IsMap() && !profile_node.IsNull()) {
            throw std::invalid_argument("prepared fire manifest contains a profile while configuration selects no temporal profile");
        }
        const auto sources = manifest_["sources"];
        const auto interval_nodes = manifest_["intervals"];
        if (!sources || !sources.IsSequence() || sources.size() == 0 || !interval_nodes || !interval_nodes.IsSequence() || interval_nodes.size() == 0 ||
            interval_nodes.size() > 4096) {
            throw std::invalid_argument("prepared fire manifest requires 1..4096 source intervals and a nonempty sources list");
        }
        source_records_.clear();
        for (const auto& source : sources) {
            const auto id = Text(source["source_id"], "manifest.sources.source_id");
            if (!source_records_.emplace(id, source).second) throw std::invalid_argument("prepared fire manifest has duplicate source_id values");
        }
    }

    void LoadCutoffSchedule() {
        if (config_.cutoff_mode == FireCutoffMode::Fixed) {
            if (manifest_["cutoff_schedule_sha256"] && !manifest_["cutoff_schedule_sha256"].IsNull()) {
                throw std::invalid_argument("fixed-cutoff prepared fire manifest must not reference a cutoff schedule");
            }
            return;
        }
        const auto schedule_hash = Text(manifest_["cutoff_schedule_sha256"], "manifest.cutoff_schedule_sha256");
        if (FireFileSha256(config_.cutoff_schedule) != schedule_hash) {
            throw std::invalid_argument("prepared fire cutoff_schedule SHA256 does not match the normalized manifest");
        }
        const auto schedule = YAML::LoadFile(std::filesystem::canonical(config_.cutoff_schedule).string());
        if (schedule["schema_version"].as<int>(-1) != 1 || !schedule["intervals"] || !schedule["intervals"].IsSequence()) {
            throw std::invalid_argument("prepared fire cutoff_schedule requires schema_version: 1 and an intervals sequence");
        }
        std::optional<FireUtcTime> previous_cutoff;
        std::optional<FireUtcTime> previous_end;
        for (const auto& entry : schedule["intervals"]) {
            const auto start_text = Text(entry["interval_start"], "cutoff_schedule.interval_start");
            const auto end_text = Text(entry["interval_end"], "cutoff_schedule.interval_end");
            const auto start = FireUtcTime(start_text);
            const auto end = FireUtcTime(end_text);
            const auto cutoff = Time(entry["availability_cutoff"], "cutoff_schedule.availability_cutoff");
            if (end <= start || (previous_end && start < *previous_end) || (previous_cutoff && cutoff < *previous_cutoff) ||
                !schedule_cutoffs_.emplace(std::pair{start_text, end_text}, cutoff).second) {
                throw std::invalid_argument("prepared fire cutoff_schedule contains duplicate interval bounds");
            }
            previous_end = end;
            previous_cutoff = cutoff;
        }
    }

    FireUtcTime ResolveCutoff(const FireRuntimeIntervalRequest& request) const {
        if (config_.cutoff_mode == FireCutoffMode::Fixed) {
            if (!request.fixed_cutoff) throw std::invalid_argument("prepared fire fixed-cutoff request has no cutoff");
            if (config_.availability_cutoff && request.fixed_cutoff != config_.availability_cutoff) {
                throw std::invalid_argument("prepared fire request cutoff differs from configured fixed cutoff");
            }
            return *request.fixed_cutoff;
        }
        const auto key = std::pair{request.interval_start.ToString(), request.interval_end.ToString()};
        const auto found = schedule_cutoffs_.find(key);
        if (found == schedule_cutoffs_.end()) throw std::invalid_argument("prepared fire cutoff_schedule has no exact model interval entry");
        if (request.fixed_cutoff && request.fixed_cutoff != found->second) {
            throw std::invalid_argument("prepared fire request cutoff disagrees with cutoff_schedule");
        }
        return found->second;
    }

    void ReadIntervals() {
        intervals_.clear();
        for (const auto& record : manifest_["intervals"]) {
            const auto source_id = Text(record["source_id"], "manifest.intervals.source_id");
            const auto source = source_records_.find(source_id);
            if (source == source_records_.end()) throw std::invalid_argument("prepared interval references an unknown source_id");
            const auto source_sha256 = Text(source->second["content_sha256"], "manifest.sources.content_sha256");
            if (Text(record["content_sha256"], "manifest.intervals.content_sha256").size() != 64) {
                throw std::invalid_argument("prepared interval content_sha256 is invalid");
            }
            if (Text(record["source_content_sha256"], "manifest.intervals.source_content_sha256") != source_sha256) {
                throw std::invalid_argument("prepared interval source hash disagrees with its source record");
            }
            const auto record_cutoff_mode = Text(record["cutoff_mode"], "manifest.interval.cutoff_mode");
            if (record_cutoff_mode != (config_.cutoff_mode == FireCutoffMode::Fixed ? "fixed" : "host_supplied")) {
                throw std::invalid_argument("prepared interval cutoff mode disagrees with manifest and configuration");
            }
            intervals_.push_back({record, Time(record["interval_start"], "manifest.intervals.interval_start"),
                                  Time(record["interval_end"], "manifest.intervals.interval_end"), source_id,
                                  Time(source->second["acquired_at"], "manifest.sources.acquired_at"),
                                  Time(source->second["available_at"], "manifest.sources.available_at"),
                                  Text(source->second["content_sha256"], "manifest.sources.content_sha256")});
        }
        std::sort(intervals_.begin(), intervals_.end(), [](const auto& lhs, const auto& rhs) { return lhs.start < rhs.start; });
        for (std::size_t i = 1; i < intervals_.size(); ++i) {
            if (intervals_[i].start < intervals_[i - 1].end) {
                throw std::invalid_argument("prepared product intervals overlap; duplicate or competing contributions are unsupported");
            }
        }
    }

    LoadedInterval LoadInterval(const PreparedInterval& interval) const {
        const auto path = ManifestPath(manifest_path_, interval.record["path"]);
        const auto normalized_sha256 = Text(interval.record["content_sha256"], "manifest.interval.content_sha256");
        if (FireFileSha256(path.string()) != normalized_sha256) throw std::invalid_argument("prepared fire interval file SHA256 mismatch");
        std::optional<FireComponentProfile> component_profile;
        const auto& component = interval.record["component_profile"];
        if (component && component.IsMap()) {
            component_profile = FireComponentProfile{ProfileIdentity(component["biome"], "component_profile.biome"),
                                                     ProfileIdentity(component["composition"], "component_profile.composition")};
        } else if (component && !component.IsNull()) {
            throw std::invalid_argument("prepared interval component_profile must be a mapping when present");
        }
        const auto component_id = Text(interval.record["component_id"], "manifest.interval.component_id");

        int file = -1;
        NetcdfCheck(nc_open(path.c_str(), NC_NOWRITE, &file), "cannot open prepared interval", path.string());
        struct Close {
            int* file;
            ~Close() {
                if (*file >= 0) nc_close(*file);
            }
        } close{&file};
        int nx_id = -1, ny_id = -1;
        NetcdfCheck(nc_inq_dimid(file, "x", &nx_id), "missing x dimension", path.string());
        NetcdfCheck(nc_inq_dimid(file, "y", &ny_id), "missing y dimension", path.string());
        std::size_t nx = 0, ny = 0;
        NetcdfCheck(nc_inq_dimlen(file, nx_id, &nx), "cannot read x dimension", path.string());
        NetcdfCheck(nc_inq_dimlen(file, ny_id, &ny), "cannot read y dimension", path.string());
        if (nx == 0 || ny == 0 || nx > std::numeric_limits<std::size_t>::max() / ny) {
            throw std::invalid_argument("prepared fire interval has invalid grid dimensions");
        }
        const auto cells = nx * ny;
        const auto lon = ReadDoubles(file, path.string(), "longitude", cells);
        const auto lat = ReadDoubles(file, path.string(), "latitude", cells);
        const auto lon_bounds = ReadDoubles(file, path.string(), "longitude_bounds", cells * 4);
        const auto lat_bounds = ReadDoubles(file, path.string(), "latitude_bounds", cells * 4);
        const auto area = ReadDoubles(file, path.string(), "cell_area", cells);
        const auto coverage = ReadDoubles(file, path.string(), "coverage_fraction", cells);
        int quality_id = -1;
        NetcdfCheck(nc_inq_varid(file, "quality_state", &quality_id), "missing quality_state variable", path.string());
        std::vector<signed char> quality_values(cells);
        NetcdfCheck(nc_get_var_schar(file, quality_id, quality_values.data()), "cannot read quality_state variable", path.string());
        const auto grid_hash = GlobalText(file, path.string(), "grid_sha256");
        if (grid_hash != Text(interval.record["grid_sha256"], "manifest.interval.grid_sha256")) {
            throw std::invalid_argument("prepared interval grid hash disagrees with manifest");
        }
        const auto convention_text = GlobalText(file, path.string(), "longitude_convention");
        FireLongitudeConvention convention;
        if (convention_text == "0_to_360")
            convention = FireLongitudeConvention::ZeroTo360;
        else if (convention_text == "minus180_to_180")
            convention = FireLongitudeConvention::Minus180To180;
        else
            throw std::invalid_argument("prepared fire interval has unsupported longitude convention");
        const auto crs = GlobalText(file, path.string(), "crs");
        std::vector<std::array<double, 4>> lon_corner(cells), lat_corner(cells);
        for (std::size_t cell = 0; cell < cells; ++cell) {
            lon_corner[cell] = {lon_bounds[cell * 4], lon_bounds[cell * 4 + 1], lon_bounds[cell * 4 + 2], lon_bounds[cell * 4 + 3]};
            lat_corner[cell] = {lat_bounds[cell * 4], lat_bounds[cell * 4 + 1], lat_bounds[cell * 4 + 2], lat_bounds[cell * 4 + 3]};
        }
        const auto grid_id = GlobalText(file, path.string(), "grid_id");
        auto grid = std::make_shared<const FireGrid>(FireGridDefinition{grid_id, grid_hash, nx, ny, lon, lat, lon_corner, lat_corner, area,
                                                                       crs, convention});
        if (GlobalText(file, path.string(), "source_manifest_id") != interval.source_id ||
            GlobalText(file, path.string(), "source_id") != Text(interval.record["product_id"], "interval.product_id") ||
            GlobalText(file, path.string(), "source_content_sha256") != interval.source_sha256 ||
            GlobalText(file, path.string(), "interval_start") != interval.start.ToString() ||
            GlobalText(file, path.string(), "interval_end") != interval.end.ToString() ||
            GlobalText(file, path.string(), "availability_cutoff") != Time(interval.record["availability_cutoff"], "interval.availability_cutoff").ToString() ||
            GlobalText(file, path.string(), "cutoff_mode") != Text(interval.record["cutoff_mode"], "interval.cutoff_mode") ||
            GlobalText(file, path.string(), "product_id") != Text(interval.record["product_id"], "interval.product_id") ||
            GlobalText(file, path.string(), "product_version") != Text(interval.record["product_version"], "interval.product_version") ||
            GlobalText(file, path.string(), "algorithm_version") != Text(interval.record["algorithm_version"], "interval.algorithm_version") ||
            GlobalText(file, path.string(), "product_variant") != Text(interval.record["variant"], "interval.variant") ||
            GlobalText(file, path.string(), "product_time_allocation") != Text(manifest_["product_time_allocation"], "manifest.product_time_allocation")) {
            throw std::invalid_argument("prepared fire interval NetCDF identity disagrees with manifest/source metadata");
        }
        const auto profile_id = temporal_profile_ ? temporal_profile_->identity.id : std::string{};
        if (config_.product_time_allocation == FireTimeAllocation::Profile &&
            (GlobalText(file, path.string(), "product_time_profile_id") != profile_id ||
             GlobalText(file, path.string(), "product_time_profile_version") != temporal_profile_->identity.version ||
             GlobalText(file, path.string(), "product_time_profile_sha256") != temporal_profile_->identity.content_sha256)) {
            throw std::invalid_argument("prepared fire interval records a different temporal profile than the configured profile");
        }

        std::vector<double> masses;
        masses.reserve(config_.requested_species.size() * cells);
        for (const auto species : config_.requested_species) {
            const auto variable = "fire_mass_" + std::string(FireSpeciesName(species));
            int variable_id = -1;
            if (nc_inq_varid(file, variable.c_str(), &variable_id) != NC_NOERR) {
                throw std::invalid_argument("prepared fire interval is missing requested species '" + std::string(FireSpeciesName(species)) + "'");
            }
            std::vector<double> species_mass(cells);
            NetcdfCheck(nc_get_var_double(file, variable_id, species_mass.data()), "cannot read requested fire species", path.string());
            if (!std::all_of(species_mass.begin(), species_mass.end(), [](double value) { return std::isfinite(value) && value >= 0; })) {
                throw std::invalid_argument("prepared fire species contains nonfinite or negative mass");
            }
            masses.insert(masses.end(), species_mass.begin(), species_mass.end());
        }
        std::vector<FireQualityState> quality;
        quality.reserve(cells);
        for (const auto value : quality_values) {
            if (value == 0)
                quality.push_back(FireQualityState::Observed);
            else if (value == 1)
                quality.push_back(FireQualityState::Reconstructed);
            else if (value == 2)
                quality.push_back(FireQualityState::ValidZero);
            else
                throw std::invalid_argument("prepared fire interval contains an unknown quality_state code");
        }
        if (!std::all_of(coverage.begin(), coverage.end(), [](double value) { return std::isfinite(value) && value >= 0 && value <= 1; })) {
            throw std::invalid_argument("prepared fire coverage_fraction is outside [0,1]");
        }
        const auto cutoff = Time(interval.record["availability_cutoff"], "manifest.interval.availability_cutoff");
        FireIdentityMetadata identity{1,
                                      interval.source_id,
                                      Text(interval.record["product_version"], "manifest.interval.product_version"),
                                      Text(interval.record["algorithm_version"], "manifest.interval.algorithm_version"),
                                      interval.source_id,
                                      interval.source_sha256,
                                      interval.start,
                                      interval.end,
                                      interval.acquired_at,
                                      interval.available_at,
                                      cutoff,
                                      Text(interval.record["cutoff_mode"], "manifest.interval.cutoff_mode") == "fixed" ? FireCutoffMode::Fixed
                                                                                                                       : FireCutoffMode::HostSupplied,
                                      grid};
        FireBulkInventoryData data{FireInputIdentity(std::move(identity)), config_.requested_species, {component_id}, {}, std::move(masses),
                                   std::move(quality), coverage, {interval.source_sha256}, std::nullopt, std::nullopt, std::nullopt,
                                   std::nullopt, std::nullopt};
        if (component_profile) {
            data.component_profiles.emplace(component_id, std::move(*component_profile));
        } else {
            data.unprofiled_component_ids.push_back(component_id);
        }
        data.native_diagnostic_species = config_.native_diagnostic_species;
        return {FireBulkInventory(std::move(data)), normalized_sha256};
    }

    FireConfig config_{FireInputMode::Product, FireInputProvider::Prepared, FireCutoffMode::Fixed};
    std::shared_ptr<const FireGrid> target_grid_;
    std::filesystem::path manifest_path_;
    YAML::Node manifest_;
    std::map<std::string, YAML::Node> source_records_;
    std::vector<PreparedInterval> intervals_;
    std::map<std::pair<std::string, std::string>, FireUtcTime> schedule_cutoffs_;
    std::optional<FireTemporalAllocationProfile> temporal_profile_;
    std::optional<FireBulkInventory> pending_inventory_;
    bool initialized_ = false;
    bool pending_ = false;
};

struct PreparedAdapterRegistration {
    PreparedAdapterRegistration() {
        RegisterFireRuntimeAdapter(FireInputProvider::Prepared, [] { return std::make_unique<PreparedProductAdapter>(); });
    }
} prepared_adapter_registration;

}  // namespace
}  // namespace cece
