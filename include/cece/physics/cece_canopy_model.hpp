#ifndef CECE_CANOPY_MODEL_HPP
#define CECE_CANOPY_MODEL_HPP

/**
 * @file cece_canopy_model.hpp
 * @brief Multi-layer canopy radiation and energy balance model (MEGCANOPY).
 *
 * Implements the MEGCANOPY algorithm from CMAQ MEGAN3 with 5 Gaussian
 * integration layers. Computes canopy-integrated PAR and leaf temperature
 * profiles for use in biogenic emission activity factor calculations.
 *
 * The canopy integration is implemented as KOKKOS_INLINE_FUNCTION helpers
 * callable from within the main MEGAN3 Kokkos kernel, following the same
 * pattern as the existing gamma functions in cece_megan.cpp.
 */

#include <yaml-cpp/yaml.h>

#include <Kokkos_Core.hpp>

namespace cece {

/**
 * @struct CanopyLayerResult
 * @brief Per-layer canopy radiation and temperature results.
 *
 * Stores the PAR partitioning (sunlit/shaded), sunlit fraction, and
 * leaf temperatures for a single canopy layer.
 */
struct CanopyLayerResult {
    double par_sunlit;        ///< PAR absorbed by sunlit leaves [W/m²]
    double par_shaded;        ///< PAR absorbed by shaded leaves [W/m²]
    double frac_sunlit;       ///< Fraction of leaves that are sunlit [0-1]
    double leaf_temp_sunlit;  ///< Leaf temperature of sunlit leaves [K]
    double leaf_temp_shaded;  ///< Leaf temperature of shaded leaves [K]
};

// Forward declarations of KOKKOS_INLINE_FUNCTION helpers are below
// as full implementations (required for inlining in Kokkos kernels).

// ============================================================================
// Inline function implementations
// ============================================================================

KOKKOS_INLINE_FUNCTION
double compute_canopy_par(double par_direct, double par_diffuse, double lai, double suncos, double extinction_coeff, int layer_idx,
                          const double* gauss_weights, const double* gauss_points) {
    // Canopy depth at this Gaussian quadrature point (fraction of total LAI)
    double depth = gauss_points[layer_idx];

    // Cumulative LAI from canopy top to this layer
    double cum_lai = lai * depth;

    // Beer-Lambert extinction for diffuse PAR (isotropic)
    double par_diff_layer = par_diffuse * std::exp(-extinction_coeff * cum_lai);

    // Beer-Lambert extinction for direct PAR (depends on solar angle)
    // Use extinction_coeff / suncos for the direct beam path through canopy
    double kb = (suncos > 0.01) ? extinction_coeff / suncos : extinction_coeff / 0.01;
    double par_dir_layer = par_direct * std::exp(-kb * cum_lai);

    return par_dir_layer + par_diff_layer;
}

KOKKOS_INLINE_FUNCTION
double compute_leaf_temperature(double air_temp, double par_absorbed, double wind_speed, double lai, int layer_idx, const double* gauss_points) {
    // Energy balance: T_leaf = T_air + delta_T
    // delta_T depends on absorbed radiation and wind-driven convective cooling

    // Ensure minimum wind speed to avoid division by zero
    double ws = (wind_speed > 0.1) ? wind_speed : 0.1;

    // Canopy depth fraction at this layer
    double depth = gauss_points[layer_idx];

    // Wind speed decreases exponentially within canopy
    double wind_attenuation = std::exp(-0.5 * lai * depth);
    double ws_layer = ws * wind_attenuation;

    // Leaf boundary layer conductance for heat (mol/m²/s)
    // Simplified: g_bh ~ 0.135 * sqrt(wind_speed / leaf_width)
    // Using typical leaf width of 0.05 m
    double g_bh = 0.135 * std::sqrt(ws_layer / 0.05);

    // Sensible heat coefficient (W/m²/K per unit conductance)
    // cp_air * rho_air / (r_boundary) simplified
    double sensible_coeff = 29.3 * g_bh;  // ~29.3 J/mol/K * conductance

    // delta_T = absorbed_radiation / (2 * sensible_heat_coefficient)
    // Factor of 2 for two sides of leaf
    double delta_t = 0.0;
    if (sensible_coeff > 0.0) {
        delta_t = par_absorbed / (2.0 * sensible_coeff);
    }

    // Clamp delta_T to reasonable range [-10, +10] K
    delta_t = std::min(std::max(delta_t, -10.0), 10.0);

    return air_temp + delta_t;
}

KOKKOS_INLINE_FUNCTION
double integrate_canopy_emission(double par_direct, double par_diffuse, double lai, double suncos, double air_temp, double wind_speed,
                                 double extinction_coeff, const double* gauss_weights, const double* gauss_points, double ct1, double ceo,
                                 double pt_15, double gas_constant, double ct2, double t_opt_c1, double t_opt_c2, double e_opt_coeff) {
    constexpr int NUM_LAYERS = 5;

    // Nighttime: all light-dependent factors are zero
    if (suncos <= 0.0) {
        return 0.0;
    }

    double canopy_activity = 0.0;

    for (int layer = 0; layer < NUM_LAYERS; ++layer) {
        // Compute PAR at this canopy layer
        double par_layer = compute_canopy_par(par_direct, par_diffuse, lai, suncos, extinction_coeff, layer, gauss_weights, gauss_points);

        // Compute sunlit fraction at this layer: f_sun = exp(-k * LAI * depth / suncos)
        double depth = gauss_points[layer];
        double cum_lai = lai * depth;
        double kb = extinction_coeff / ((suncos > 0.01) ? suncos : 0.01);
        double frac_sunlit = std::exp(-kb * cum_lai);
        double frac_shaded = 1.0 - frac_sunlit;

        // PAR for sunlit and shaded leaves
        // Sunlit leaves receive direct beam + scattered diffuse
        // Shaded leaves receive only scattered diffuse
        double par_sunlit = par_layer;
        double par_shaded = par_diffuse * std::exp(-extinction_coeff * cum_lai);

        // Compute leaf temperatures via energy balance
        double t_leaf_sunlit = compute_leaf_temperature(air_temp, par_sunlit, wind_speed, lai, layer, gauss_points);
        double t_leaf_shaded = compute_leaf_temperature(air_temp, par_shaded, wind_speed, lai, layer, gauss_points);

        // Compute temperature-dependent gamma at each leaf temperature
        // Using the Guenther et al. (2012) light-dependent temperature response
        double e_opt_sun = ceo * std::exp(e_opt_coeff * (pt_15 - 297.0));
        double t_opt_sun = t_opt_c1 + t_opt_c2 * (pt_15 - 297.0);
        double x_sun = (1.0 / t_opt_sun - 1.0 / t_leaf_sunlit) / gas_constant;
        double gamma_t_sun = e_opt_sun * ct2 * std::exp(ct1 * x_sun) / (ct2 - ct1 * (1.0 - std::exp(ct2 * x_sun)));
        gamma_t_sun = std::max(gamma_t_sun, 0.0);

        double x_shade = (1.0 / t_opt_sun - 1.0 / t_leaf_shaded) / gas_constant;
        double gamma_t_shade = e_opt_sun * ct2 * std::exp(ct1 * x_shade) / (ct2 - ct1 * (1.0 - std::exp(ct2 * x_shade)));
        gamma_t_shade = std::max(gamma_t_shade, 0.0);

        // Weighted contribution from this layer
        // Sunlit contribution: fraction_sunlit * PAR_sunlit * gamma_T_sunlit
        // Shaded contribution: fraction_shaded * PAR_shaded * gamma_T_shaded
        double layer_activity = frac_sunlit * par_sunlit * gamma_t_sun + frac_shaded * par_shaded * gamma_t_shade;

        canopy_activity += gauss_weights[layer] * layer_activity;
    }

    return canopy_activity;
}

/**
 * @class CanopyModel
 * @brief Multi-layer canopy model with 5 Gaussian integration layers.
 *
 * Implements the MEGCANOPY algorithm for computing canopy-integrated
 * radiation profiles and leaf temperatures. The model divides the canopy
 * into 5 layers using Gaussian quadrature for numerical integration.
 *
 * Usage:
 *   1. Call Initialize() with YAML config to set up quadrature and parameters
 *   2. Use the KOKKOS_INLINE_FUNCTION helpers (compute_canopy_par,
 *      compute_leaf_temperature, integrate_canopy_emission) from within
 *      Kokkos kernels, passing the device-side gauss_weights_ and
 *      gauss_points_ data pointers
 */
class CanopyModel {
   public:
    /**
     * @brief Initialize the canopy model from YAML configuration.
     *
     * Sets up 5-point Gaussian quadrature weights and points, copies them
     * to device memory, and reads the configurable extinction coefficient.
     *
     * @param config YAML node containing canopy model options.
     */
    void Initialize(const YAML::Node& config);

    /// Number of Gaussian quadrature layers for canopy integration.
    static constexpr int NUM_LAYERS = 5;

    /// Gaussian quadrature weights stored on device.
    Kokkos::View<double[5], Kokkos::DefaultExecutionSpace> gauss_weights_;

    /// Gaussian quadrature points stored on device.
    Kokkos::View<double[5], Kokkos::DefaultExecutionSpace> gauss_points_;

   private:
    double extinction_coeff_ = 0.5;  ///< Beer-Lambert extinction coefficient

   public:
    /// Get the extinction coefficient for use in Kokkos kernels.
    double extinction_coeff() const {
        return extinction_coeff_;
    }
};

}  // namespace cece

#endif  // CECE_CANOPY_MODEL_HPP
