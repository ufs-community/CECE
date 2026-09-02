/**
 * @file cece_bdsnp.cpp
 * @brief Standalone BDSNP soil NO physics module implementation.
 *
 * Implements the Berkeley-Dalhousie Soil NOx Parameterization (BDSNP) with
 * Yienger & Levy (1995) fallback alongside the legacy SoilNoxScheme
 * ("soil_nox") registration.
 *
 * Two algorithms are supported, selectable via `soil_no_method` YAML key:
 *   - "yl95": Yienger & Levy (1995) temperature and moisture response
 *   - "bdsnp" (default): validated effective-input BDSNP arithmetic with
 *     24-biome weighting, temperature/moisture responses, canopy reduction,
 *     pulse scaling, fertilizer, and deposited nitrogen
 *
 * The canonical mode accepts either direct soil temperature or surface air
 * temperature with its biome/moisture conversion. YL95 uses soil temperature.
 * Both write "soil_nox_emissions" for consumption by MEGAN3.
 *
 * References:
 * - Yienger, J.J. and H. Levy II (1995), JGR, 100(D6), 11447-11464.
 * - Hudman et al. (2012), BDSNP parameterization.
 *
 * @author CECE Team
 * @date 2024
 */

#include "cece/physics/cece_bdsnp.hpp"

#include <Kokkos_Array.hpp>
#include <Kokkos_Core.hpp>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <iostream>
#include <stdexcept>

#include "cece/cece_physics_factory.hpp"

namespace cece {

/// @brief Self-registration for the BDSNP soil NO emission scheme.
static PhysicsRegistration<BdsnpScheme> reg("bdsnp");

// ============================================================================
// YL95 inline helper functions (matching existing SoilNoxScheme exactly)
// ============================================================================

/**
 * @brief Calculate temperature-dependent soil NOx emission factor (YL95).
 *
 * Computes the exponential temperature response of soil microbial activity.
 * Returns 0 when soil temperature is at or below freezing.
 *
 * @param tc Soil temperature [°C]
 * @param tc_max Maximum temperature for emission calculation [°C]
 * @param exp_coeff Exponential temperature coefficient [1/°C]
 * @return Temperature-dependent emission factor (dimensionless)
 */
KOKKOS_INLINE_FUNCTION
double bdsnp_soil_temp_term(double tc, double tc_max, double exp_coeff) {
    if (tc <= 0.0) {
        return 0.0;  // No emission below freezing
    }
    return std::exp(exp_coeff * std::min(tc_max, tc));
}

/**
 * @brief Calculate soil moisture-dependent NOx emission factor (YL95).
 *
 * Computes the water-filled pore space (WFPS) effect on soil NOx emissions
 * using a Poisson-like response function.
 *
 * @param gw Water-filled pore space fraction [0-1]
 * @param wet_c1 Moisture response coefficient 1
 * @param wet_c2 Moisture response coefficient 2 [1/WFPS²]
 * @return Moisture-dependent emission factor (dimensionless)
 */
KOKKOS_INLINE_FUNCTION
double bdsnp_soil_wet_term(double gw, double wet_c1, double wet_c2) {
    return wet_c1 * gw * std::exp(wet_c2 * gw * gw);
}

// ============================================================================
// Initialize
// ============================================================================

void BdsnpScheme::Initialize(const conf::Value& config, CeceDiagnosticManager* diag_manager) {
    // Call base class to parse input_mapping, output_mapping, diagnostics
    BasePhysicsScheme::Initialize(config, diag_manager);

    // Parse the external string once; runtime dispatch uses a typed selector.
    const std::string soil_no_method = config["soil_no_method"].string_or("bdsnp");
    if (soil_no_method == "bdsnp") {
        soil_no_method_ = SoilNoMethod::kBdsnp;
    } else if (soil_no_method == "yl95") {
        soil_no_method_ = SoilNoMethod::kYl95;
    } else {
        throw std::invalid_argument("BdsnpScheme: unknown soil_no_method '" + soil_no_method + "'; expected 'bdsnp' or 'yl95'");
    }

    for (const char* removed_option : {"fert_emission_factor", "wet_dep_scaling", "dry_dep_scaling", "pulse_decay_constant"}) {
        if (config[removed_option]) {
            throw std::invalid_argument(std::string("BdsnpScheme: removed simplified-BDSNP option '") + removed_option +
                                        "' is not valid for canonical bdsnp");
        }
    }

    use_soil_temperature_ = false;
    if (config["use_soil_temperature"]) {
        use_soil_temperature_ = config["use_soil_temperature"].as_bool();
    }

    // These tunable coefficients belong only to the YL95 fallback. Canonical
    // BDSNP coefficients remain fixed in RunBdsnp; its variability comes from
    // required input fields.
    const std::initializer_list<const char*> yl95_only_options = {"biome_coefficient_wet", "a_biome_wet", "temp_limit",  "tc_max",
                                                                  "temp_exp_coeff",        "exp_coeff",   "wet_coeff_1", "wet_c1",
                                                                  "wet_coeff_2",           "wet_c2"};
    if (soil_no_method_ == SoilNoMethod::kBdsnp) {
        for (const char* option : yl95_only_options) {
            if (config[option]) {
                throw std::invalid_argument(std::string("BdsnpScheme: YL95-only option '") + option +
                                            "' is not valid when soil_no_method is 'bdsnp'");
            }
        }
    } else {
        yl95_parameters_ = {};

        if (config["biome_coefficient_wet"]) {
            yl95_parameters_.biome_coefficient_wet = config["biome_coefficient_wet"].as_double();
        } else if (config["a_biome_wet"]) {
            yl95_parameters_.biome_coefficient_wet = config["a_biome_wet"].as_double();
        } else {
            std::cout << "BdsnpScheme: Using default a_biome_wet = " << yl95_parameters_.biome_coefficient_wet << "\n";
        }

        if (config["temp_limit"]) {
            yl95_parameters_.temperature_limit = config["temp_limit"].as_double();
        } else if (config["tc_max"]) {
            yl95_parameters_.temperature_limit = config["tc_max"].as_double();
        } else {
            std::cout << "BdsnpScheme: Using default tc_max = " << yl95_parameters_.temperature_limit << "\n";
        }

        if (config["temp_exp_coeff"]) {
            yl95_parameters_.temperature_exponential_coefficient = config["temp_exp_coeff"].as_double();
        } else if (config["exp_coeff"]) {
            yl95_parameters_.temperature_exponential_coefficient = config["exp_coeff"].as_double();
        } else {
            std::cout << "BdsnpScheme: Using default exp_coeff = " << yl95_parameters_.temperature_exponential_coefficient << "\n";
        }

        if (config["wet_coeff_1"]) {
            yl95_parameters_.wetness_coefficient_1 = config["wet_coeff_1"].as_double();
        } else if (config["wet_c1"]) {
            yl95_parameters_.wetness_coefficient_1 = config["wet_c1"].as_double();
        } else {
            std::cout << "BdsnpScheme: Using default wet_c1 = " << yl95_parameters_.wetness_coefficient_1 << "\n";
        }

        if (config["wet_coeff_2"]) {
            yl95_parameters_.wetness_coefficient_2 = config["wet_coeff_2"].as_double();
        } else if (config["wet_c2"]) {
            yl95_parameters_.wetness_coefficient_2 = config["wet_c2"].as_double();
        } else {
            std::cout << "BdsnpScheme: Using default wet_c2 = " << yl95_parameters_.wetness_coefficient_2 << "\n";
        }
    }

    std::cout << "BdsnpScheme: Initialized with soil_no_method='" << soil_no_method << "'\n";
}

// ============================================================================
// Run
// ============================================================================

void BdsnpScheme::Run(CeceImportState& import_state, CeceExportState& export_state) {
    switch (soil_no_method_) {
        case SoilNoMethod::kBdsnp:
            RunBdsnp(import_state, export_state);
            return;
        case SoilNoMethod::kYl95:
            RunYl95(import_state, export_state);
            return;
    }

    throw std::logic_error("BdsnpScheme: invalid internal soil NO method");
}

void BdsnpScheme::RunBdsnp(CeceImportState& import_state, CeceExportState& export_state) {
    // Validated stateless BDSNP cell arithmetic. Stateful quantities and
    // upstream canopy/deposited-N calculations are explicit inputs.
    const std::string temperature_name = use_soil_temperature_ ? "soil_temperature" : "surface_temperature";
    auto temperature = ResolveImport(temperature_name, import_state);
    auto soil_moisture = ResolveImport("soil_moisture", import_state);
    auto land_fractions = ResolveImport("soilnox_land_fractions", import_state);
    auto arid_fraction = ResolveImport("soilnox_arid_fraction", import_state);
    auto nonarid_fraction = ResolveImport("soilnox_nonarid_fraction", import_state);
    auto lai = ResolveImport("leaf_area_index", import_state);
    auto canopy_nox = ResolveImport("soilnox_canopy_nox", import_state);
    auto wind_speed_squared = ResolveImport("wind_speed_squared", import_state);
    auto solar_zenith_cosine = ResolveImport("solar_zenith_cosine", import_state);
    auto soil_fertilizer = ResolveImport("soil_fertilizer", import_state);
    auto deposited_nitrogen = ResolveImport("deposited_nitrogen", import_state);
    auto pulse_factor = ResolveImport("soilnox_pulse_factor", import_state);
    auto soil_nox = ResolveExport("soil_nox_emissions", export_state);
    auto fertilizer_nox = ResolveExport("soil_nox_fertilizer_emissions", export_state);

    RequireFields("BdsnpScheme bdsnp mode", {{temperature_name, temperature.data() != nullptr},
                                             {"soil_moisture", soil_moisture.data() != nullptr},
                                             {"soilnox_land_fractions", land_fractions.data() != nullptr},
                                             {"soilnox_arid_fraction", arid_fraction.data() != nullptr},
                                             {"soilnox_nonarid_fraction", nonarid_fraction.data() != nullptr},
                                             {"leaf_area_index", lai.data() != nullptr},
                                             {"soilnox_canopy_nox", canopy_nox.data() != nullptr},
                                             {"wind_speed_squared", wind_speed_squared.data() != nullptr},
                                             {"solar_zenith_cosine", solar_zenith_cosine.data() != nullptr},
                                             {"soil_fertilizer", soil_fertilizer.data() != nullptr},
                                             {"deposited_nitrogen", deposited_nitrogen.data() != nullptr},
                                             {"soilnox_pulse_factor", pulse_factor.data() != nullptr},
                                             {"soil_nox_emissions", soil_nox.data() != nullptr},
                                             {"soil_nox_fertilizer_emissions", fertilizer_nox.data() != nullptr}});

    const int nx = static_cast<int>(soil_nox.extent(0));
    const int ny = static_cast<int>(soil_nox.extent(1));
    auto require_scalar_shape = [nx, ny](const auto& view, const char* name) {
        if (view.extent(0) != nx || view.extent(1) != ny || view.extent(2) != 1) {
            throw std::runtime_error(std::string("BdsnpScheme bdsnp field has incompatible shape: ") + name);
        }
    };
    require_scalar_shape(soil_nox, "soil_nox_emissions");
    require_scalar_shape(temperature, temperature_name.c_str());
    require_scalar_shape(soil_moisture, "soil_moisture");
    require_scalar_shape(arid_fraction, "soilnox_arid_fraction");
    require_scalar_shape(nonarid_fraction, "soilnox_nonarid_fraction");
    require_scalar_shape(lai, "leaf_area_index");
    require_scalar_shape(wind_speed_squared, "wind_speed_squared");
    require_scalar_shape(solar_zenith_cosine, "solar_zenith_cosine");
    require_scalar_shape(soil_fertilizer, "soil_fertilizer");
    require_scalar_shape(deposited_nitrogen, "deposited_nitrogen");
    require_scalar_shape(pulse_factor, "soilnox_pulse_factor");
    require_scalar_shape(fertilizer_nox, "soil_nox_fertilizer_emissions");
    if (land_fractions.extent(0) != nx || land_fractions.extent(1) != ny || land_fractions.extent(2) != 24 || canopy_nox.extent(0) != nx ||
        canopy_nox.extent(1) != ny || canopy_nox.extent(2) != 24) {
        throw std::runtime_error("BdsnpScheme bdsnp requires exactly 24 land-fraction and canopy-NOx layers");
    }

    const Kokkos::Array<double, 24> a_biome = {0.00, 0.00, 0.00, 0.00, 0.00, 0.06, 0.09, 0.09, 0.01, 0.84, 0.84, 0.24,
                                               0.42, 0.62, 0.03, 0.36, 0.36, 0.35, 1.66, 0.08, 0.44, 0.57, 0.57, 0.57};
    const Kokkos::Array<double, 24> soil_ta = {0.00, 0.92, 0.00, 0.66, 0.66, 0.66, 0.66, 0.66, 0.66, 0.66, 0.66, 0.66,
                                               0.66, 0.66, 0.84, 0.84, 0.84, 0.84, 0.84, 0.84, 0.84, 1.03, 1.03, 1.03};
    const Kokkos::Array<double, 24> soil_tb = {0.00, 4.40, 0.00, 8.80, 8.80, 8.80, 8.80, 8.80, 8.80, 8.80, 8.80, 8.80,
                                               8.80, 8.80, 3.60, 3.60, 3.60, 3.60, 3.60, 3.60, 3.60, 2.90, 2.90, 2.90};
    const Kokkos::Array<double, 24> soil_exc = {0.10, 0.50, 0.10, 0.10, 0.10, 0.10, 0.10, 0.10, 0.10, 1.00, 1.00, 1.00,
                                                1.00, 2.00, 4.00, 4.00, 4.00, 4.00, 4.00, 4.00, 4.00, 2.00, 0.10, 2.00};
    const bool use_soil_temperature = use_soil_temperature_;
    constexpr double unit_conversion = 1.0e-12 / 14.0 * 30.0;
    constexpr double fertilizer_scale = 0.0068;
    constexpr double seconds_per_year = 3.1536e7;
    constexpr double soil_exp_coefficient = static_cast<double>(0.103F);
    constexpr double cubic_3 = static_cast<double>(-0.009F);
    constexpr double cubic_2 = static_cast<double>(0.837F);
    constexpr double cubic_1 = static_cast<double>(-22.527F);
    constexpr double cubic_0 = static_cast<double>(196.149F);

    Kokkos::parallel_for(
        "BdsnpKernel_BDSNP", Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<2>>({0, 0}, {nx, ny}), KOKKOS_LAMBDA(int i, int j) {
            soil_nox(i, j, 0) = 0.0;
            fertilizer_nox(i, j, 0) = 0.0;

            // Match HEMCO's exact no-soil gate before updating any state.
            if (land_fractions(i, j, 0) == 1.0) {
                return;
            }

            const double temperature_c = temperature(i, j, 0) - 273.15;
            const double gwet = soil_moisture(i, j, 0);
            const double arid = arid_fraction(i, j, 0);
            const double nonarid = nonarid_fraction(i, j, 0);
            const double wetness =
                (arid >= nonarid && arid > 0.0) ? 8.24 * gwet * std::exp(-12.5 * gwet * gwet) : 5.5 * gwet * std::exp(-5.55 * gwet * gwet);
            const double fertilizer = (soil_fertilizer(i, j, 0) + deposited_nitrogen(i, j, 0)) / seconds_per_year * fertilizer_scale;
            const double leaf_area_index = lai(i, j, 0);
            const double wind_squared = wind_speed_squared(i, j, 0);
            const double sun_cosine = solar_zenith_cosine(i, j, 0);
            const double pulse = pulse_factor(i, j, 0);

            double total = 0.0;
            double added_n_total = 0.0;
            for (int k = 0; k < 24; ++k) {
                double adjusted_temperature = temperature_c;
                if (!use_soil_temperature) {
                    adjusted_temperature = gwet < 0.3 ? adjusted_temperature + 5.0 : soil_ta[k] * adjusted_temperature + soil_tb[k];
                }

                double temperature_term = 0.0;
                if (adjusted_temperature > 0.0) {
                    if (!use_soil_temperature) {
                        adjusted_temperature = adjusted_temperature >= 30.0 ? 30.0 : adjusted_temperature;
                        temperature_term = std::exp(0.103 * adjusted_temperature);
                    } else {
                        adjusted_temperature = adjusted_temperature >= 40.0 ? 40.0 : adjusted_temperature;
                        temperature_term = adjusted_temperature <= 20.0
                                               ? std::exp(soil_exp_coefficient * adjusted_temperature)
                                               : cubic_3 * adjusted_temperature * adjusted_temperature * adjusted_temperature +
                                                     cubic_2 * adjusted_temperature * adjusted_temperature + cubic_1 * adjusted_temperature + cubic_0;
                    }
                }

                double canopy_reduction = 0.0;
                const double canopy = canopy_nox(i, j, k);
                if (leaf_area_index > 0.0 && canopy > 0.0) {
                    double ventilation = sun_cosine > 0.0 ? 1.0e-2 : 0.2e-2;
                    ventilation *= std::sqrt(wind_squared / 9.0 * 7.0 / leaf_area_index) * (soil_exc[20] / soil_exc[k]);
                    canopy_reduction = canopy / (canopy + ventilation);
                }

                const double common = temperature_term * wetness * pulse * land_fractions(i, j, k) * (1.0 - canopy_reduction);
                total += (a_biome[k] * unit_conversion + fertilizer) * common;
                added_n_total += fertilizer * common;
            }

            soil_nox(i, j, 0) = total > 0.0 ? total : 0.0;
            fertilizer_nox(i, j, 0) = added_n_total;
        });

    Kokkos::fence();
    MarkModified("soil_nox_emissions", export_state);
    MarkModified("soil_nox_fertilizer_emissions", export_state);
}

void BdsnpScheme::RunYl95(CeceImportState& import_state, CeceExportState& export_state) {
    // ---- Resolve import fields ----
    auto soil_temp = ResolveImport("soil_temperature", import_state);
    auto soil_moisture = ResolveImport("soil_moisture", import_state);
    auto soil_nox = ResolveExport("soil_nox_emissions", export_state);

    // Fail loudly: a silent return creates a valid-looking all-zero output.
    RequireFields("BdsnpScheme yl95 mode", {{"soil_temperature", soil_temp.data() != nullptr},
                                            {"soil_moisture", soil_moisture.data() != nullptr},
                                            {"soil_nox_emissions", soil_nox.data() != nullptr}});

    int nx = static_cast<int>(soil_nox.extent(0));
    int ny = static_cast<int>(soil_nox.extent(1));

    // YL95 mode: identical algorithm to the existing SoilNoxScheme.
    const double MW_NO = 30.0;
    const double UNITCONV = 1.0e-12 / 14.0 * MW_NO;  // ng N -> kg NO
    double a_biome_wet = yl95_parameters_.biome_coefficient_wet;
    double tc_max = yl95_parameters_.temperature_limit;
    double exp_coeff = yl95_parameters_.temperature_exponential_coefficient;
    double wet_c1 = yl95_parameters_.wetness_coefficient_1;
    double wet_c2 = yl95_parameters_.wetness_coefficient_2;

    Kokkos::parallel_for(
        "BdsnpKernel_YL95", Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<2>>({0, 0}, {nx, ny}), KOKKOS_LAMBDA(int i, int j) {
            double tc = soil_temp(i, j, 0) - 273.15;
            double gw = soil_moisture(i, j, 0);

            if (tc <= 0.0) {
                soil_nox(i, j, 0) = 0.0;
                return;
            }

            double t_term = bdsnp_soil_temp_term(tc, tc_max, exp_coeff);
            double w_term = bdsnp_soil_wet_term(gw, wet_c1, wet_c2);
            soil_nox(i, j, 0) = a_biome_wet * UNITCONV * t_term * w_term;
        });

    Kokkos::fence();
    MarkModified("soil_nox_emissions", export_state);
}

}  // namespace cece
