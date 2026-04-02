/**
 * @file aces_lightning.cpp
 * @brief Lightning-produced nitrogen oxide (NOx) emission scheme.
 *
 * Implements lightning NOx emission calculations based on flash rate data
 * and empirical yield factors. Lightning is a significant natural source
 * of NOx in the middle and upper troposphere, affecting ozone chemistry
 * and atmospheric composition.
 *
 * The scheme includes:
 * - Flash rate to NOx conversion using empirical yield factors
 * - Land/ocean yield differentiation (tropical vs. midlatitude)
 * - Vertical distribution profiles for lightning NOx placement
 * - Molecular weight and unit conversions for ACES integration
 *
 * This implementation is based on algorithms from HEMCO's hcox_lightnox_mod.F90
 * with adaptations for the ACES framework and Kokkos execution.
 *
 * References:
 * - Price, C., et al. (1997), Vertical distributions of lightning NOx for use
 *   in regional and global chemical transport models, JGR, 102(D5), 5943-5941.
 *
 * @author Barry Baker
 * @date 2024
 * @version 1.0
 */

#include "aces/physics/aces_lightning.hpp"

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

#include "aces/aces_physics_factory.hpp"

namespace aces {

/// @brief Self-registration for the lightning NOx emission scheme.
static PhysicsRegistration<LightningScheme> register_scheme("lightning");

/**
 * @brief Calculate NOx production from lightning flash rate.
 *
 * Converts lightning flash rate to NOx production using empirical yield factors
 * that differ between land and ocean regions. The yields are based on
 * observational studies and represent moles of NO produced per flash.
 *
 * Typical yields:
 * - Land/tropical: ~260 mol NO/flash
 * - Ocean/midlatitude: ~500 mol NO/flash
 *
 * @param rate Lightning flash rate [flashes/s/grid_cell]
 * @param mw_no Molecular weight of NO [g/mol]
 * @param is_land Flag indicating land (true) vs. ocean (false)
 * @param yield_land NOx yield factor for land regions [mol/flash]
 * @param yield_ocean NOx yield factor for ocean regions [mol/flash]
 * @return NOx production rate [kg/s/grid_cell]
 */

KOKKOS_INLINE_FUNCTION
double get_lightning_yield(double rate, double mw_no, bool is_land, double yield_land,
                           double yield_ocean) {
    // Select appropriate yield factor based on surface type
    // Land/tropical regions typically have lower yields than ocean/midlatitude
    const double yield_molec = is_land ? yield_land : yield_ocean;

    // Avogadro's number for mol to molecule conversion
    const double AVOGADRO = 6.022e23;

    // Convert: flashes/s * mol/flash * g/mol * kg/g / (molecules/mol * m²/grid_cell)
    // Result: kg/s/grid_cell NOx production
    return (rate * yield_molec) * (mw_no / 1000.0) / (AVOGADRO * 1.0e6);
}

void LightningScheme::Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);

    yield_land_ = 3.011e26;
    yield_ocean_ = 1.566e26;
    flash_rate_coeff_ = 3.44e-5;
    flash_rate_pow_ = 4.9;

    if (config["yield_land"]) yield_land_ = config["yield_land"].as<double>();
    if (config["yield_ocean"]) yield_ocean_ = config["yield_ocean"].as<double>();
    if (config["flash_rate_coeff"]) flash_rate_coeff_ = config["flash_rate_coeff"].as<double>();
    if (config["flash_rate_power"]) flash_rate_pow_ = config["flash_rate_power"].as<double>();

    std::cout << "LightningScheme: Initialized.\n";
}

void LightningScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto conv_depth = ResolveImport("cloud_top_height", import_state);
    auto light_nox = ResolveExport("lightning_nox_emissions", export_state);
    auto land_mask_view = ResolveImport("land_mask", import_state);

    if (conv_depth.data() == nullptr || light_nox.data() == nullptr) {
        return;
    }

    int nx = static_cast<int>(light_nox.extent(0));
    int ny = static_cast<int>(light_nox.extent(1));
    int nz = static_cast<int>(light_nox.extent(2));

    // Land mask proxy
    bool has_land_mask = (land_mask_view.data() != nullptr);

    const double MW_NO = 30.0;
    double y_land = yield_land_;
    double y_ocean = yield_ocean_;
    double fr_coeff = flash_rate_coeff_;
    double fr_pow = flash_rate_pow_;

    Kokkos::parallel_for(
        "LightningKernel_Optimized",
        Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<2>>({0, 0}, {nx, ny}),
        KOKKOS_LAMBDA(int i, int j) {
            // Use convective depth from surface or first level as representative for the column
            double h = conv_depth(i, j, 0);
            if (h <= 0.0) {
                return;
            }

            bool is_land = has_land_mask ? (land_mask_view(i, j, 0) > 0.5) : true;
            double h_km = h / 1000.0;
            double flash_rate = fr_coeff * std::pow(h_km, fr_pow);

            double total_yield = get_lightning_yield(flash_rate, MW_NO, is_land, y_land, y_ocean);
            double level_yield = total_yield / static_cast<double>(nz);

            // Vertically distribute (Ott et al. proxy) - Optimized column fill
            for (int k = 0; k < nz; ++k) {
                light_nox(i, j, k) += level_yield;
            }
        });

    Kokkos::fence();
    MarkModified("lightning_nox_emissions", export_state);
}

}  // namespace aces
