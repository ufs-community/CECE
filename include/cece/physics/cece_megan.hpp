#ifndef CECE_MEGAN_HPP
#define CECE_MEGAN_HPP

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>

#include "cece/physics_scheme.hpp"

namespace cece {

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
 * @brief Native C++ implementation of the MEGAN biogenics emission scheme.
 */
class MeganScheme : public BasePhysicsScheme {
   public:
    MeganScheme() = default;
    ~MeganScheme() override = default;

    void Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;

   private:
    double gamma_co2_ = 0.0;
    double beta_ = 0.13;
    double ct1_ = 95.0;
    double ceo_ = 2.0;
    double ldf_ = 1.0;

    // Configurable list of species and their emission factors (AEF)
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

    // Additional parameters for gamma_age
    double anew_ = 1.0;

    // Additional parameter for gamma_sm
    bool is_ald2_or_eoh_ = false;
    double agro_ = 1.0;
    double amat_ = 1.0;
    double aold_ = 1.0;
    bool is_bidirectional_ = false;
    bool use_wilkinson_ = false;
};

}  // namespace cece

#endif  // CECE_MEGAN_HPP
