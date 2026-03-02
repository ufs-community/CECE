#include <Kokkos_Core.hpp>
#include <iostream>

#include "aces/aces_physics_factory.hpp"
#include "aces/physics/aces_megan_fortran.hpp"

extern "C" {
void run_megan_fortran(double* temp, double* lai, double* pardr, double* pardf, double* suncos,
                       double* isop, int nx, int ny, int nz);
}

namespace aces {

#ifdef ACES_HAS_FORTRAN
/// Self-registration for the MeganFortranScheme scheme.
static PhysicsRegistration<MeganFortranScheme> register_scheme("megan_fortran");
#endif

void MeganFortranScheme::Initialize(const YAML::Node& /*config*/,
                                    AcesDiagnosticManager* /*diag_manager*/) {
    std::cout << "MeganFortranScheme: Initialized." << "\n";
}

void MeganFortranScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto it_temp = import_state.fields.find("temperature");
    auto it_isop = export_state.fields.find("total_isoprene_emissions");
    auto it_lai = import_state.fields.find("lai");
    auto it_pardr = import_state.fields.find("pardr");
    auto it_pardf = import_state.fields.find("pardf");
    auto it_suncos = import_state.fields.find("suncos");

    if (it_temp == import_state.fields.end() || it_isop == export_state.fields.end() ||
        it_lai == import_state.fields.end() || it_pardr == import_state.fields.end() ||
        it_pardf == import_state.fields.end() || it_suncos == import_state.fields.end())
        return;

    auto& dv_temp = it_temp->second;
    auto& dv_isop = it_isop->second;
    auto& dv_lai = it_lai->second;
    auto& dv_pardr = it_pardr->second;
    auto& dv_pardf = it_pardf->second;
    auto& dv_suncos = it_suncos->second;

    dv_temp.sync<Kokkos::HostSpace>();
    dv_isop.sync<Kokkos::HostSpace>();
    dv_lai.sync<Kokkos::HostSpace>();
    dv_pardr.sync<Kokkos::HostSpace>();
    dv_pardf.sync<Kokkos::HostSpace>();
    dv_suncos.sync<Kokkos::HostSpace>();

    int nx = dv_isop.extent(0);
    int ny = dv_isop.extent(1);
    int nz = dv_isop.extent(2);

    run_megan_fortran(dv_temp.view_host().data(), dv_lai.view_host().data(),
                      dv_pardr.view_host().data(), dv_pardf.view_host().data(),
                      dv_suncos.view_host().data(), dv_isop.view_host().data(), nx, ny, nz);

    dv_isop.modify<Kokkos::HostSpace>();
    dv_isop.sync<Kokkos::DefaultExecutionSpace>();
}

}  // namespace aces
