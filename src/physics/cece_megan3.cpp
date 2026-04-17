/**
 * @file cece_megan3.cpp
 * @brief Full MEGAN3 multi-species, multi-class biogenic emission scheme.
 *
 * Implements the Megan3Scheme orchestrator that computes emissions for 19
 * MEGAN3 emission classes using a 5-layer canopy model, full vegetation
 * emission activity factors, and chemical mechanism speciation.
 *
 * Registered as "megan3" via PhysicsRegistration. The existing "megan" scheme
 * remains unchanged for backward compatibility.
 *
 * @author CECE Team
 * @date 2024
 */

#include "cece/physics/cece_megan3.hpp"

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

#include "cece/cece_physics_factory.hpp"
#include "cece/physics/cece_canopy_model.hpp"
#include "cece/physics/cece_emission_activity.hpp"
#include "cece/physics/cece_speciation_config.hpp"
#include "cece/physics/cece_speciation_engine.hpp"

namespace cece {

/// @brief Self-registration for the MEGAN3 biogenic emission scheme.
static PhysicsRegistration<Megan3Scheme> reg("megan3");

// ============================================================================
// Re-declare existing KOKKOS_INLINE_FUNCTION gamma functions from cece_megan.cpp
// These are defined in the megan translation unit but since they are
// KOKKOS_INLINE_FUNCTION they are available via the header or redeclaration.
// We forward-declare them here for use in the MEGAN3 kernel.
// ============================================================================

KOKKOS_INLINE_FUNCTION
double get_gamma_lai(double lai, double c1, double c2, bool is_bidirectional);

KOKKOS_INLINE_FUNCTION
double get_gamma_age(double cmlai, double pmlai, double dbtwn, double tt, double an, double ag, double am, double ao);

KOKKOS_INLINE_FUNCTION
double get_gamma_sm(double gwetroot, bool is_ald2_or_eoh);

KOKKOS_INLINE_FUNCTION
double get_gamma_t_li(double temp, double beta, double t_standard);

KOKKOS_INLINE_FUNCTION
double get_gamma_t_ld(double T, double PT_15, double CT1, double CEO, double R, double CT2, double t_opt_c1, double t_opt_c2, double e_opt_coeff);

KOKKOS_INLINE_FUNCTION
double get_gamma_par_pceea(double q_dir, double q_diff, double par_avg, double suncos, int doy, double wm2_to_umol, double ptoa_c1, double ptoa_c2,
                           double gamma_p_c1, double gamma_p_c2, double gamma_p_c3, double gamma_p_c4);

// ============================================================================
// Default AEF values for the 19 emission classes
// ============================================================================
static constexpr double kDefaultAef[19] = {
    1.0e-9,   // ISOP
    2.0e-10,  // MBO
    3.0e-10,  // MT_PINE
    3.0e-10,  // MT_ACYC
    3.0e-10,  // MT_CAMP
    3.0e-10,  // MT_SABI
    3.0e-10,  // MT_AROM
    0.0,      // NO (soil NO comes from export state)
    1.0e-10,  // SQT_HR
    1.0e-10,  // SQT_LR
    5.0e-10,  // MEOH
    2.0e-10,  // ACTO
    2.0e-10,  // ETOH
    1.0e-10,  // ACID
    1.0e-10,  // LVOC
    1.0e-10,  // OXPROD
    1.0e-10,  // STRESS
    1.0e-10,  // OTHER
    3.0e-10   // CO
};

// ============================================================================
// Initialize
// ============================================================================

void Megan3Scheme::Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) {
    // Call base class to parse input_mapping, output_mapping, diagnostics
    BasePhysicsScheme::Initialize(config, diag_manager);

    // ---- Load speciation configuration ----
    std::string mechanism_path = "data/speciation/spc_cb6.yaml";
    std::string speciation_path = "data/speciation/map_cb6.yaml";
    std::string speciation_dataset = "MEGAN";

    if (config["mechanism_file"]) {
        mechanism_path = config["mechanism_file"].as<std::string>();
    }
    if (config["speciation_file"]) {
        speciation_path = config["speciation_file"].as<std::string>();
    }
    if (config["speciation_dataset"]) {
        speciation_dataset = config["speciation_dataset"].as<std::string>();
    }

    SpeciationConfig spec_config = config_loader_.Load(mechanism_path, speciation_path, speciation_dataset);

    // ---- Initialize sub-components ----
    canopy_model_.Initialize(config);
    activity_calc_.Initialize(config);
    speciation_engine_.Initialize(spec_config);

    // ---- Read per-class default AEF values from YAML ----
    default_aef_ = Kokkos::View<double[19], Kokkos::DefaultExecutionSpace>("megan3_default_aef");
    auto h_default_aef = Kokkos::create_mirror_view(default_aef_);

    for (int i = 0; i < NUM_CLASSES; ++i) {
        h_default_aef(i) = kDefaultAef[i];
    }

    if (config["emission_classes"]) {
        const auto& classes_node = config["emission_classes"];
        for (auto it = classes_node.begin(); it != classes_node.end(); ++it) {
            std::string class_name = it->first.as<std::string>();
            const auto& class_config = it->second;

            EmissionClass ec;
            if (!StringToEmissionClass(class_name, ec)) {
                continue;
            }
            int idx = static_cast<int>(ec);

            if (class_config["default_aef"]) {
                h_default_aef(idx) = class_config["default_aef"].as<double>();
            }
        }
    }

    Kokkos::deep_copy(default_aef_, h_default_aef);

    // ---- Register dynamic export fields with "MEGAN_" prefix ----
    const auto& mech_species = speciation_engine_.GetMechanismSpeciesNames();
    export_field_names_.clear();
    export_field_names_.reserve(mech_species.size());
    for (const auto& sp_name : mech_species) {
        export_field_names_.push_back("MEGAN_" + sp_name);
    }

    std::cout << "Megan3Scheme: Initialized with " << NUM_CLASSES << " emission classes, " << export_field_names_.size()
              << " mechanism species output fields\n";
}

// ============================================================================
// Run
// ============================================================================

void Megan3Scheme::Run(CeceImportState& import_state, CeceExportState& export_state) {
    // ---- Resolve required import fields ----
    auto temp = ResolveImport("temperature", import_state);
    auto lai = ResolveImport("leaf_area_index", import_state);
    auto lai_prev = ResolveImport("leaf_area_index_prev", import_state);
    auto par_direct = ResolveImport("par_direct", import_state);
    auto par_diffuse = ResolveImport("par_diffuse", import_state);
    auto suncos = ResolveImport("solar_cosine", import_state);
    auto soil_moisture = ResolveImport("soil_moisture_root", import_state);
    auto wind_speed = ResolveImport("wind_speed", import_state);

    // Early return if required fields are null
    if (temp.data() == nullptr || lai.data() == nullptr || par_direct.data() == nullptr || par_diffuse.data() == nullptr ||
        suncos.data() == nullptr) {
        return;
    }

    // Determine grid dimensions from the first available export field
    int nx = 0;
    int ny = 0;
    if (!export_field_names_.empty()) {
        auto first_export = ResolveExport(export_field_names_[0], export_state);
        if (first_export.data() != nullptr) {
            nx = static_cast<int>(first_export.extent(0));
            ny = static_cast<int>(first_export.extent(1));
        }
    }
    // Fallback: use temperature field dimensions
    if (nx == 0 || ny == 0) {
        nx = static_cast<int>(temp.extent(0));
        ny = static_cast<int>(temp.extent(1));
    }

    int num_cells = nx * ny;

    // ---- Resolve per-class AEF fields from import state ----
    // Try to find AEF_<CLASS_NAME> in import state; use default_aef_ if missing
    Kokkos::View<double**, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> aef_grid("megan3_aef_grid", NUM_CLASSES, num_cells);
    Kokkos::deep_copy(aef_grid, 0.0);

    // Track which classes have gridded AEF fields
    bool has_aef_field[19] = {false};

    for (int c = 0; c < NUM_CLASSES; ++c) {
        std::string aef_field_name = std::string("AEF_") + CLASS_NAMES[c];
        auto aef_view = ResolveImport(aef_field_name, import_state);
        if (aef_view.data() != nullptr) {
            has_aef_field[c] = true;
            int class_idx = c;
            Kokkos::parallel_for(
                "CopyAEF_" + std::string(CLASS_NAMES[c]), Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<2>>({0, 0}, {nx, ny}),
                KOKKOS_LAMBDA(int i, int j) {
                    int cell = i + j * nx;
                    aef_grid(class_idx, cell) = aef_view(i, j, 0);
                });
        }
    }
    Kokkos::fence();

    // Fill missing AEF fields with default values
    auto d_default_aef = default_aef_;
    for (int c = 0; c < NUM_CLASSES; ++c) {
        if (!has_aef_field[c]) {
            int class_idx = c;
            Kokkos::parallel_for(
                "FillDefaultAEF", Kokkos::RangePolicy<Kokkos::DefaultExecutionSpace>(0, num_cells),
                KOKKOS_LAMBDA(int cell) { aef_grid(class_idx, cell) = d_default_aef(class_idx); });
        }
    }
    Kokkos::fence();

    // ---- Read soil_nox_emissions from export state for NO class ----
    auto soil_nox_view = ResolveExport("soil_nox_emissions", export_state);
    bool has_soil_nox = (soil_nox_view.data() != nullptr);
    if (!has_soil_nox) {
        std::cerr << "Megan3Scheme: WARNING - soil_nox_emissions not found in "
                  << "export state, setting soil NO contribution to zero\n";
    }

    // ---- Allocate class_totals_ (19 x num_cells) ----
    class_totals_ = Kokkos::View<double**, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>("megan3_class_totals", NUM_CLASSES, num_cells);
    Kokkos::deep_copy(class_totals_, 0.0);

    // ---- Capture sub-component data for the kernel ----
    auto d_ldf = activity_calc_.coefficients_.ldf;
    auto d_ct1 = activity_calc_.coefficients_.ct1;
    auto d_cleo = activity_calc_.coefficients_.cleo;
    auto d_beta = activity_calc_.coefficients_.beta;
    auto d_anew = activity_calc_.coefficients_.anew;
    auto d_agro = activity_calc_.coefficients_.agro;
    auto d_amat = activity_calc_.coefficients_.amat;
    auto d_aold = activity_calc_.coefficients_.aold;
    auto d_bidir = activity_calc_.bidirectional_;
    double gamma_co2_val = activity_calc_.gamma_co2_;
    bool enable_wind_stress = activity_calc_.enable_wind_stress_;
    bool enable_temp_stress = activity_calc_.enable_temp_stress_;

    auto d_gauss_weights = canopy_model_.gauss_weights_;
    auto d_gauss_points = canopy_model_.gauss_points_;
    double extinction_coeff = canopy_model_.extinction_coeff();

    // Standard constants (matching MeganScheme)
    constexpr double NORM_FAC = 1.0 / 1.0101081;
    constexpr double LAI_C1 = 0.49;
    constexpr double LAI_C2 = 0.2;
    constexpr double STD_TEMP = 303.0;
    constexpr double GAS_CONSTANT = 8.3144598e-3;
    constexpr double CT2_CONST = 200.0;
    constexpr double T_OPT_C1 = 313.0;
    constexpr double T_OPT_C2 = 0.6;
    constexpr double E_OPT_COEFF = 0.08;
    constexpr double WM2_TO_UMOL = 4.766;
    constexpr double PTOA_C1 = 3000.0;
    constexpr double PTOA_C2 = 99.0;
    constexpr double GP_C1 = 1.0;
    constexpr double GP_C2 = 0.0005;
    constexpr double GP_C3 = 2.46;
    constexpr double GP_C4 = 0.9;
    constexpr int NO_CLASS_IDX = static_cast<int>(EmissionClass::NO);

    bool has_lai_prev = (lai_prev.data() != nullptr);
    bool has_soil_moist = (soil_moisture.data() != nullptr);
    bool has_wind = (wind_speed.data() != nullptr);

    auto d_class_totals = class_totals_;
    int local_nx = nx;

    // ---- Main Kokkos kernel: compute 19 class totals ----
    Kokkos::parallel_for(
        "Megan3Kernel", Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<2>>({0, 0}, {nx, ny}), KOKKOS_LAMBDA(int i, int j) {
            double T = temp(i, j, 0);
            double L = lai(i, j, 0);
            double sc = suncos(i, j, 0);
            double pdr = par_direct(i, j, 0);
            double pdf = par_diffuse(i, j, 0);

            if (L <= 0.0) {
                return;
            }

            int cell = i + j * local_nx;

            // Averaged values (defaults when not dynamically passed)
            double T_AVG_15 = 297.0;
            double PAR_AVG = 400.0;
            int doy = 180;
            double dbtwn = 30.0;

            double L_prev = has_lai_prev ? lai_prev(i, j, 0) : L;
            double gwet = has_soil_moist ? soil_moisture(i, j, 0) : 1.0;
            double ws = has_wind ? wind_speed(i, j, 0) : 0.0;

            // Compute shared gamma factors
            double g_lai = get_gamma_lai(L, LAI_C1, LAI_C2, false);
            double g_age = get_gamma_age(L, L_prev, dbtwn, T, 1.0, 1.0, 1.0, 1.0);
            double g_sm = get_gamma_sm(gwet, false);
            double g_par = get_gamma_par_pceea(pdr, pdf, PAR_AVG, sc, doy, WM2_TO_UMOL, PTOA_C1, PTOA_C2, GP_C1, GP_C2, GP_C3, GP_C4);

            // Compute per-class emissions
            for (int c = 0; c < 19; ++c) {
                double ldf = d_ldf(c);
                double ct1 = d_ct1(c);
                double cleo = d_cleo(c);
                double beta = d_beta(c);
                double anew = d_anew(c);
                double agro = d_agro(c);
                double amat = d_amat(c);
                double aold = d_aold(c);
                bool bidir = d_bidir(c);

                // Per-class gamma factors
                double g_lai_c = get_gamma_lai(L, LAI_C1, LAI_C2, bidir);
                double g_age_c = get_gamma_age(L, L_prev, dbtwn, T, anew, agro, amat, aold);
                double g_t_li = get_gamma_t_li(T, beta, STD_TEMP);
                double g_t_ld = get_gamma_t_ld(T, T_AVG_15, ct1, cleo, GAS_CONSTANT, CT2_CONST, T_OPT_C1, T_OPT_C2, E_OPT_COEFF);

                // LDF partitioning: (1-LDF)*gamma_t_li + LDF*gamma_par*gamma_t_ld
                double ldf_combined = (1.0 - ldf) * g_t_li + ldf * g_par * g_t_ld;

                // Stress factors
                double g_stress = 1.0;
                if (enable_wind_stress) {
                    g_stress *= get_gamma_wind_stress(ws);
                }
                if (enable_temp_stress) {
                    g_stress *= get_gamma_temp_stress(T);
                }

                // AEF for this class and cell
                double aef = aef_grid(c, cell);

                // Combined emission for this class
                double emission = NORM_FAC * aef * g_lai_c * g_age_c * g_sm * gamma_co2_val * ldf_combined * g_stress;

                // Special handling for NO class: use soil NO from export state
                if (c == NO_CLASS_IDX) {
                    if (has_soil_nox) {
                        emission = soil_nox_view(i, j, 0);
                    } else {
                        emission = 0.0;
                    }
                }

                d_class_totals(c, cell) = emission;
            }
        });

    Kokkos::fence();

    // ---- Run speciation engine to convert class totals to mechanism species ----
    auto const_class_totals = Kokkos::View<const double**, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>(class_totals_);
    speciation_engine_.Run(const_class_totals, export_state, nx, ny);

    Kokkos::fence();

    // ---- Mark all export fields as modified ----
    for (const auto& field_name : export_field_names_) {
        MarkModified(field_name, export_state);
    }
}

}  // namespace cece
