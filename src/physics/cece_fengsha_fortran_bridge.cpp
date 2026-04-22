/**
 * @file cece_fengsha_fortran_bridge.cpp
 * @brief Fortran bridge implementation for the FENGSHA dust emission scheme.
 */
#include <Kokkos_Core.hpp>
#include <cmath>
#include <iostream>
#include <vector>

#include "cece/cece_logger.hpp"
#include "cece/cece_physics_factory.hpp"
#include "cece/physics/cece_fengsha_fortran.hpp"

namespace {

/// @brief Compute Kok (2011) normalized dust aerosol size distribution.
inline std::vector<double> compute_kok_distribution(const std::vector<double>& radii, const std::vector<double>& lower_edges,
                                                    const std::vector<double>& upper_edges) {
    constexpr double mmd = 3.4;
    constexpr double stddev = 3.0;
    constexpr double lambda = 12.0;
    const double factor = 1.0 / (std::sqrt(2.0) * std::log(stddev));

    int nbins = static_cast<int>(radii.size());
    std::vector<double> dist(nbins, 0.0);
    double total = 0.0;

    for (int n = 0; n < nbins; ++n) {
        double diameter = 2.0 * radii[n] * 1e6;
        double rLow = lower_edges[n] * 1e6;
        double rUp = upper_edges[n] * 1e6;
        double dlam = diameter / lambda;
        dist[n] = diameter * (1.0 + std::erf(factor * std::log(diameter / mmd))) * std::exp(-dlam * dlam * dlam) * std::log(rUp / rLow);
        total += dist[n];
    }

    if (total > 0.0) {
        for (int n = 0; n < nbins; ++n) dist[n] /= total;
    }
    return dist;
}

}  // anonymous namespace

extern "C" {
void run_fengsha_fortran(double* ustar, double* uthrs, double* slc, double* clay, double* sand, double* silt, double* ssm, double* rdrag,
                         double* airdens, double* fraclake, double* fracsnow, double* oro, double* t_soil, double* emissions, int nx, int ny,
                         int nbins, double alpha, double gamma_param, double kvhmax, double grav, double drylimit_factor,
                         double frozen_soil_threshold, double* distribution);
}

namespace cece {

#ifdef CECE_HAS_FORTRAN
/// Self-registration for the FengshaFortranScheme scheme.
static PhysicsRegistration<FengshaFortranScheme> register_fengsha_fortran("fengsha_fortran");
#endif

void FengshaFortranScheme::Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);

    if (config["alpha"]) alpha_ = config["alpha"].as<double>();
    if (config["gamma"]) gamma_ = config["gamma"].as<double>();
    if (config["kvhmax"]) kvhmax_ = config["kvhmax"].as<double>();
    if (config["grav"]) grav_ = config["grav"].as<double>();
    if (config["drylimit_factor"]) drylimit_factor_ = config["drylimit_factor"].as<double>();
    if (config["frozen_soil_threshold"]) frozen_soil_threshold_ = config["frozen_soil_threshold"].as<double>();
    if (config["num_bins"]) num_bins_ = config["num_bins"].as<int>();

    // Compute Kok distribution from particle size parameters if provided
    if (config["particle_radii"] && config["bin_lower_edges"] && config["bin_upper_edges"]) {
        auto radii = config["particle_radii"].as<std::vector<double>>();
        auto lower = config["bin_lower_edges"].as<std::vector<double>>();
        auto upper = config["bin_upper_edges"].as<std::vector<double>>();
        num_bins_ = static_cast<int>(radii.size());

        bin_distribution_ = compute_kok_distribution(radii, lower, upper);
        has_custom_distribution_ = true;
    } else {
        // Fallback: hardcoded 5-bin Kok distribution for backward compatibility
        bin_distribution_ = {0.1, 0.25, 0.25, 0.25, 0.15};
        has_custom_distribution_ = false;
    }

    std::cout << "FengshaFortranScheme: Initialized." << "\n";
}

void FengshaFortranScheme::Run(CeceImportState& import_state, CeceExportState& export_state) {
    // Resolve import fields
    auto it_ustar = import_state.fields.find("friction_velocity");
    auto it_uthrs = import_state.fields.find("threshold_velocity");
    auto it_slc = import_state.fields.find("soil_moisture");
    auto it_clay = import_state.fields.find("clay_fraction");
    auto it_sand = import_state.fields.find("sand_fraction");
    auto it_silt = import_state.fields.find("silt_fraction");
    auto it_ssm = import_state.fields.find("erodibility");
    auto it_rdrag = import_state.fields.find("drag_partition");
    auto it_airdens = import_state.fields.find("air_density");
    auto it_fraclake = import_state.fields.find("lake_fraction");
    auto it_fracsnow = import_state.fields.find("snow_fraction");
    auto it_oro = import_state.fields.find("land_mask");
    auto it_t_soil = import_state.fields.find("soil_temperature");

    // Resolve export field
    auto it_emis = export_state.fields.find("fengsha_dust_emissions");

    // Early return if any field is missing
    if (it_ustar == import_state.fields.end() || it_uthrs == import_state.fields.end() || it_slc == import_state.fields.end() ||
        it_clay == import_state.fields.end() || it_sand == import_state.fields.end() || it_silt == import_state.fields.end() ||
        it_ssm == import_state.fields.end() || it_rdrag == import_state.fields.end() || it_airdens == import_state.fields.end() ||
        it_fraclake == import_state.fields.end() || it_fracsnow == import_state.fields.end() || it_oro == import_state.fields.end() ||
        it_emis == export_state.fields.end()) {
        CECE_LOG_WARNING("FengshaFortranScheme::Run: One or more required fields could not be resolved. Skipping execution.");
        return;
    }

    auto& dv_ustar = it_ustar->second;
    auto& dv_uthrs = it_uthrs->second;
    auto& dv_slc = it_slc->second;
    auto& dv_clay = it_clay->second;
    auto& dv_sand = it_sand->second;
    auto& dv_silt = it_silt->second;
    auto& dv_ssm = it_ssm->second;
    auto& dv_rdrag = it_rdrag->second;
    auto& dv_airdens = it_airdens->second;
    auto& dv_fraclake = it_fraclake->second;
    auto& dv_fracsnow = it_fracsnow->second;
    auto& dv_oro = it_oro->second;
    auto& dv_emis = it_emis->second;

    // Sync all imports to host
    dv_ustar.sync<Kokkos::HostSpace>();
    dv_uthrs.sync<Kokkos::HostSpace>();
    dv_slc.sync<Kokkos::HostSpace>();
    dv_clay.sync<Kokkos::HostSpace>();
    dv_sand.sync<Kokkos::HostSpace>();
    dv_silt.sync<Kokkos::HostSpace>();
    dv_ssm.sync<Kokkos::HostSpace>();
    dv_rdrag.sync<Kokkos::HostSpace>();
    dv_airdens.sync<Kokkos::HostSpace>();
    dv_fraclake.sync<Kokkos::HostSpace>();
    dv_fracsnow.sync<Kokkos::HostSpace>();
    dv_oro.sync<Kokkos::HostSpace>();
    dv_emis.sync<Kokkos::HostSpace>();

    int nx = static_cast<int>(dv_emis.extent(0));
    int ny = static_cast<int>(dv_emis.extent(1));
    int nbins = static_cast<int>(dv_emis.extent(2));

    // Prepare distribution for the Fortran kernel
    // If custom distribution, use it directly; otherwise use the default 5-bin
    // but size it to match nbins (pad with zeros if nbins > 5)
    std::vector<double> dist_for_fortran;
    if (has_custom_distribution_) {
        dist_for_fortran = bin_distribution_;
    } else {
        dist_for_fortran.resize(nbins, 0.0);
        int copy_size = std::min(nbins, static_cast<int>(bin_distribution_.size()));
        for (int n = 0; n < copy_size; ++n) {
            dist_for_fortran[n] = bin_distribution_[n];
        }
    }

    // Resolve optional soil temperature field
    double* t_soil_data = nullptr;
    if (it_t_soil != import_state.fields.end()) {
        auto& dv_t_soil = it_t_soil->second;
        dv_t_soil.sync<Kokkos::HostSpace>();
        t_soil_data = dv_t_soil.view_host().data();
    }

    // Call Fortran kernel
    run_fengsha_fortran(dv_ustar.view_host().data(), dv_uthrs.view_host().data(), dv_slc.view_host().data(), dv_clay.view_host().data(),
                        dv_sand.view_host().data(), dv_silt.view_host().data(), dv_ssm.view_host().data(), dv_rdrag.view_host().data(),
                        dv_airdens.view_host().data(), dv_fraclake.view_host().data(), dv_fracsnow.view_host().data(), dv_oro.view_host().data(),
                        t_soil_data, dv_emis.view_host().data(), nx, ny, nbins, alpha_, gamma_, kvhmax_, grav_, drylimit_factor_,
                        frozen_soil_threshold_, dist_for_fortran.data());

    // Mark export modified on host and sync back to device
    dv_emis.modify<Kokkos::HostSpace>();
    dv_emis.sync<Kokkos::DefaultExecutionSpace>();
}

}  // namespace cece
