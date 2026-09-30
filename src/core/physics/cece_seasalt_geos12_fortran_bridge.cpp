/**
 * @file cece_seasalt_geos12_fortran_bridge.cpp
 * @brief Fortran bridge for the GEOS-12 (GEOS-Chem 2012) sea salt emission scheme.
 *
 * Parses per-bin size properties (density, lower/upper/effective dry radius)
 * from YAML, syncs the required met fields to host, calls the Fortran kernel
 * run_seasalt_geos12_fortran, and publishes per-bin mass and number emissions
 * as export fields. Mass, number, their column totals, and the per-bin
 * effective radius are additionally mirrored into diagnostic fields.
 *
 * Registered as "sea_salt_geos12_fortran" (guarded by CECE_HAS_FORTRAN).
 *
 * @author CECE Team
 * @date 2026
 */

#include <Kokkos_Core.hpp>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cece/cece_physics_factory.hpp"
#include "cece/physics/cece_seasalt_geos12_fortran.hpp"
#include "cece/physics/cece_speciation_config.hpp"

extern "C" {
/**
 * @brief External Fortran subroutine for GEOS-12 sea salt emission calculations.
 *
 * @param frocean      Ocean fraction [1] (nx, ny)
 * @param frseaice     Sea-ice fraction [1] (nx, ny)
 * @param lat          Latitude [deg] (nx, ny)
 * @param lon          Longitude [deg] (nx, ny)
 * @param sst          Sea surface temperature [K] (nx, ny)
 * @param u10m         10-m eastward wind [m/s] (nx, ny)
 * @param v10m         10-m northward wind [m/s] (nx, ny)
 * @param ustar        Friction velocity [m/s] (nx, ny)
 * @param density      Per-bin dry particle density [kg/m^3] (ns)
 * @param r_low        Per-bin lower dry radius [um] (ns)
 * @param r_up         Per-bin upper dry radius [um] (ns)
 * @param mass_emis    Output mass emission flux [kg/m^2/s] (nx, ny, ns)
 * @param number_emis  Output number emission flux [#/m^2/s] (nx, ny, ns)
 * @param nx, ny, ns   Grid dimensions and bin count
 * @param weibull_flag Enable Weibull wind correction (0/1)
 * @param scale_factor Global tuning scale factor [1]
 * @param pi           Value of pi
 */
void run_seasalt_geos12_fortran(double* frocean, double* frseaice, double* lat, double* lon, double* sst, double* u10m, double* v10m, double* ustar,
                                double* density, double* r_low, double* r_up, double* mass_emis, double* number_emis, int nx, int ny, int ns,
                                int weibull_flag, double scale_factor, double pi);
}

namespace cece {

#ifdef CECE_HAS_FORTRAN
/// @brief Self-registration for the GEOS-12 sea salt Fortran bridge scheme.
static PhysicsRegistration<SeaSaltGeos12FortranScheme> register_scheme("sea_salt_geos12_fortran");
#endif

// ============================================================================
// Initialize
// ============================================================================

void SeaSaltGeos12FortranScheme::Initialize(const conf::Value& config, CeceDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);

    if (config["weibull_flag"].is_defined()) weibull_flag_ = config["weibull_flag"].as_bool();
    if (config["scale_factor"].is_defined()) scale_factor_ = config["scale_factor"].as_double();

    // Per-bin properties come from the shared MICM mechanism file; the map's
    // aerosol dataset selects which bins (and their emission scale) to emit,
    // using the same nested speciation format and loader as the gas schemes.
    std::string mechanism_file = "data/speciation/spc_cb6_gocart.yaml";
    std::string speciation_file = "data/speciation/map_cb6_gocart.yaml";
    std::string dataset = "SEASALT";
    if (config["mechanism_file"].is_defined()) mechanism_file = config["mechanism_file"].as_string();
    if (config["speciation_file"].is_defined()) speciation_file = config["speciation_file"].as_string();
    if (config["speciation_dataset"].is_defined()) dataset = config["speciation_dataset"].as_string();

    SpeciationConfigLoader loader;
    const SpeciationConfig spec = loader.Load(mechanism_file, speciation_file, dataset);

    // Index mechanism species by name for property lookup.
    std::unordered_map<std::string, const MechanismSpecies*> by_name;
    for (const auto& sp : spec.species) {
        by_name[sp.name] = &sp;
    }

    // Each aerosol bin maps to itself (identity); collect the target species in
    // mapping order, deduplicated, pulling size properties from the mechanism.
    species_names_.clear();
    species_density_.clear();
    species_lower_radius_.clear();
    species_upper_radius_.clear();
    species_radius_.clear();
    species_scale_.clear();
    std::unordered_set<std::string> seen;
    for (const auto& mapping : spec.mappings) {
        if (seen.count(mapping.mechanism_species)) continue;
        auto it = by_name.find(mapping.mechanism_species);
        if (it == by_name.end() || !it->second->is_aerosol) continue;
        seen.insert(mapping.mechanism_species);
        const MechanismSpecies* sp = it->second;
        species_names_.push_back(sp->name);
        species_density_.push_back(sp->density);
        species_lower_radius_.push_back(sp->lower_radius);
        species_upper_radius_.push_back(sp->upper_radius);
        species_radius_.push_back(sp->effective_radius);
        species_scale_.push_back(mapping.scale_factor);
    }
    num_species_ = static_cast<int>(species_names_.size());
    if (num_species_ == 0) {
        throw std::invalid_argument("SeaSaltGeos12FortranScheme: aerosol dataset '" + dataset + "' selected no aerosol bins");
    }

    std::cout << "SeaSaltGeos12FortranScheme: Initialized " << num_species_ << " bins from '" << mechanism_file << "' + '" << speciation_file
              << "' (dataset " << dataset << "), weibull_flag=" << weibull_flag_ << ", scale_factor=" << scale_factor_ << ". Export fields:";
    for (const auto& name : species_names_) {
        std::cout << " seasalt_mass_" << name << " seasalt_number_" << name;
    }
    std::cout << " seasalt_mass_total seasalt_number_total\n";
}

// ============================================================================
// Run
// ============================================================================

void SeaSaltGeos12FortranScheme::Run(CeceImportState& import_state, CeceExportState& export_state) {
    // Resolve required met imports and sync them to host.
    auto get_import = [&](const std::string& internal) -> double* {
        auto it = import_state.fields.find(MapInput(internal));
        if (it == import_state.fields.end()) return nullptr;
        it->second.sync<Kokkos::HostSpace>();
        return it->second.view_host().data();
    };

    double* frocean = get_import("frocean");
    double* frseaice = get_import("frseaice");
    double* lat = get_import("lat");
    double* lon = get_import("lon");
    double* sst = get_import("sst");
    double* u10m = get_import("u10m");
    double* v10m = get_import("v10m");
    double* ustar = get_import("ustar");

    if (frocean == nullptr || frseaice == nullptr || lat == nullptr || lon == nullptr || sst == nullptr || u10m == nullptr || v10m == nullptr ||
        ustar == nullptr) {
        return;
    }

    // Grid dimensions come from a met field; bins are a scheme-internal axis and
    // are NOT tied to the grid vertical dimension.
    auto& dv_frocean = import_state.fields.at(MapInput("frocean"));
    const int nx = static_cast<int>(dv_frocean.extent(0));
    const int ny = static_cast<int>(dv_frocean.extent(1));
    const int ns = num_species_;
    const std::size_t slab = static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny);

    // The Fortran kernel fills contiguous (nx, ny, ns) mass/number buffers; the
    // per-bin slabs are then scattered into individual export fields.
    std::vector<double> mass(slab * static_cast<std::size_t>(ns), 0.0);
    std::vector<double> number(slab * static_cast<std::size_t>(ns), 0.0);

    run_seasalt_geos12_fortran(frocean, frseaice, lat, lon, sst, u10m, v10m, ustar, species_density_.data(), species_lower_radius_.data(),
                               species_upper_radius_.data(), mass.data(), number.data(), nx, ny, ns, weibull_flag_ ? 1 : 0, scale_factor_, M_PI);

    // Write a per-cell surface slab (bin n) into level 0 of an export field.
    auto write_slab = [&](const std::string& internal, const double* src) {
        auto it = export_state.fields.find(MapOutput(internal));
        if (it == export_state.fields.end()) return;
        auto& dv = it->second;
        dv.sync<Kokkos::HostSpace>();
        std::memcpy(dv.view_host().data(), src, sizeof(double) * slab);
        dv.modify<Kokkos::HostSpace>();
        dv.sync<Kokkos::DefaultExecutionSpace>();
    };

    std::vector<double> mass_total(slab, 0.0);
    std::vector<double> number_total(slab, 0.0);
    for (int n = 0; n < ns; ++n) {
        double* mass_slab = mass.data() + static_cast<std::size_t>(n) * slab;
        double* number_slab = number.data() + static_cast<std::size_t>(n) * slab;
        // Apply the per-bin emission scale from the aerosol map (identity = 1.0).
        const double sc = species_scale_[static_cast<std::size_t>(n)];
        if (sc != 1.0) {
            for (std::size_t c = 0; c < slab; ++c) {
                mass_slab[c] *= sc;
                number_slab[c] *= sc;
            }
        }
        write_slab("seasalt_mass_" + species_names_[static_cast<std::size_t>(n)], mass_slab);
        write_slab("seasalt_number_" + species_names_[static_cast<std::size_t>(n)], number_slab);
        for (std::size_t c = 0; c < slab; ++c) {
            mass_total[c] += mass_slab[c];
            number_total[c] += number_slab[c];
        }
    }
    write_slab("seasalt_mass_total", mass_total.data());
    write_slab("seasalt_number_total", number_total.data());

    // Static per-bin effective radius as a diagnostic (decoupled from grid nz).
    diag_effective_radius_ = ResolveDiagnostic("seasalt_effective_radius", nx, ny, ns, "um", "GEOS-12 sea salt effective dry radius per bin");
    if (diag_effective_radius_.view_host().data() == nullptr || diag_effective_radius_.extent(0) != static_cast<std::size_t>(nx) ||
        diag_effective_radius_.extent(1) != static_cast<std::size_t>(ny) || diag_effective_radius_.extent(2) != static_cast<std::size_t>(ns)) {
        return;
    }
    auto d_radius = diag_effective_radius_.view_host();
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int n = 0; n < ns; ++n) {
                d_radius(i, j, n) = species_radius_[static_cast<std::size_t>(n)];
            }
        }
    }
    diag_effective_radius_.modify<Kokkos::HostSpace>();
    diag_effective_radius_.sync<Kokkos::DefaultExecutionSpace>();
}

}  // namespace cece
