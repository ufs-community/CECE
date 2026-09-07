#ifndef CECE_HEMCO_MEGAN_STATELESS_HPP
#define CECE_HEMCO_MEGAN_STATELESS_HPP

/**
 * @file hemco_megan_stateless.hpp
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

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <stdexcept>

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

#endif  // CECE_HEMCO_MEGAN_STATELESS_HPP
