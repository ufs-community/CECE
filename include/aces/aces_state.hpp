#ifndef ACES_STATE_HPP
#define ACES_STATE_HPP

/**
 * @file aces_state.hpp
 * @brief Defines the core state structures for ACES.
 */

#include <iostream>
#include <string>
#include <unordered_map>

#include "aces/aces_compute.hpp"

namespace aces {

/**
 * @brief Structure containing all meteorology and base emissions imported from
 * other components.
 *
 * Uses an unordered_map to allow flexible addition of fields without hardcoding.
 */
struct AcesImportState {
    /// Map of field names to their respective DualViews.
    std::unordered_map<std::string, DualView3D> fields;
};

/**
 * @brief Structure containing all computed emissions to be exported back to the
 * framework.
 */
struct AcesExportState {
    /// Map of field names to their respective DualViews.
    std::unordered_map<std::string, DualView3D> fields;
};

/**
 * @brief FieldResolver that pulls from the unified AcesImportState and
 * AcesExportState.
 */
class AcesStateResolver : public FieldResolver {
    const AcesImportState& import_state;
    const AcesExportState& export_state;
    const std::unordered_map<std::string, std::string>& met_mapping;
    const std::unordered_map<std::string, std::string>& sf_mapping;
    const std::unordered_map<std::string, std::string>& mask_mapping;

    static const std::unordered_map<std::string, std::string>& EmptyMap() {
        static const std::unordered_map<std::string, std::string> empty;
        return empty;
    }

    [[nodiscard]] std::string ResolveName(const std::string& name) const {
        auto it = met_mapping.find(name);
        if (it != met_mapping.end()) {
            return it->second;
        }
        it = sf_mapping.find(name);
        if (it != sf_mapping.end()) {
            return it->second;
        }
        it = mask_mapping.find(name);
        if (it != mask_mapping.end()) {
            return it->second;
        }
        return name;
    }

   public:
    AcesStateResolver(const AcesImportState& imp, const AcesExportState& exp,
                      const std::unordered_map<std::string, std::string>& met_map,
                      const std::unordered_map<std::string, std::string>& sf_map = EmptyMap(),
                      const std::unordered_map<std::string, std::string>& mask_map = EmptyMap())
        : import_state(imp),
          export_state(exp),
          met_mapping(met_map),
          sf_mapping(sf_map),
          mask_mapping(mask_map) {}

    UnmanagedHostView3D ResolveImport(const std::string& name, int nx, int ny, int nz) override {
        std::string resolve_name = ResolveName(name);

        auto it = import_state.fields.find(resolve_name);
        if (it != import_state.fields.end()) {
            auto view = it->second.view_host();
            // Handle 1D fields (like ak/bk) in a 3D context
            if (view.extent(0) == 1 && view.extent(1) == 1 &&
                view.extent(2) == static_cast<size_t>(nz)) {
                return view;
            }
            // Handle 2D fields in a 3D context
            if (view.extent(2) == 1 && nz > 1) {
                if (view.extent(0) != static_cast<size_t>(nx) ||
                    view.extent(1) != static_cast<size_t>(ny)) {
                    std::cerr << "ACES_Resolver Error: 2D Dimension mismatch for import "
                              << resolve_name << ". Expected " << nx << "x" << ny << ", got "
                              << view.extent(0) << "x" << view.extent(1) << "\n";
                    return {};
                }
                return view;
            }
            if (view.extent(0) != static_cast<size_t>(nx) ||
                view.extent(1) != static_cast<size_t>(ny) ||
                view.extent(2) != static_cast<size_t>(nz)) {
                std::cerr << "ACES_Resolver Error: Dimension mismatch for import " << resolve_name
                          << ". Expected " << nx << "x" << ny << "x" << nz << ", got "
                          << view.extent(0) << "x" << view.extent(1) << "x" << view.extent(2)
                          << "\n";
                return {};
            }
            return view;
        }
        return {};
    }

    UnmanagedHostView3D ResolveExport(const std::string& name, int nx, int ny, int nz) override {
        auto it = export_state.fields.find(name);
        if (it != export_state.fields.end()) {
            auto view = it->second.view_host();
            if (view.extent(0) != static_cast<size_t>(nx) ||
                view.extent(1) != static_cast<size_t>(ny) ||
                view.extent(2) != static_cast<size_t>(nz)) {
                std::cerr << "ACES_Resolver Error: Dimension mismatch for export " << name
                          << ". Expected " << nx << "x" << ny << "x" << nz << ", got "
                          << view.extent(0) << "x" << view.extent(1) << "x" << view.extent(2)
                          << "\n";
                return {};
            }
            return view;
        }
        return {};
    }

    Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>
    ResolveImportDevice(const std::string& name, int nx, int ny, int nz) override {
        std::string resolve_name = ResolveName(name);

        auto it = import_state.fields.find(resolve_name);
        if (it != import_state.fields.end()) {
            auto view = it->second.view_device();
            // Handle 1D fields (like ak/bk) in a 3D context
            if (view.extent(0) == 1 && view.extent(1) == 1 &&
                view.extent(2) == static_cast<size_t>(nz)) {
                return view;
            }
            // Handle 2D fields in a 3D context
            if (view.extent(2) == 1 && nz > 1) {
                if (view.extent(0) != static_cast<size_t>(nx) ||
                    view.extent(1) != static_cast<size_t>(ny)) {
                    std::cerr << "ACES_Resolver Error: 2D Dimension mismatch for import "
                              << resolve_name << " (device). Expected " << nx << "x" << ny
                              << ", got " << view.extent(0) << "x" << view.extent(1) << "\n";
                    return {};
                }
                return view;
            }
            if (view.extent(0) != static_cast<size_t>(nx) ||
                view.extent(1) != static_cast<size_t>(ny) ||
                view.extent(2) != static_cast<size_t>(nz)) {
                std::cerr << "ACES_Resolver Error: Dimension mismatch for import " << resolve_name
                          << " (device). Expected " << nx << "x" << ny << "x" << nz << ", got "
                          << view.extent(0) << "x" << view.extent(1) << "x" << view.extent(2)
                          << "\n";
                return {};
            }
            return view;
        }
        return {};
    }

    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> ResolveExportDevice(
        const std::string& name, int nx, int ny, int nz) override {
        auto it = export_state.fields.find(name);
        if (it != export_state.fields.end()) {
            auto view = it->second.view_device();
            if (view.extent(0) != static_cast<size_t>(nx) ||
                view.extent(1) != static_cast<size_t>(ny) ||
                view.extent(2) != static_cast<size_t>(nz)) {
                std::cerr << "ACES_Resolver Error: Dimension mismatch for export " << name
                          << " (device). Expected " << nx << "x" << ny << "x" << nz << ", got "
                          << view.extent(0) << "x" << view.extent(1) << "x" << view.extent(2)
                          << "\n";
                return {};
            }
            return view;
        }
        return {};
    }
};

}  // namespace aces

#endif  // ACES_STATE_HPP
