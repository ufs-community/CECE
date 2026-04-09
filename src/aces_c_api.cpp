#include "aces/aces_c_api.h"
#include "aces/aces_config.hpp"
#include "aces/aces_config_path.hpp"
#include "aces/aces_internal.hpp"
#include <vector>
#include <string>

// Singleton-style access to the current config (assumes only one config in use)
static aces::AcesConfig* get_config() {
    // In a real implementation, this would be managed more robustly
    static aces::AcesConfig* config_ptr = nullptr;
    if (!config_ptr) {
        static aces::AcesConfig config = aces::ParseConfig(aces::GetConfigFilePath());
        config_ptr = &config;
    }
    return config_ptr;
}

extern "C" {

size_t aces_get_met_registry_count() {
    auto* config = get_config();
    return config->met_registry.size();
}

const char* aces_get_met_registry_internal_name(size_t idx) {
    auto* config = get_config();
    if (idx >= config->met_registry.size()) return nullptr;
    auto it = config->met_registry.begin();
    std::advance(it, idx);
    return it->first.c_str();
}

size_t aces_get_met_registry_alias_count(size_t idx) {
    auto* config = get_config();
    if (idx >= config->met_registry.size()) return 0;
    auto it = config->met_registry.begin();
    std::advance(it, idx);
    return it->second.size();
}

const char* aces_get_met_registry_alias(size_t idx, size_t alias_idx) {
    auto* config = get_config();
    if (idx >= config->met_registry.size()) return nullptr;
    auto it = config->met_registry.begin();
    std::advance(it, idx);
    if (alias_idx >= it->second.size()) return nullptr;
    return it->second[alias_idx].c_str();
}

} // extern "C"