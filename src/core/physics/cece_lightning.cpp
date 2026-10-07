/**
 * @file cece_lightning.cpp
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
 * - Molecular weight and unit conversions for CECE integration
 *
 * This implementation is based on algorithms from HEMCO's hcox_lightnox_mod.F90
 * with adaptations for the CECE framework and Kokkos execution.
 *
 * References:
 * - Price, C., et al. (1997), Vertical distributions of lightning NOx for use
 *   in regional and global chemical transport models, JGR, 102(D5), 5943-5941.
 *
 * @author Barry Baker
 * @date 2024
 * @version 1.0
 */

#include "cece/physics/cece_lightning.hpp"

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

#include "cece/cece_physics_factory.hpp"

namespace cece {

/// @brief Self-registration for the lightning NOx emission scheme.
static PhysicsRegistration<LightningScheme> register_scheme("lightning");

/**
 * @brief Calculate NOx production from lightning flash rate.
 *
 * Converts lightning flash rate to NOx production using empirical yield factors
 * that differ between land and ocean regions. The yields are based on
 * observational studies and represent molecules of NO produced per flash.
 *
 * Typical yields:
 * - Land: ~500 mol NO/flash
 * - Ocean: ~260 mol NO/flash
 *
 * @param rate Lightning flash rate [flashes/s/grid_cell]
 * @param mw_no Molecular weight of NO [g/mol]
 * @param is_land Flag indicating land (true) vs. ocean (false)
 * @param yield_land NOx yield factor for land regions [molecules/flash]
 * @param yield_ocean NOx yield factor for ocean regions [molecules/flash]
 * @return NOx production rate [kg/s/grid_cell]
 */

KOKKOS_INLINE_FUNCTION
double get_lightning_yield(double rate, double mw_no, bool is_land, double yield_land, double yield_ocean) {
    const double yield_molec = is_land ? yield_land : yield_ocean;
    const double AVOGADRO = 6.022e23;
    return (rate * yield_molec) * (mw_no / 1000.0) / AVOGADRO;
}

void LightningScheme::Initialize(const conf::Value& config, CeceDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);

    yield_land_ = 3.011e26;
    yield_ocean_ = 1.566e26;
    flash_rate_coeff_ = 3.44e-5;
    flash_rate_pow_ = 4.9;
    flash_rate_time_seconds_ = 60.0;

    if (config["yield_land"]) yield_land_ = config["yield_land"].as_double();
    if (config["yield_ocean"]) yield_ocean_ = config["yield_ocean"].as_double();
    if (config["flash_rate_coeff"]) flash_rate_coeff_ = config["flash_rate_coeff"].as_double();
    if (config["flash_rate_power"]) flash_rate_pow_ = config["flash_rate_power"].as_double();
    const std::string time_unit = config["flash_rate_time_unit"].string_or("minutes");
    if (time_unit == "seconds") {
        flash_rate_time_seconds_ = 1.0;
    } else if (time_unit != "minutes") {
        throw std::invalid_argument("lightning.flash_rate_time_unit must be 'minutes' or 'seconds'");
    }
    if (!std::isfinite(yield_land_) || yield_land_ < 0.0 || !std::isfinite(yield_ocean_) || yield_ocean_ < 0.0 || !std::isfinite(flash_rate_coeff_) ||
        flash_rate_coeff_ < 0.0 || !std::isfinite(flash_rate_pow_) || flash_rate_pow_ <= 0.0) {
        throw std::invalid_argument(
            "lightning yields and flash_rate_coeff must be finite and nonnegative; flash_rate_power must be finite and positive");
    }

    std::cout << "LightningScheme: Initialized.\n";
}

void LightningScheme::Run(CeceImportState& import_state, CeceExportState& export_state) {
    auto conv_depth = ResolveImport("cloud_top_height", import_state);
    auto light_nox = ResolveExport("lightning_nox_emissions", export_state);
    auto land_mask_view = ResolveImport("land_mask", import_state);
    auto cell_area = ResolveImport("cell_area", import_state);
    auto flash_rate_output = ResolveExport("lightning_flash_rate", export_state);
    auto flash_density_output = ResolveExport("lightning_flash_density", export_state);
    auto efficiency_output = ResolveExport("lightning_nox_efficiency", export_state);
    auto production_output = ResolveExport("lightning_nox_production", export_state);

    if (light_nox.extent(0) > 0 && light_nox.extent(1) == 0 && light_nox.extent(2) > 0) return;
    RequireFields("lightning",
                  {{"cloud_top_height", conv_depth.data() != nullptr},
                   {"lightning_nox_emissions", light_nox.data() != nullptr},
                   {"cell_area (required for lightning_flash_density)", flash_density_output.data() == nullptr || cell_area.data() != nullptr}});

    int nx = static_cast<int>(light_nox.extent(0));
    int ny = static_cast<int>(light_nox.extent(1));
    int nz = static_cast<int>(light_nox.extent(2));
    if (nz < 1) throw std::invalid_argument("lightning_nox_emissions must have at least one vertical level");
    const auto validate_column_shape = [nx, ny](const auto& view, const std::string& name) {
        if (view.data() != nullptr &&
            (view.extent(0) != static_cast<std::size_t>(nx) || view.extent(1) != static_cast<std::size_t>(ny) || view.extent(2) < 1)) {
            throw std::invalid_argument("lightning field '" + name + "' must match the emission grid and have at least one vertical level");
        }
    };
    validate_column_shape(conv_depth, "cloud_top_height");
    validate_column_shape(land_mask_view, "land_mask");
    validate_column_shape(cell_area, "cell_area");
    validate_column_shape(flash_rate_output, "lightning_flash_rate");
    validate_column_shape(flash_density_output, "lightning_flash_density");
    validate_column_shape(efficiency_output, "lightning_nox_efficiency");
    validate_column_shape(production_output, "lightning_nox_production");
    if (flash_density_output.data() != nullptr) {
        int invalid_areas = 0;
        Kokkos::parallel_reduce(
            "LightningValidateCellArea", Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<2>>({0, 0}, {nx, ny}),
            KOKKOS_LAMBDA(int i, int j, int& count) {
                const double area = cell_area(i, j, 0);
                if (!(area > 0.0) || area > std::numeric_limits<double>::max()) ++count;
            },
            invalid_areas);
        if (invalid_areas != 0) throw std::invalid_argument("lightning.cell_area must be finite and positive in m2");
    }

    // Land mask proxy
    bool has_land_mask = (land_mask_view.data() != nullptr);

    const double MW_NO = 30.0;
    double y_land = yield_land_;
    double y_ocean = yield_ocean_;
    double fr_coeff = flash_rate_coeff_;
    double fr_pow = flash_rate_pow_;
    double fr_time_seconds = flash_rate_time_seconds_;
    const double missing = std::numeric_limits<double>::quiet_NaN();

    Kokkos::parallel_for(
        "LightningKernel_Optimized", Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<2>>({0, 0}, {nx, ny}),
        KOKKOS_LAMBDA(int i, int j) {
            // Use convective depth from surface or first level as representative for the column
            double h = conv_depth(i, j, 0);
            bool is_land = has_land_mask ? (land_mask_view(i, j, 0) > 0.5) : true;
            double h_km = h / 1000.0;
            double flash_rate = h > 0.0 && h <= std::numeric_limits<double>::max() ? fr_coeff * std::pow(h_km, fr_pow) / fr_time_seconds : 0.0;

            double total_yield = get_lightning_yield(flash_rate, MW_NO, is_land, y_land, y_ocean);
            double level_yield = total_yield / static_cast<double>(nz);
            const double efficiency = flash_rate > 0.0 ? (total_yield / flash_rate) / (MW_NO / 1000.0) : missing;
            if (flash_rate_output.data() != nullptr) {
                for (std::size_t level = 0; level < flash_rate_output.extent(2); ++level) {
                    flash_rate_output(i, j, level) = level == 0 ? flash_rate : 0.0;
                }
            }
            if (flash_density_output.data() != nullptr) {
                const double density = flash_rate * 3600.0 * 1.0e6 / cell_area(i, j, 0);
                for (std::size_t level = 0; level < flash_density_output.extent(2); ++level) {
                    flash_density_output(i, j, level) = level == 0 ? density : 0.0;
                }
            }
            if (efficiency_output.data() != nullptr) {
                for (std::size_t level = 0; level < efficiency_output.extent(2); ++level) {
                    efficiency_output(i, j, level) = level == 0 ? efficiency : missing;
                }
            }
            if (production_output.data() != nullptr) {
                for (std::size_t level = 0; level < production_output.extent(2); ++level) {
                    production_output(i, j, level) = level == 0 ? total_yield : 0.0;
                }
            }

            // Vertically distribute (Ott et al. proxy) - Optimized column fill
            for (int k = 0; k < nz; ++k) {
                light_nox(i, j, k) = level_yield;
            }
        });

    Kokkos::fence();
    MarkModified("lightning_nox_emissions", export_state);
    MarkModified("lightning_flash_rate", export_state);
    MarkModified("lightning_flash_density", export_state);
    MarkModified("lightning_nox_efficiency", export_state);
    MarkModified("lightning_nox_production", export_state);
}

}  // namespace cece
