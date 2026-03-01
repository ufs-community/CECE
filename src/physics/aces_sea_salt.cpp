#include "aces/physics/aces_sea_salt.hpp"

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

#include "aces/aces_physics_factory.hpp"

namespace aces {

/// Self-registration for the SeaSaltScheme scheme.
static PhysicsRegistration<SeaSaltScheme> register_scheme("sea_salt");

/**
 * @brief Gong (2003) Sea Salt source function dF/dr_80 [particles/m2/s/um]
 */
KOKKOS_INLINE_FUNCTION
double gong_source(double u10, double r80) {
    if (r80 <= 0.0) return 0.0;
    double a = 4.7 * std::pow(1.0 + 30.0 * r80, -0.017 * std::pow(r80, -1.44));
    double b = (0.433 - std::log10(r80)) / 0.433;
    return 1.373 * std::pow(u10, 3.41) * std::pow(r80, -a) * (1.0 + 0.057 * std::pow(r80, 3.45)) *
           std::pow(10.0, 1.607 * std::exp(-b * b));
}

void SeaSaltScheme::Initialize(const YAML::Node& /*config*/,
                               AcesDiagnosticManager* /*diag_manager*/) {
    std::cout << "SeaSaltScheme: Initialized." << std::endl;
}

void SeaSaltScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto u10m = ResolveImport("wind_speed_10m", import_state);
    auto tskin = ResolveImport("tskin", import_state);
    auto sala = ResolveExport("total_SALA_emissions", export_state);
    auto salc = ResolveExport("total_SALC_emissions", export_state);

    if (!u10m.data() || !tskin.data()) return;

    int nx = u10m.extent(0);
    int ny = u10m.extent(1);
    int nz = u10m.extent(2);

    // Bins (dry radius in um)
    const double r_sala_min = 0.01, r_sala_max = 0.5;
    const double r_salc_min = 0.5, r_salc_max = 8.0;
    const double dr = 0.05;         // Integration step in um (dry)
    const double betha = 2.0;       // r80 / r_dry
    const double ss_dens = 2200.0;  // kg/m3
    const double pi = 3.14159265358979323846;

    if (sala.data()) {
        Kokkos::parallel_for(
            "SeaSalt_SALA_Gong",
            Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<3>>({0, 0, 0},
                                                                                  {nx, ny, nz}),
            KOKKOS_LAMBDA(int i, int j, int k) {
                if (k != 0) return;  // Restricted to surface
                double u = u10m(i, j, k);
                double sst = tskin(i, j, k) - 273.15;
                sst = std::max(0.0, std::min(30.0, sst));
                double scale =
                    0.329 + 0.0904 * sst - 0.00717 * sst * sst + 0.000207 * sst * sst * sst;

                double total_kg = 0.0;
                for (double r = r_sala_min; r < r_sala_max; r += dr) {
                    double r_mid = r + 0.5 * dr;
                    double r80_mid = r_mid * betha;
                    double df_dr80 = gong_source(u, r80_mid);
                    double n_particles = df_dr80 * betha * dr;
                    total_kg +=
                        n_particles * (4.0 / 3.0 * pi * std::pow(r_mid * 1.0e-6, 3) * ss_dens);
                }
                sala(i, j, k) += scale * total_kg;
            });
        MarkModified("total_SALA_emissions", export_state);
    }

    if (salc.data()) {
        Kokkos::parallel_for(
            "SeaSalt_SALC_Gong",
            Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<3>>({0, 0, 0},
                                                                                  {nx, ny, nz}),
            KOKKOS_LAMBDA(int i, int j, int k) {
                if (k != 0) return;  // Restricted to surface
                double u = u10m(i, j, k);
                double sst = tskin(i, j, k) - 273.15;
                sst = std::max(0.0, std::min(30.0, sst));
                double scale =
                    0.329 + 0.0904 * sst - 0.00717 * sst * sst + 0.000207 * sst * sst * sst;

                double total_kg = 0.0;
                for (double r = r_salc_min; r < r_salc_max; r += dr) {
                    double r_mid = r + 0.5 * dr;
                    double r80_mid = r_mid * betha;
                    double df_dr80 = gong_source(u, r80_mid);
                    double n_particles = df_dr80 * betha * dr;
                    total_kg +=
                        n_particles * (4.0 / 3.0 * pi * std::pow(r_mid * 1.0e-6, 3) * ss_dens);
                }
                salc(i, j, k) += scale * total_kg;
            });
        MarkModified("total_SALC_emissions", export_state);
    }
    Kokkos::fence();
}

}  // namespace aces
