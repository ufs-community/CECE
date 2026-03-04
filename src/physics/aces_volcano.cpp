#include "aces/physics/aces_volcano.hpp"

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

#include "aces/aces_physics_factory.hpp"

namespace aces {

/// Self-registration for the VolcanoScheme scheme.
static PhysicsRegistration<VolcanoScheme> register_scheme("volcano");

void VolcanoScheme::Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);
    std::cout << "VolcanoScheme: Initialized." << "\n";
}

void VolcanoScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto so2 = ResolveExport("so2", export_state);
    auto zsfc = ResolveImport("zsfc", import_state);
    auto bxheight = ResolveImport("bxheight_m", import_state);

    if (so2.data() == nullptr || zsfc.data() == nullptr || bxheight.data() == nullptr) {
        return;
    }

    int nx = static_cast<int>(so2.extent(0));
    int ny = static_cast<int>(so2.extent(1));
    int nz = static_cast<int>(so2.extent(2));

    // Mock volcano location for this port (should come from config table in real
    // port) Lat: 50.17, Lon: 6.85, Sulf: 1.0kg/s, Elv: 600m, Cld: 2000m
    const int target_i = 1, target_j = 1;
    const double volcano_sulf = 1.0;
    const double volcano_elv = 600.0;
    const double volcano_cld = 2000.0;

    Kokkos::parallel_for(
        "VolcanoKernel_Optimized", Kokkos::RangePolicy<Kokkos::DefaultExecutionSpace>(0, nz),
        KOKKOS_LAMBDA(int k) {
            // Refactored to only iterate over the vertical column for the point source.
            // In a real implementation, target_i/target_j would be mapped from local process
            // bounds.
            int i = target_i;
            int j = target_j;

            double z_bot_box = zsfc(i, j, 0);
            for (int l = 0; l < k; ++l) z_bot_box += bxheight(i, j, l);
            double z_top_box = z_bot_box + bxheight(i, j, k);

            double z_bot_volc = std::max(volcano_elv, zsfc(i, j, 0));
            double z_top_volc = std::max(volcano_cld, zsfc(i, j, 0));

            // Eruptive: top 1/3
            if (z_bot_volc != z_top_volc) {
                z_bot_volc = z_top_volc - (z_top_volc - z_bot_volc) / 3.0;
            }

            double plume_hgt = z_top_volc - z_bot_volc;
            if (plume_hgt <= 0.0) {
                if (k == 0) so2(i, j, k) += volcano_sulf;
                return;
            }

            if (z_bot_volc >= z_top_box || z_top_volc <= z_bot_box) {
                return;
            }

            double overlap = std::min(z_top_volc, z_top_box) - std::max(z_bot_volc, z_bot_box);
            double frac = overlap / plume_hgt;
            so2(i, j, k) += frac * volcano_sulf;
        });

    Kokkos::fence();
    MarkModified("so2", export_state);
}

}  // namespace aces
