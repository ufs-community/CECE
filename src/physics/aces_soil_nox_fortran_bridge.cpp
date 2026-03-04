#include <Kokkos_Core.hpp>
#include <iostream>

#include "aces/aces_physics_factory.hpp"
#include "aces/physics/aces_soil_nox_fortran.hpp"

extern "C" {
void run_soil_nox_fortran(double* temp, double* gwet, double* soil_nox, int nx, int ny, int nz);
}

namespace aces {

#ifdef ACES_HAS_FORTRAN
/// Self-registration for the SoilNoxFortranScheme scheme.
static PhysicsRegistration<SoilNoxFortranScheme> register_scheme("soil_nox_fortran");
#endif

void SoilNoxFortranScheme::Initialize(const YAML::Node& /*config*/,
                                      AcesDiagnosticManager* /*diag_manager*/) {
    std::cout << "SoilNoxFortranScheme: Initialized." << "\n";
}

void SoilNoxFortranScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto it_temp = import_state.fields.find("temperature");
    auto it_gwet = import_state.fields.find("gwettop");
    auto it_soil_nox = export_state.fields.find("soil_nox");

    if (it_temp == import_state.fields.end() || it_gwet == import_state.fields.end() ||
        it_soil_nox == export_state.fields.end())
        return;

    auto& dv_temp = it_temp->second;
    auto& dv_gwet = it_gwet->second;
    auto& dv_soil_nox = it_soil_nox->second;

    dv_temp.sync<Kokkos::HostSpace>();
    dv_gwet.sync<Kokkos::HostSpace>();
    dv_soil_nox.sync<Kokkos::HostSpace>();

    int nx = dv_soil_nox.extent(0);
    int ny = dv_soil_nox.extent(1);
    int nz = dv_soil_nox.extent(2);

    run_soil_nox_fortran(dv_temp.view_host().data(), dv_gwet.view_host().data(),
                         dv_soil_nox.view_host().data(), nx, ny, nz);

    dv_soil_nox.modify<Kokkos::HostSpace>();
    dv_soil_nox.sync<Kokkos::DefaultExecutionSpace>();
}

}  // namespace aces
