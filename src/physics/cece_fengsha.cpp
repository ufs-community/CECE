/**
 * @file cece_fengsha.cpp
 * @brief Native C++ implementation of the FENGSHA dust emission scheme.
 *
 * Implements the FENGSHA dust emission algorithm using Kokkos for GPU-portable
 * parallel execution. Includes helper functions for soil moisture conversion,
 * Fécan moisture correction, and MB95 vertical-to-horizontal flux ratio.
 *
 * References:
 * - Fécan, F., et al. (1999), Annales Geophysicae, 17, 149–157.
 * - Marticorena, B. and Bergametti, G. (1995), JGR, 100(D8), 16415–16430.
 * - Webb, N.P., et al. (2020), Current Opinion in Environmental Sustainability, 44, 138–146.
 *
 * @author CECE Development Team
 * @date 2025
 * @version 1.0
 */

#include "cece/physics/cece_fengsha.hpp"

#include <Kokkos_Core.hpp>
#include <cmath>
#include <iostream>
#include <vector>

#include "cece/cece_logger.hpp"
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

/// @brief Self-registration for the FENGSHA dust emission scheme.
static PhysicsRegistration<FengshaScheme> register_fengsha("fengsha");

/// @brief Convert volumetric to gravimetric soil moisture.
/// @param vsoil Volumetric soil moisture fraction [1]
/// @param sandfrac Fractional sand content [1]
/// @return Gravimetric soil moisture [percent]
KOKKOS_INLINE_FUNCTION
double fengsha_soil_moisture_vol2grav(double vsoil, double sandfrac) {
    constexpr double rhow = 1000.0;
    constexpr double rhop = 1700.0;
    double vsat = 0.489 - 0.126 * sandfrac;
    return 100.0 * vsoil * rhow / (rhop * (1.0 - vsat));
}

/// @brief Compute Fécan soil moisture correction factor.
/// @param slc Liquid water content, volumetric fraction [1]
/// @param sand Fractional sand content [1]
/// @param clay Fractional clay content [1]
/// @param b Drylimit factor [1]
/// @return Soil moisture correction factor [1]
KOKKOS_INLINE_FUNCTION
double fengsha_moisture_correction_fecan(double slc, double sand, double clay, double b) {
    double grvsoilm = fengsha_soil_moisture_vol2grav(slc, sand);
    double drylimit = b * clay * (14.0 * clay + 17.0);
    double excess = Kokkos::max(0.0, grvsoilm - drylimit);
    return Kokkos::sqrt(1.0 + 1.21 * Kokkos::pow(excess, 0.68));
}

/// @brief Compute vertical-to-horizontal dust flux ratio (MB95).
/// @param clay Fractional clay content [1]
/// @param kvhmax Maximum flux ratio [1]
/// @return Vertical-to-horizontal dust flux ratio [1]
KOKKOS_INLINE_FUNCTION
double fengsha_flux_v2h_ratio_mb95(double clay, double kvhmax) {
    constexpr double clay_thresh = 0.2;
    if (clay > clay_thresh) {
        return kvhmax;
    }
    return Kokkos::pow(10.0, 13.4 * clay - 6.0);
}

void FengshaScheme::Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);

    if (config["alpha"]) alpha_ = config["alpha"].as<double>();
    if (config["gamma"]) gamma_ = config["gamma"].as<double>();
    if (config["kvhmax"]) kvhmax_ = config["kvhmax"].as<double>();
    if (config["grav"]) grav_ = config["grav"].as<double>();
    if (config["drylimit_factor"]) drylimit_factor_ = config["drylimit_factor"].as<double>();
    if (config["frozen_soil_threshold"]) frozen_soil_threshold_ = config["frozen_soil_threshold"].as<double>();
    if (config["num_bins"]) num_bins_ = config["num_bins"].as<int>();

    // Compute Kok distribution from particle size parameters if provided
    if (config["particle_radii"] && config["bin_lower_edges"] && config["bin_upper_edges"]) {
        auto radii = config["particle_radii"].as<std::vector<double>>();
        auto lower = config["bin_lower_edges"].as<std::vector<double>>();
        auto upper = config["bin_upper_edges"].as<std::vector<double>>();
        num_bins_ = static_cast<int>(radii.size());

        auto dist = compute_kok_distribution(radii, lower, upper);

        bin_distribution_ = Kokkos::View<double*, Kokkos::DefaultExecutionSpace>("fengsha_bin_dist", num_bins_);
        auto h_dist = Kokkos::create_mirror_view(bin_distribution_);
        for (int n = 0; n < num_bins_; ++n) {
            h_dist(n) = dist[n];
        }
        Kokkos::deep_copy(bin_distribution_, h_dist);
        has_custom_distribution_ = true;
    } else {
        // Fallback: hardcoded 5-bin Kok distribution for backward compatibility
        constexpr double default_dist[5] = {0.1, 0.25, 0.25, 0.25, 0.15};
        int dist_size = num_bins_ < 5 ? num_bins_ : 5;
        bin_distribution_ = Kokkos::View<double*, Kokkos::DefaultExecutionSpace>("fengsha_bin_dist", dist_size);
        auto h_dist = Kokkos::create_mirror_view(bin_distribution_);
        for (int n = 0; n < dist_size; ++n) {
            h_dist(n) = default_dist[n];
        }
        Kokkos::deep_copy(bin_distribution_, h_dist);
        has_custom_distribution_ = false;
    }

    std::cout << "FengshaScheme: Initialized. alpha=" << alpha_ << " gamma=" << gamma_ << " kvhmax=" << kvhmax_ << "\n";
}

void FengshaScheme::Run(CeceImportState& import_state, CeceExportState& export_state) {
    // Resolve import fields
    auto ustar = ResolveImport("friction_velocity", import_state);
    auto uthrs = ResolveImport("threshold_velocity", import_state);
    auto slc = ResolveImport("soil_moisture", import_state);
    auto clay = ResolveImport("clay_fraction", import_state);
    auto sand = ResolveImport("sand_fraction", import_state);
    auto silt = ResolveImport("silt_fraction", import_state);
    auto ssm = ResolveImport("erodibility", import_state);
    auto rdrag = ResolveImport("drag_partition", import_state);
    auto airdens = ResolveImport("air_density", import_state);
    auto fraclake = ResolveImport("lake_fraction", import_state);
    auto fracsnow = ResolveImport("snow_fraction", import_state);
    auto oro = ResolveImport("land_mask", import_state);
    auto t_soil = ResolveImport("soil_temperature", import_state);

    // Resolve export field
    auto emissions = ResolveExport("fengsha_dust_emissions", export_state);

    // Early return if any field is missing
    if (ustar.data() == nullptr || uthrs.data() == nullptr || slc.data() == nullptr || clay.data() == nullptr || sand.data() == nullptr ||
        silt.data() == nullptr || ssm.data() == nullptr || rdrag.data() == nullptr || airdens.data() == nullptr || fraclake.data() == nullptr ||
        fracsnow.data() == nullptr || oro.data() == nullptr || emissions.data() == nullptr) {
        CECE_LOG_WARNING("FengshaScheme::Run: One or more required fields could not be resolved. Skipping execution.");
        return;
    }

    int nx = static_cast<int>(emissions.extent(0));
    int ny = static_cast<int>(emissions.extent(1));
    int nbins = static_cast<int>(emissions.extent(2));

    // Capture config parameters for the lambda
    double alpha = alpha_;
    double gamma_param = gamma_;
    double kvhmax = kvhmax_;
    double grav = grav_;
    double drylimit_factor = drylimit_factor_;
    double frozen_soil_threshold = frozen_soil_threshold_;
    bool has_soil_temp = (t_soil.data() != nullptr);

    // Capture the pre-computed Kok distribution view
    auto bin_dist = bin_distribution_;
    int dist_size = static_cast<int>(bin_dist.extent(0));
    bool has_custom = has_custom_distribution_;

    constexpr double LAND = 1.0;
    constexpr double SSM_THRESH = 1.0e-2;

    Kokkos::parallel_for(
        "FengshaKernel", Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<2>>({0, 0}, {nx, ny}), KOKKOS_LAMBDA(int i, int j) {
            // Skip if not on land
            double oro_val = oro(i, j, 0);
            if (Kokkos::round(oro_val) != LAND) return;

            double ssm_val = ssm(i, j, 0);
            if (ssm_val < SSM_THRESH) return;

            // Skip frozen ground
            if (has_soil_temp && t_soil(i, j, 0) < frozen_soil_threshold) return;

            double clay_val = clay(i, j, 0);
            double sand_val = sand(i, j, 0);
            double rdrag_val = rdrag(i, j, 0);
            if (clay_val < 0.0 || sand_val < 0.0 || rdrag_val < 0.0) return;

            double fracland =
                Kokkos::max(0.0, Kokkos::min(1.0, 1.0 - fraclake(i, j, 0))) * Kokkos::max(0.0, Kokkos::min(1.0, 1.0 - fracsnow(i, j, 0)));

            // Vertical-to-horizontal mass flux ratio
            double kvh = fengsha_flux_v2h_ratio_mb95(clay_val, kvhmax);

            // Total emissions scaling
            double alpha_grav = alpha / grav;
            double total_emissions = alpha_grav * fracland * Kokkos::pow(ssm_val, gamma_param) * airdens(i, j, 0) * kvh;

            // Drag-partition-adjusted friction velocity
            double rustar = rdrag_val * ustar(i, j, 0);

            // Fécan moisture correction
            double smois = slc(i, j, 0);
            double h = fengsha_moisture_correction_fecan(smois, sand_val, clay_val, drylimit_factor);

            // Adjusted threshold
            double u_thresh = uthrs(i, j, 0) * h;
            double u_sum = rustar + u_thresh;

            // Horizontal saltation flux (Webb et al. 2020, Eq. 9)
            double q = Kokkos::max(0.0, rustar - u_thresh) * u_sum * u_sum;

            // Distribute to bins using pre-computed Kok distribution
            int bins_to_use = has_custom ? nbins : (nbins < dist_size ? nbins : dist_size);
            for (int n = 0; n < bins_to_use; ++n) {
                emissions(i, j, n) += bin_dist(n) * total_emissions * q;
            }
        });

    Kokkos::fence();
    MarkModified("fengsha_dust_emissions", export_state);
}

}  // namespace cece
