#ifndef CECE_MEGAN_HPP
#define CECE_MEGAN_HPP

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <conf/value.hpp>
#include <cstddef>
#include <numbers>
#include <stdexcept>
#include <string>

#include "cece/physics_scheme.hpp"

/**
 * @section hemco_reference HEMCO 3.12.1 source-pinned calculation
 * @brief Device-callable stateless HEMCO 3.12.1 MEGAN isoprene equations.
 *
 * This layer contains no I/O, regridding, or persistent averaging state.
 * All state-dependent quantities (historical temperature, historical direct
 * and diffuse PAR, previous-day LAI, and day-of-year) are supplied as
 * arguments. This evaluates one cell without evolving HEMCO restart state.
 *
 * Science source: HEMCO 3.12.1, hcox_megan_mod.F90
 * Repository:     https://github.com/geoschem/HEMCO
 * Release tag:    3.12.1
 * Commit:         07da3c29fd85abc3824cb6288578b0b68c2395a3
 * Source SHA-256: a298e4003210c7dba86c53cdd37f85a868dcb3a89b3de56ab175257e04614f31
 *
 * Provenance contract
 * -------------------
 * Algorithm constants in this file are frozen at the values found in the
 * pinned HEMCO 3.12.1 source. Reference date and CO₂ settings are fixture
 * choices. The historical PAR inputs retain HEMCO's restart-field
 * units of W m⁻² and are converted internally before forming PAC_DAILY.
 * PTOA uses 3000+99·cos with day-of-year phase offset 10.
 *
 * Scope and limits
 * ----------------
 * The stateless mode defaults to HEMCO's no-restart initialization values:
 * REAL(sp)-projected 288.15 K historical temperature, 30 W m⁻² historical direct PAR, and
 * 48 W m⁻² historical diffuse PAR. The selected reference case enables CO₂
 * inhibition at 390 ppm; those two choices are explicit inputs, not universal
 * HEMCO constants.
 * This implementation does not reproduce:
 *   - State evolution with HEMCO's 5-day and 12-hour e-folding updates.
 *   - HEMCO's latitude/local-time solar-angle calculation; `suncos` must be
 *     the effective sine of solar elevation (equivalently cosine of zenith).
 *   - Gridded PFT/AEF and normalized-LAI preprocessing. Callers supply an
 *     effective AEF (including ISOP_SCALING where applicable) and separately
 *     supply current/previous LAI after any requested HEMCO normalization.
 *   - Non-isoprene species LDF/CT1/CEO values.
 *   - Coupled upstream photosynthesis or stomatal conductance.
 */

namespace cece::hemco_megan::v3_12_1 {

// ============================================================================
// Frozen HEMCO 3.12.1 scalar constants for isoprene
// ============================================================================

/**
 * Emission normalisation factor computed by HEMCO CALC_NORM_FAC.
 *
 * HEMCO's source comment rounds GAMMA_STANDARD to 1.0101081; the executable
 * code evaluates the full expression, which yields the reciprocal below in
 * double precision.
 */
inline constexpr double kNormFac = 0.9899364002107353;

/** Light-dependent fraction for isoprene (HEMCO 3.12.1, ISOP class). */
inline constexpr double kLdf = 1.0;

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

/** Empirical coefficient for e_opt dependence on historical temperature. */
inline constexpr double kEOptCoeff = 0.08;

/** W m⁻² to µmol m⁻² s⁻¹ conversion factor for PAR. */
inline constexpr double kWm2ToUmol = 4.766;

/** PTOA baseline [µmol m⁻² s⁻¹]. */
inline constexpr double kPtoaC1 = 3000.0;

/** PTOA seasonal amplitude [µmol m⁻² s⁻¹]. */
inline constexpr double kPtoaC2 = 99.0;

/** Day-of-year offset in the PTOA cosine. */
inline constexpr double kPtoaDoyOffset = 10.0;

/** Cold-start direct-PAR history [W m⁻²] when PARDR_DAVG is unavailable. */
inline constexpr double kParDirectHistoryWm2 = 30.0;

/** Cold-start diffuse-PAR history [W m⁻²] when PARDF_DAVG is unavailable. */
inline constexpr double kParDiffuseHistoryWm2 = 48.0;

/** Cold-start temperature history [K] when T_DAVG is unavailable. */
inline constexpr double kTemperatureHistoryK = static_cast<double>(288.15F);

/** HEMCO's interval between instantaneous and previous-day LAI. */
inline constexpr double kDaysBetweenLai = 1.0;

/** Day-of-year for the pinned 20 June 2021 analytical reference case. */
inline constexpr int kReferenceDoy = 171;

/** CO₂ inhibition coefficient c₁ (Possell and Hewitt 2011). */
inline constexpr double kGammaCO2C1 = 8.9406;

/** CO₂ inhibition coefficient c₂ (Possell and Hewitt 2011) [ppm⁻¹]. */
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

// ============================================================================
// Scalar helper functions (pure, stateless)
// ============================================================================

/**
 * @brief CO₂ inhibition factor — Possell and Hewitt (2011).
 * @param co2_ppm Ambient CO₂ concentration [ppm].
 * @return gamma_CO2 ∈ (0, ∞).
 */
KOKKOS_INLINE_FUNCTION double GammaCO2(double co2_ppm) noexcept {
    return kGammaCO2C1 / (1.0 + kGammaCO2C1 * kGammaCO2C2 * co2_ppm);
}

/**
 * @brief Leaf-area-index correction factor.
 * @param lai Current-month LAI [m² m⁻²].
 * @return gamma_LAI ≥ 0.
 */
KOKKOS_INLINE_FUNCTION double GammaLAI(double lai) noexcept {
    if (lai <= 0.0) return 0.0;
    return kLaiC1 * lai / std::sqrt(1.0 + kLaiC2 * lai * lai);
}

/**
 * @brief Light-independent temperature response.
 * @param T_K Temperature [K].
 * @return gamma_T_LI ≥ 0.
 */
KOKKOS_INLINE_FUNCTION double GammaTLI(double T_K) noexcept {
    return std::exp(kBeta * (T_K - kTStd));
}

/**
 * @brief Light-dependent temperature response (Guenther et al. 2012).
 * @param T_K         Instantaneous temperature [K].
 * @param T_history_K Historical temperature supplied as HEMCO T_DAVG [K].
 * @return gamma_T_LD ≥ 0.
 */
KOKKOS_INLINE_FUNCTION double GammaTLD(double T_K, double T_history_K = kTemperatureHistoryK) noexcept {
    const double e_opt = kCEO * std::exp(kEOptCoeff * (T_history_K - 297.0));
    const double t_opt = kTOptC1 + kTOptC2 * (T_history_K - 297.0);
    const double x = (1.0 / t_opt - 1.0 / T_K) / kR;
    const double num = e_opt * kCT2 * std::exp(kCT1 * x);
    const double den = kCT2 - kCT1 * (1.0 - std::exp(kCT2 * x));
    return std::max(num / den, 0.0);
}

/**
 * @brief PAR activity factor via the PCEEA algorithm (Guenther et al. 2006).
 *
 * Instantaneous and historical direct/diffuse PAR are supplied in W m⁻² and
 * converted internally, matching HEMCO's Q_DIR_2/Q_DIFF_2 and
 * PARDR_DAVG/PARDF_DAVG contracts.
 *
 * @param pardr_Wm2     Instantaneous direct PAR [W m⁻²].
 * @param pardf_Wm2     Instantaneous diffuse PAR [W m⁻²].
 * @param suncos        Finite effective sine of solar elevation in [-1,1],
 *                      matching HEMCO's solar-angle calculation; nonpositive
 *                      values represent night.
 * @param pardr_history_Wm2 Historical direct PAR [W m⁻²].
 * @param pardf_history_Wm2 Historical diffuse PAR [W m⁻²].
 * @param doy           Day-of-year (default: kReferenceDoy).
 * @return gamma_PAR ≥ 0.
 */
KOKKOS_INLINE_FUNCTION double GammaPAR(double pardr_Wm2, double pardf_Wm2, double suncos, double pardr_history_Wm2 = kParDirectHistoryWm2,
                                       double pardf_history_Wm2 = kParDiffuseHistoryWm2, int doy = kReferenceDoy) noexcept {
    // Nonpositive values are valid nighttime inputs. The upper-bound check is
    // defensive for direct scalar callers; MeganScheme rejects values outside
    // the complete finite [-1, 1] runtime contract before kernel execution.
    if (!(suncos > 0.0 && suncos <= 1.0)) return 0.0;

    const double sin_beta = suncos;
    const double pac_i = pardr_Wm2 * kWm2ToUmol + pardf_Wm2 * kWm2ToUmol;
    const double pac_daily = pardr_history_Wm2 * kWm2ToUmol + pardf_history_Wm2 * kWm2ToUmol;
    const double bbb = kGpC1 + kGpC2 * (pac_daily - 400.0);
    const double ptoa = kPtoaC1 + kPtoaC2 * std::cos(2.0 * std::numbers::pi * (doy - kPtoaDoyOffset) / 365.0);
    const double phi = pac_i / (sin_beta * ptoa);
    const double aaa = kGpC3 * bbb * phi - kGpC4 * phi * phi;
    double gamma_par = sin_beta * aaa;

    const double beta_degrees = std::asin(sin_beta) * 180.0 / std::numbers::pi;
    if (beta_degrees < 1.0 && gamma_par > 0.1) gamma_par = 0.0;
    return std::max(gamma_par, 0.0);
}

/**
 * @brief Leaf-age correction factor (Guenther et al. 2012).
 * @param cmlai  Current-month LAI [m² m⁻²].
 * @param pmlai  Previous-day effective LAI [m² m⁻²].
 * @param T_history_K HEMCO T_DAVG [K] (controls leaf-growth timescale).
 * @param days_between_lai Days between current and historical LAI.
 * @return gamma_age ≥ 0.
 */
KOKKOS_INLINE_FUNCTION double GammaAge(double cmlai, double pmlai, double T_history_K, double days_between_lai = kDaysBetweenLai) noexcept {
    const double ti = (T_history_K <= 303.0) ? (5.0 + 0.7 * (300.0 - T_history_K)) : 2.9;
    const double tm = 2.3 * ti;
    double fnew, fgro, fmat, fold;

    if (cmlai == pmlai) {
        fnew = 0.0;
        fgro = 0.1;
        fmat = 0.8;
        fold = 0.1;
    } else if (cmlai > pmlai) {
        fnew = (days_between_lai > ti) ? (ti / days_between_lai) * (1.0 - pmlai / cmlai) : (1.0 - pmlai / cmlai);
        fmat = (days_between_lai > tm) ? (pmlai / cmlai) + ((days_between_lai - tm) / days_between_lai) * (1.0 - pmlai / cmlai) : (pmlai / cmlai);
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
    double T_K = 303.0;                                   ///< Instantaneous temperature [K]
    double lai = 0.0;                                     ///< Current-month LAI [m² m⁻²]
    double lai_prev = 0.0;                                ///< Exact post-preprocessing previous-day effective LAI [m² m⁻²]
    double pardr_Wm2 = 0.0;                               ///< Direct PAR [W m⁻²]
    double pardf_Wm2 = 0.0;                               ///< Diffuse PAR [W m⁻²]
    double suncos = 0.0;                                  ///< Effective sine of solar elevation (cosine of zenith)
    double gwetroot = 1.0;                                ///< Root-zone soil moisture (unused for ISOP)
    double co2_ppm = 390.0;                               ///< Ambient CO₂ [ppm]
    bool apply_co2_inhibition = true;                     ///< HEMCO's configurable CO₂ switch
    double pardr_history_Wm2 = kParDirectHistoryWm2;      ///< PARDR_DAVG [W m⁻²]
    double pardf_history_Wm2 = kParDiffuseHistoryWm2;     ///< PARDF_DAVG [W m⁻²]
    double temperature_history_K = kTemperatureHistoryK;  ///< T_DAVG [K]
    double days_between_lai = kDaysBetweenLai;            ///< HEMCO DAYS_BTW_M [days]
    int doy = kReferenceDoy;                              ///< Day-of-year
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
KOKKOS_INLINE_FUNCTION double IsopreneEmissionFactor(const MeganInputs& in) noexcept {
    if (in.lai <= 0.0) return 0.0;

    const double gc = in.apply_co2_inhibition ? GammaCO2(in.co2_ppm) : 1.0;
    const double glai = GammaLAI(in.lai);
    const double gage = GammaAge(in.lai, in.lai_prev, in.temperature_history_K, in.days_between_lai);
    const double gtli = GammaTLI(in.T_K);
    const double gtld = GammaTLD(in.T_K, in.temperature_history_K);
    const double gpar = GammaPAR(in.pardr_Wm2, in.pardf_Wm2, in.suncos, in.pardr_history_Wm2, in.pardf_history_Wm2, in.doy);

    const double combined_t = (1.0 - kLdf) * gtli + kLdf * gpar * gtld;
    return kNormFac * gage * glai * gc * combined_t;
}

}  // namespace cece::hemco_megan::v3_12_1

namespace cece {

// Shared MEGAN configuration and units, not a rolling-history calculator.
// CeceDiagnosticManager registers/writes diagnostic fields; these settings
// only validate caller-supplied effective histories for stateless execution.

/// Scalar effective histories for native C++ MEGAN and MEGAN3.
/// Defaults preserve the previous behavior; no history evolution is performed.
struct MeganHistory {
    double temperature_k = 297.0;
    double par_wm2 = 400.0;
    double days_between_lai = 30.0;
    int day_of_year = 180;
    bool leaf_age_uses_history = false;

    static MeganHistory FromConfig(const conf::Value& config) {
        MeganHistory result;
        if (config["temperature_history_k"].is_defined()) result.temperature_k = config["temperature_history_k"].as_double();
        if (config["par_history_wm2"].is_defined()) result.par_wm2 = config["par_history_wm2"].as_double();
        if (config["days_between_lai"].is_defined()) result.days_between_lai = config["days_between_lai"].as_double();
        if (config["day_of_year"].is_defined()) result.day_of_year = config["day_of_year"].as_int();
        if (config["leaf_age_uses_temperature_history"].is_defined()) {
            result.leaf_age_uses_history = config["leaf_age_uses_temperature_history"].as_bool();
        }
        if (!std::isfinite(result.temperature_k) || result.temperature_k <= 0.0 || !std::isfinite(result.par_wm2) || result.par_wm2 < 0.0 ||
            !std::isfinite(result.days_between_lai) || result.days_between_lai <= 0.0 || result.day_of_year < 1 || result.day_of_year > 366) {
            throw std::invalid_argument(
                "MEGAN history requires positive finite temperature_history_k and days_between_lai, "
                "nonnegative finite par_history_wm2, and day_of_year in [1, 366]");
        }
        return result;
    }
};

/// Molecular weight of NO. Numerically, g/mol is equivalent to kg/kmol.
inline constexpr double kSoilNoMolecularWeightKgPerKmol = 30.01;

/// Convert a soil-NO mass flux [kg NO m-2 s-1] to an amount flux [kmol NO m-2
/// s-1].
KOKKOS_INLINE_FUNCTION
constexpr double SoilNoMassToAmountFlux(double mass_flux) {
    return mass_flux / kSoilNoMolecularWeightKgPerKmol;
}

// ============================================================================
// Shared KOKKOS_INLINE_FUNCTION gamma helpers used by MeganScheme, Megan3Scheme,
// and EmissionActivityCalculator. Defined in the header so they can be inlined
// into Kokkos kernels across multiple translation units.
// ============================================================================

KOKKOS_INLINE_FUNCTION
double get_gamma_lai(double lai, double c1, double c2, bool is_bidirectional) {
    if (is_bidirectional) {
        if (lai <= 6.0) {
            if (lai <= 2.0) {
                return 0.5 * lai;
            } else {
                return 1.0 - 0.0625 * (lai - 2.0);
            }
        } else {
            return 0.75;
        }
    } else {
        return c1 * lai / std::sqrt(1.0 + c2 * lai * lai);
    }
}

KOKKOS_INLINE_FUNCTION
double get_gamma_age(double cmlai, double pmlai, double dbtwn, double tt, double an, double ag, double am, double ao) {
    double fnew = 0.0, fgro = 0.0, fmat = 0.0, fold = 0.0;
    double ti = (tt <= 303.0) ? (5.0 + 0.7 * (300.0 - tt)) : 2.9;
    double tm = 2.3 * ti;

    if (cmlai == pmlai) {
        fnew = 0.0;
        fgro = 0.1;
        fmat = 0.8;
        fold = 0.1;
    } else if (cmlai > pmlai) {
        if (dbtwn > ti)
            fnew = (ti / dbtwn) * (1.0 - pmlai / cmlai);
        else
            fnew = 1.0 - (pmlai / cmlai);
        if (dbtwn > tm)
            fmat = (pmlai / cmlai) + ((dbtwn - tm) / dbtwn) * (1.0 - pmlai / cmlai);
        else
            fmat = pmlai / cmlai;
        fgro = 1.0 - fnew - fmat;
        fold = 0.0;
    } else {
        fnew = 0.0;
        fgro = 0.0;
        fold = (pmlai - cmlai) / pmlai;
        fmat = 1.0 - fold;
    }
    return std::max(fnew * an + fgro * ag + fmat * am + fold * ao, 0.0);
}

KOKKOS_INLINE_FUNCTION
double get_gamma_sm(double gwetroot, bool is_ald2_or_eoh) {
    double gamma_sm = 1.0;
    double gwetroot_clamped = std::min(std::max(gwetroot, 0.0), 1.0);
    if (is_ald2_or_eoh) {
        gamma_sm = std::max(20.0 * gwetroot_clamped - 17.0, 1.0);
    }
    return gamma_sm;
}

KOKKOS_INLINE_FUNCTION
double get_gamma_t_li(double temp, double beta, double t_standard) {
    return std::exp(beta * (temp - t_standard));
}

KOKKOS_INLINE_FUNCTION
double get_gamma_t_ld(double T, double PT_15, double CT1, double CEO, double R, double CT2, double t_opt_c1, double t_opt_c2, double e_opt_coeff) {
    double e_opt = CEO * std::exp(e_opt_coeff * (PT_15 - 297.0));
    double t_opt = t_opt_c1 + t_opt_c2 * (PT_15 - 297.0);
    double x = (1.0 / t_opt - 1.0 / T) / R;
    double c_t = e_opt * CT2 * std::exp(CT1 * x) / (CT2 - CT1 * (1.0 - std::exp(CT2 * x)));
    return std::max(c_t, 0.0);
}

KOKKOS_INLINE_FUNCTION
double get_gamma_par_pceea(double q_dir, double q_diff, double par_avg, double suncos, int doy, double wm2_to_umol, double ptoa_c1, double ptoa_c2,
                           double gamma_p_c1, double gamma_p_c2, double gamma_p_c3, double gamma_p_c4) {
    const double PI = std::numbers::pi;
    if (suncos <= 0.0) return 0.0;
    double pac_instant = (q_dir + q_diff) * wm2_to_umol;
    double pac_daily = par_avg * wm2_to_umol;
    double ptoa = ptoa_c1 + ptoa_c2 * std::cos(2.0 * PI * (doy - 10.0) / 365.0);
    double phi = pac_instant / (suncos * ptoa);
    double bbb = gamma_p_c1 + gamma_p_c2 * (pac_daily - 400.0);
    double aaa = (gamma_p_c3 * bbb * phi) - (gamma_p_c4 * phi * phi);
    double gamma_p = suncos * aaa;
    return std::max(gamma_p, 0.0);
}

KOKKOS_INLINE_FUNCTION
double get_gamma_co2(double co2a, double c1, double c2, bool use_wilkinson) {
    if (!use_wilkinson) {
        return c1 / (1.0 + c1 * c2 * co2a);
    } else {
        double ismaxi, hexpi, cstari;
        if (co2a <= 600.0) {
            ismaxi = 1.036 - (1.036 - 1.072) / (600.0 - 400.0) * (600.0 - co2a);
            hexpi = 2.0125 - (2.0125 - 1.7000) / (600.0 - 400.0) * (600.0 - co2a);
            cstari = 1150.0 - (1150.0 - 1218.0) / (600.0 - 400.0) * (600.0 - co2a);
        } else if (co2a > 600.0 && co2a < 800.0) {
            ismaxi = 1.046 - (1.046 - 1.036) / (800.0 - 600.0) * (800.0 - co2a);
            hexpi = 1.5380 - (1.5380 - 2.0125) / (800.0 - 600.0) * (800.0 - co2a);
            cstari = 2025.0 - (2025.0 - 1150.0) / (800.0 - 600.0) * (800.0 - co2a);
        } else {
            ismaxi = 1.014 - (1.014 - 1.046) / (1200.0 - 800.0) * (1200.0 - co2a);
            hexpi = 2.8610 - (2.8610 - 1.5380) / (1200.0 - 800.0) * (1200.0 - co2a);
            cstari = 1525.0 - (1525.0 - 2025.0) / (1200.0 - 800.0) * (1200.0 - co2a);
        }
        double ismaxa = 1.344;
        double hexpa = 1.4614;
        double cstara = 585.0;
        double co2i = 0.7 * co2a;
        double term1 = ismaxi - ismaxi * std::pow(co2i, hexpi) / (std::pow(cstari, hexpi) + std::pow(co2i, hexpi));
        double term2 = ismaxa - ismaxa * std::pow(0.7 * co2a, hexpa) / (std::pow(cstara, hexpa) + std::pow(0.7 * co2a, hexpa));
        return term1 * term2;
    }
}

/**
 * @class MeganScheme
 * @brief C++ implementation of the MEGAN biogenics emission scheme.
 *
 * Supported `megan_method` values:
 *   - `"megan21"` (default) - fully configurable MEGAN2.1 isoprene calculation.
 *     `"native"` remains a compatibility alias with identical behavior.
 *   - `"hemco_3_12_1"` — source-pinned HEMCO 3.12.1 stateless cell arithmetic
 *     using the frozen HEMCO constants and equations above. All megan21-mode
 *     tuning parameters are ignored. HEMCO restart-derived temperature and
 *     direct/diffuse PAR histories are explicit scalar options in this mode;
 *     the mode consumes them but does not evolve them. HEMCO's one-day LAI
 *     interval remains fixed by the source contract. The caller must provide
 *     exact effective current and previous-day LAI after upstream HEMCO
 *     preprocessing; CECE does not reconstruct or re-round those fields.
 */
class MeganScheme : public BasePhysicsScheme {
   public:
    MeganScheme() = default;
    ~MeganScheme() override = default;

    void Initialize(const conf::Value& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;

   private:
    // ---- Emission method selection ----
    std::string megan_method_ = "megan21";  // Normalized value: "megan21" or "hemco_3_12_1".

    // ---- HEMCO 3.12.1 source-conformance-mode settings ----
    double hemco_co2_ppm_ = 390.0;  ///< selected reference-case CO₂ [ppm]
    bool hemco_co2_inhibition_ = true;
    double hemco_par_direct_history_wm2_ = hemco_megan::v3_12_1::kParDirectHistoryWm2;
    double hemco_par_diffuse_history_wm2_ = hemco_megan::v3_12_1::kParDiffuseHistoryWm2;
    double hemco_temperature_history_k_ = hemco_megan::v3_12_1::kTemperatureHistoryK;
    int hemco_day_of_year_ = hemco_megan::v3_12_1::kReferenceDoy;

    // ---- Configurable MEGAN2.1 parameters (ignored in hemco_3_12_1 mode) ----
    MeganHistory history_;
    double gamma_co2_ = 0.0;
    double beta_ = 0.13;
    double ct1_ = 95.0;
    double ceo_ = 2.0;
    double ldf_ = 1.0;

    std::string species_name_ = "isoprene";
    std::string export_field_name_ = "isoprene_emissions";
    double aef_ = 1.0e-9;

    double lai_coeff_1_ = 0.49;
    double lai_coeff_2_ = 0.2;
    double standard_temp_ = 303.0;
    double gas_constant_ = 8.3144598e-3;
    double ct2_const_ = 200.0;
    double t_opt_coeff_1_ = 313.0;
    double t_opt_coeff_2_ = 0.6;
    double e_opt_coeff_ = 0.08;
    double wm2_to_umolm2s_ = 4.766;
    double ptoa_coeff_1_ = 3000.0;
    double ptoa_coeff_2_ = 99.0;
    double gamma_p_coeff_1_ = 1.0;
    double gamma_p_coeff_2_ = 0.0005;
    double gamma_p_coeff_3_ = 2.46;
    double gamma_p_coeff_4_ = 0.9;
    double gamma_co2_coeff_1_ = 8.9406;
    double gamma_co2_coeff_2_ = 0.0024;

    double anew_ = 1.0;
    bool is_ald2_or_eoh_ = false;
    double agro_ = 1.0;
    double amat_ = 1.0;
    double aold_ = 1.0;
    bool is_bidirectional_ = false;
    bool use_wilkinson_ = false;
};

}  // namespace cece

#endif  // CECE_MEGAN_HPP
