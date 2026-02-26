#ifndef ACES_PHYSICS_SCHEME_HPP
#define ACES_PHYSICS_SCHEME_HPP

#include "aces/aces_state.hpp"
#include <yaml-cpp/yaml.h>
#include <memory>

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
     */
    virtual void Initialize(const YAML::Node& config) = 0;

    /**
     * @brief Executes the physics scheme.
     * @param import_state The input meteorology and base emissions.
     * @param export_state The output emissions to be updated.
     */
    virtual void Run(AcesImportState& import_state, AcesExportState& export_state) = 0;
};

} // namespace aces

#endif // ACES_PHYSICS_SCHEME_HPP
