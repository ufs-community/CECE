/**
 * @file cece_diagnostic_manager.cpp
 * @brief Implementation of the CECE diagnostic field management system.
 *
 * The CeceDiagnosticManager provides centralized management for diagnostic
 * output fields in CECE simulations. It handles field registration, metadata
 * management, and output coordination for scientific analysis and validation.
 *
 * Key capabilities:
 * - Dynamic diagnostic field registration with metadata
 * - Automated memory management using Kokkos DualView
 * - Integration with physics schemes for custom diagnostics
 * - Output coordination with NetCDF and other formats
 * - Performance-optimized field access patterns
 *
 * The manager works closely with physics schemes to provide intermediate
 * calculation results, validation fields, and custom analysis outputs
 * needed for scientific research and model development.
 *
 * @author Barry Baker
 * @date 2024
 * @version 1.0
 */

#include <algorithm>
#include <array>
#include <iostream>
#include <string_view>
#include <utility>

#include "cece/cece_diagnostics.hpp"
#include "cece/cece_logger.hpp"

namespace cece {
namespace {

// Names the standalone writer always emits as coordinate/bounds/mesh variables.
// A diagnostic may never reuse one (FR-009); mirrors the writer's manifest.
constexpr std::array<std::string_view, 7> kReservedCoordinateNames{"lon", "lat", "lev", "time", "lon_bnds", "lat_bnds", "mesh"};

bool IsReservedCoordinate(std::string_view name) {
    return std::ranges::find(kReservedCoordinateNames, name) != kReservedCoordinateNames.end();
}

}  // namespace

void CeceDiagnosticManager::ValidateName(const std::string& name) const {
    if (IsReservedCoordinate(name)) {
        throw std::invalid_argument("diagnostic name '" + name + "' collides with a reserved coordinate variable name");
    }
    if (std::ranges::find(reserved_names_, name) != reserved_names_.end()) {
        throw std::invalid_argument("diagnostic name '" + name + "' collides with a reserved species name");
    }
}

void CeceDiagnosticManager::WarnUnregisteredSelection(const std::vector<std::string>& requested) const {
    // FR-013: a selected name no scheme ever registered is a non-fatal
    // configuration gap — warn once per offender, then stay quiet.
    for (const auto& name : requested) {
        if (fields_.contains(name) || pending_.contains(name)) {
            continue;
        }
        if (std::ranges::find(warned_selection_names_, name) == warned_selection_names_.end()) {
            warned_selection_names_.push_back(name);
            CECE_LOG_WARNING("[CECE] diagnostics:variables requests '" + name + "' but no scheme registered it; skipping publication");
        }
    }
}

DiagnosticFieldInfo& CeceDiagnosticManager::Allocate(const DiagnosticDeclaration& declaration) {
    // Grid-sized field, zero-initialized so unfilled regions are well-defined.
    DualView3D view(declaration.name, nx_, ny_, nz_);
    Kokkos::deep_copy(view.view_host(), 0.0);
    view.modify_host();

    DiagnosticFieldInfo info;
    info.data = view;
    info.units = declaration.units;
    info.long_name = declaration.long_name;

    auto [it, inserted] = fields_.emplace(declaration.name, std::move(info));
    if (inserted && std::ranges::find(order_, declaration.name) == order_.end()) {
        order_.push_back(declaration.name);
    }
    return it->second;
}

void CeceDiagnosticManager::SetGrid(int nx, int ny, int nz) {
    if (grid_set_) {
        if (nx != nx_ || ny != ny_ || nz != nz_) {
            throw std::invalid_argument("CeceDiagnosticManager::SetGrid called twice with conflicting grid (" + std::to_string(nx_) + "," +
                                        std::to_string(ny_) + "," + std::to_string(nz_) + ") vs (" + std::to_string(nx) + "," + std::to_string(ny) +
                                        "," + std::to_string(nz) + ")");
        }
        return;  // Idempotent re-set with identical dims.
    }
    grid_set_ = true;
    nx_ = nx;
    ny_ = ny;
    nz_ = nz;

    // Promote every pending declaration to a grid-sized field.
    for (auto& [name, declaration] : pending_) {
        Allocate(declaration);
    }
    pending_.clear();
}

void CeceDiagnosticManager::SetReservedNames(std::vector<std::string> names) {
    reserved_names_ = std::move(names);
}

/**
 * @brief Register a diagnostic, returning the shared grid-sized field.
 *
 * Duplicate names return the existing field (FR-003). Coordinate/reserved names
 * throw naming the offender (FR-009). Metadata is first-non-empty-wins. Before
 * SetGrid the declaration is held pending and an empty handle is returned.
 */
DualView3D CeceDiagnosticManager::RegisterDiagnostic(const std::string& name, int /*nx*/, int /*ny*/, int /*nz*/, const std::string& units,
                                                     const std::string& long_name) {
    ValidateName(name);

    if (grid_set_) {
        auto it = fields_.find(name);
        if (it != fields_.end()) {
            // Shared field exists: fill unset metadata, never clobber (FR-004).
            if (it->second.units.empty() && !units.empty()) {
                it->second.units = units;
            }
            if (it->second.long_name.empty() && !long_name.empty()) {
                it->second.long_name = long_name;
            }
            return it->second.data;
        }
        DiagnosticDeclaration declaration{name, units, long_name};
        return Allocate(declaration).data;
    }

    // Grid not yet known: record a pending declaration (allocation deferred).
    auto it = pending_.find(name);
    if (it == pending_.end()) {
        pending_.emplace(name, DiagnosticDeclaration{name, units, long_name});
        if (std::ranges::find(order_, name) == order_.end()) {
            order_.push_back(name);
        }
    } else {
        if (it->second.units.empty() && !units.empty()) {
            it->second.units = units;
        }
        if (it->second.long_name.empty() && !long_name.empty()) {
            it->second.long_name = long_name;
        }
    }
    return {};
}

}  // namespace cece
