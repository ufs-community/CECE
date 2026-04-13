#ifndef CECE_NATIVE_EXAMPLE_HPP
#define CECE_NATIVE_EXAMPLE_HPP

/**
 * @file cece_native_example.hpp
 * @brief Header for the sample native C++ physics scheme.
 */

#include "cece/physics_scheme.hpp"

namespace cece {

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
    void Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) override;

    /**
     * @brief Executes the computational kernel.
     * @param import_state Meteorlogy and base emissions input.
     * @param export_state Output emissions to be updated.
     */
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;

   private:
    double multiplier_ = 2.0;
};

}  // namespace cece

#endif  // CECE_NATIVE_EXAMPLE_HPP
