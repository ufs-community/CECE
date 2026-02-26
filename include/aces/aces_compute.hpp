#ifndef ACES_COMPUTE_HPP
#define ACES_COMPUTE_HPP

/**
 * @file aces_compute.hpp
 * @brief Computational kernels for ACES emissions processing.
 */

#include "aces/aces_state.hpp"

namespace aces {

/**
 * @brief Kokkos functor for scaling and masking emissions.
 * Multiplies base emissions by scaling factors and geographical masks.
 */
struct EmissionScalingFunctor {
    Kokkos::View<const double***, Kokkos::LayoutLeft> base_emissions;
    Kokkos::View<const double***, Kokkos::LayoutLeft> scaling_factor;
    Kokkos::View<const double***, Kokkos::LayoutLeft> mask;
    Kokkos::View<double***, Kokkos::LayoutLeft> scaled_emissions;

    /**
     * @brief 3D iteration operator.
     * @param i Grid index i (e.g., longitude).
     * @param j Grid index j (e.g., latitude).
     * @param k Grid index k (e.g., level).
     */
    KOKKOS_INLINE_FUNCTION
    void operator()(const int i, const int j, const int k) const {
        // Compute scaled emissions: base * scale * mask
        // Note: No branching (if/else) used here for GPU performance.
        scaled_emissions(i, j, k) = base_emissions(i, j, k) * scaling_factor(i, j, k) * mask(i, j, k);
    }
};

/**
 * @brief Executes the emissions scaling computation.
 * @param importState Input emissions and factors.
 * @param exportState Output scaled emissions.
 */
void ComputeEmissions(AcesImportState& importState, AcesExportState& exportState);

} // namespace aces

#endif // ACES_COMPUTE_HPP
