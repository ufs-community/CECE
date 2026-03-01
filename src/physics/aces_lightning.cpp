#include "aces/physics/aces_lightning.hpp"

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

#include "aces/aces_physics_factory.hpp"

namespace aces {

/// Self-registration for the LightningScheme scheme.
static PhysicsRegistration<LightningScheme> register_scheme("lightning");

/**
 * @brief Lightning NOx Yield and Vertical Distribution (Ported from
 * hcox_lightnox_mod.F90)
 */

KOKKOS_INLINE_FUNCTION
double get_lightning_yield(double rate, double mw_no, bool is_land) {
    // Ported from RFLASH_MIDLAT/TROPIC logic
    // Midlat: 500 mol/flash, Tropic: 260 mol/flash (simplified yield factor here)
    const double yield_molec = is_land ? 3.011e26 : 1.566e26;
    const double AVOGADRO = 6.022e23;
    return (rate * yield_molec) * (mw_no / 1000.0) / (AVOGADRO * 1.0e6);
}

void LightningScheme::Initialize(const YAML::Node& /*config*/,
                                 AcesDiagnosticManager* /*diag_manager*/) {
    std::cout << "LightningScheme: Initialized." << std::endl;
}

void LightningScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto conv_depth = ResolveImport("convective_cloud_top_height", import_state);
    auto light_nox = ResolveExport("total_lightning_nox_emissions", export_state);
    auto it_land = import_state.fields.find("land_mask");

    if (!conv_depth.data() || !light_nox.data()) return;

    // Land mask proxy
    bool has_land_mask = (it_land != import_state.fields.end());
    auto land_mask =
        has_land_mask
            ? it_land->second.view_device()
            : Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>();

    int nx = light_nox.extent(0);
    int ny = light_nox.extent(1);
    int nz = light_nox.extent(2);

    const double MW_NO = 30.0;

    Kokkos::parallel_for(
        "LightningKernel_Faithful",
        Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<3>>({0, 0, 0},
                                                                              {nx, ny, nz}),
        KOKKOS_LAMBDA(int i, int j, int k) {
            double h = conv_depth(i, j, k);
            if (h <= 0.0) return;

            bool is_land = has_land_mask ? (land_mask(i, j, 0) > 0.5) : true;
            double h_km = h / 1000.0;
            double flash_rate = 3.44e-5 * std::pow(h_km, 4.9);

            double total_yield = get_lightning_yield(flash_rate, MW_NO, is_land);

            // Vertically distribute (Ott et al. proxy)
            light_nox(i, j, k) += total_yield / nz;
        });

    Kokkos::fence();
    MarkModified("total_lightning_nox_emissions", export_state);
}

}  // namespace aces
