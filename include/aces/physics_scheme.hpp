#ifndef ACES_PHYSICS_SCHEME_HPP
#define ACES_PHYSICS_SCHEME_HPP

#include <yaml-cpp/yaml.h>

#include <memory>

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
     * @param diag_manager Pointer to the diagnostic manager for registering variables.
     */
    virtual void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) = 0;

    /**
     * @brief Executes the physics scheme.
     * @param import_state The input meteorology and base emissions.
     * @param export_state The output emissions to be updated.
     */
    virtual void Run(AcesImportState& import_state, AcesExportState& export_state) = 0;
};

}  // namespace aces

#endif  // ACES_PHYSICS_SCHEME_HPP
