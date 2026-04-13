/**
 * @file cece_gocart2g.hpp
 * @brief Native C++ implementation of the GOCART2G dust emission scheme.
 * @author CECE Development Team
 * @date 2025
 * @version 1.0
 */
#ifndef CECE_GOCART2G_HPP
#define CECE_GOCART2G_HPP

#include "cece/physics_scheme.hpp"
#include <Kokkos_Core.hpp>

namespace cece {

/**
 * @class Gocart2gScheme
 * @brief GPU-portable GOCART2G dust emission scheme using Kokkos.
 *
 * Implements the GOCART2G (Ginoux) dust emission algorithm using Kokkos for
 * GPU-portable parallel execution, including Marticorena dry-soil threshold
 * velocity and Ginoux moisture modification.
 */
class Gocart2gScheme : public BasePhysicsScheme {
   public:
    Gocart2gScheme() = default;
    ~Gocart2gScheme() override = default;

    void Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;

   private:
    double ch_du_ = 0.8e-9;
    double grav_ = 9.81;
    int num_bins_ = 5;
    Kokkos::View<double*, Kokkos::DefaultExecutionSpace> particle_radii_;
};

}  // namespace cece

#endif  // CECE_GOCART2G_HPP
