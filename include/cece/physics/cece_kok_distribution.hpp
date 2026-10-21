#pragma once
#ifndef CECE_KOK_DISTRIBUTION_HPP
#define CECE_KOK_DISTRIBUTION_HPP

#include <cmath>
#include <vector>

namespace {

/// @brief Compute Kok (2011) normalized dust aerosol size distribution.
/// @param radii Particle bin effective radii [m]
/// @param lower_edges Bin lower edge radii [m]
/// @param upper_edges Bin upper edge radii [m]
/// @return Normalized distribution weights summing to 1.0
inline std::vector<double> compute_kok_distribution(const std::vector<double>& radii, const std::vector<double>& lower_edges,
                                                    const std::vector<double>& upper_edges) {
    constexpr double mmd = 3.4;      // median mass diameter [μm]
    constexpr double stddev = 3.0;   // geometric standard deviation
    constexpr double lambda = 12.0;  // crack propagation length [μm]
    const double factor = 1.0 / (std::sqrt(2.0) * std::log(stddev));

    int nbins = static_cast<int>(radii.size());
    std::vector<double> dist(nbins, 0.0);
    double total = 0.0;

    for (int n = 0; n < nbins; ++n) {
        double diameter = 2.0 * radii[n] * 1e6;  // convert m → μm
        double rLow = lower_edges[n] * 1e6;
        double rUp = upper_edges[n] * 1e6;
        double dlam = diameter / lambda;
        dist[n] = diameter * (1.0 + std::erf(factor * std::log(diameter / mmd))) * std::exp(-dlam * dlam * dlam) * std::log(rUp / rLow);
        total += dist[n];
    }

    if (total > 0.0) {
        for (int n = 0; n < nbins; ++n) dist[n] /= total;
    }
    return dist;
}

}  // anonymous namespace

#endif
