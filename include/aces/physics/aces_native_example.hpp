#ifndef ACES_NATIVE_EXAMPLE_HPP
#define ACES_NATIVE_EXAMPLE_HPP

/**
 * @file aces_native_example.hpp
 * @brief Header for the sample native C++ physics scheme.
 */

#include "aces/physics_scheme.hpp"

namespace aces {

/**
 * @class NativePhysicsExample
 * @brief A reference implementation of a native Kokkos C++ physics plugin.
 *
 * This scheme demonstrates how to implement emissions logic directly in C++
 * using Kokkos kernels for high performance on both CPU and GPU.
 */
class NativePhysicsExample : public BasePhysicsScheme {
   public:
    /**
     * @brief Default constructor.
     */
    NativePhysicsExample() = default;

    /**
     * @brief Virtual destructor.
     */
    ~NativePhysicsExample() override = default;

    /**
     * @brief Initializes the scheme.
     * @param config YAML node with scheme options.
     * @param diag_manager Pointer to the diagnostic manager.
     */
    void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) override;

    /**
     * @brief Executes the computational kernel.
     * @param import_state Meteorlogy and base emissions input.
     * @param export_state Output emissions to be updated.
     */
    void Run(AcesImportState& import_state, AcesExportState& export_state) override;
};

}  // namespace aces

#endif  // ACES_NATIVE_EXAMPLE_HPP
