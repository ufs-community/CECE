#ifndef ACES_FORTRAN_BRIDGE_HPP
#define ACES_FORTRAN_BRIDGE_HPP

/**
 * @file aces_fortran_bridge.hpp
 * @brief Header for the legacy Fortran bridge physics scheme.
 */

#include "aces/physics_scheme.hpp"

namespace aces {

/**
 * @class FortranBridgeExample
 * @brief A reference implementation of a bridge to legacy Fortran physics.
 *
 * This scheme demonstrates how to wrap existing Fortran emissions logic,
 * handling the necessary data synchronization between Kokkos Views and
 * raw Fortran-compatible pointers.
 */
class FortranBridgeExample : public PhysicsScheme {
   public:
    /**
     * @brief Default constructor.
     */
    FortranBridgeExample() = default;

    /**
     * @brief Virtual destructor.
     */
    ~FortranBridgeExample() override = default;

    /**
     * @brief Initializes the Fortran bridge.
     * @param config YAML node with bridge options.
     * @param diag_manager Pointer to the diagnostic manager.
     */
    void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) override;

    /**
     * @brief Coordinates the execution of the Fortran routine.
     * @param import_state Meteorlogy and base emissions input.
     * @param export_state Output emissions to be updated.
     */
    void Run(AcesImportState& import_state, AcesExportState& export_state) override;
};

}  // namespace aces

#endif  // ACES_FORTRAN_BRIDGE_HPP
