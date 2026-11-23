/**
 * @file cece_physics_factory.cpp
 * @brief Implementation of the PhysicsFactory for dynamic scheme registration and instantiation.
 *
 * The PhysicsFactory provides a flexible plugin system for CECE physics schemes,
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

#include "cece/cece_physics_factory.hpp"

#include <string>

#include "cece/cece_logger.hpp"

using namespace std::literals::string_literals;

namespace cece {

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
        CECE_LOG_INFO("CECE_PhysicsFactory: Creating scheme "s + config.name + "\n"s);
        return it->second();
    }

    std::string errmsg = "CECE_PhysicsFactory: Error - Unknown physics scheme '"s + config.name + "'. Registered schemes:"s;
    for (const auto& [name, _] : registry) {
        errmsg += " "s + name;
    }
    errmsg += "\n"s;
    CECE_LOG_ERROR(errmsg);
    return nullptr;
}

}  // namespace cece
