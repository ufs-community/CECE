#include "aces/aces_physics_factory.hpp"

#include <iostream>

#include "aces/physics/aces_fortran_bridge.hpp"
#include "aces/physics/aces_native_example.hpp"

/**
 * @file aces_physics_factory.cpp
 * @brief Implementation of the PhysicsFactory for scheme instantiation.
 */

namespace aces {

/**
 * @brief Creates a physics scheme based on the provided configuration.
 *
 * Supports both native C++ schemes and Fortran-based schemes via a bridge.
 *
 * @param config Configuration for the physics scheme.
 * @return A unique pointer to the created PhysicsScheme, or nullptr if type is unknown.
 */
std::unique_ptr<PhysicsScheme> PhysicsFactory::CreateScheme(const PhysicsSchemeConfig& config) {
    std::unique_ptr<PhysicsScheme> scheme;

    if (config.language == "fortran" || config.name == "fortran_bridge_example") {
#ifdef ACES_HAS_FORTRAN
        std::cout << "ACES_PhysicsFactory: Creating Fortran scheme " << config.name << std::endl;
        scheme = std::make_unique<FortranBridgeExample>();
#else
        std::cerr << "ACES_PhysicsFactory: Error - Fortran scheme " << config.name
                  << " requested but Fortran support is disabled." << std::endl;
#endif
    } else {
        // Default to Native C++
        std::cout << "ACES_PhysicsFactory: Creating Native scheme " << config.name << std::endl;
        scheme = std::make_unique<NativePhysicsExample>();
    }

    return scheme;
}

}  // namespace aces
