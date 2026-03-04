#include <Kokkos_Core.hpp>
#include <iostream>

#include "aces/aces_physics_factory.hpp"
#include "aces/physics/aces_sea_salt_fortran.hpp"

extern "C" {
void run_sea_salt_fortran(double* u10m_ptr, double* tskin_ptr, double* sala_ptr, double* salc_ptr,
                          int nx, int ny, int nz);
}

namespace aces {

#ifdef ACES_HAS_FORTRAN
/// Self-registration for the SeaSaltFortranScheme scheme.
static PhysicsRegistration<SeaSaltFortranScheme> register_scheme("sea_salt_fortran");
#endif

void SeaSaltFortranScheme::Initialize(const YAML::Node& /*config*/,
                                      AcesDiagnosticManager* /*diag_manager*/) {
    std::cout << "SeaSaltFortranScheme: Initialized." << "\n";
}

void SeaSaltFortranScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto it_u10 = import_state.fields.find("wind_speed_10m");
    auto it_tskin = import_state.fields.find("tskin");
    auto it_sala = export_state.fields.find("total_SALA_emissions");
    auto it_salc = export_state.fields.find("total_SALC_emissions");

    if (it_u10 == import_state.fields.end() || it_tskin == import_state.fields.end()) {
        return;
    }

    auto& dv_u10 = it_u10->second;
    auto& dv_tskin = it_tskin->second;

    dv_u10.sync<Kokkos::HostSpace>();
    dv_tskin.sync<Kokkos::HostSpace>();

    int nx = static_cast<int>(dv_u10.extent(0));
    int ny = static_cast<int>(dv_u10.extent(1));
    int nz = static_cast<int>(dv_u10.extent(2));

    double* sala_ptr = nullptr;
    double* salc_ptr = nullptr;

    if (it_sala != export_state.fields.end()) {
        it_sala->second.sync<Kokkos::HostSpace>();
        sala_ptr = it_sala->second.view_host().data();
    }
    if (it_salc != export_state.fields.end()) {
        it_salc->second.sync<Kokkos::HostSpace>();
        salc_ptr = it_salc->second.view_host().data();
    }

    run_sea_salt_fortran(dv_u10.view_host().data(), dv_tskin.view_host().data(), sala_ptr, salc_ptr,
                         nx, ny, nz);

    if (it_sala != export_state.fields.end()) {
        it_sala->second.modify<Kokkos::HostSpace>();
        it_sala->second.sync<Kokkos::DefaultExecutionSpace>();
    }
    if (it_salc != export_state.fields.end()) {
        it_salc->second.modify<Kokkos::HostSpace>();
        it_salc->second.sync<Kokkos::DefaultExecutionSpace>();
    }
}

}  // namespace aces
