#ifndef ACES_FORTRAN_BRIDGE_HPP
#define ACES_FORTRAN_BRIDGE_HPP

#include "aces/physics_scheme.hpp"

namespace aces {

/**
 * @brief Example of a Fortran bridge physics plugin.
 */
class FortranBridgeExample : public PhysicsScheme {
public:
    FortranBridgeExample() = default;
    ~FortranBridgeExample() override = default;

    void Initialize(const YAML::Node& config) override;
    void Run(AcesImportState& import_state, AcesExportState& export_state) override;
};

} // namespace aces

#endif // ACES_FORTRAN_BRIDGE_HPP
