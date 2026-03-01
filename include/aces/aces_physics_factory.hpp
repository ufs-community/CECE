#ifndef ACES_PHYSICS_FACTORY_HPP
#define ACES_PHYSICS_FACTORY_HPP

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "aces/aces_config.hpp"
#include "aces/physics_scheme.hpp"

namespace aces {

/**
 * @brief Factory class for creating and managing physics schemes.
 *
 * Uses a self-registration pattern to allow new schemes to be added without
 * modifying the factory itself.
 */
class PhysicsFactory {
   public:
    /// Type for the creator function.
    using CreatorFunc = std::function<std::unique_ptr<PhysicsScheme>()>;

    /**
     * @brief Creates a physics scheme based on the provided configuration.
     * @param config The configuration for the scheme.
     * @return A unique pointer to the created PhysicsScheme.
     */
    static std::unique_ptr<PhysicsScheme> CreateScheme(const PhysicsSchemeConfig& config);

    /**
     * @brief Registers a new physics scheme creator.
     * @param name The name of the physics scheme.
     * @param creator A function that creates an instance of the scheme.
     */
    static void RegisterScheme(const std::string& name, CreatorFunc creator);

   private:
    /**
     * @brief Retrieves the registry of creator functions.
     * @return A reference to the static registry map.
     */
    static std::unordered_map<std::string, CreatorFunc>& GetRegistry();
};

/**
 * @brief Helper class for automatic registration of physics schemes.
 */
template <typename T>
class PhysicsRegistration {
   public:
    /**
     * @brief Registers the scheme type T with the factory.
     * @param name The name of the physics scheme.
     */
    explicit PhysicsRegistration(const std::string& name) {
        PhysicsFactory::RegisterScheme(name, []() { return std::make_unique<T>(); });
    }
};

}  // namespace aces

#endif  // ACES_PHYSICS_FACTORY_HPP
