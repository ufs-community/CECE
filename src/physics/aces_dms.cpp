/**
 * @file aces_dms.cpp
 * @brief Dimethyl sulfide (DMS) sea-air exchange flux calculations.
 *
 * Implements oceanic DMS emission calculations based on seawater DMS
 * concentrations, wind speed, and sea-air gas transfer parameterizations.
 * DMS is the dominant natural source of sulfur to the marine atmosphere
 * and plays a critical role in marine aerosol formation and cloud processes.
 *
 * The scheme includes:
 * - Schmidt number temperature dependence for DMS solubility
 * - Wind speed-dependent gas transfer velocity (Nightingale et al., 2000)
 * - Seawater DMS concentration climatology integration
 * - Temperature and salinity corrections for gas exchange
 *
 * This implementation is based on algorithms from HEMCO's hcox_seaflux_mod.F90
 * with adaptations for ACES framework integration and Kokkos execution.
 *
 * References:
 * - Nightingale, P.D., et al. (2000), In situ evaluation of air-sea gas exchange
 *   parameterizations using novel conservative and volatile tracers, Global
 *   Biogeochem. Cycles, 14(1), 373-387.
 *
 * @author Barry Baker
 * @date 2024
 * @version 1.0
 */

#include "aces/physics/aces_dms.hpp"

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

#include "aces/aces_physics_factory.hpp"

namespace aces {

/// @brief Self-registration for the DMS sea-air flux scheme.
static PhysicsRegistration<DMSScheme> register_scheme("dms");

/**
 * @brief Initialize the DMS sea-air exchange scheme.
 *
 * Sets up parameterization coefficients for Schmidt number calculation
 * and gas transfer velocity computations. The Schmidt number represents
 * the ratio of kinematic viscosity to molecular diffusion coefficient,
 * which varies with temperature and affects gas exchange rates.
 *
 * Configuration parameters:
 * - sc_c0-sc_c3: Schmidt number polynomial coefficients vs. temperature
 * - kw_c0-kw_c1: Transfer velocity coefficients for wind speed dependence
 *
 * @param config YAML configuration node with DMS parameters
 * @param diag_manager Diagnostic manager for tracking outputs
 */
void DMSScheme::Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);

    // Default Schmidt number coefficients for DMS
    sc_c0_ = 2674.0;
    sc_c1_ = -147.12;
    sc_c2_ = 3.726;
    sc_c3_ = -0.038;

    // Default transfer velocity coefficients
    kw_c0_ = 0.222;
    kw_c1_ = 0.333;

    if (config["schmidt_coeff"]) {
        auto sc = config["schmidt_coeff"];
        if (sc.IsSequence() && sc.size() == 4) {
            sc_c0_ = sc[0].as<double>();
            sc_c1_ = sc[1].as<double>();
            sc_c2_ = sc[2].as<double>();
            sc_c3_ = sc[3].as<double>();
        }
    }

    if (config["kw_coeff"]) {
        auto kw = config["kw_coeff"];
        if (kw.IsSequence() && kw.size() == 2) {
            kw_c0_ = kw[0].as<double>();
            kw_c1_ = kw[1].as<double>();
        }
    }

    std::cout << "DMSScheme: Initialized." << "\n";
}

void DMSScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto u10m = ResolveImport("wind_speed", import_state);
    auto tskin = ResolveImport("tskin", import_state);
    auto seaconc = ResolveImport("seawater_conc", import_state);
    auto dms_emis = ResolveExport("dms_emissions", export_state);

    if (u10m.data() == nullptr || tskin.data() == nullptr || seaconc.data() == nullptr ||
        dms_emis.data() == nullptr) {
        return;
    }

    int nx = static_cast<int>(dms_emis.extent(0));
    int ny = static_cast<int>(dms_emis.extent(1));

    double sc0 = sc_c0_, sc1 = sc_c1_, sc2 = sc_c2_, sc3 = sc_c3_;
    double kw0 = kw_c0_, kw1 = kw_c1_;

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
            double sc_w = sc0 + tc * (sc1 + tc * (sc2 + tc * sc3));

            double k_w = (kw0 * w * w + kw1 * w) * std::pow(sc_w / 600.0, -0.5);  // cm/hr
            k_w /= 360000.0;                                                      // cm/hr -> m/s

            dms_emis(i, j, 0) += k_w * conc;
        });

    Kokkos::fence();
    MarkModified("dms_emissions", export_state);
}

}  // namespace aces
