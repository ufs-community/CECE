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

KOKKOS_INLINE_FUNCTION
double get_sc_w_dms(double tc) {
    return 2674.0 - 147.12 * tc + 3.726 * tc * tc - 0.038 * tc * tc * tc;
}

KOKKOS_INLINE_FUNCTION
double get_kw_nightingale(double u10, double sc_w) {
    return (0.222 * u10 * u10 + 0.333 * u10) * std::pow(sc_w / 600.0, -0.5);
}

void DMSScheme::Initialize(const YAML::Node& /*config*/, AcesDiagnosticManager* /*diag_manager*/) {
    std::cout << "DMSScheme: Initialized." << std::endl;
}

void DMSScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto u10m = ResolveImport("wind_speed_10m", import_state);
    auto tskin = ResolveImport("tskin", import_state);
    auto seaconc = ResolveImport("DMS_seawater", import_state);
    auto dms_emis = ResolveExport("total_dms_emissions", export_state);

    if (!u10m.data() || !tskin.data() || !seaconc.data() || !dms_emis.data()) return;

    int nx = dms_emis.extent(0);
    int ny = dms_emis.extent(1);
    int nz = dms_emis.extent(2);

    Kokkos::parallel_for(
        "DMSKernel_Faithful",
        Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<3>>({0, 0, 0},
                                                                              {nx, ny, nz}),
        KOKKOS_LAMBDA(int i, int j, int k) {
            if (k != 0) return;  // Surface restricted

            double tk = tskin(i, j, k);
            double tc = tk - 273.15;
            double w = u10m(i, j, k);
            double conc = seaconc(i, j, k);

            if (tc < -10.0) return;

            double sc_w = get_sc_w_dms(tc);
            double k_w = get_kw_nightingale(w, sc_w);  // cm/hr
            k_w /= 360000.0;                           // cm/hr -> m/s

            dms_emis(i, j, k) += k_w * conc;
        });

    Kokkos::fence();
    MarkModified("total_dms_emissions", export_state);
}

}  // namespace aces
