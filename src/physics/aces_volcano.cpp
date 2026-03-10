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

    target_i_ = 1;
    target_j_ = 1;
    volcano_sulf_ = 1.0;
    volcano_elv_ = 600.0;
    volcano_cld_ = 2000.0;

    if (config["target_i"]) target_i_ = config["target_i"].as<int>();
    if (config["target_j"]) target_j_ = config["target_j"].as<int>();
    if (config["sulfur_emission"]) volcano_sulf_ = config["sulfur_emission"].as<double>();
    if (config["elevation"]) volcano_elv_ = config["elevation"].as<double>();
    if (config["cloud_top"]) volcano_cld_ = config["cloud_top"].as<double>();

    std::cout << "VolcanoScheme: Initialized." << "\n";
}

void VolcanoScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto so2 = ResolveExport("so2", export_state);
    auto zsfc = ResolveImport("zsfc", import_state);
    auto bxheight = ResolveImport("bxheight_m", import_state);

    if (so2.data() == nullptr || zsfc.data() == nullptr || bxheight.data() == nullptr) {
        return;
    }

    int nz = static_cast<int>(so2.extent(2));

    int target_i = target_i_;
    int target_j = target_j_;
    double volcano_sulf = volcano_sulf_;
    double volcano_elv = volcano_elv_;
    double volcano_cld = volcano_cld_;

    Kokkos::parallel_for(
        "VolcanoKernel_Optimized", Kokkos::RangePolicy<Kokkos::DefaultExecutionSpace>(0, nz),
        KOKKOS_LAMBDA(int k) {
            // Refactored to only iterate over the vertical column for the point source.
            // In a real implementation, target_i/target_j would be mapped from local process
            // bounds.
            int i = target_i;
            int j = target_j;

            double z_bot_box = zsfc(i, j, 0);
            for (int l = 0; l < k; ++l) {
                z_bot_box += bxheight(i, j, l);
            }
            double z_top_box = z_bot_box + bxheight(i, j, k);

            double z_bot_volc = std::max(volcano_elv, zsfc(i, j, 0));
            double z_top_volc = std::max(volcano_cld, zsfc(i, j, 0));

            // Eruptive: top 1/3
            if (z_bot_volc != z_top_volc) {
                z_bot_volc = z_top_volc - (z_top_volc - z_bot_volc) / 3.0;
            }

            double plume_hgt = z_top_volc - z_bot_volc;
            if (plume_hgt <= 0.0) {
                if (k == 0) {
                    so2(i, j, k) += volcano_sulf;
                }
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
