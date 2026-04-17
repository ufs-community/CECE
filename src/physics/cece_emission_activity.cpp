/**
 * @file cece_emission_activity.cpp
 * @brief Vegetation emission activity factor calculator (MEGVEA) implementation.
 *
 * Implements the EmissionActivityCalculator::Initialize method which parses
 * per-class coefficients from the YAML `emission_classes` section, reads
 * stress enable flags, CO2 method/concentration, and bidirectional flags,
 * and pre-computes gamma_co2 from the configured CO2 concentration.
 *
 * The KOKKOS_INLINE_FUNCTION gamma helpers (gamma_canopy_depth,
 * gamma_wind_stress, gamma_temp_stress, gamma_aq_stress) are implemented
 * in the header file cece_emission_activity.hpp for inlining in Kokkos kernels.
 *
 * Existing gamma functions (gamma_lai, gamma_t_li, gamma_t_ld, gamma_par,
 * gamma_co2, gamma_age, gamma_sm) are defined in cece_megan.cpp and reused
 * by the Megan3Scheme kernel.
 *
 * @author CECE Team
 * @date 2024
 */

#include "cece/physics/cece_emission_activity.hpp"

#include <Kokkos_Core.hpp>
#include <iostream>
#include <string>

namespace cece {

// Gamma functions (including get_gamma_co2) are defined in cece_megan.hpp
#include "cece/physics/cece_megan.hpp"

/// Default per-class coefficient values for the 19 MEGAN3 emission classes.
/// Order matches the EmissionClass enum: ISOP, MBO, MT_PINE, MT_ACYC, MT_CAMP,
/// MT_SABI, MT_AROM, NO, SQT_HR, SQT_LR, MEOH, ACTO, ETOH, ACID, LVOC,
/// OXPROD, STRESS, OTHER, CO.
static constexpr int NUM_CLASSES = static_cast<int>(EmissionClass::COUNT);

// Default LDF values per class
static constexpr double kDefaultLdf[NUM_CLASSES] = {0.9996, 0.80, 0.10, 0.10, 0.10, 0.10, 0.10, 0.00, 0.50, 0.50,
                                                    0.80,   0.20, 0.80, 0.00, 0.00, 0.20, 1.00, 0.20, 0.00};

// Default CT1 values per class
static constexpr double kDefaultCt1[NUM_CLASSES] = {95.0, 80.0, 80.0, 80.0, 80.0, 80.0, 80.0, 80.0, 80.0, 80.0,
                                                    60.0, 80.0, 60.0, 80.0, 80.0, 80.0, 95.0, 80.0, 80.0};

// Default Cleo values per class
static constexpr double kDefaultCleo[NUM_CLASSES] = {2.0,  1.83, 1.83, 1.83, 1.83, 1.83, 1.83, 1.83, 1.83, 1.83,
                                                     1.83, 1.83, 1.83, 1.83, 1.83, 1.83, 2.0,  1.83, 1.83};

// Default beta values per class
static constexpr double kDefaultBeta[NUM_CLASSES] = {0.13, 0.13, 0.10, 0.10, 0.10, 0.10, 0.10, 0.10, 0.17, 0.17,
                                                     0.08, 0.10, 0.08, 0.10, 0.13, 0.10, 0.13, 0.10, 0.10};

// Default Anew values per class
static constexpr double kDefaultAnew[NUM_CLASSES] = {0.05, 0.05, 2.0, 2.0,  2.0,  2.0,  2.0,  0.00, 0.05, 0.05,
                                                     3.5,  0.05, 3.5, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05};

// Default Agro values per class
static constexpr double kDefaultAgro[NUM_CLASSES] = {0.6, 0.6, 1.8, 1.8, 1.8, 1.8, 1.8, 0.0, 0.6, 0.6, 3.0, 0.6, 3.0, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6};

// Default Amat values per class
static constexpr double kDefaultAmat[NUM_CLASSES] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};

// Default Aold values per class
static constexpr double kDefaultAold[NUM_CLASSES] = {0.9, 0.9, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0, 0.9, 0.9, 1.2, 0.9, 1.2, 0.9, 0.9, 0.9, 0.9, 0.9, 0.9};

void EmissionActivityCalculator::Initialize(const YAML::Node& config) {
    // ---- Allocate device views for per-class coefficients ----
    coefficients_.ldf = Kokkos::View<double[19], Kokkos::DefaultExecutionSpace>("eac_ldf");
    coefficients_.ct1 = Kokkos::View<double[19], Kokkos::DefaultExecutionSpace>("eac_ct1");
    coefficients_.cleo = Kokkos::View<double[19], Kokkos::DefaultExecutionSpace>("eac_cleo");
    coefficients_.beta = Kokkos::View<double[19], Kokkos::DefaultExecutionSpace>("eac_beta");
    coefficients_.anew = Kokkos::View<double[19], Kokkos::DefaultExecutionSpace>("eac_anew");
    coefficients_.agro = Kokkos::View<double[19], Kokkos::DefaultExecutionSpace>("eac_agro");
    coefficients_.amat = Kokkos::View<double[19], Kokkos::DefaultExecutionSpace>("eac_amat");
    coefficients_.aold = Kokkos::View<double[19], Kokkos::DefaultExecutionSpace>("eac_aold");
    bidirectional_ = Kokkos::View<bool[19], Kokkos::DefaultExecutionSpace>("eac_bidir");

    // ---- Create host mirrors ----
    auto h_ldf = Kokkos::create_mirror_view(coefficients_.ldf);
    auto h_ct1 = Kokkos::create_mirror_view(coefficients_.ct1);
    auto h_cleo = Kokkos::create_mirror_view(coefficients_.cleo);
    auto h_beta = Kokkos::create_mirror_view(coefficients_.beta);
    auto h_anew = Kokkos::create_mirror_view(coefficients_.anew);
    auto h_agro = Kokkos::create_mirror_view(coefficients_.agro);
    auto h_amat = Kokkos::create_mirror_view(coefficients_.amat);
    auto h_aold = Kokkos::create_mirror_view(coefficients_.aold);
    auto h_bidir = Kokkos::create_mirror_view(bidirectional_);

    // ---- Fill with defaults ----
    for (int i = 0; i < NUM_CLASSES; ++i) {
        h_ldf(i) = kDefaultLdf[i];
        h_ct1(i) = kDefaultCt1[i];
        h_cleo(i) = kDefaultCleo[i];
        h_beta(i) = kDefaultBeta[i];
        h_anew(i) = kDefaultAnew[i];
        h_agro(i) = kDefaultAgro[i];
        h_amat(i) = kDefaultAmat[i];
        h_aold(i) = kDefaultAold[i];
        h_bidir(i) = false;
    }

    // ---- Parse per-class coefficients from YAML emission_classes section ----
    if (config && config["emission_classes"]) {
        const auto& classes_node = config["emission_classes"];
        for (auto it = classes_node.begin(); it != classes_node.end(); ++it) {
            std::string class_name = it->first.as<std::string>();
            const auto& class_config = it->second;

            EmissionClass ec;
            if (!StringToEmissionClass(class_name, ec)) {
                std::cout << "EmissionActivityCalculator: Unknown emission class '" << class_name << "', skipping\n";
                continue;
            }

            int idx = static_cast<int>(ec);

            if (class_config["ldf"]) h_ldf(idx) = class_config["ldf"].as<double>();
            if (class_config["ct1"]) h_ct1(idx) = class_config["ct1"].as<double>();
            if (class_config["cleo"]) h_cleo(idx) = class_config["cleo"].as<double>();
            if (class_config["beta"]) h_beta(idx) = class_config["beta"].as<double>();
            if (class_config["anew"]) h_anew(idx) = class_config["anew"].as<double>();
            if (class_config["agro"]) h_agro(idx) = class_config["agro"].as<double>();
            if (class_config["amat"]) h_amat(idx) = class_config["amat"].as<double>();
            if (class_config["aold"]) h_aold(idx) = class_config["aold"].as<double>();
            if (class_config["bidirectional"]) {
                h_bidir(idx) = class_config["bidirectional"].as<bool>();
            }
        }
    } else {
        std::cout << "EmissionActivityCalculator: No emission_classes section found, "
                  << "using defaults for all 19 classes\n";
    }

    // ---- Copy to device ----
    Kokkos::deep_copy(coefficients_.ldf, h_ldf);
    Kokkos::deep_copy(coefficients_.ct1, h_ct1);
    Kokkos::deep_copy(coefficients_.cleo, h_cleo);
    Kokkos::deep_copy(coefficients_.beta, h_beta);
    Kokkos::deep_copy(coefficients_.anew, h_anew);
    Kokkos::deep_copy(coefficients_.agro, h_agro);
    Kokkos::deep_copy(coefficients_.amat, h_amat);
    Kokkos::deep_copy(coefficients_.aold, h_aold);
    Kokkos::deep_copy(bidirectional_, h_bidir);

    // ---- Read stress enable flags ----
    enable_wind_stress_ = false;
    enable_temp_stress_ = false;
    enable_aq_stress_ = false;
    if (config) {
        if (config["enable_wind_stress"]) {
            enable_wind_stress_ = config["enable_wind_stress"].as<bool>();
        }
        if (config["enable_temp_stress"]) {
            enable_temp_stress_ = config["enable_temp_stress"].as<bool>();
        }
        if (config["enable_aq_stress"]) {
            enable_aq_stress_ = config["enable_aq_stress"].as<bool>();
        }
    }

    // ---- Read CO2 method and concentration, pre-compute gamma_co2 ----
    co2_method_ = "possell";
    co2_concentration_ = 400.0;
    if (config) {
        if (config["co2_method"]) {
            co2_method_ = config["co2_method"].as<std::string>();
        }
        if (config["co2_concentration"]) {
            co2_concentration_ = config["co2_concentration"].as<double>();
        }
    }

    // Pre-compute gamma_co2 using the existing get_gamma_co2 function
    // Default Possell coefficients from MeganScheme
    double gamma_co2_coeff_1 = 8.9406;
    double gamma_co2_coeff_2 = 0.0024;
    bool use_wilkinson = (co2_method_ == "wilkinson");
    gamma_co2_ = get_gamma_co2(co2_concentration_, gamma_co2_coeff_1, gamma_co2_coeff_2, use_wilkinson);

    std::cout << "EmissionActivityCalculator: Initialized with " << NUM_CLASSES << " emission classes, "
              << "co2_method=" << co2_method_ << ", co2=" << co2_concentration_ << " ppm, gamma_co2=" << gamma_co2_
              << ", wind_stress=" << (enable_wind_stress_ ? "on" : "off") << ", temp_stress=" << (enable_temp_stress_ ? "on" : "off")
              << ", aq_stress=" << (enable_aq_stress_ ? "on" : "off") << "\n";
}

}  // namespace cece
