/**
 * @file aces_physics_factory.cpp
 * @brief Implementation of the PhysicsFactory for dynamic scheme registration and instantiation.
 *
 * The PhysicsFactory provides a flexible plugin system for ACES physics schemes,
 * enabling runtime registration and creation of emission calculation modules.
 * This design supports:
 *
 * - Dynamic scheme loading and registration
 * - Automatic discovery of available physics schemes
 * - Type-safe scheme instantiation with polymorphic interfaces
 * - Fallback mechanisms for unknown or missing schemes
 * - Integration with both C++ and Fortran implementations
 *
 * The factory uses a static registry pattern to maintain scheme creators
 * across the application lifetime, with self-registration occurring during
 * static initialization through PhysicsRegistration template instances.
 *
 * @author Barry Baker
 * @date 2024
 * @version 1.0
 */

#include "aces/aces_physics_factory.hpp"

#include <iostream>

namespace aces {

/**
 * @brief Retrieves the static registry of physics schemes.
 */
std::unordered_map<std::string, PhysicsFactory::CreatorFunc>& PhysicsFactory::GetRegistry() {
    static std::unordered_map<std::string, CreatorFunc> registry;
    return registry;
}

/**
 * @brief Registers a new physics scheme in the factory.
 */
void PhysicsFactory::RegisterScheme(const std::string& name, CreatorFunc creator) {
    GetRegistry()[name] = creator;
}

/**
 * @brief Creates a physics scheme based on the provided configuration.
 */
std::unique_ptr<PhysicsScheme> PhysicsFactory::CreateScheme(const PhysicsSchemeConfig& config) {
    auto& registry = GetRegistry();
    auto it = registry.find(config.name);

    if (it != registry.end()) {
        std::cout << "ACES_PhysicsFactory: Creating scheme " << config.name << "\n";
        return it->second();
    }

    std::cerr << "ACES_PhysicsFactory: Error - Unknown physics scheme '" << config.name
              << "'. Registered schemes:";
    for (const auto& [name, _] : registry) {
        std::cerr << " " << name;
    }
    std::cerr << "\n";
    return nullptr;
}

}  // namespace aces
