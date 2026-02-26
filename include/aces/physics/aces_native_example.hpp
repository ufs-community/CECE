#ifndef ACES_NATIVE_EXAMPLE_HPP
#define ACES_NATIVE_EXAMPLE_HPP

#include "aces/physics_scheme.hpp"

namespace aces {

/**
 * @brief Example of a native Kokkos C++ physics plugin.
 */
class NativePhysicsExample : public PhysicsScheme {
public:
    NativePhysicsExample() = default;
    ~NativePhysicsExample() override = default;

    void Initialize(const YAML::Node& config) override;
    void Run(AcesImportState& import_state, AcesExportState& export_state) override;
};

} // namespace aces

#endif // ACES_NATIVE_EXAMPLE_HPP
