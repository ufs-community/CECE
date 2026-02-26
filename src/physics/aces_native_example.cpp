#include "aces/physics/aces_native_example.hpp"
#include <Kokkos_Core.hpp>
#include <iostream>

namespace aces {

void NativePhysicsExample::Initialize(const YAML::Node& config) {
    std::cout << "NativePhysicsExample: Initialized." << std::endl;
}

void NativePhysicsExample::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto base_nox = import_state.base_anthropogenic_nox.view_device();
    auto total_nox = export_state.total_nox_emissions.view_device();

    int nx = total_nox.extent(0);
    int ny = total_nox.extent(1);
    int nz = total_nox.extent(2);

    if (nx == 0 || ny == 0 || nz == 0) return;

    Kokkos::parallel_for("NativePhysicsExampleKernel",
        Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<3>>({0, 0, 0}, {nx, ny, nz}),
        KOKKOS_LAMBDA(int i, int j, int k) {
            total_nox(i, j, k) += base_nox(i, j, k) * 2.0;
        }
    );
    Kokkos::fence();

    // Mark the device data as modified so a sync can happen if needed later
    export_state.total_nox_emissions.modify_device();
}

} // namespace aces
