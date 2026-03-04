#include "aces/physics/aces_dms.hpp"

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

#include "aces/aces_physics_factory.hpp"

namespace aces {

/// Self-registration for the DMSScheme scheme.
static PhysicsRegistration<DMSScheme> register_scheme("dms");

/**
 * @brief DMS Sea-Air Flux (Ported from hcox_seaflux_mod.F90)
 */

void DMSScheme::Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);
    std::cout << "DMSScheme: Initialized." << "\n";
}

void DMSScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto u10m = ResolveImport("wind_speed_10m", import_state);
    auto tskin = ResolveImport("tskin", import_state);
    auto seaconc = ResolveImport("DMS_seawater", import_state);
    auto dms_emis = ResolveExport("dms", export_state);

    if (u10m.data() == nullptr || tskin.data() == nullptr || seaconc.data() == nullptr ||
        dms_emis.data() == nullptr)
        return;

    int nx = static_cast<int>(dms_emis.extent(0));
    int ny = static_cast<int>(dms_emis.extent(1));
    int nz = static_cast<int>(dms_emis.extent(2));

    Kokkos::parallel_for(
        "DMSKernel_Optimized",
        Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<2>>({0, 0}, {nx, ny}),
        KOKKOS_LAMBDA(int i, int j) {
            double tk = tskin(i, j, 0);
            double tc = tk - 273.15;
            double w = u10m(i, j, 0);
            double conc = seaconc(i, j, 0);

            if (tc < -10.0) {
                return;
            }

            // Horner's Method for Schmidt number
            double sc_w = 2674.0 + tc * (-147.12 + tc * (3.726 + tc * -0.038));

            double k_w = (0.222 * w * w + 0.333 * w) * std::pow(sc_w / 600.0, -0.5);  // cm/hr
            k_w /= 360000.0;  // cm/hr -> m/s

            dms_emis(i, j, 0) += k_w * conc;
        });

    Kokkos::fence();
    MarkModified("dms", export_state);
}

}  // namespace aces
