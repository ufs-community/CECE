#include "aces/aces_physics_factory.hpp"

#include <iostream>

/**
 * @file aces_physics_factory.cpp
 * @brief Implementation of the PhysicsFactory for scheme instantiation.
 */

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
        std::cout << "ACES_PhysicsFactory: Creating scheme " << config.name << std::endl;
        return it->second();
    }

    // Fallback for unknown schemes if they might be generic
    if (config.language == "cpp" || config.language == "") {
        it = registry.find("native_example");
        if (it != registry.end()) {
            std::cout << "ACES_PhysicsFactory: Falling back to native_example for " << config.name
                      << std::endl;
            return it->second();
        }
    }

    std::cerr << "ACES_PhysicsFactory: Error - Unknown physics scheme " << config.name << std::endl;
    return nullptr;
}

}  // namespace aces
