/**
 * @file cece_k14.cpp
 * @brief Native C++ implementation of the K14 (Kok et al., 2014) dust emission scheme.
 *
 * Implements the K14 dust emission algorithm using Kokkos for GPU-portable
 * parallel execution. Includes helper functions for Kok vertical dust flux,
 * MacKinnon drag partition, smooth roughness lookup, and Laurent erodibility.
 *
 * References:
 * - Kok, J.F., et al. (2012), An improved dust emission model, ACP, 12, 7413–7430.
 * - Kok, J.F., et al. (2014), An improved dust emission model — Part 2, ACP, 14, 13023–13041.
 * - MacKinnon, D.J., et al. (2004), A geomorphological approach, JGR, 109, F01013.
 * - Laurent, B., et al. (2008), Modeling mineral dust emissions, ACP, 8, 395–409.
 * - Fécan, F., et al. (1999), Annales Geophysicae, 17, 149–157.
 *
 * @author CECE Development Team
 * @date 2025
 * @version 1.0
 */

#include "cece/physics/cece_k14.hpp"

#include <Kokkos_Core.hpp>
#include <cmath>
#include <iostream>
#include <vector>

#include "cece/cece_physics_factory.hpp"

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

namespace cece {

/// @brief Self-registration for the K14 dust emission scheme.
static PhysicsRegistration<K14Scheme> register_k14("k14");

}  // namespace cece

/// @brief Compute Kok (2012, 2014) vertical dust mass flux.
/// @param u Soil friction velocity [m/s]
/// @param u_t Threshold friction velocity [m/s]
/// @param rho_air Air density [kg/m³]
/// @param f_erod Erodibility [1]
/// @param k_gamma Clay/silt modulation term [1]
/// @return Vertical dust mass flux [kg/(m²·s)]
KOKKOS_INLINE_FUNCTION
double k14_vertical_dust_flux(double u, double u_t, double rho_air, double f_erod, double k_gamma) {
    constexpr double rho_a0 = 1.225;
    constexpr double u_st0 = 0.16;
    constexpr double C_d0 = 4.4e-5;
    constexpr double C_e = 2.0;
    constexpr double C_a = 2.7;
    double u_st = Kokkos::max(u_t * Kokkos::sqrt(rho_air / rho_a0), u_st0);
    double f_ust = (u_st - u_st0) / u_st0;
    double C_d = C_d0 * Kokkos::exp(-C_e * f_ust);
    return C_d * f_erod * k_gamma * rho_air * ((u * u - u_t * u_t) / u_st) * Kokkos::pow(u / u_t, C_a * f_ust);
}

/// @brief Compute MacKinnon (2004) drag partition correction.
/// @param z0 Aeolian roughness length [m]
/// @param z0s Smooth roughness length [m]
/// @return Drag partition correction factor [1]
KOKKOS_INLINE_FUNCTION
double k14_drag_partition(double z0, double z0s) {
    constexpr double z0_max = 5.0e-4;
    if (z0 <= z0s || z0 >= z0_max) return 1.0;
    return 1.0 - Kokkos::log(z0 / z0s) / Kokkos::log(0.7 * Kokkos::pow(122.55 / z0s, 0.8));
}

/// @brief Look up smooth roughness length from soil texture.
/// @param texture Soil texture index (1-12 valid)
/// @return Smooth roughness length z0s [m]
KOKKOS_INLINE_FUNCTION
double k14_smooth_roughness(int texture) {
    constexpr double Dc_soil[12] = {710e-6, 710e-6, 125e-6, 125e-6, 125e-6, 160e-6, 710e-6, 125e-6, 125e-6, 160e-6, 125e-6, 2e-6};
    if (texture >= 1 && texture <= 12) {
        return Dc_soil[texture - 1] / 30.0;
    }
    return 125e-6 / 30.0;
}

/// @brief Compute Laurent (2008) erodibility parameterization.
/// @param z0 Aeolian roughness length [m]
/// @param texture Soil texture index
/// @return Erodibility factor [1]
KOKKOS_INLINE_FUNCTION
double k14_erodibility(double z0, int texture) {
    constexpr double z0_max = 5.0e-4;
    if (texture == 15) return 0.0;
    if (z0 > 3.0e-5 && z0 < z0_max) {
        return 0.7304 - 0.0804 * Kokkos::log10(100.0 * z0);
    }
    return 1.0;
}

namespace cece {

void K14Scheme::Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);

    if (config["ch_du"]) ch_du_ = config["ch_du"].as<double>();
    if (config["f_w"]) f_w_ = config["f_w"].as<double>();
    if (config["f_c"]) f_c_ = config["f_c"].as<double>();
    if (config["uts_gamma"]) uts_gamma_ = config["uts_gamma"].as<double>();
    if (config["undef"]) undef_ = config["undef"].as<double>();
    if (config["grav"]) grav_ = config["grav"].as<double>();
    if (config["von_karman"]) von_karman_ = config["von_karman"].as<double>();
    if (config["opt_clay"]) opt_clay_ = config["opt_clay"].as<int>();
    if (config["num_bins"]) num_bins_ = config["num_bins"].as<int>();

    // Compute Kok distribution from particle size parameters if provided
    if (config["particle_radii"] && config["bin_lower_edges"] && config["bin_upper_edges"]) {
        auto radii = config["particle_radii"].as<std::vector<double>>();
        auto lower = config["bin_lower_edges"].as<std::vector<double>>();
        auto upper = config["bin_upper_edges"].as<std::vector<double>>();
        num_bins_ = static_cast<int>(radii.size());

        auto dist = compute_kok_distribution(radii, lower, upper);

        bin_distribution_ = Kokkos::View<double*, Kokkos::DefaultExecutionSpace>("k14_bin_dist", num_bins_);
        auto h_dist = Kokkos::create_mirror_view(bin_distribution_);
        for (int n = 0; n < num_bins_; ++n) {
            h_dist(n) = dist[n];
        }
        Kokkos::deep_copy(bin_distribution_, h_dist);
        has_custom_distribution_ = true;
    } else {
        // Fallback: uniform distribution for backward compatibility
        bin_distribution_ = Kokkos::View<double*, Kokkos::DefaultExecutionSpace>("k14_bin_dist", num_bins_);
        auto h_dist = Kokkos::create_mirror_view(bin_distribution_);
        double uniform_weight = 1.0 / num_bins_;
        for (int n = 0; n < num_bins_; ++n) {
            h_dist(n) = uniform_weight;
        }
        Kokkos::deep_copy(bin_distribution_, h_dist);
        has_custom_distribution_ = false;
    }

    std::cout << "K14Scheme: Initialized. ch_du=" << ch_du_ << " opt_clay=" << opt_clay_ << " num_bins=" << num_bins_ << "\n";
}

void K14Scheme::Run(CeceImportState& import_state, CeceExportState& export_state) {
    // Resolve 17 import fields
    auto ustar = ResolveImport("friction_velocity", import_state);
    auto t_soil = ResolveImport("soil_temperature", import_state);
    auto w_top = ResolveImport("volumetric_soil_moisture", import_state);
    auto rho_air = ResolveImport("air_density", import_state);
    auto z0 = ResolveImport("roughness_length", import_state);
    auto z = ResolveImport("height", import_state);
    auto u_z = ResolveImport("u_wind", import_state);
    auto v_z = ResolveImport("v_wind", import_state);
    auto f_land = ResolveImport("land_fraction", import_state);
    auto f_snow = ResolveImport("snow_fraction", import_state);
    auto f_src = ResolveImport("dust_source", import_state);
    auto f_sand = ResolveImport("sand_fraction", import_state);
    auto f_silt = ResolveImport("silt_fraction", import_state);
    auto f_clay = ResolveImport("clay_fraction", import_state);
    auto texture = ResolveImport("soil_texture", import_state);
    auto vegetation = ResolveImport("vegetation_type", import_state);
    auto gvf = ResolveImport("vegetation_fraction", import_state);

    // Resolve export field
    auto emissions = ResolveExport("k14_dust_emissions", export_state);

    // Early return if any field is missing
    if (ustar.data() == nullptr || t_soil.data() == nullptr || w_top.data() == nullptr || rho_air.data() == nullptr || z0.data() == nullptr ||
        z.data() == nullptr || u_z.data() == nullptr || v_z.data() == nullptr || f_land.data() == nullptr || f_snow.data() == nullptr ||
        f_src.data() == nullptr || f_sand.data() == nullptr || f_silt.data() == nullptr || f_clay.data() == nullptr || texture.data() == nullptr ||
        vegetation.data() == nullptr || gvf.data() == nullptr || emissions.data() == nullptr) {
        return;
    }

    int nx = static_cast<int>(emissions.extent(0));
    int ny = static_cast<int>(emissions.extent(1));
    int nbins = static_cast<int>(emissions.extent(2));

    // Capture config parameters for the lambda
    double ch_du = ch_du_;
    double f_w_param = f_w_;
    double f_c_param = f_c_;
    double uts_gamma = uts_gamma_;
    double undef = undef_;
    double grav = grav_;
    int opt_clay = opt_clay_;

    // Capture the pre-computed bin distribution view
    auto bin_dist = bin_distribution_;

    // Physical constants
    constexpr double a_n = 0.0123;
    constexpr double Dp_size = 75e-6;
    constexpr double rho_p = 2.65e3;
    constexpr double rho_water = 1000.0;
    constexpr double rho_soil = 2500.0;
    constexpr double z0_max = 5.0e-4;

    Kokkos::parallel_for(
        "K14Kernel", Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<2>>({0, 0}, {nx, ny}), KOKKOS_LAMBDA(int i, int j) {
            double f_land_val = f_land(i, j, 0);
            double z0_val = z0(i, j, 0);

            // Initialize emissions to zero for all bins
            for (int n = 0; n < nbins; ++n) {
                emissions(i, j, n) = 0.0;
            }

            // Skip cells with no land or invalid roughness
            if (f_land_val <= 0.0) return;
            if (z0_val >= z0_max || z0_val <= 0.0) return;

            double rho_air_val = rho_air(i, j, 0);

            // ---------------------------------------------------------------
            // Step 1: Threshold friction velocity over smooth surface (Shao)
            // ---------------------------------------------------------------
            double u_ts = Kokkos::sqrt(a_n * ((rho_p / rho_air_val) * grav * Dp_size + uts_gamma / (rho_air_val * Dp_size)));

            // ---------------------------------------------------------------
            // Step 2: Gravimetric soil moisture (Zender formulation)
            // ---------------------------------------------------------------
            double f_sand_val = f_sand(i, j, 0);
            double w_top_val = w_top(i, j, 0);
            double vsat = 0.489 - 0.126 * f_sand_val;
            double w_g = 100.0 * f_w_param * rho_water / rho_soil / (1.0 - vsat) * w_top_val;

            // ---------------------------------------------------------------
            // Step 3: Fécan soil moisture correction
            // ---------------------------------------------------------------
            double f_clay_val = f_clay(i, j, 0);
            double f_silt_val = f_silt(i, j, 0);
            double clay_val = undef;
            double silt_val = undef;
            double w_gt = undef;
            double H_w = 1.0;

            if (f_clay_val >= 0.0 && f_clay_val <= 1.0) {
                clay_val = f_c_param * f_clay_val;
                silt_val = f_silt_val + (1.0 - f_c_param) * f_clay_val;
                w_gt = 14.0 * clay_val * clay_val + 17.0 * clay_val;
            }

            if (w_g > w_gt) {
                H_w = Kokkos::sqrt(1.0 + 1.21 * Kokkos::pow(w_g - w_gt, 0.68));
            }

            // ---------------------------------------------------------------
            // Step 4: Clay parameterization (opt_clay)
            // ---------------------------------------------------------------
            double k_gamma = 0.0;
            if (opt_clay == 1) {
                // Ito and Kok, 2017 variant 1
                k_gamma = 0.05;
                if (clay_val >= 0.05 && clay_val < 0.2) {
                    k_gamma = clay_val;
                } else if (clay_val >= 0.2 && clay_val <= 1.0) {
                    k_gamma = 0.2;
                }
            } else if (opt_clay == 2) {
                // Ito and Kok, 2017 variant 2
                k_gamma = 1.0 / 1.4;
                if (clay_val >= 0.0 && clay_val < 0.2) {
                    k_gamma = 1.0 / (1.4 - clay_val - silt_val);
                } else if (clay_val >= 0.2 && clay_val <= 1.0) {
                    k_gamma = 1.0 / (1.0 + clay_val - silt_val);
                }
            } else {
                // Default: Kok et al., 2014
                if (clay_val >= 0.0 && clay_val <= 1.0) {
                    k_gamma = clay_val;
                }
            }

            // ---------------------------------------------------------------
            // Step 5: Smooth roughness length from soil texture
            // ---------------------------------------------------------------
            int texture_val = static_cast<int>(Kokkos::round(texture(i, j, 0)));
            double z0s = k14_smooth_roughness(texture_val);

            // ---------------------------------------------------------------
            // Step 6: MacKinnon drag partition
            // ---------------------------------------------------------------
            double R = k14_drag_partition(z0_val, z0s);

            // ---------------------------------------------------------------
            // Step 7: Soil friction velocity
            // ---------------------------------------------------------------
            double u_val = R * ustar(i, j, 0);

            // ---------------------------------------------------------------
            // Step 8: Soil threshold friction velocity
            // ---------------------------------------------------------------
            double u_t = u_ts * H_w;

            // ---------------------------------------------------------------
            // Step 9: Laurent erodibility
            // ---------------------------------------------------------------
            double f_erod = k14_erodibility(z0_val, texture_val);

            // ---------------------------------------------------------------
            // Step 10: IGBP vegetation mask
            // ---------------------------------------------------------------
            double veg_val = vegetation(i, j, 0);
            double f_veg = 0.0;
            if (Kokkos::abs(veg_val - 7.0) < 0.1) f_veg = 1.0;   // open shrublands
            if (Kokkos::abs(veg_val - 16.0) < 0.1) f_veg = 1.0;  // barren

            double gvf_val = gvf(i, j, 0);
            if (gvf_val >= 0.0 && gvf_val < 0.8) {
                f_veg = f_veg * (1.0 - gvf_val);
            }

            // ---------------------------------------------------------------
            // Step 11: Final erodibility
            // ---------------------------------------------------------------
            double f_snow_val = f_snow(i, j, 0);
            f_erod = f_erod * f_veg * f_land_val * (1.0 - f_snow_val);

            double f_src_val = f_src(i, j, 0);
            if (f_src_val >= 0.0) {
                f_erod = f_src_val * f_erod;
            }

            // ---------------------------------------------------------------
            // Step 12: Kok vertical dust flux
            // ---------------------------------------------------------------
            double flux = 0.0;
            if (f_erod > 0.0 && u_val > u_t) {
                flux = k14_vertical_dust_flux(u_val, u_t, rho_air_val, f_erod, k_gamma);
            }

            // ---------------------------------------------------------------
            // Step 13: Scale and distribute across bins
            // ---------------------------------------------------------------
            for (int n = 0; n < nbins; ++n) {
                emissions(i, j, n) = flux * ch_du * bin_dist(n);
            }
        });

    Kokkos::fence();
    MarkModified("k14_dust_emissions", export_state);
}

}  // namespace cece
