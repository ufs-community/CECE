/**
 * @file cece_ginoux_fortran_bridge.cpp
 * @brief Fortran bridge implementation for the Ginoux (GOCART2G) dust emission scheme.
 */
#include <Kokkos_Core.hpp>
#include <iostream>

#include "cece/cece_physics_factory.hpp"
#include "cece/physics/cece_ginoux_fortran.hpp"

extern "C" {
void run_ginoux_fortran(double* radius, double* fraclake, double* gwettop, double* oro, double* u10m, double* v10m, double* du_src, double* emissions,
                        int nx, int ny, int nbins, double Ch_DU, double grav);
}

namespace cece {

#ifdef CECE_HAS_FORTRAN
/// Self-registration for the GinouxFortranScheme scheme.
static PhysicsRegistration<GinouxFortranScheme> register_ginoux_fortran("ginoux_fortran");
#endif

void GinouxFortranScheme::Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);

    if (config["ch_du"]) ch_du_ = config["ch_du"].as<double>();
    if (config["grav"]) grav_ = config["grav"].as<double>();
    if (config["num_bins"]) num_bins_ = config["num_bins"].as<int>();

    std::cout << "GinouxFortranScheme: Initialized." << "\n";
}

void GinouxFortranScheme::Run(CeceImportState& import_state, CeceExportState& export_state) {
    // Resolve import fields
    auto it_u10m = import_state.fields.find("u10m");
    auto it_v10m = import_state.fields.find("v10m");
    auto it_gwettop = import_state.fields.find("surface_soil_wetness");
    auto it_oro = import_state.fields.find("land_mask");
    auto it_fraclake = import_state.fields.find("lake_fraction");
    auto it_du_src = import_state.fields.find("dust_source");
    auto it_radius = import_state.fields.find("particle_radius");

    // Resolve export field
    auto it_emis = export_state.fields.find("ginoux_dust_emissions");

    // Early return if any field is missing
    if (it_u10m == import_state.fields.end() || it_v10m == import_state.fields.end() || it_gwettop == import_state.fields.end() ||
        it_oro == import_state.fields.end() || it_fraclake == import_state.fields.end() || it_du_src == import_state.fields.end() ||
        it_radius == import_state.fields.end() || it_emis == export_state.fields.end())
        return;

    auto& dv_u10m = it_u10m->second;
    auto& dv_v10m = it_v10m->second;
    auto& dv_gwettop = it_gwettop->second;
    auto& dv_oro = it_oro->second;
    auto& dv_fraclake = it_fraclake->second;
    auto& dv_du_src = it_du_src->second;
    auto& dv_radius = it_radius->second;
    auto& dv_emis = it_emis->second;

    // Sync all imports to host
    dv_u10m.sync<Kokkos::HostSpace>();
    dv_v10m.sync<Kokkos::HostSpace>();
    dv_gwettop.sync<Kokkos::HostSpace>();
    dv_oro.sync<Kokkos::HostSpace>();
    dv_fraclake.sync<Kokkos::HostSpace>();
    dv_du_src.sync<Kokkos::HostSpace>();
    dv_radius.sync<Kokkos::HostSpace>();
    dv_emis.sync<Kokkos::HostSpace>();

    int nx = static_cast<int>(dv_emis.extent(0));
    int ny = static_cast<int>(dv_emis.extent(1));
    int nbins = static_cast<int>(dv_emis.extent(2));

    // Call Fortran kernel
    run_ginoux_fortran(dv_radius.view_host().data(), dv_fraclake.view_host().data(), dv_gwettop.view_host().data(), dv_oro.view_host().data(),
                       dv_u10m.view_host().data(), dv_v10m.view_host().data(), dv_du_src.view_host().data(), dv_emis.view_host().data(), nx, ny,
                       nbins, ch_du_, grav_);

    // Mark export modified on host and sync back to device
    dv_emis.modify<Kokkos::HostSpace>();
    dv_emis.sync<Kokkos::DefaultExecutionSpace>();
}

}  // namespace cece
