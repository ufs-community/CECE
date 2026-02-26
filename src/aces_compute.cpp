#include "aces/aces_compute.hpp"

namespace aces {

/**
 * @brief Executes the emissions scaling computation using Kokkos.
 */
void ComputeEmissions(AcesImportState& importState, AcesExportState& exportState) {
    // Ensure data is on the device
    importState.base_emissions.sync<Kokkos::DefaultExecutionSpace>();
    importState.scaling_factor.sync<Kokkos::DefaultExecutionSpace>();
    importState.mask.sync<Kokkos::DefaultExecutionSpace>();
    exportState.scaled_emissions.sync<Kokkos::DefaultExecutionSpace>();

    // Mark export state as modified on device
    exportState.scaled_emissions.modify<Kokkos::DefaultExecutionSpace>();

    auto base = importState.base_emissions.view<Kokkos::DefaultExecutionSpace>();
    auto scale = importState.scaling_factor.view<Kokkos::DefaultExecutionSpace>();
    auto mask = importState.mask.view<Kokkos::DefaultExecutionSpace>();
    auto result = exportState.scaled_emissions.view<Kokkos::DefaultExecutionSpace>();

    int ni = base.extent(0);
    int nj = base.extent(1);
    int nk = base.extent(2);

    // Use MDRangePolicy for 3D iteration
    using Policy = Kokkos::MDRangePolicy<Kokkos::Rank<3>>;

    Kokkos::parallel_for("EmissionScaling",
                        Policy({0, 0, 0}, {ni, nj, nk}),
                        EmissionScalingFunctor{base, scale, mask, result});

    // Wait for completion
    Kokkos::fence();
}

} // namespace aces
