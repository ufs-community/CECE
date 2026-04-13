/**
 * @file cece_k14_fortran_bridge.cpp
 * @brief Fortran bridge implementation for the K14 (Kok et al., 2014) dust emission scheme.
 */
#include <Kokkos_Core.hpp>
#include <iostream>

#include "cece/cece_physics_factory.hpp"
#include "cece/physics/cece_k14_fortran.hpp"

extern "C" {
void run_k14_fortran(
    double* t_soil, double* w_top, double* rho_air, double* z0,
    double* z, double* u_z, double* v_z, double* ustar,
    double* f_land, double* f_snow, double* f_src,
    double* f_sand, double* f_silt, double* f_clay,
    double* texture, double* vegetation, double* gvf,
    double* emissions,
    int nx, int ny, int nbins, int km,
    double f_w, double f_c, double uts_gamma,
    double UNDEF, double GRAV, double VON_KARMAN,
    int opt_clay, double Ch_DU);
}

namespace cece {

#ifdef CECE_HAS_FORTRAN
/// Self-registration for the K14FortranScheme scheme.
static PhysicsRegistration<K14FortranScheme> register_k14_fortran("k14_fortran");
#endif

void K14FortranScheme::Initialize(const YAML::Node& config,
                                  CeceDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);

    if (config["ch_du"]) ch_du_ = config["ch_du"].as<double>();
    if (config["f_w"]) f_w_ = config["f_w"].as<double>();
    if (config["f_c"]) f_c_ = config["f_c"].as<double>();
    if (config["uts_gamma"]) uts_gamma_ = config["uts_gamma"].as<double>();
    if (config["undef"]) undef_ = config["undef"].as<double>();
    if (config["grav"]) grav_ = config["grav"].as<double>();
    if (config["von_karman"]) von_karman_ = config["von_karman"].as<double>();
    if (config["opt_clay"]) opt_clay_ = config["opt_clay"].as<int>();
    if (config["num_bins"]) num_bins_ = config["num_bins"].as<int>();

    std::cout << "K14FortranScheme: Initialized." << "\n";
}

void K14FortranScheme::Run(CeceImportState& import_state, CeceExportState& export_state) {
    // Resolve import fields
    auto it_ustar = import_state.fields.find("friction_velocity");
    auto it_t_soil = import_state.fields.find("soil_temperature");
    auto it_w_top = import_state.fields.find("volumetric_soil_moisture");
    auto it_rho_air = import_state.fields.find("air_density");
    auto it_z0 = import_state.fields.find("roughness_length");
    auto it_z = import_state.fields.find("height");
    auto it_u_z = import_state.fields.find("u_wind");
    auto it_v_z = import_state.fields.find("v_wind");
    auto it_f_land = import_state.fields.find("land_fraction");
    auto it_f_snow = import_state.fields.find("snow_fraction");
    auto it_f_src = import_state.fields.find("dust_source");
    auto it_f_sand = import_state.fields.find("sand_fraction");
    auto it_f_silt = import_state.fields.find("silt_fraction");
    auto it_f_clay = import_state.fields.find("clay_fraction");
    auto it_texture = import_state.fields.find("soil_texture");
    auto it_vegetation = import_state.fields.find("vegetation_type");
    auto it_gvf = import_state.fields.find("vegetation_fraction");

    // Resolve export field
    auto it_emis = export_state.fields.find("k14_dust_emissions");

    // Early return if any field is missing
    if (it_ustar == import_state.fields.end() || it_t_soil == import_state.fields.end() ||
        it_w_top == import_state.fields.end() || it_rho_air == import_state.fields.end() ||
        it_z0 == import_state.fields.end() || it_z == import_state.fields.end() ||
        it_u_z == import_state.fields.end() || it_v_z == import_state.fields.end() ||
        it_f_land == import_state.fields.end() || it_f_snow == import_state.fields.end() ||
        it_f_src == import_state.fields.end() || it_f_sand == import_state.fields.end() ||
        it_f_silt == import_state.fields.end() || it_f_clay == import_state.fields.end() ||
        it_texture == import_state.fields.end() || it_vegetation == import_state.fields.end() ||
        it_gvf == import_state.fields.end() || it_emis == export_state.fields.end())
        return;

    auto& dv_ustar = it_ustar->second;
    auto& dv_t_soil = it_t_soil->second;
    auto& dv_w_top = it_w_top->second;
    auto& dv_rho_air = it_rho_air->second;
    auto& dv_z0 = it_z0->second;
    auto& dv_z = it_z->second;
    auto& dv_u_z = it_u_z->second;
    auto& dv_v_z = it_v_z->second;
    auto& dv_f_land = it_f_land->second;
    auto& dv_f_snow = it_f_snow->second;
    auto& dv_f_src = it_f_src->second;
    auto& dv_f_sand = it_f_sand->second;
    auto& dv_f_silt = it_f_silt->second;
    auto& dv_f_clay = it_f_clay->second;
    auto& dv_texture = it_texture->second;
    auto& dv_vegetation = it_vegetation->second;
    auto& dv_gvf = it_gvf->second;
    auto& dv_emis = it_emis->second;

    // Sync all imports to host
    dv_ustar.sync<Kokkos::HostSpace>();
    dv_t_soil.sync<Kokkos::HostSpace>();
    dv_w_top.sync<Kokkos::HostSpace>();
    dv_rho_air.sync<Kokkos::HostSpace>();
    dv_z0.sync<Kokkos::HostSpace>();
    dv_z.sync<Kokkos::HostSpace>();
    dv_u_z.sync<Kokkos::HostSpace>();
    dv_v_z.sync<Kokkos::HostSpace>();
    dv_f_land.sync<Kokkos::HostSpace>();
    dv_f_snow.sync<Kokkos::HostSpace>();
    dv_f_src.sync<Kokkos::HostSpace>();
    dv_f_sand.sync<Kokkos::HostSpace>();
    dv_f_silt.sync<Kokkos::HostSpace>();
    dv_f_clay.sync<Kokkos::HostSpace>();
    dv_texture.sync<Kokkos::HostSpace>();
    dv_vegetation.sync<Kokkos::HostSpace>();
    dv_gvf.sync<Kokkos::HostSpace>();
    dv_emis.sync<Kokkos::HostSpace>();

    int nx = static_cast<int>(dv_emis.extent(0));
    int ny = static_cast<int>(dv_emis.extent(1));
    int nbins = static_cast<int>(dv_emis.extent(2));
    int km = 1;

    // Call Fortran kernel
    run_k14_fortran(dv_t_soil.view_host().data(), dv_w_top.view_host().data(),
                    dv_rho_air.view_host().data(), dv_z0.view_host().data(),
                    dv_z.view_host().data(), dv_u_z.view_host().data(),
                    dv_v_z.view_host().data(), dv_ustar.view_host().data(),
                    dv_f_land.view_host().data(), dv_f_snow.view_host().data(),
                    dv_f_src.view_host().data(), dv_f_sand.view_host().data(),
                    dv_f_silt.view_host().data(), dv_f_clay.view_host().data(),
                    dv_texture.view_host().data(), dv_vegetation.view_host().data(),
                    dv_gvf.view_host().data(), dv_emis.view_host().data(),
                    nx, ny, nbins, km,
                    f_w_, f_c_, uts_gamma_,
                    undef_, grav_, von_karman_,
                    opt_clay_, ch_du_);

    // Mark export modified on host and sync back to device
    dv_emis.modify<Kokkos::HostSpace>();
    dv_emis.sync<Kokkos::DefaultExecutionSpace>();
}

}  // namespace cece
