// SPDX-License-Identifier: Apache-2.0
#include "cece/cece_fire_runtime.hpp"

#include <algorithm>
#include <map>
#include <mutex>
#include <stdexcept>

namespace cece {
namespace {

std::map<FireInputProvider, FireRuntimeAdapterFactory>& AdapterFactories() {
    static std::map<FireInputProvider, FireRuntimeAdapterFactory> factories;
    return factories;
}

std::mutex& AdapterFactoriesMutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<std::string> FireExportNames(const CeceConfig& config, const FireScheme& scheme) {
    auto names = scheme.ExportFieldNames();
    if (!names.empty()) return names;
    for (const auto& field_ref : config.output_config.fields.GetDataFields()) {
        const auto& field = field_ref.get();
        if (field.name.starts_with("FIRE_")) names.push_back(field.name);
    }
    for (const auto& [name, _] : config.nuopc.export_fields) {
        if (name.starts_with("FIRE_") && std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
    }
    return names;
}

}  // namespace

void RegisterFireRuntimeAdapter(FireInputProvider provider, FireRuntimeAdapterFactory factory) {
    if (!factory) throw std::invalid_argument("Fire runtime adapter factory must be callable");
    std::lock_guard lock(AdapterFactoriesMutex());
    if (!AdapterFactories().emplace(provider, std::move(factory)).second) {
        throw std::logic_error("Fire runtime adapter is already registered for the selected provider");
    }
}

std::unique_ptr<FireRuntimeAdapter> CreateFireRuntimeAdapter(FireInputProvider provider) {
    FireRuntimeAdapterFactory factory;
    {
        std::lock_guard lock(AdapterFactoriesMutex());
        const auto found = AdapterFactories().find(provider);
        if (found == AdapterFactories().end()) return nullptr;
        factory = found->second;
    }
    auto adapter = factory();
    if (!adapter) throw std::runtime_error("Fire runtime adapter factory returned a null adapter");
    return adapter;
}

void ValidateFireExportDescriptors(const CeceConfig& config, const FireScheme& scheme) {
    constexpr std::string_view kFireFluxUnits = "kg m-2 s-1";
    const auto names = FireExportNames(config, scheme);
    if (names.empty()) throw std::invalid_argument("Fire scheme has no configured FIRE_ export fields");

    bool has_export_surface = false;
    if (config.output_config.enabled) {
        has_export_surface = true;
        for (const auto& name : names) {
            const auto* descriptor = config.output_config.fields.Find(name);
            if (!descriptor) throw std::invalid_argument("Fire output.fields is missing required field '" + name + "'");
            const auto units = descriptor->attributes.find("units");
            if (units == descriptor->attributes.end() || units->second != kFireFluxUnits) {
                throw std::invalid_argument("Fire output field '" + name + "' must declare units '" + std::string(kFireFluxUnits) + "'");
            }
        }
    }
    if (!config.nuopc.export_fields.empty()) {
        has_export_surface = true;
        for (const auto& name : names) {
            const auto field = std::find_if(config.nuopc.export_fields.begin(), config.nuopc.export_fields.end(),
                                            [&](const auto& entry) { return entry.first == name; });
            if (field == config.nuopc.export_fields.end())
                throw std::invalid_argument("nuopc.export_fields is missing required field '" + name + "'");
            if (field->second.units != kFireFluxUnits) {
                throw std::invalid_argument("NUOPC Fire export field '" + name + "' must explicitly declare units '" + std::string(kFireFluxUnits) +
                                            "'");
            }
        }
    }
    if (!has_export_surface) {
        throw std::invalid_argument("Fire scheme requires output.fields or nuopc.export_fields to declare its FIRE_ fields");
    }
}

}  // namespace cece
