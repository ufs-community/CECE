#include "cece/cece_c_api.h"

#include <string>
#include <vector>

#include "cece/cece_config.hpp"
#include "cece/cece_config_path.hpp"
#include "cece/cece_internal.hpp"

// Singleton-style access to the current config (assumes only one config in use)
static cece::CeceConfig* get_config() {
    // In a real implementation, this would be managed more robustly
    static cece::CeceConfig* config_ptr = nullptr;
    if (!config_ptr) {
        static cece::CeceConfig config = cece::ParseConfig(cece::GetConfigFilePath());
        config_ptr = &config;
    }
    return config_ptr;
}

extern "C" {

size_t cece_get_met_registry_count() {
    auto* config = get_config();
    return config->met_registry.size();
}

const char* cece_get_met_registry_internal_name(size_t idx) {
    auto* config = get_config();
    if (idx >= config->met_registry.size()) return nullptr;
    auto it = config->met_registry.begin();
    std::advance(it, idx);
    return it->first.c_str();
}

size_t cece_get_met_registry_alias_count(size_t idx) {
    auto* config = get_config();
    if (idx >= config->met_registry.size()) return 0;
    auto it = config->met_registry.begin();
    std::advance(it, idx);
    return it->second.size();
}

const char* cece_get_met_registry_alias(size_t idx, size_t alias_idx) {
    auto* config = get_config();
    if (idx >= config->met_registry.size()) return nullptr;
    auto it = config->met_registry.begin();
    std::advance(it, idx);
    if (alias_idx >= it->second.size()) return nullptr;
    return it->second[alias_idx].c_str();
}

}  // extern "C"
