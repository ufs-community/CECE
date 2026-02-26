#ifndef ACES_PHYSICS_FACTORY_HPP
#define ACES_PHYSICS_FACTORY_HPP

#include "aces/physics_scheme.hpp"
#include "aces/aces_config.hpp"
#include <string>
#include <memory>
#include <vector>

namespace aces {

/**
 * @brief Factory class for creating and managing physics schemes.
 */
class PhysicsFactory {
public:
    /**
     * @brief Creates a physics scheme based on the provided configuration.
     * @param config The configuration for the scheme.
     * @return A unique pointer to the created PhysicsScheme.
     */
    static std::unique_ptr<PhysicsScheme> CreateScheme(const PhysicsSchemeConfig& config);
};

} // namespace aces

#endif // ACES_PHYSICS_FACTORY_HPP
