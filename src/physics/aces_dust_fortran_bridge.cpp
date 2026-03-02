#include <Kokkos_Core.hpp>
#include <iostream>

#include "aces/aces_physics_factory.hpp"
#include "aces/physics/aces_dust_fortran.hpp"

extern "C" {
void run_dust_fortran(double* u10m, double* gwet, double* sand, double* dust_emis, int nx, int ny,
                      int nz);
}

namespace aces {

#ifdef ACES_HAS_FORTRAN
/// Self-registration for the DustFortranScheme scheme.
static PhysicsRegistration<DustFortranScheme> register_scheme("dust_fortran");
#endif

void DustFortranScheme::Initialize(const YAML::Node& /*config*/,
                                   AcesDiagnosticManager* /*diag_manager*/) {
    std::cout << "DustFortranScheme: Initialized." << "\n";
}

void DustFortranScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto it_u10 = import_state.fields.find("wind_speed_10m");
    auto it_gwet = import_state.fields.find("gwettop");
    auto it_sand = import_state.fields.find("GINOUX_SAND");
    auto it_dust = export_state.fields.find("total_dust_emissions");

    if (it_u10 == import_state.fields.end() || it_gwet == import_state.fields.end() ||
        it_sand == import_state.fields.end() || it_dust == export_state.fields.end())
        return;

    auto& dv_u10 = it_u10->second;
    auto& dv_gwet = it_gwet->second;
    auto& dv_sand = it_sand->second;
    auto& dv_dust = it_dust->second;

    dv_u10.sync<Kokkos::HostSpace>();
    dv_gwet.sync<Kokkos::HostSpace>();
    dv_sand.sync<Kokkos::HostSpace>();
    dv_dust.sync<Kokkos::HostSpace>();

    int nx = dv_dust.extent(0);
    int ny = dv_dust.extent(1);
    int nz = dv_dust.extent(2);

    run_dust_fortran(dv_u10.view_host().data(), dv_gwet.view_host().data(),
                     dv_sand.view_host().data(), dv_dust.view_host().data(), nx, ny, nz);

    dv_dust.modify<Kokkos::HostSpace>();
    dv_dust.sync<Kokkos::DefaultExecutionSpace>();
}

}  // namespace aces
