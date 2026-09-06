#ifndef CECE_HEMCO_MEGAN_STATELESS_HPP
#define CECE_HEMCO_MEGAN_STATELESS_HPP

/**
 * @file hemco_megan_stateless.hpp
 * @brief Host-scalar HEMCO 3.12.1 MEGAN isoprene equations for reference parity.
 *
 * This layer contains no I/O, regridding, or persistent averaging state.
 * All state-dependent quantities (15-day temperature average, 24-hour PAR
 * average, day-of-year) are supplied as arguments so the output can be
 * compared against a frozen HEMCO global reference run.
 *
 * Science source: HEMCO 3.12.1, hcox_megan_mod.F90
 * Repository:     https://github.com/geoschem/HEMCO
 * Release tag:    3.12.1
 * Commit:         to be pinned by downstream oracle generator
 *
 * Provenance contract
 * -------------------
 * Every constant in this file is frozen at the value found in the pinned
 * HEMCO 3.12.1 source.  The non-obvious differences from the CECE `megan`
 * scheme defaults are:
 *   - PTOA coefficients: 2650+130·cos vs CECE default 3000+99·cos
 *   - DOY phase offset:  18 (HEMCO) vs 10 (CECE default)
 *   - PAR_AVG is supplied directly in µmol m⁻² s⁻¹; no W m⁻² conversion
 *     is applied (HEMCO maintains a 24-hr running average already in those
 *     units, while the CECE native path converts from W m⁻²).
 *
 * Scope and limits
 * ----------------
 * The stateless mode fixes the 15-day temperature average to 297 K and the
 * 24-hr PAR average to 400 µmol m⁻² s⁻¹ when no state is available.  Those
 * values are the HEMCO climatological defaults used in cold-start offline runs.
 * This implementation does not reproduce:
 *   - State-evolving 15-day T or 24-hr PAR averages.
 *   - Non-isoprene species LDF/CT1/CEO values.
 *   - Coupled upstream photosynthesis or stomatal conductance.
 */

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <stdexcept>

namespace cece::hemco_megan::v3_12_1 {

// ============================================================================
// Frozen HEMCO 3.12.1 scalar constants for isoprene
// ============================================================================

/** Emission normalisation factor (= 1 / 1.0101081). */
inline constexpr double kNormFac = 1.0 / 1.0101081;

/** Light-dependent fraction for isoprene (HEMCO 3.12.1, ISOP class). */
inline constexpr double kLdf = 0.9996;

/** Exponential temperature sensitivity (β) for the light-independent pathway. */
inline constexpr double kBeta = 0.13;

/** Standard temperature [K] at which light-independent emission = AEF. */
inline constexpr double kTStd = 303.0;

/** Gas constant [kJ mol⁻¹ K⁻¹]. */
inline constexpr double kR = 8.3144598e-3;

/** Activation energy CT1 [kJ mol⁻¹] for light-dependent temperature response. */
inline constexpr double kCT1 = 95.0;

/** Empirical scaling CEO for light-dependent temperature response. */
inline constexpr double kCEO = 2.0;

/** De-activation energy CT2 [kJ mol⁻¹]. */
inline constexpr double kCT2 = 200.0;

/** T_opt intercept [K]. */
inline constexpr double kTOptC1 = 313.0;

/** T_opt slope [K K⁻¹]. */
inline constexpr double kTOptC2 = 0.6;

/** Empirical coefficient for e_opt dependence on T_avg_15. */
inline constexpr double kEOptCoeff = 0.08;

/** W m⁻² to µmol m⁻² s⁻¹ conversion factor for PAR. */
inline constexpr double kWm2ToUmol = 4.766;

/** PTOA baseline [µmol m⁻² s⁻¹] (HEMCO 3.12.1; differs from CECE default 3000). */
inline constexpr double kPtoaC1 = 2650.0;

/** PTOA amplitude [µmol m⁻² s⁻¹] (HEMCO 3.12.1; differs from CECE default 99). */
inline constexpr double kPtoaC2 = 130.0;

/** Day-of-year offset in PTOA cosine (HEMCO 3.12.1; differs from CECE default 10). */
inline constexpr double kPtoaDoyOffset = 18.0;

/** Climatological 24-hr PAR average [µmol m⁻² s⁻¹] used when state is unavailable. */
inline constexpr double kParAvgUmol = 400.0;

/** Climatological 15-day temperature average [K] used when state is unavailable. */
inline constexpr double kTAvg15 = 297.0;

/** Reference day-of-year used when no clock is available (Northern Hemisphere mid-summer). */
inline constexpr int kReferenceDoy = 180;

/** CO₂ inhibition coefficient c₁ (Possell et al. 2005). */
inline constexpr double kGammaC02C1 = 8.9406;

/** CO₂ inhibition coefficient c₂ (Possell et al. 2005) [ppm⁻¹]. */
inline constexpr double kGammaCO2C2 = 0.0024;

/** LAI gamma shape coefficient c₁. */
inline constexpr double kLaiC1 = 0.49;

/** LAI gamma shape coefficient c₂. */
inline constexpr double kLaiC2 = 0.2;

/** PCEEA PAR gamma coefficient g₁. */
inline constexpr double kGpC1 = 1.0;
/** PCEEA PAR gamma coefficient g₂. */
inline constexpr double kGpC2 = 0.0005;
/** PCEEA PAR gamma coefficient g₃. */
inline constexpr double kGpC3 = 2.46;
/** PCEEA PAR gamma coefficient g₄. */
inline constexpr double kGpC4 = 0.9;

/** Leaf-age weight for new leaves (isoprene). */
inline constexpr double kANew = 0.05;
/** Leaf-age weight for growing leaves (isoprene). */
inline constexpr double kAGro = 0.60;
/** Leaf-age weight for mature leaves (isoprene). */
inline constexpr double kAMat = 1.00;
/** Leaf-age weight for old/senescent leaves (isoprene). */
inline constexpr double kAOld = 0.90;

/** Days between sequential LAI estimates used in leaf-age calculation. */
inline constexpr double kDbtwn = 30.0;

// ============================================================================
// Scalar helper functions (pure, stateless)
// ============================================================================

/**
 * @brief CO₂ inhibition factor — Possell et al. (2005).
 * @param co2_ppm Ambient CO₂ concentration [ppm].
 * @return gamma_CO2 ∈ (0, ∞).
 */
inline double GammaCO2(double co2_ppm) noexcept {
    return kGammaC02C1 / (1.0 + kGammaC02C1 * kGammaCO2C2 * co2_ppm);
}

/**
 * @brief Leaf-area-index correction factor.
 * @param lai Current-month LAI [m² m⁻²].
 * @return gamma_LAI ≥ 0.
 */
inline double GammaLAI(double lai) noexcept {
    if (lai <= 0.0) return 0.0;
    return kLaiC1 * lai / std::sqrt(1.0 + kLaiC2 * lai * lai);
}

/**
 * @brief Light-independent temperature response.
 * @param T_K Temperature [K].
 * @return gamma_T_LI ≥ 0.
 */
inline double GammaTLI(double T_K) noexcept {
    return std::exp(kBeta * (T_K - kTStd));
}

/**
 * @brief Light-dependent temperature response (Guenther et al. 2012).
 * @param T_K         Instantaneous temperature [K].
 * @param T_avg_15_K  15-day average temperature [K].
 * @return gamma_T_LD ≥ 0.
 */
inline double GammaTLD(double T_K, double T_avg_15_K = kTAvg15) noexcept {
    const double e_opt = kCEO * std::exp(kEOptCoeff * (T_avg_15_K - 297.0));
    const double t_opt = kTOptC1 + kTOptC2 * (T_avg_15_K - 297.0);
    const double x = (1.0 / t_opt - 1.0 / T_K) / kR;
    const double num = e_opt * kCT2 * std::exp(kCT1 * x);
    const double den = kCT2 - kCT1 * (1.0 - std::exp(kCT2 * x));
    return (den > 0.0) ? std::max(num / den, 0.0) : 0.0;
}

/**
 * @brief PAR activity factor via the PCEEA algorithm (Guenther et al. 2006).
 *
 * The 24-hr PAR average is supplied directly in µmol m⁻² s⁻¹ (HEMCO convention).
 * The instantaneous direct and diffuse PAR are supplied in W m⁻² and converted
 * internally.
 *
 * @param pardr_Wm2     Instantaneous direct PAR [W m⁻²].
 * @param pardf_Wm2     Instantaneous diffuse PAR [W m⁻²].
 * @param suncos        Cosine of solar zenith angle.
 * @param par_avg_umol  24-hr average PAR [µmol m⁻² s⁻¹] (default: kParAvgUmol).
 * @param doy           Day-of-year (default: kReferenceDoy).
 * @return gamma_PAR ≥ 0.
 */
inline double GammaPAR(double pardr_Wm2, double pardf_Wm2, double suncos, double par_avg_umol = kParAvgUmol, int doy = kReferenceDoy) noexcept {
    if (suncos <= 0.0) return 0.0;
    const double pac_i = (pardr_Wm2 + pardf_Wm2) * kWm2ToUmol;
    const double bbb = kGpC1 + kGpC2 * (par_avg_umol - 400.0);
    const double ptoa = kPtoaC1 + kPtoaC2 * std::cos(2.0 * std::numbers::pi * (doy - kPtoaDoyOffset) / 365.0);
    const double phi = pac_i / (suncos * ptoa);
    const double aaa = kGpC3 * bbb * phi - kGpC4 * phi * phi;
    return std::max(suncos * aaa, 0.0);
}

/**
 * @brief Leaf-age correction factor (Guenther et al. 2012).
 * @param cmlai  Current-month LAI [m² m⁻²].
 * @param pmlai  Previous-month LAI [m² m⁻²].
 * @param T_K    Temperature [K] (controls leaf-growth timescale).
 * @return gamma_age ≥ 0.
 */
inline double GammaAge(double cmlai, double pmlai, double T_K) noexcept {
    const double ti = (T_K <= 303.0) ? (5.0 + 0.7 * (300.0 - T_K)) : 2.9;
    const double tm = 2.3 * ti;
    double fnew, fgro, fmat, fold;

    if (cmlai == pmlai) {
        fnew = 0.0;
        fgro = 0.1;
        fmat = 0.8;
        fold = 0.1;
    } else if (cmlai > pmlai) {
        fnew = (kDbtwn > ti) ? (ti / kDbtwn) * (1.0 - pmlai / cmlai) : (1.0 - pmlai / cmlai);
        fmat = (kDbtwn > tm) ? (pmlai / cmlai) + ((kDbtwn - tm) / kDbtwn) * (1.0 - pmlai / cmlai) : (pmlai / cmlai);
        fgro = 1.0 - fnew - fmat;
        fold = 0.0;
    } else {
        fnew = 0.0;
        fgro = 0.0;
        fold = (pmlai - cmlai) / pmlai;
        fmat = 1.0 - fold;
    }
    return std::max(fnew * kANew + fgro * kAGro + fmat * kAMat + fold * kAOld, 0.0);
}

// ============================================================================
// Full per-cell isoprene emission factor (normalised, per unit AEF)
// ============================================================================

/**
 * @struct MeganInputs
 * @brief All inputs for one stateless HEMCO 3.12.1 MEGAN isoprene cell.
 */
struct MeganInputs {
    double T_K = 303.0;                 ///< Instantaneous temperature [K]
    double lai = 0.0;                   ///< Current-month LAI [m² m⁻²]
    double lai_prev = 0.0;              ///< Previous-month LAI [m² m⁻²]
    double pardr_Wm2 = 0.0;             ///< Direct PAR [W m⁻²]
    double pardf_Wm2 = 0.0;             ///< Diffuse PAR [W m⁻²]
    double suncos = 0.0;                ///< Solar zenith cosine
    double gwetroot = 1.0;              ///< Root-zone soil moisture (unused for ISOP)
    double co2_ppm = 390.0;             ///< Ambient CO₂ [ppm]
    double par_avg_umol = kParAvgUmol;  ///< 24-hr PAR avg [µmol m⁻² s⁻¹]
    double T_avg_15_K = kTAvg15;        ///< 15-day T avg [K]
    int doy = kReferenceDoy;            ///< Day-of-year
};

/**
 * @brief Compute the HEMCO 3.12.1 MEGAN isoprene emission normalised factor.
 *
 * The actual flux is: flux [kg m⁻² s⁻¹] = AEF [kg m⁻² s⁻¹] × IsopreneEmissionFactor(inputs).
 *
 * gamma_SM is 1.0 for isoprene (not ALD2 or ETOH), so it is omitted.
 *
 * @param in  Struct of per-cell inputs.
 * @return Dimensionless activity factor ∈ [0, ∞).
 */
inline double IsopreneEmissionFactor(const MeganInputs& in) noexcept {
    if (in.lai <= 0.0) return 0.0;

    const double gc = GammaCO2(in.co2_ppm);
    const double glai = GammaLAI(in.lai);
    const double gage = GammaAge(in.lai, in.lai_prev, in.T_K);
    const double gtli = GammaTLI(in.T_K);
    const double gtld = GammaTLD(in.T_K, in.T_avg_15_K);
    const double gpar = GammaPAR(in.pardr_Wm2, in.pardf_Wm2, in.suncos, in.par_avg_umol, in.doy);

    const double combined_t = (1.0 - kLdf) * gtli + kLdf * gpar * gtld;
    return kNormFac * gage * glai * gc * combined_t;
}

}  // namespace cece::hemco_megan::v3_12_1

#endif  // CECE_HEMCO_MEGAN_STATELESS_HPP
