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
double get_lightning_yield(double rate, double mw_no, bool is_land, double yield_land,
                           double yield_ocean) {
    // Ported from RFLASH_MIDLAT/TROPIC logic
    // Midlat: 500 mol/flash, Tropic: 260 mol/flash (simplified yield factor here)
    const double yield_molec = is_land ? yield_land : yield_ocean;
    const double AVOGADRO = 6.022e23;
    return (rate * yield_molec) * (mw_no / 1000.0) / (AVOGADRO * 1.0e6);
}

void LightningScheme::Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);

    yield_land_ = 3.011e26;
    yield_ocean_ = 1.566e26;

    if (config["yield_land"]) yield_land_ = config["yield_land"].as<double>();
    if (config["yield_ocean"]) yield_ocean_ = config["yield_ocean"].as<double>();

    std::cout << "LightningScheme: Initialized.\n";
}

void LightningScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto conv_depth = ResolveImport("convective_cloud_top_height", import_state);
    auto light_nox = ResolveExport("lightning_nox", export_state);
    auto it_land = import_state.fields.find("land_mask");

    if (conv_depth.data() == nullptr || light_nox.data() == nullptr) {
        return;
    }

    int nx = static_cast<int>(light_nox.extent(0));
    int ny = static_cast<int>(light_nox.extent(1));
    int nz = static_cast<int>(light_nox.extent(2));

    // Land mask proxy
    bool has_land_mask = (it_land != import_state.fields.end());
    auto land_mask =
        has_land_mask
            ? it_land->second.view_device()
            : Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>();

    const double MW_NO = 30.0;
    double y_land = yield_land_;
    double y_ocean = yield_ocean_;

    Kokkos::parallel_for(
        "LightningKernel_Optimized",
        Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<2>>({0, 0}, {nx, ny}),
        KOKKOS_LAMBDA(int i, int j) {
            // Use convective depth from surface or first level as representative for the column
            double h = conv_depth(i, j, 0);
            if (h <= 0.0) {
                return;
            }

            bool is_land = has_land_mask ? (land_mask(i, j, 0) > 0.5) : true;
            double h_km = h / 1000.0;
            double flash_rate = 3.44e-5 * std::pow(h_km, 4.9);

            double total_yield = get_lightning_yield(flash_rate, MW_NO, is_land, y_land, y_ocean);
            double level_yield = total_yield / static_cast<double>(nz);

            // Vertically distribute (Ott et al. proxy) - Optimized column fill
            for (int k = 0; k < nz; ++k) {
                light_nox(i, j, k) += level_yield;
            }
        });

    Kokkos::fence();
    MarkModified("lightning_nox", export_state);
}

}  // namespace aces
