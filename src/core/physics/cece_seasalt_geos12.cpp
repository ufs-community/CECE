/**
 * @file cece_seasalt_geos12.cpp
 * @brief Native Kokkos implementation of the GEOS-12 sea salt emission scheme.
 *
 * GPU-portable scheme implementing the Jaeglé et al. 2011 SST correction, the
 * Gong 2003 source function, an optional Fan & Toon 2011 Weibull wind correction,
 * the Great Lakes/Caspian deep-lake mask, and NR=10 sub-bin integration. Met
 * inputs are read as device views and the per-bin results are written on the
 * device, avoiding a host round-trip.
 *
 * Registered as "sea_salt_geos12" (always compiled; no Fortran dependency).
 *
 * @author CECE Team
 * @date 2026
 */

#include "cece/physics/cece_seasalt_geos12.hpp"

#include <Kokkos_Core.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cece/cece_physics_factory.hpp"
#include "cece/physics/cece_speciation_config.hpp"

namespace {

// Fixed GEOS-12 Gong (2003) source-function parameters (match the Fortran kernel).
constexpr double SS_SCALEFAC = 33.0e3;
constexpr double SS_RPOW = 3.45;
constexpr double SS_EXPPOW = 1.607;
constexpr double SS_WPOW = 3.41 - 1.0;
constexpr int SS_NR = 10;           // linear dry sub-bins
constexpr double SS_R80FAC = 1.65;  // r(RH=0.8)/r(dry) [Gerber]

/// @brief Gong (2003) size- and wind-dependent sea salt source function.
KOKKOS_INLINE_FUNCTION
double seasalt_emission_gong(double r, double dr, double w, double scalefac_in, double afac, double bfac) {
    double emis = scalefac_in * 1.373 * Kokkos::pow(r, -afac) * (1.0 + 0.057 * Kokkos::pow(r, SS_RPOW)) *
                  Kokkos::pow(10.0, SS_EXPPOW * Kokkos::exp(-bfac * bfac)) * dr;
    return Kokkos::pow(w, SS_WPOW) * emis;
}

/// @brief Jaeglé et al. (2011) SST correction (temperature-range branch).
KOKKOS_INLINE_FUNCTION
double jeagle_sst_correction(double sst) {
    double tc = sst - 273.15;
    tc = Kokkos::fmax(-0.1, tc);
    tc = Kokkos::fmin(36.0, tc);
    double f = -1.107211 - 0.010681 * tc - 0.002276 * tc * tc + 60.288927 * 1.0 / (40.0 - tc);
    f = Kokkos::fmax(0.0, f);
    f = Kokkos::fmin(7.0, f);
    return f;
}

/// @brief Upper incomplete Gamma function used by the Weibull correction.
/// @param[out] ok false if the argument overflows (mirrors the Fortran rc/=0 skip).
KOKKOS_INLINE_FUNCTION
double ss_igamma(double a, double x, bool& ok) {
    ok = true;
    double xam = -x + a * Kokkos::log(x);
    if (xam > 700.0 || a > 170.0) {
        ok = false;
        return 0.0;
    }
    double gout = 0.0;
    if (Kokkos::fabs(x) < 1.0e-30) {
        gout = Kokkos::tgamma(a);
    } else if (x <= 1.0 + a) {
        double s = 1.0 / a;
        double r = s;
        for (int k = 1; k <= 60; ++k) {
            r = r * x / (a + k);
            s = s + r;
            if (Kokkos::fabs(r / s) < 1.0e-15) break;
        }
        double gin = Kokkos::exp(xam) * s;
        gout = Kokkos::tgamma(a) - gin;
    } else {
        double t0 = 0.0;
        for (int k = 60; k >= 1; --k) {
            t0 = (k - a) / (1.0 + k / (x + t0));
        }
        gout = Kokkos::exp(xam) / (x + t0);
    }
    return gout;
}

/// @brief Fan & Toon (2011) Weibull wind-speed correction factor.
/// @param[out] ok false if the incomplete-gamma evaluation overflows.
KOKKOS_INLINE_FUNCTION
double weibull_factor(bool on, double wm, bool& ok) {
    ok = true;
    double gweibull = 1.0;
    constexpr double wt = 4.0;
    if (on) {
        gweibull = 0.0;
        if (wm > 0.01) {
            double k = 0.94 * Kokkos::sqrt(wm);
            double c = wm / Kokkos::tgamma(1.0 + 1.0 / k);
            double x = Kokkos::pow(wt / c, k);
            double a = 3.41 / k + 1.0;
            gweibull = Kokkos::pow(c / wm, 3.41) * ss_igamma(a, x, ok);
        }
    }
    return gweibull;
}

}  // namespace

namespace cece {

/// @brief Self-registration for the native GEOS-12 sea salt scheme.
static PhysicsRegistration<SeaSaltGeos12Scheme> register_scheme("sea_salt_geos12");

// ============================================================================
// Initialize
// ============================================================================

void SeaSaltGeos12Scheme::Initialize(const conf::Value& config, CeceDiagnosticManager* diag_manager) {
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
    species_radius_.clear();
    std::vector<double> density;
    std::vector<double> rlow;
    std::vector<double> rup;
    std::vector<double> scale;
    std::unordered_set<std::string> seen;
    for (const auto& mapping : spec.mappings) {
        if (seen.count(mapping.mechanism_species)) continue;
        auto it = by_name.find(mapping.mechanism_species);
        if (it == by_name.end() || !it->second->is_aerosol) continue;
        seen.insert(mapping.mechanism_species);
        const MechanismSpecies* sp = it->second;
        species_names_.push_back(sp->name);
        species_radius_.push_back(sp->effective_radius);
        density.push_back(sp->density);
        rlow.push_back(sp->lower_radius);
        rup.push_back(sp->upper_radius);
        scale.push_back(mapping.scale_factor);
    }
    num_species_ = static_cast<int>(species_names_.size());
    if (num_species_ == 0) {
        throw std::invalid_argument("SeaSaltGeos12Scheme: aerosol dataset '" + dataset + "' selected no aerosol bins");
    }

    // Mirror the per-bin properties onto the device for the compute kernel.
    density_d_ = Kokkos::View<double*, Kokkos::DefaultExecutionSpace>("ss_density", num_species_);
    rlow_d_ = Kokkos::View<double*, Kokkos::DefaultExecutionSpace>("ss_rlow", num_species_);
    rup_d_ = Kokkos::View<double*, Kokkos::DefaultExecutionSpace>("ss_rup", num_species_);
    scale_d_ = Kokkos::View<double*, Kokkos::DefaultExecutionSpace>("ss_scale", num_species_);
    auto h_density = Kokkos::create_mirror_view(density_d_);
    auto h_rlow = Kokkos::create_mirror_view(rlow_d_);
    auto h_rup = Kokkos::create_mirror_view(rup_d_);
    auto h_scale = Kokkos::create_mirror_view(scale_d_);
    for (int n = 0; n < num_species_; ++n) {
        h_density(n) = density[static_cast<std::size_t>(n)];
        h_rlow(n) = rlow[static_cast<std::size_t>(n)];
        h_rup(n) = rup[static_cast<std::size_t>(n)];
        h_scale(n) = scale[static_cast<std::size_t>(n)];
    }
    Kokkos::deep_copy(density_d_, h_density);
    Kokkos::deep_copy(rlow_d_, h_rlow);
    Kokkos::deep_copy(rup_d_, h_rup);
    Kokkos::deep_copy(scale_d_, h_scale);

    std::cout << "SeaSaltGeos12Scheme: Initialized " << num_species_ << " bins from '" << mechanism_file << "' + '" << speciation_file
              << "' (dataset " << dataset << "), weibull_flag=" << weibull_flag_ << ", scale_factor=" << scale_factor_ << ". Export fields:";
    for (const auto& name : species_names_) {
        std::cout << " seasalt_mass_" << name << " seasalt_number_" << name;
    }
    std::cout << " seasalt_mass_total seasalt_number_total\n";
}

// ============================================================================
// Run
// ============================================================================

void SeaSaltGeos12Scheme::Run(CeceImportState& import_state, CeceExportState& export_state) {
    auto frocean = ResolveImport("frocean", import_state);
    auto frseaice = ResolveImport("frseaice", import_state);
    auto lat = ResolveImport("lat", import_state);
    auto lon = ResolveImport("lon", import_state);
    auto sst = ResolveImport("sst", import_state);
    auto u10m = ResolveImport("u10m", import_state);
    auto v10m = ResolveImport("v10m", import_state);
    auto ustar = ResolveImport("ustar", import_state);

    if (frocean.data() == nullptr || frseaice.data() == nullptr || lat.data() == nullptr || lon.data() == nullptr || sst.data() == nullptr ||
        u10m.data() == nullptr || v10m.data() == nullptr || ustar.data() == nullptr) {
        return;
    }

    const int nx = static_cast<int>(frocean.extent(0));
    const int ny = static_cast<int>(frocean.extent(1));
    const int ns = num_species_;

    // (Re)allocate the per-bin scratch buffers if the grid changed.
    if (static_cast<int>(mass_scratch_.extent(0)) != nx || static_cast<int>(mass_scratch_.extent(1)) != ny ||
        static_cast<int>(mass_scratch_.extent(2)) != ns) {
        mass_scratch_ = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>("ss_mass_scratch", nx, ny, ns);
        number_scratch_ = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>("ss_number_scratch", nx, ny, ns);
    }

    // Optional column-total export fields (written directly in the kernel).
    auto mass_total = ResolveExport("seasalt_mass_total", export_state);
    auto number_total = ResolveExport("seasalt_number_total", export_state);
    const bool has_mass_total = mass_total.data() != nullptr;
    const bool has_number_total = number_total.data() != nullptr;

    // Capture members/parameters for the device lambda.
    auto mass_s = mass_scratch_;
    auto number_s = number_scratch_;
    auto density = density_d_;
    auto rlow = rlow_d_;
    auto rup = rup_d_;
    auto scale_bin = scale_d_;
    const bool do_weibull = weibull_flag_;
    const double scale_factor = scale_factor_;
    const double pi = M_PI;

    Kokkos::parallel_for(
        "SeaSaltGeos12Kernel", Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<2>>({0, 0}, {nx, ny}), KOKKOS_LAMBDA(int i, int j) {
            for (int n = 0; n < ns; ++n) {
                mass_s(i, j, n) = 0.0;
                number_s(i, j, n) = 0.0;
            }
            if (has_mass_total) mass_total(i, j, 0) = 0.0;
            if (has_number_total) number_total(i, j, 0) = 0.0;

            // Skip cells with no open ocean.
            double ocean_frac = frocean(i, j, 0) - frseaice(i, j, 0);
            if (ocean_frac <= 0.0) return;

            // 10-m mean wind speed (used only for the Weibull correction).
            double w10m = Kokkos::sqrt(u10m(i, j, 0) * u10m(i, j, 0) + v10m(i, j, 0) * v10m(i, j, 0));

            bool ok = true;
            double gweibull = weibull_factor(do_weibull, w10m, ok);
            if (!ok) return;

            double fsstemis = jeagle_sst_correction(sst(i, j, 0));

            // Deep-lakes mask (Great Lakes and Caspian Sea).
            double deep_lakes_mask = 1.0;
            double dummylon = lon(i, j, 0);
            if (dummylon < 0.0) dummylon = dummylon + 360.0;
            double latij = lat(i, j, 0);
            if (latij >= 40.5 && latij <= 50.0 && dummylon >= 267.0 && dummylon <= 285.0) deep_lakes_mask = 0.0;
            if (latij >= 35.0 && latij <= 48.0 && dummylon >= 45.0 && dummylon <= 56.0) deep_lakes_mask = 0.0;

            double scale = Kokkos::fmin(Kokkos::fmax(0.0, ocean_frac * deep_lakes_mask), 1.0) * gweibull * fsstemis * scale_factor;

            double ustar_ij = ustar(i, j, 0);
            double mtot = 0.0;
            double ntot = 0.0;
            for (int n = 0; n < ns; ++n) {
                double delta_dry = (rup(n) - rlow(n)) / static_cast<double>(SS_NR);
                double dry_radius = rlow(n) + 0.5 * delta_dry;

                double mass_acc = 0.0;
                double number_acc = 0.0;
                for (int ir = 0; ir < SS_NR; ++ir) {
                    double rwet = SS_R80FAC * dry_radius;
                    double drwet = SS_R80FAC * delta_dry;

                    double afac = 4.7 * Kokkos::pow(1.0 + 30.0 * rwet, -0.017 * Kokkos::pow(rwet, -1.44));
                    double bfac = (0.433 - Kokkos::log10(rwet)) / 0.433;

                    double mass_scale = SS_SCALEFAC * 4.0 / 3.0 * pi * density(n) * (dry_radius * dry_radius * dry_radius) * 1.0e-18;

                    number_acc += seasalt_emission_gong(rwet, drwet, ustar_ij, SS_SCALEFAC, afac, bfac);
                    mass_acc += seasalt_emission_gong(rwet, drwet, ustar_ij, mass_scale, afac, bfac);

                    dry_radius += delta_dry;
                }

                double sc = scale_bin(n);
                double m = Kokkos::fmax(0.0, mass_acc * scale) * sc;
                double num = Kokkos::fmax(0.0, number_acc * scale) * sc;
                mass_s(i, j, n) = m;
                number_s(i, j, n) = num;
                mtot += m;
                ntot += num;
            }
            if (has_mass_total) mass_total(i, j, 0) = mtot;
            if (has_number_total) number_total(i, j, 0) = ntot;
        });
    Kokkos::fence();

    if (has_mass_total) MarkModified("seasalt_mass_total", export_state);
    if (has_number_total) MarkModified("seasalt_number_total", export_state);

    // Scatter each per-bin slab into its named export field (device-to-device).
    for (int n = 0; n < ns; ++n) {
        const std::string& bin = species_names_[static_cast<std::size_t>(n)];
        auto mass_field = ResolveExport("seasalt_mass_" + bin, export_state);
        if (mass_field.data() != nullptr) {
            Kokkos::deep_copy(Kokkos::subview(mass_field, Kokkos::ALL, Kokkos::ALL, 0), Kokkos::subview(mass_s, Kokkos::ALL, Kokkos::ALL, n));
            MarkModified("seasalt_mass_" + bin, export_state);
        }
        auto number_field = ResolveExport("seasalt_number_" + bin, export_state);
        if (number_field.data() != nullptr) {
            Kokkos::deep_copy(Kokkos::subview(number_field, Kokkos::ALL, Kokkos::ALL, 0), Kokkos::subview(number_s, Kokkos::ALL, Kokkos::ALL, n));
            MarkModified("seasalt_number_" + bin, export_state);
        }
    }

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
