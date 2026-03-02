#include <Kokkos_Core.hpp>
#include <iostream>

#include "aces/aces_physics_factory.hpp"
#include "aces/physics/aces_lightning_fortran.hpp"

extern "C" {
void run_lightning_fortran(double* conv_depth, double* light_nox, int nx, int ny, int nz);
}

namespace aces {

#ifdef ACES_HAS_FORTRAN
/// Self-registration for the LightningFortranScheme scheme.
static PhysicsRegistration<LightningFortranScheme> register_scheme("lightning_fortran");
#endif

void LightningFortranScheme::Initialize(const YAML::Node& /*config*/,
                                        AcesDiagnosticManager* /*diag_manager*/) {
    std::cout << "LightningFortranScheme: Initialized." << "\n";
}

void LightningFortranScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto it_conv_depth = import_state.fields.find("convective_cloud_top_height");
    auto it_light_emis = export_state.fields.find("total_lightning_nox_emissions");

    if (it_conv_depth == import_state.fields.end() || it_light_emis == export_state.fields.end())
        return;

    auto& dv_conv_depth = it_conv_depth->second;
    auto& dv_light_emis = it_light_emis->second;

    dv_conv_depth.sync<Kokkos::HostSpace>();
    dv_light_emis.sync<Kokkos::HostSpace>();

    int nx = dv_light_emis.extent(0);
    int ny = dv_light_emis.extent(1);
    int nz = dv_light_emis.extent(2);

    run_lightning_fortran(dv_conv_depth.view_host().data(), dv_light_emis.view_host().data(), nx,
                          ny, nz);

    dv_light_emis.modify<Kokkos::HostSpace>();
    dv_light_emis.sync<Kokkos::DefaultExecutionSpace>();
}

}  // namespace aces
