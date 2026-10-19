/**
 * @file cece_diagnostic_api.cpp
 * @brief C-ABI surface for the diagnostic registry (feature 003, US3).
 *
 * The NUOPC cap consumes these entry points to advertise diagnostics during
 * initialization and to hand ESMF-owned field storage to the core at realize
 * time. Semantics follow contracts/diagnostics-c-api.md:
 *   - Enumeration (§1) walks the registry's stable first-seen order over the
 *     union of pending declarations and allocated fields; strings are
 *     NUL-terminated and truncation is an error, never silent.
 *   - Persistent-pointer registration (§2) validates the grid extents and the
 *     name against the registry before storing the caller-owned buffer; the
 *     core never frees it (ESMF owns the memory), mirroring
 *     cece_core_set_export_field in cece_core_field_helpers.cpp.
 */

#include <algorithm>
#include <conf/conf.hpp>
#include <cstring>
#include <string>
#include <vector>

#include "cece/cece_diagnostics.hpp"
#include "cece/cece_internal.hpp"
#include "cece/cece_logger.hpp"

namespace cece {
namespace {

/// Copy a string into a caller-owned fixed buffer, NUL-terminating it.
/// Returns false WITHOUT writing when the buffer cannot hold the string plus
/// its terminator, so the entry point surfaces an error instead of silently
/// truncating (contract §1). On success *out_len receives the string length.
bool diag_copy_str(const std::string& value, char* buf, int max_len, int* out_len) {
    if (buf == nullptr || max_len <= 0) {
        return false;
    }
    if (static_cast<int>(value.size()) >= max_len) {
        return false;
    }
    std::memcpy(buf, value.data(), value.size());
    buf[value.size()] = '\0';
    if (out_len != nullptr) {
        *out_len = static_cast<int>(value.size());
    }
    return true;
}

/// Resolve the internal data handle and its diagnostic manager. On any
/// failure, logs under @p label, sets *rc = -1, and returns false.
bool resolve_manager(void* data_ptr, const char* label, cece::CeceDiagnosticManager*& manager, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }
    if (data_ptr == nullptr) {
        CECE_LOG_ERROR(std::string("[CECE] ") + label + " - data_ptr is null");
        if (rc != nullptr) {
            *rc = -1;
        }
        return false;
    }
    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);
    manager = internal_data->diagnostic_manager.get();
    if (manager == nullptr) {
        CECE_LOG_ERROR(std::string("[CECE] ") + label + " - diagnostic manager not initialized (core phase 1 has not run)");
        if (rc != nullptr) {
            *rc = -1;
        }
        return false;
    }
    return true;
}

/**
 * @brief Metadata for the enumerated diagnostic at @p index.
 *
 * The registry's Order() is the union of pending declarations and allocated
 * fields in first-seen order; an index maps to exactly one of the two maps
 * (allocated wins once SetGrid promotes a declaration). Returns false when
 * the index is out of range.
 */
bool lookup_metadata(const cece::CeceDiagnosticManager& manager, int index, std::string& name, std::string& units, std::string& long_name) {
    const auto& order = manager.Order();
    if (index < 0 || static_cast<size_t>(index) >= order.size()) {
        return false;
    }
    name = order[static_cast<size_t>(index)];
    const auto& fields = manager.Fields();
    if (auto it = fields.find(name); it != fields.end()) {
        units = it->second.units;
        long_name = it->second.long_name;
        return true;
    }
    const auto& pending = manager.PendingDeclarations();
    if (auto it = pending.find(name); it != pending.end()) {
        units = it->second.units;
        long_name = it->second.long_name;
        return true;
    }
    // Order() and the two maps are maintained together by the registry, so
    // reaching here is an internal invariant violation.
    units.clear();
    long_name.clear();
    return true;
}

/**
 * @brief Names the NUOPC cap should advertise, enumerated from the config.
 *
 * The cap runs Advertise before any simulation exists, so the names cannot
 * come from the registry (it is created inside InitializeRealize). This
 * mirrors the path-based species enumeration (cece_nuopc_export_count/_spec)
 * with the writer's selection rules applied so the file and coupled channels
 * never diverge in which names are offered:
 *   - gate: output.include_diagnostics (absent/false => advertise nothing);
 *   - union of each scheme's options.diagnostics list, in config order,
 *     first-seen and deduped (deterministic across ranks);
 *   - filter: diagnostics:variables when non-empty selects exactly those
 *     names; empty/absent selects all declared names.
 */
std::vector<std::string> advertise_names(const std::string& yaml_path) {
    cece::CeceConfig config = cece::ParseConfig(yaml_path);
    std::vector<std::string> names;
    if (!config.output_config.include_diagnostics) {
        return names;
    }
    for (const auto& scheme : config.physics_schemes) {
        conf::Value diags = scheme.options["diagnostics"];
        if (!diags.is_defined()) {
            continue;
        }
        for (std::size_t i = 0; i < diags.size(); ++i) {
            std::string candidate = diags[i].as_string();
            if (std::find(names.begin(), names.end(), candidate) == names.end()) {
                names.push_back(std::move(candidate));
            }
        }
    }
    const auto& selection = config.diagnostics.variables;
    if (!selection.empty()) {
        names.erase(
            std::remove_if(names.begin(), names.end(),
                           [&selection](const std::string& n) { return std::find(selection.begin(), selection.end(), n) == selection.end(); }),
            names.end());
    }
    return names;
}

}  // namespace
}  // namespace cece

extern "C" {

void cece_core_diagnostic_count(void* data_ptr, int* count, int* rc) {
    cece::CeceDiagnosticManager* manager = nullptr;
    if (!cece::resolve_manager(data_ptr, "cece_core_diagnostic_count", manager, rc)) {
        return;
    }
    if (count == nullptr) {
        CECE_LOG_ERROR("[CECE] cece_core_diagnostic_count - count pointer is null");
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }
    *count = static_cast<int>(manager->Order().size());
}

void cece_core_diagnostic_name(void* data_ptr, int index, char* name, int max_len, int* rc) {
    cece::CeceDiagnosticManager* manager = nullptr;
    if (!cece::resolve_manager(data_ptr, "cece_core_diagnostic_name", manager, rc)) {
        return;
    }
    std::string diag_name, units, long_name;
    if (!cece::lookup_metadata(*manager, index, diag_name, units, long_name)) {
        CECE_LOG_ERROR("[CECE] cece_core_diagnostic_name - index out of range: " + std::to_string(index) +
                       " (count: " + std::to_string(manager->Order().size()) + ")");
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }
    if (!cece::diag_copy_str(diag_name, name, max_len, nullptr)) {
        CECE_LOG_ERROR("[CECE] cece_core_diagnostic_name - buffer too small (" + std::to_string(max_len) + ") for diagnostic '" + diag_name + "'");
        if (rc != nullptr) {
            *rc = -1;
        }
    }
}

void cece_core_diagnostic_units(void* data_ptr, int index, char* units, int max_len, int* rc) {
    cece::CeceDiagnosticManager* manager = nullptr;
    if (!cece::resolve_manager(data_ptr, "cece_core_diagnostic_units", manager, rc)) {
        return;
    }
    std::string diag_name, diag_units, long_name;
    if (!cece::lookup_metadata(*manager, index, diag_name, diag_units, long_name)) {
        CECE_LOG_ERROR("[CECE] cece_core_diagnostic_units - index out of range: " + std::to_string(index) +
                       " (count: " + std::to_string(manager->Order().size()) + ")");
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }
    if (!cece::diag_copy_str(diag_units, units, max_len, nullptr)) {
        CECE_LOG_ERROR("[CECE] cece_core_diagnostic_units - buffer too small (" + std::to_string(max_len) + ") for diagnostic '" + diag_name + "'");
        if (rc != nullptr) {
            *rc = -1;
        }
    }
}

void cece_core_diagnostic_long_name(void* data_ptr, int index, char* long_name_buf, int max_len, int* rc) {
    cece::CeceDiagnosticManager* manager = nullptr;
    if (!cece::resolve_manager(data_ptr, "cece_core_diagnostic_long_name", manager, rc)) {
        return;
    }
    std::string diag_name, units, diag_long_name;
    if (!cece::lookup_metadata(*manager, index, diag_name, units, diag_long_name)) {
        CECE_LOG_ERROR("[CECE] cece_core_diagnostic_long_name - index out of range: " + std::to_string(index) +
                       " (count: " + std::to_string(manager->Order().size()) + ")");
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }
    if (!cece::diag_copy_str(diag_long_name, long_name_buf, max_len, nullptr)) {
        CECE_LOG_ERROR("[CECE] cece_core_diagnostic_long_name - buffer too small (" + std::to_string(max_len) + ") for diagnostic '" + diag_name +
                       "'");
        if (rc != nullptr) {
            *rc = -1;
        }
    }
}

void cece_core_set_diagnostic_field(void* data_ptr, const char* name, int name_len, double* field_data, int nx, int ny, int nz, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }
    if (data_ptr == nullptr) {
        CECE_LOG_ERROR("[CECE] cece_core_set_diagnostic_field - data_ptr is null");
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }
    if (name == nullptr || name_len <= 0) {
        CECE_LOG_ERROR("[CECE] cece_core_set_diagnostic_field - invalid name argument");
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }
    if (field_data == nullptr) {
        CECE_LOG_ERROR("[CECE] cece_core_set_diagnostic_field - field_data is null");
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);
    auto* manager = internal_data->diagnostic_manager.get();
    if (manager == nullptr) {
        CECE_LOG_ERROR("[CECE] cece_core_set_diagnostic_field - diagnostic manager not initialized");
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    std::string name_str(name, static_cast<size_t>(name_len));
    const auto nul = name_str.find('\0');
    if (nul != std::string::npos) {
        name_str.resize(nul);
    }

    // Grid check (contract §2): the buffer must span the registry's grid, or
    // the per-advance sync would write out of bounds. Name both triples.
    if (nx != internal_data->nx || ny != internal_data->ny || nz != internal_data->nz) {
        CECE_LOG_ERROR("[CECE] cece_core_set_diagnostic_field - grid mismatch for '" + name_str + "': (" + std::to_string(nx) + "," +
                       std::to_string(ny) + "," + std::to_string(nz) + ") != core grid (" + std::to_string(internal_data->nx) + "," +
                       std::to_string(internal_data->ny) + "," + std::to_string(internal_data->nz) + ")");
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    // Typo guard (contract §2): the name must be a registered or pending
    // diagnostic.
    const bool known = manager->Fields().contains(name_str) || manager->PendingDeclarations().contains(name_str);
    if (!known) {
        CECE_LOG_ERROR("[CECE] cece_core_set_diagnostic_field - '" + name_str + "' is not a registered diagnostic");
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }

    // Overwrite allowed (re-realize). Ownership stays with the caller.
    internal_data->persistent_diagnostic_ptrs[name_str] = field_data;

    CECE_LOG_INFO("[CECE] cece_core_set_diagnostic_field - bound '" + name_str + "' to caller buffer (" + std::to_string(nx) + "x" +
                  std::to_string(ny) + "x" + std::to_string(nz) + ")");
}

// -----------------------------------------------------------------------------
// Path-based advertisement enumeration (NUOPC cap InitializeAdvertise).
//
// The cap advertises before any simulation/registry exists (the manager is
// created inside InitializeRealize), so the advertised names come straight
// from the config YAML — the same mechanism the species path uses
// (cece_nuopc_export_count/_spec in cece_core_field_helpers.cpp). Each call
// re-parses; advertise_names() applies the writer's gate/union/filter rules
// so the coupled channel and the file output never diverge in which names are
// offered. Truncation and parse failure are surfaced as rc = -1, never silent.
// -----------------------------------------------------------------------------

void cece_nuopc_diagnostic_count(const char* config_path, int path_len, int* count, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }
    if (count == nullptr) {
        CECE_LOG_ERROR("[CECE] cece_nuopc_diagnostic_count - count pointer is null");
        if (rc != nullptr) {
            *rc = -1;
        }
        return;
    }
    *count = 0;
    try {
        std::string yaml_path(config_path, path_len);
        *count = static_cast<int>(cece::advertise_names(yaml_path).size());
    } catch (const std::exception& e) {
        CECE_LOG_ERROR(std::string("[CECE] cece_nuopc_diagnostic_count - failed to parse config: ") + e.what());
        if (rc != nullptr) {
            *rc = -1;
        }
    }
}

void cece_nuopc_diagnostic_name(const char* config_path, int path_len, int index, char* name, int max_len, int* name_len_out, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }
    if (name_len_out != nullptr) {
        *name_len_out = 0;
    }
    try {
        std::string yaml_path(config_path, path_len);
        std::vector<std::string> names = cece::advertise_names(yaml_path);
        if (index < 0 || static_cast<size_t>(index) >= names.size()) {
            CECE_LOG_ERROR("[CECE] cece_nuopc_diagnostic_name - index out of range: " + std::to_string(index) +
                           " (count: " + std::to_string(names.size()) + ")");
            if (rc != nullptr) {
                *rc = -1;
            }
            return;
        }
        if (!cece::diag_copy_str(names[static_cast<size_t>(index)], name, max_len, name_len_out)) {
            CECE_LOG_ERROR("[CECE] cece_nuopc_diagnostic_name - buffer too small (" + std::to_string(max_len) + ") for diagnostic '" +
                           names[static_cast<size_t>(index)] + "'");
            if (rc != nullptr) {
                *rc = -1;
            }
        }
    } catch (const std::exception& e) {
        CECE_LOG_ERROR(std::string("[CECE] cece_nuopc_diagnostic_name - failed to parse config: ") + e.what());
        if (rc != nullptr) {
            *rc = -1;
        }
    }
}

}  // extern "C"
