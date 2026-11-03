#include "cece/cece_scheme_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "cece/physics/cece_speciation_config.hpp"

namespace cece {
namespace {
SchemeFieldSpec field(const std::string& name, const std::string& units, bool required = true, int extent = 1, const std::string& axis = "surface",
                      bool output = false) {
    return {name, units, axis, extent, required, output};
}
std::unique_lock<std::mutex> exclusive(std::mutex& mutex) {
    std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
    if (!lock.owns_lock()) throw std::runtime_error("Concurrent calls on one CECE scheme runtime are not supported");
    return lock;
}
DualView3D allocate(const std::string& name, int ncol, int extent) {
    DualView3D result(name, ncol, 1, extent);
    Kokkos::deep_copy(result.view_host(), 0.0);
    result.modify_host();
    result.sync_device();
    return result;
}
}  // namespace

SchemeDescriptor DescribeScheme(const SchemeRequest& request, int nlev) {
    if (nlev <= 0) throw std::invalid_argument("nlev must be positive");
    auto config = conf::Config::from_string(request.options_yaml);
    auto options = config.root();
    if (options["input_mapping"] || options["output_mapping"])
        throw std::invalid_argument("Use the adapter's field mapping, not scheme input_mapping/output_mapping, in column execution");
    SchemeDescriptor result;
    result.name = request.name;
    auto& f = result.fields;
    if (request.name == "native_example") {
        f = {field("base_anthropogenic_nox", "caller", true, nlev, "level"), field("multiplier_input", "1", false, nlev, "level"),
             field("nox", "caller", true, nlev, "level", true)};
    } else if (request.name == "megan3") {
        f = {field("temperature", "K"),
             field("leaf_area_index", "1"),
             field("par_direct", "W m-2"),
             field("par_diffuse", "W m-2"),
             field("solar_cosine", "1"),
             field("leaf_area_index_prev", "1", false),
             field("soil_moisture_root", "1", false),
             field("wind_speed", "m s-1", false)};
        for (int i = 0; i < static_cast<int>(EmissionClass::COUNT); ++i)
            f.push_back(field("AEF_" + EmissionClassToString(static_cast<EmissionClass>(i)), "kmol m-2 s-1", false));
        auto speciation = SpeciationConfigLoader().Load(options["mechanism_file"].string_or("data/speciation/spc_cb6.yaml"),
                                                        options["speciation_file"].string_or("data/speciation/map_cb6.yaml"),
                                                        options["speciation_dataset"].string_or("MEGAN"));
        for (const auto& species : speciation.species) f.push_back(field("MEGAN_" + species.name, "kg m-2 s-1", true, 1, "surface", true));
        // This kernel reads soil NO from export state. The adapter must explicitly
        // supply zero/external NO or order a preceding BDSNP producer.
        f.push_back(field("soil_nox_emissions", "kg m-2 s-1"));
    } else if (request.name == "bdsnp") {
        const auto mode = options["soil_no_method"].string_or("bdsnp");
        // The two input contracts are intentionally different by mode.
        // NOLINTNEXTLINE(bugprone-branch-clone)
        if (mode == "yl95") {
            f = {field("soil_temperature", "K"), field("soil_moisture", "1")};
        } else if (mode == "bdsnp") {
            f = {field(options["use_soil_temperature"].bool_or(false) ? "soil_temperature" : "surface_temperature", "K"),
                 field("soil_moisture", "1"),
                 field("soilnox_land_fractions", "1", true, 24, "biome"),
                 field("soilnox_arid_fraction", "1"),
                 field("soilnox_nonarid_fraction", "1"),
                 field("leaf_area_index", "1"),
                 field("soilnox_canopy_nox", "source_contract", true, 24, "biome"),
                 field("wind_speed_squared", "m2 s-2"),
                 field("solar_zenith_cosine", "1"),
                 field("soil_fertilizer", "source_contract"),
                 field("deposited_nitrogen", "source_contract"),
                 field("soilnox_pulse_factor", "1")};
            f.push_back(field("soil_nox_fertilizer_emissions", "kg m-2 s-1", true, 1, "surface", true));
        } else
            throw std::invalid_argument("Unknown BDSNP soil_no_method: " + mode);
        f.push_back(field("soil_nox_emissions", "kg m-2 s-1", true, 1, "surface", true));
    } else
        throw std::invalid_argument("Scheme has no reviewed column contract: " + request.name);
    return result;
}

SchemeRuntime::SchemeRuntime(const std::vector<SchemeRequest>& requests, int ncol, int nlev) : ncol_(ncol), nlev_(nlev) {
    if (!Kokkos::is_initialized() || Kokkos::is_finalized()) throw std::runtime_error("Initialize Kokkos before creating a scheme runtime");
    if (ncol <= 0 || nlev <= 0 || requests.empty()) throw std::invalid_argument("Positive geometry and at least one scheme are required");
    configs_.reserve(requests.size());
    std::unordered_map<std::string, int> producers;
    for (std::size_t i = 0; i < requests.size(); ++i) {
        auto descriptor = DescribeScheme(requests[i], nlev);
        for (const auto& f : descriptor.fields) {
            if (f.output && !producers.emplace(f.name, static_cast<int>(i)).second)
                throw std::invalid_argument("Duplicate output producer: " + f.name);
            if (auto it = specs_.find(f.name); it != specs_.end()) {
                if (it->second.extent != f.extent || it->second.units != f.units)
                    throw std::invalid_argument("Incompatible shared field contract: " + f.name);
                it->second.required = it->second.required || f.required;
            } else
                specs_.emplace(f.name, f);
        }
        descriptors_.push_back(std::move(descriptor));
    }
    for (std::size_t i = 0; i < descriptors_.size(); ++i) {
        for (const auto& f : descriptors_[i].fields) {
            auto producer = producers.find(f.name);
            if (!f.output && producer != producers.end() && producer->second >= static_cast<int>(i))
                throw std::invalid_argument("Schemes must be ordered before their consumers: " + f.name);
        }
    }
    for (const auto& [name, index] : producers) exports_.fields.emplace(name, allocate(name, ncol, specs_.at(name).extent));
    for (const auto& request : requests) {
        configs_.push_back(conf::Config::from_string(request.options_yaml));
        PhysicsSchemeConfig config;
        config.name = request.name;
        auto scheme = PhysicsFactory::CreateScheme(config);
        if (!scheme) throw std::invalid_argument("Scheme is not compiled into this CECE build: " + request.name);
        scheme->Initialize(configs_.back().root(), &diagnostics_);
        schemes_.push_back(std::move(scheme));
    }
}

void SchemeRuntime::require_open() const {
    if (closed_) throw std::runtime_error("CECE scheme runtime is closed");
    if (failed_) throw std::runtime_error("CECE scheme runtime failed; create a new instance");
}

void SchemeRuntime::set_field(const std::string& name, const double* values, std::size_t count, int extent) {
    auto lock = exclusive(mutex_);
    require_open();
    auto it = specs_.find(name);
    if (it == specs_.end()) throw std::invalid_argument("Unknown scheme field: " + name);
    const auto& spec = it->second;
    if (!values || extent != spec.extent || count != static_cast<std::size_t>(ncol_) * spec.extent)
        throw std::invalid_argument("Field dimensions do not match contract: " + name);
    if (spec.output) throw std::invalid_argument("Cannot overwrite a scheme-owned output: " + name);
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(values[i])) throw std::invalid_argument("Non-finite scheme input: " + name);
        if ((name == "solar_cosine" || name == "solar_zenith_cosine") && (values[i] < 0 || values[i] > 1))
            throw std::invalid_argument("Solar cosine must be in [0, 1]");
    }
    // The existing MEGAN3 kernel resolves this dependency from export state.
    const bool dependency = name == "soil_nox_emissions";
    auto& fields = dependency ? exports_.fields : imports_.fields;
    auto target = fields.find(name);
    if (target == fields.end()) {
        target = fields.emplace(name, allocate(name, ncol_, extent)).first;
        for (auto& scheme : schemes_)
            if (auto* base = dynamic_cast<BasePhysicsScheme*>(scheme.get())) base->ClearPhysicsCache();
    }
    auto host = target->second.view_host();
    for (int k = 0; k < extent; ++k)
        for (int i = 0; i < ncol_; ++i) host(i, 0, k) = values[i + static_cast<std::size_t>(ncol_) * k];
    target->second.modify_host();
    target->second.sync_device();
    supplied_[name] = true;
    if (dependency) external_exports_[name] = true;
}

void SchemeRuntime::step(double start_seconds, double dt_seconds) {
    auto lock = exclusive(mutex_);
    require_open();
    if (!std::isfinite(start_seconds) || !std::isfinite(dt_seconds) || dt_seconds <= 0 || !std::isfinite(start_seconds + dt_seconds))
        throw std::invalid_argument("Finite time and positive timestep are required");
    if (next_time_ && std::abs(start_seconds - *next_time_) > 1.e-6)
        throw std::invalid_argument("Timesteps must be contiguous; restore state to reset time");
    for (const auto& descriptor : descriptors_)
        for (const auto& f : descriptor.fields)
            if (f.required && !f.output && !supplied_[f.name] && !exports_.fields.count(f.name))
                throw std::invalid_argument(descriptor.name + " missing required input: " + f.name);
    try {
        for (auto& [name, value] : exports_.fields) {
            if (external_exports_.count(name)) continue;
            Kokkos::deep_copy(value.view_device(), 0.0);
            value.modify_device();
        }
        for (auto& scheme : schemes_) scheme->Run(imports_, exports_);
        Kokkos::fence();
        // Scientific failures must not become successful-looking host results.
        for (auto& [name, value] : exports_.fields) {
            value.sync_host();
            const auto host = value.view_host();
            for (std::size_t k = 0; k < host.extent(2); ++k)
                for (int i = 0; i < ncol_; ++i)
                    if (!std::isfinite(host(i, 0, k))) throw std::runtime_error("Non-finite scheme output: " + name);
        }
        next_time_ = start_seconds + dt_seconds;
    } catch (...) {
        failed_ = true;
        throw;
    }
}

std::vector<double> SchemeRuntime::get_field(const std::string& name) {
    auto lock = exclusive(mutex_);
    require_open();
    auto it = exports_.fields.find(name);
    if (it == exports_.fields.end()) throw std::invalid_argument("Unknown output field: " + name);
    it->second.sync_host();
    const auto host = it->second.view_host();
    std::vector<double> result(static_cast<std::size_t>(ncol_) * host.extent(2));
    for (std::size_t k = 0; k < host.extent(2); ++k)
        for (int i = 0; i < ncol_; ++i) result[i + static_cast<std::size_t>(ncol_) * k] = host(i, 0, k);
    return result;
}

std::optional<double> SchemeRuntime::next_time() {
    auto lock = exclusive(mutex_);
    require_open();
    return next_time_;
}

void SchemeRuntime::restore_time(std::optional<double> next_time) {
    auto lock = exclusive(mutex_);
    require_open();
    if (next_time && !std::isfinite(*next_time)) throw std::invalid_argument("Non-finite restart time");
    next_time_ = next_time;
}

void SchemeRuntime::close() {
    auto lock = exclusive(mutex_);
    if (closed_) return;
    for (auto& scheme : schemes_) scheme->Finalize();
    schemes_.clear();
    imports_.fields.clear();
    exports_.fields.clear();
    diagnostics_ = CeceDiagnosticManager{};
    closed_ = true;
}
SchemeRuntime::~SchemeRuntime() {
    // Destructors must not throw; explicit close() remains available to report errors.
    try {
        close();
    } catch (...) {  // NOLINT(bugprone-empty-catch)
    }
}
}  // namespace cece
