#include "aces/aces_physics_factory.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>

// Forward declarations of concrete schemes
namespace aces {
class NativePhysicsExample;
class FortranBridgeExample;
}

// These will be defined in their respective source files
#include "aces/physics/aces_native_example.hpp"
#include "aces/physics/aces_fortran_bridge.hpp"

namespace aces {

std::unique_ptr<PhysicsScheme> PhysicsFactory::CreateScheme(const PhysicsSchemeConfig& config) {
    std::unique_ptr<PhysicsScheme> scheme;

    if (config.name == "native_example" || config.language == "cpp") {
        scheme = std::make_unique<NativePhysicsExample>();
    } else if (config.name == "fortran_bridge_example" || config.language == "fortran") {
        scheme = std::make_unique<FortranBridgeExample>();
    } else {
        throw std::runtime_error("Unknown physics scheme: " + config.name);
    }

    if (scheme) {
        scheme->Initialize(config.options);
    }

    return scheme;
}

} // namespace aces
