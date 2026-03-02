#ifndef ACES_PHYSICS_SCHEME_HPP
#define ACES_PHYSICS_SCHEME_HPP

/**
 * @file physics_scheme.hpp
 * @brief Defines the base classes for physics schemes in ACES.
 */

#include <yaml-cpp/yaml.h>

#include <memory>
#include <string>
#include <unordered_map>

#include "aces/aces_diagnostics.hpp"
#include "aces/aces_state.hpp"

namespace aces {

/**
 * @brief Abstract base class for all physics schemes in ACES.
 */
class PhysicsScheme {
   public:
    virtual ~PhysicsScheme() = default;

    /**
     * @brief Initializes the physics scheme with configuration options.
     * @param config YAML node containing scheme-specific options.
     * @param diag_manager Pointer to the diagnostic manager for registering
     * variables.
     */
    virtual void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) = 0;

    /**
     * @brief Executes the physics scheme.
     * @param import_state The input meteorology and base emissions.
     * @param export_state The output emissions to be updated.
     */
    virtual void Run(AcesImportState& import_state, AcesExportState& export_state) = 0;
};

/**
 * @brief A scientist-friendly base class that provides common helper methods
 * and reduces boilerplate for implementing new physics schemes.
 *
 * This class handles common tasks like resolving fields from the state,
 * simplifying the transition to Kokkos-based compute kernels.
 */
class BasePhysicsScheme : public PhysicsScheme {
   public:
    /**
     * @brief Default implementation of Initialize.
     * Can be overridden by subclasses if they need specific setup.
     */
    void Initialize(const YAML::Node& config, AcesDiagnosticManager* /*diag_manager*/) override {
        if (config["input_mapping"]) {
            for (auto const& node : config["input_mapping"]) {
                input_mapping_[node.first.as<std::string>()] = node.second.as<std::string>();
            }
        }
        if (config["output_mapping"]) {
            for (auto const& node : config["output_mapping"]) {
                output_mapping_[node.first.as<std::string>()] = node.second.as<std::string>();
            }
        }
    }

   protected:
    /**
     * @brief Maps an internal input name to an external field name.
     */
    [[nodiscard]] std::string MapInput(const std::string& name) const {
        auto it = input_mapping_.find(name);
        return (it != input_mapping_.end()) ? it->second : name;
    }

    /**
     * @brief Maps an internal output name to an external field name.
     */
    [[nodiscard]] std::string MapOutput(const std::string& name) const {
        auto it = output_mapping_.find(name);
        return (it != output_mapping_.end()) ? it->second : name;
    }

    /**
     * @brief Helper to resolve an import field's device-side View.
     * @details Caches the View to avoid redundant map lookups.
     * @param name Name of the field.
     * @param state The import state.
     * @return A device-side Kokkos::View.
     */
    Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> ResolveImport(
        const std::string& name, AcesImportState& state) {
        std::string resolved_name = MapInput(name);
        if (auto it = import_cache_.find(resolved_name); it != import_cache_.end()) {
            return it->second;
        }
        auto it = state.fields.find(resolved_name);
        if (it != state.fields.end()) {
            auto view = it->second.view_device();
            import_cache_[resolved_name] = view;
            return view;
        }
        return {};
    }

    /**
     * @brief Helper to resolve an export field's device-side View.
     * @details Caches the View to avoid redundant map lookups.
     * @param name Name of the field.
     * @param state The export state.
     * @return A device-side Kokkos::View.
     */
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> ResolveExport(
        const std::string& name, AcesExportState& state) {
        std::string resolved_name = MapOutput(name);
        if (auto it = export_cache_.find(resolved_name); it != export_cache_.end()) {
            return it->second;
        }
        auto it = state.fields.find(resolved_name);
        if (it != state.fields.end()) {
            auto view = it->second.view_device();
            export_cache_[resolved_name] = view;
            return view;
        }
        return {};
    }

    /**
     * @brief Helper to resolve a field from either import or export state.
     * @details Useful for schemes that depend on emissions computed by other
     * schemes. Checks import state first, then export state.
     * @param name Internal name of the field.
     * @param import_state The import state.
     * @param export_state The export state.
     * @return A device-side Kokkos::View (read-only).
     */
    Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> ResolveInput(
        const std::string& name, AcesImportState& import_state, AcesExportState& export_state) {
        std::string resolved_name = MapInput(name);

        // Try import state first
        if (auto it = import_cache_.find(resolved_name); it != import_cache_.end()) {
            return it->second;
        }
        auto it_imp = import_state.fields.find(resolved_name);
        if (it_imp != import_state.fields.end()) {
            auto view = it_imp->second.view_device();
            import_cache_[resolved_name] = view;
            return view;
        }

        // Then try export state (read-only access to previously computed fields)
        // We use import_cache_ for both to keep input resolution consistent
        auto it_exp = export_state.fields.find(resolved_name);
        if (it_exp != export_state.fields.end()) {
            auto view = it_exp->second.view_device();
            import_cache_[resolved_name] = view;
            return view;
        }

        return {};
    }

    /**
     * @brief Clears the cached field handles.
     * @details Call this if the underlying state pointers change.
     */
   public:
    void ClearPhysicsCache() {
        import_cache_.clear();
        export_cache_.clear();
    }

   protected:
    /**
     * @brief Marks an export field as modified on the device.
     * @param name Name of the field.
     * @param state The export state.
     */
    void MarkModified(const std::string& name, AcesExportState& state) {
        std::string resolved_name = MapOutput(name);
        auto it = state.fields.find(resolved_name);
        if (it != state.fields.end()) {
            it->second.modify_device();
        }
    }

   private:
    std::unordered_map<std::string, Kokkos::View<const double***, Kokkos::LayoutLeft,
                                                 Kokkos::DefaultExecutionSpace>>
        import_cache_;
    std::unordered_map<std::string,
                       Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>>
        export_cache_;

    std::unordered_map<std::string, std::string> input_mapping_;
    std::unordered_map<std::string, std::string> output_mapping_;
};

}  // namespace aces

#endif  // ACES_PHYSICS_SCHEME_HPP
