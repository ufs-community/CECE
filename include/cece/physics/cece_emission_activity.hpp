#ifndef CECE_EMISSION_ACTIVITY_HPP
#define CECE_EMISSION_ACTIVITY_HPP

/**
 * @file cece_emission_activity.hpp
 * @brief Vegetation emission activity factor calculator (MEGVEA).
 *
 * Computes all gamma factors for each of the 19 MEGAN3 emission classes.
 * Reuses the existing KOKKOS_INLINE_FUNCTION gamma functions from
 * cece_megan.cpp (gamma_lai, gamma_t_li, gamma_t_ld, gamma_par, gamma_co2,
 * gamma_age, gamma_sm) and adds new ones for canopy depth, wind stress,
 * temperature stress, and air quality stress.
 *
 * Per-class coefficients are stored in a struct-of-arrays layout on device
 * for coalesced access in Kokkos kernels.
 */

#include <yaml-cpp/yaml.h>

#include <Kokkos_Core.hpp>
#include <string>

#include "cece/physics/cece_speciation_config.hpp"

namespace cece {

/**
 * @struct EmissionClassCoefficients
 * @brief Per-class coefficient set stored in a struct-of-arrays layout on device.
 *
 * Each Kokkos::View holds one coefficient value for each of the 19 MEGAN3
 * emission classes, indexed by the EmissionClass enum.
 */
struct EmissionClassCoefficients {
    Kokkos::View<double[19], Kokkos::DefaultExecutionSpace> ldf;   ///< Light dependence fraction [0-1]
    Kokkos::View<double[19], Kokkos::DefaultExecutionSpace> ct1;   ///< Temperature coefficient 1
    Kokkos::View<double[19], Kokkos::DefaultExecutionSpace> cleo;  ///< Emission coefficient (Cleo)
    Kokkos::View<double[19], Kokkos::DefaultExecutionSpace> beta;  ///< Temperature response coefficient
    Kokkos::View<double[19], Kokkos::DefaultExecutionSpace> anew;  ///< Relative emission factor (new leaves)
    Kokkos::View<double[19], Kokkos::DefaultExecutionSpace> agro;  ///< Relative emission factor (growing leaves)
    Kokkos::View<double[19], Kokkos::DefaultExecutionSpace> amat;  ///< Relative emission factor (mature leaves)
    Kokkos::View<double[19], Kokkos::DefaultExecutionSpace> aold;  ///< Relative emission factor (old leaves)
};

/**
 * @class EmissionActivityCalculator
 * @brief Computes all gamma factors for each of the 19 emission classes.
 *
 * Manages per-class coefficients and configuration for the MEGVEA algorithm.
 * The actual gamma computations are performed by KOKKOS_INLINE_FUNCTION
 * helpers (defined in cece_megan.cpp and cece_canopy_model.hpp) that are
 * called from within the Megan3Scheme Kokkos kernel.
 *
 * Usage:
 *   1. Call Initialize() with YAML config to parse per-class coefficients,
 *      stress flags, CO2 parameters, and bidirectional flags
 *   2. Access coefficients_ and configuration members from within the
 *      Megan3Scheme kernel to compute combined gamma factors per class
 */
// ============================================================================
// KOKKOS_INLINE_FUNCTION helpers for new gamma factors
// (gamma_canopy_depth, gamma_wind_stress, gamma_temp_stress, gamma_aq_stress)
//
// These must be in the header so they can be inlined in Kokkos kernels.
// The existing gamma functions (gamma_lai, gamma_t_li, gamma_t_ld, gamma_par,
// gamma_co2, gamma_age, gamma_sm) are defined in cece_megan.cpp.
// ============================================================================

/**
 * @brief Canopy depth gamma factor.
 *
 * Accounts for within-canopy light extinction effects on emission profiles.
 * Uses an exponential decay based on canopy depth (cumulative LAI fraction).
 *
 * @param lai Leaf area index [m²/m²]
 * @param canopy_depth_fraction Fractional depth into canopy [0-1]
 * @param extinction_coeff Beer-Lambert extinction coefficient
 * @return Canopy depth correction factor (dimensionless, non-negative)
 */
KOKKOS_INLINE_FUNCTION
double get_gamma_canopy_depth(double lai, double canopy_depth_fraction, double extinction_coeff) {
    if (lai <= 0.0) {
        return 1.0;
    }
    // Exponential decay of emission potential with canopy depth
    double cum_lai = lai * canopy_depth_fraction;
    return std::exp(-extinction_coeff * cum_lai);
}

/**
 * @brief Wind stress gamma factor.
 *
 * Computes a stress-induced emission enhancement when wind speed exceeds
 * a threshold. Based on MEGAN3 stress emission parameterization.
 *
 * @param wind_speed Wind speed at reference height [m/s]
 * @param wind_threshold Threshold wind speed for stress onset [m/s] (default 10.0)
 * @param wind_scale Scaling factor for stress response (default 0.05)
 * @return Wind stress correction factor (dimensionless, >= 1.0)
 */
KOKKOS_INLINE_FUNCTION
double get_gamma_wind_stress(double wind_speed, double wind_threshold = 10.0, double wind_scale = 0.05) {
    if (wind_speed <= wind_threshold) {
        return 1.0;
    }
    // Linear increase above threshold
    return 1.0 + wind_scale * (wind_speed - wind_threshold);
}

/**
 * @brief Temperature stress gamma factor.
 *
 * Computes a stress-induced emission enhancement when temperature exceeds
 * a high threshold or falls below a low threshold.
 *
 * @param temp Air temperature [K]
 * @param temp_high_threshold High temperature stress threshold [K] (default 313.0)
 * @param temp_low_threshold Low temperature stress threshold [K] (default 263.0)
 * @param temp_scale Scaling factor for stress response (default 0.1)
 * @return Temperature stress correction factor (dimensionless, >= 1.0)
 */
KOKKOS_INLINE_FUNCTION
double get_gamma_temp_stress(double temp, double temp_high_threshold = 313.0, double temp_low_threshold = 263.0, double temp_scale = 0.1) {
    if (temp >= temp_high_threshold) {
        return 1.0 + temp_scale * (temp - temp_high_threshold);
    }
    if (temp <= temp_low_threshold) {
        return 1.0 + temp_scale * (temp_low_threshold - temp);
    }
    return 1.0;
}

/**
 * @brief Air quality stress gamma factor.
 *
 * Computes a stress-induced emission enhancement based on ozone or other
 * air quality indicators. Uses a simple linear response above a threshold.
 *
 * @param ozone_ppb Ozone concentration [ppb]
 * @param ozone_threshold Threshold ozone for stress onset [ppb] (default 80.0)
 * @param ozone_scale Scaling factor for stress response (default 0.01)
 * @return Air quality stress correction factor (dimensionless, >= 1.0)
 */
KOKKOS_INLINE_FUNCTION
double get_gamma_aq_stress(double ozone_ppb, double ozone_threshold = 80.0, double ozone_scale = 0.01) {
    if (ozone_ppb <= ozone_threshold) {
        return 1.0;
    }
    return 1.0 + ozone_scale * (ozone_ppb - ozone_threshold);
}

class EmissionActivityCalculator {
   public:
    /**
     * @brief Initialize from YAML configuration.
     *
     * Parses per-class coefficients (LDF, CT1, Cleo, beta, Anew, Agro, Amat,
     * Aold) from the `emission_classes` section, reads stress enable flags,
     * CO2 method/concentration, and bidirectional flags. Pre-computes
     * gamma_co2 from the configured CO2 concentration and method.
     *
     * @param config YAML node containing MEGAN3 scheme options.
     */
    void Initialize(const YAML::Node& config);

    /// Per-class coefficients stored on device.
    EmissionClassCoefficients coefficients_;

    // ---- Stress factor enable flags ----

    bool enable_wind_stress_ = false;  ///< Enable wind stress gamma factor
    bool enable_temp_stress_ = false;  ///< Enable temperature stress gamma factor
    bool enable_aq_stress_ = false;    ///< Enable air quality stress gamma factor

    // ---- CO2 parameterization ----

    std::string co2_method_ = "possell";  ///< CO2 method: "possell" or "wilkinson"
    double co2_concentration_ = 400.0;    ///< Ambient CO2 concentration [ppm]
    double gamma_co2_ = 1.0;              ///< Pre-computed CO2 gamma factor

    // ---- Bidirectional exchange flags per class ----

    /// Per-class bidirectional exchange flag stored on device.
    Kokkos::View<bool[19], Kokkos::DefaultExecutionSpace> bidirectional_;
};

}  // namespace cece

#endif  // CECE_EMISSION_ACTIVITY_HPP
