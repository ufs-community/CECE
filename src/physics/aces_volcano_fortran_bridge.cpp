#include <Kokkos_Core.hpp>
#include <iostream>

#include "aces/aces_physics_factory.hpp"
#include "aces/physics/aces_volcano_fortran.hpp"

extern "C" {
void run_volcano_fortran(double* zsfc, double* bxheight, double* so2, int nx, int ny, int nz);
}

namespace aces {

#ifdef ACES_HAS_FORTRAN
/// Self-registration for the VolcanoFortranScheme scheme.
static PhysicsRegistration<VolcanoFortranScheme> register_scheme("volcano_fortran");
#endif

void VolcanoFortranScheme::Initialize(const YAML::Node& config,
                                      AcesDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);
    std::cout << "VolcanoFortranScheme: Initialized.\n";
}

void VolcanoFortranScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto it_so2 = export_state.fields.find("so2");
    auto it_zsfc = import_state.fields.find("zsfc");
    auto it_bxheight = import_state.fields.find("bxheight_m");

    if (it_so2 == export_state.fields.end() || it_zsfc == import_state.fields.end() ||
        it_bxheight == import_state.fields.end()) {
        return;
    }

    auto& dv_so2 = it_so2->second;
    auto& dv_zsfc = it_zsfc->second;
    auto& dv_bxheight = it_bxheight->second;

    dv_so2.sync<Kokkos::HostSpace>();
    dv_zsfc.sync<Kokkos::HostSpace>();
    dv_bxheight.sync<Kokkos::HostSpace>();

    int nx = static_cast<int>(dv_so2.extent(0));
    int ny = static_cast<int>(dv_so2.extent(1));
    int nz = static_cast<int>(dv_so2.extent(2));

    run_volcano_fortran(dv_zsfc.view_host().data(), dv_bxheight.view_host().data(),
                        dv_so2.view_host().data(), nx, ny, nz);

    dv_so2.modify<Kokkos::HostSpace>();
    dv_so2.sync<Kokkos::DefaultExecutionSpace>();
}

}  // namespace aces
