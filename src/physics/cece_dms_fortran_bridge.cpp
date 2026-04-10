#include <Kokkos_Core.hpp>
#include <iostream>

#include "cece/cece_physics_factory.hpp"
#include "cece/physics/cece_dms_fortran.hpp"

extern "C" {
void run_dms_fortran(double* u10m, double* tskin, double* seaconc, double* dms_emis, int nx, int ny,
                     int nz);
}

namespace cece {

#ifdef CECE_HAS_FORTRAN
/// Self-registration for the DMSFortranScheme scheme.
static PhysicsRegistration<DMSFortranScheme> register_scheme("dms_fortran");
#endif

void DMSFortranScheme::Initialize(const YAML::Node& /*config*/,
                                  CeceDiagnosticManager* /*diag_manager*/) {
    std::cout << "DMSFortranScheme: Initialized." << "\n";
}

void DMSFortranScheme::Run(CeceImportState& import_state, CeceExportState& export_state) {
    auto it_u10 = import_state.fields.find("wind_speed_10m");
    auto it_tskin = import_state.fields.find("tskin");
    auto it_seaconc = import_state.fields.find("DMS_seawater");
    auto it_dms_emis = export_state.fields.find("dms");

    if (it_u10 == import_state.fields.end() || it_tskin == import_state.fields.end() ||
        it_seaconc == import_state.fields.end() || it_dms_emis == export_state.fields.end())
        return;

    auto& dv_u10 = it_u10->second;
    auto& dv_tskin = it_tskin->second;
    auto& dv_seaconc = it_seaconc->second;
    auto& dv_dms_emis = it_dms_emis->second;

    dv_u10.sync<Kokkos::HostSpace>();
    dv_tskin.sync<Kokkos::HostSpace>();
    dv_seaconc.sync<Kokkos::HostSpace>();
    dv_dms_emis.sync<Kokkos::HostSpace>();

    int nx = static_cast<int>(dv_dms_emis.extent(0));
    int ny = static_cast<int>(dv_dms_emis.extent(1));
    int nz = static_cast<int>(dv_dms_emis.extent(2));

    run_dms_fortran(dv_u10.view_host().data(), dv_tskin.view_host().data(),
                    dv_seaconc.view_host().data(), dv_dms_emis.view_host().data(), nx, ny, nz);

    dv_dms_emis.modify<Kokkos::HostSpace>();
    dv_dms_emis.sync<Kokkos::DefaultExecutionSpace>();
}

}  // namespace cece
