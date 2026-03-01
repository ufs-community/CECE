#include "aces/physics/aces_megan.hpp"

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

#include "aces/aces_physics_factory.hpp"

namespace aces {

/// Self-registration for the MeganScheme scheme.
static PhysicsRegistration<MeganScheme> register_scheme("megan");

/**
 * @brief MEGAN Gamma Factors (Ported from hcox_megan_mod.F90)
 */

KOKKOS_INLINE_FUNCTION
double get_gamma_lai(double lai) {
    return 0.49 * lai / std::sqrt(1.0 + 0.2 * lai * lai);
}

KOKKOS_INLINE_FUNCTION
double get_gamma_t_li(double temp, double beta) {
    const double T_STANDARD = 303.0;
    return std::exp(beta * (temp - T_STANDARD));
}

KOKKOS_INLINE_FUNCTION
double get_gamma_t_ld(double T, double PT_15, double CT1, double CEO) {
    const double R = 8.3144598e-3;  // kJ/mol/K
    const double CT2 = 200.0;

    double e_opt = CEO * std::exp(0.08 * (PT_15 - 297.0));
    double t_opt = 313.0 + 0.6 * (PT_15 - 297.0);
    double x = (1.0 / t_opt - 1.0 / T) / R;

    double c_t = e_opt * CT2 * std::exp(CT1 * x) / (CT2 - CT1 * (1.0 - std::exp(CT2 * x)));
    return std::max(c_t, 0.0);
}

KOKKOS_INLINE_FUNCTION
double get_gamma_par_pceea(double q_dir, double q_diff, double par_avg, double suncos, int doy) {
    const double WM2_TO_UMOLM2S = 4.766;
    const double PI = 3.14159265358979323846;

    if (suncos <= 0.0) return 0.0;

    double pac_instant = (q_dir + q_diff) * WM2_TO_UMOLM2S;
    double pac_daily = par_avg * WM2_TO_UMOLM2S;

    double ptoa = 3000.0 + 99.0 * std::cos(2.0 * PI * (doy - 10.0) / 365.0);
    double phi = pac_instant / (suncos * ptoa);

    double bbb = 1.0 + 0.0005 * (pac_daily - 400.0);
    double aaa = (2.46 * bbb * phi) - (0.9 * phi * phi);

    double gamma_p = suncos * aaa;
    return std::max(gamma_p, 0.0);
}

KOKKOS_INLINE_FUNCTION
double get_gamma_co2(double co2a) {
    // Ported from GET_GAMMA_CO2 (LPOSSELL relationship)
    return 8.9406 / (1.0 + 8.9406 * 0.0024 * co2a);
}

void MeganScheme::Initialize(const YAML::Node& /*config*/,
                             AcesDiagnosticManager* /*diag_manager*/) {
    std::cout << "MeganScheme: Initialized." << std::endl;
}

void MeganScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto temp = ResolveImport("temperature", import_state);
    auto isoprene = ResolveExport("total_isoprene_emissions", export_state);
    auto lai = ResolveImport("lai", import_state);
    auto pardr = ResolveImport("pardr", import_state);
    auto pardf = ResolveImport("pardf", import_state);
    auto suncos = ResolveImport("suncos", import_state);

    if (!temp.data() || !isoprene.data() || !lai.data() || !pardr.data() || !pardf.data() ||
        !suncos.data())
        return;

    int nx = isoprene.extent(0);
    int ny = isoprene.extent(1);
    int nz = isoprene.extent(2);

    const double BETA = 0.13;
    const double CT1 = 95.0;
    const double CEO = 2.0;
    const double LDF = 1.0;
    const double NORM_FAC = 1.0 / 1.0101081;
    const double AEF_ISOP = 1.0e-9;
    const double CO2A = 400.0;  // Default atmospheric CO2

    Kokkos::parallel_for(
        "MeganKernel_Faithful",
        Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<3>>({0, 0, 0},
                                                                              {nx, ny, nz}),
        KOKKOS_LAMBDA(int i, int j, int k) {
            if (k != 0) return;  // Surface restricted

            double T = temp(i, j, k);
            double L = lai(i, j, k);
            double sc = suncos(i, j, k);

            if (L <= 0.0) return;

            double T_AVG_15 = 297.0;
            double PAR_AVG = 400.0;
            int doy = 180;

            double gamma_lai = get_gamma_lai(L);
            double gamma_t_li = get_gamma_t_li(T, BETA);
            double gamma_t_ld = get_gamma_t_ld(T, T_AVG_15, CT1, CEO);
            double gamma_par =
                get_gamma_par_pceea(pardr(i, j, k), pardf(i, j, k), PAR_AVG, sc, doy);
            double gamma_co2 = get_gamma_co2(CO2A);

            double megan_emis = NORM_FAC * AEF_ISOP * gamma_lai * gamma_co2 *
                                ((1.0 - LDF) * gamma_t_li + (LDF * gamma_par * gamma_t_ld));

            isoprene(i, j, k) += megan_emis;
        });

    Kokkos::fence();
    MarkModified("total_isoprene_emissions", export_state);
}

}  // namespace aces
