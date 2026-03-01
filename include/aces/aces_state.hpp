#ifndef ACES_STATE_HPP
#define ACES_STATE_HPP

/**
 * @file aces_state.hpp
 * @brief Defines the core state structures for ACES.
 */

#include <iostream>
#include <map>
#include <string>

#include "aces/aces_compute.hpp"

namespace aces {

/**
 * @brief Structure containing all meteorology and base emissions imported from
 * other components.
 *
 * Uses a map to allow flexible addition of fields without hardcoding.
 */
struct AcesImportState {
    /// Map of field names to their respective DualViews.
    std::map<std::string, DualView3D> fields;
};

/**
 * @brief Structure containing all computed emissions to be exported back to the
 * framework.
 */
struct AcesExportState {
    /// Map of field names to their respective DualViews.
    std::map<std::string, DualView3D> fields;
};

/**
 * @brief FieldResolver that pulls from the unified AcesImportState and
 * AcesExportState.
 */
class AcesStateResolver : public FieldResolver {
    const AcesImportState& import_state;
    const AcesExportState& export_state;
    const std::map<std::string, std::string>& met_mapping;

   public:
    AcesStateResolver(const AcesImportState& imp, const AcesExportState& exp,
                      const std::map<std::string, std::string>& mapping)
        : import_state(imp), export_state(exp), met_mapping(mapping) {}

    UnmanagedHostView3D ResolveImport(const std::string& name, int nx, int ny, int nz) override {
        std::string resolve_name = name;
        auto map_it = met_mapping.find(name);
        if (map_it != met_mapping.end()) {
            resolve_name = map_it->second;
        }

        auto it = import_state.fields.find(resolve_name);
        if (it != import_state.fields.end()) {
            auto view = it->second.view_host();
            if (view.extent(0) != (size_t)nx || view.extent(1) != (size_t)ny ||
                view.extent(2) != (size_t)nz) {
                std::cerr << "ACES_Resolver Error: Dimension mismatch for import " << resolve_name
                          << ". Expected " << nx << "x" << ny << "x" << nz << ", got "
                          << view.extent(0) << "x" << view.extent(1) << "x" << view.extent(2)
                          << std::endl;
                return UnmanagedHostView3D();
            }
            return view;
        }
        return UnmanagedHostView3D();
    }

    UnmanagedHostView3D ResolveExport(const std::string& name, int nx, int ny, int nz) override {
        auto it = export_state.fields.find(name);
        if (it != export_state.fields.end()) {
            auto view = it->second.view_host();
            if (view.extent(0) != (size_t)nx || view.extent(1) != (size_t)ny ||
                view.extent(2) != (size_t)nz) {
                std::cerr << "ACES_Resolver Error: Dimension mismatch for export " << name
                          << ". Expected " << nx << "x" << ny << "x" << nz << ", got "
                          << view.extent(0) << "x" << view.extent(1) << "x" << view.extent(2)
                          << std::endl;
                return UnmanagedHostView3D();
            }
            return view;
        }
        return UnmanagedHostView3D();
    }

    Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>
    ResolveImportDevice(const std::string& name, int nx, int ny, int nz) override {
        std::string resolve_name = name;
        auto map_it = met_mapping.find(name);
        if (map_it != met_mapping.end()) {
            resolve_name = map_it->second;
        }

        auto it = import_state.fields.find(resolve_name);
        if (it != import_state.fields.end()) {
            auto view = it->second.view_device();
            if (view.extent(0) != (size_t)nx || view.extent(1) != (size_t)ny ||
                view.extent(2) != (size_t)nz) {
                std::cerr << "ACES_Resolver Error: Dimension mismatch for import " << resolve_name
                          << " (device). Expected " << nx << "x" << ny << "x" << nz << ", got "
                          << view.extent(0) << "x" << view.extent(1) << "x" << view.extent(2)
                          << std::endl;
                return Kokkos::View<const double***, Kokkos::LayoutLeft,
                                    Kokkos::DefaultExecutionSpace>();
            }
            return view;
        }
        return Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>();
    }

    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> ResolveExportDevice(
        const std::string& name, int nx, int ny, int nz) override {
        auto it = export_state.fields.find(name);
        if (it != export_state.fields.end()) {
            auto view = it->second.view_device();
            if (view.extent(0) != (size_t)nx || view.extent(1) != (size_t)ny ||
                view.extent(2) != (size_t)nz) {
                std::cerr << "ACES_Resolver Error: Dimension mismatch for export " << name
                          << " (device). Expected " << nx << "x" << ny << "x" << nz << ", got "
                          << view.extent(0) << "x" << view.extent(1) << "x" << view.extent(2)
                          << std::endl;
                return Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>();
            }
            return view;
        }
        return Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>();
    }
};

}  // namespace aces

#endif  // ACES_STATE_HPP
