/**
 * @file cece_ginoux.hpp
 * @brief Native C++ implementation of the Ginoux (GOCART2G) dust emission scheme.
 * @author CECE Development Team
 * @date 2025
 * @version 1.0
 */
#ifndef CECE_GINOUX_HPP
#define CECE_GINOUX_HPP

#include "cece/physics_scheme.hpp"
#include <Kokkos_Core.hpp>

namespace cece {

/**
 * @class GinouxScheme
 * @brief GPU-portable Ginoux (GOCART2G) dust emission scheme using Kokkos.
 *
 * Implements the Ginoux dust emission algorithm using Kokkos for
 * GPU-portable parallel execution, including Marticorena dry-soil threshold
 * velocity and Ginoux moisture modification.
 */
class GinouxScheme : public BasePhysicsScheme {
   public:
    GinouxScheme() = default;
    ~GinouxScheme() override = default;

    void Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;

   private:
    double ch_du_ = 0.8e-9;
    double grav_ = 9.81;
    int num_bins_ = 5;
    Kokkos::View<double*, Kokkos::DefaultExecutionSpace> particle_radii_;
};

}  // namespace cece

#endif  // CECE_GINOUX_HPP
