/**
 * @file cece_canopy_model.cpp
 * @brief Multi-layer canopy radiation and energy balance model (MEGCANOPY).
 *
 * Implements the CanopyModel::Initialize method which sets up 5-point
 * Gaussian-Legendre quadrature weights and points on [0,1], copies them
 * to device memory, and reads the configurable extinction coefficient.
 *
 * The KOKKOS_INLINE_FUNCTION helpers (compute_canopy_par,
 * compute_leaf_temperature, integrate_canopy_emission) are implemented
 * in the header file cece_canopy_model.hpp for inlining in Kokkos kernels.
 *
 * @author CECE Team
 * @date 2024
 */

#include "cece/physics/cece_canopy_model.hpp"

#include <Kokkos_Core.hpp>
#include <iostream>

namespace cece {

void CanopyModel::Initialize(const YAML::Node& config) {
    // 5-point Gaussian-Legendre quadrature points on [0,1]
    // These represent fractional canopy depth from top (0) to bottom (1)
    constexpr double points[NUM_LAYERS] = {0.04691008, 0.23076534, 0.5, 0.76923466, 0.95308992};

    // 5-point Gaussian-Legendre quadrature weights on [0,1]
    // These sum to 1.0 for proper integration over the unit interval
    constexpr double weights[NUM_LAYERS] = {0.11846345, 0.23931434, 0.28444444, 0.23931434, 0.11846345};

    // Allocate device views
    gauss_weights_ = Kokkos::View<double[5], Kokkos::DefaultExecutionSpace>("gauss_weights");
    gauss_points_ = Kokkos::View<double[5], Kokkos::DefaultExecutionSpace>("gauss_points");

    // Create host mirrors and copy data
    auto h_weights = Kokkos::create_mirror_view(gauss_weights_);
    auto h_points = Kokkos::create_mirror_view(gauss_points_);

    for (int i = 0; i < NUM_LAYERS; ++i) {
        h_weights(i) = weights[i];
        h_points(i) = points[i];
    }

    Kokkos::deep_copy(gauss_weights_, h_weights);
    Kokkos::deep_copy(gauss_points_, h_points);

    // Read configurable extinction coefficient (Beer-Lambert k)
    extinction_coeff_ = 0.5;  // Default
    if (config && config["extinction_coefficient"]) {
        extinction_coeff_ = config["extinction_coefficient"].as<double>();
    }

    std::cout << "CanopyModel: Initialized with " << NUM_LAYERS << " Gaussian layers, extinction_coeff=" << extinction_coeff_ << "\n";
}

}  // namespace cece
