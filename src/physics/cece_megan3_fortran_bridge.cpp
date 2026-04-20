/**
 * @file cece_megan3_fortran_bridge.cpp
 * @brief Fortran bridge for the full MEGAN3 biogenic emission scheme.
 *
 * This module provides the C++/Fortran interface for the MEGAN3 multi-species,
 * multi-class biogenic emission scheme. It handles:
 * - YAML speciation configuration loading via C++ SpeciationConfigLoader
 * - Flattening parsed speciation data into arrays for Fortran consumption
 * - Memory synchronization between host and device via DualViews
 * - Calling the Fortran run_megan3_fortran subroutine
 *
 * Registered as "megan3_fortran" (guarded by CECE_HAS_FORTRAN).
 *
 * @author CECE Team
 * @date 2024
 */

#include <Kokkos_Core.hpp>
#include <iostream>
#include <string>
#include <unordered_map>

#include "cece/cece_physics_factory.hpp"
#include "cece/physics/cece_megan3_fortran.hpp"
#include "cece/physics/cece_speciation_config.hpp"

extern "C" {
/**
 * @brief External Fortran subroutine for full MEGAN3 emission calculations.
 *
 * Implements the 19-emission-class system, 5-layer canopy integration,
 * all gamma factors, and speciation conversion in Fortran.
 *
 * @param temp          Temperature field [K]
 * @param lai           Leaf area index field [m²/m²]
 * @param lai_prev      Previous month LAI field [m²/m²]
 * @param pardr         Direct PAR [W/m²]
 * @param pardf         Diffuse PAR [W/m²]
 * @param suncos        Cosine of solar zenith angle
 * @param soil_moisture Root-zone soil moisture
 * @param wind_speed    Wind speed [m/s]
 * @param soil_nox      Soil NO emissions from BDSNP [kg/m²/s]
 * @param output        Output mechanism species emissions [kg/m²/s]
 * @param nx            Grid dimension in x-direction
 * @param ny            Grid dimension in y-direction
 * @param nz            Grid dimension in z-direction
 * @param num_output_species Number of mechanism output species
 * @param conversion_factors Flat array of speciation conversion factors
 * @param class_indices      Flat array of emission class indices per mapping
 * @param mechanism_indices  Flat array of mechanism species indices per mapping
 * @param molecular_weights  Flat array of mechanism species molecular weights
 * @param num_mappings       Number of speciation mappings
 * @param num_mechanism_species Number of mechanism species
 */
void run_megan3_fortran(double* temp, double* lai, double* lai_prev, double* pardr, double* pardf, double* suncos, double* soil_moisture,
                        double* wind_speed, double* soil_nox, double* output, int nx, int ny, int nz, int num_output_species,
                        double* conversion_factors, int* class_indices, int* mechanism_indices, double* molecular_weights, int num_mappings,
                        int num_mechanism_species);
}

namespace cece {

#ifdef CECE_HAS_FORTRAN
/// @brief Self-registration for the MEGAN3 Fortran bridge scheme.
static PhysicsRegistration<Megan3FortranScheme> register_scheme("megan3_fortran");
#endif

// ============================================================================
// Initialize
// ============================================================================

void Megan3FortranScheme::Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) {
    // Call base class to parse input_mapping, output_mapping, diagnostics
    BasePhysicsScheme::Initialize(config, diag_manager);

    // ---- Load speciation configuration via C++ loader ----
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

    // ---- Flatten speciation data into arrays for Fortran ----
    num_mappings_ = static_cast<int>(spec_config.mappings.size());

    // Build mechanism species name-to-index map
    std::unordered_map<std::string, int> mech_species_index;
    for (const auto& sp : spec_config.species) {
        if (mech_species_index.find(sp.name) == mech_species_index.end()) {
            int idx = static_cast<int>(mech_species_index.size());
            mech_species_index[sp.name] = idx;
        }
    }
    num_mechanism_species_ = static_cast<int>(mech_species_index.size());

    // Flatten scale factors, class indices, mechanism indices
    scale_factors_.resize(num_mappings_);
    class_indices_.resize(num_mappings_);
    mechanism_indices_.resize(num_mappings_);

    for (int m = 0; m < num_mappings_; ++m) {
        const auto& mapping = spec_config.mappings[m];
        scale_factors_[m] = mapping.scale_factor;

        // Emission class index comes directly from the mapping
        class_indices_[m] = static_cast<int>(mapping.emission_class);

        // Look up mechanism species index
        auto mi_it = mech_species_index.find(mapping.mechanism_species);
        if (mi_it != mech_species_index.end()) {
            mechanism_indices_[m] = mi_it->second;
        } else {
            mechanism_indices_[m] = 0;
        }
    }

    // Flatten molecular weights in mechanism species index order
    molecular_weights_.resize(num_mechanism_species_, 0.0);
    for (const auto& sp : spec_config.species) {
        auto it = mech_species_index.find(sp.name);
        if (it != mech_species_index.end()) {
            molecular_weights_[it->second] = sp.molecular_weight;
        }
    }

    // ---- Build export field names with "MEGAN_" prefix ----
    export_field_names_.clear();
    export_field_names_.resize(num_mechanism_species_);
    for (const auto& [name, idx] : mech_species_index) {
        export_field_names_[idx] = "MEGAN_" + name;
    }

    std::cout << "Megan3FortranScheme: Initialized with " << num_mappings_ << " speciation mappings, " << num_mechanism_species_
              << " mechanism species output fields\n";
}

// ============================================================================
// Run
// ============================================================================

void Megan3FortranScheme::Run(CeceImportState& import_state, CeceExportState& export_state) {
    // ---- Find required import fields ----
    auto it_temp = import_state.fields.find(MapInput("temperature"));
    auto it_lai = import_state.fields.find(MapInput("leaf_area_index"));
    auto it_lai_prev = import_state.fields.find(MapInput("leaf_area_index_prev"));
    auto it_pardr = import_state.fields.find(MapInput("par_direct"));
    auto it_pardf = import_state.fields.find(MapInput("par_diffuse"));
    auto it_suncos = import_state.fields.find(MapInput("solar_cosine"));
    auto it_soil_moisture = import_state.fields.find(MapInput("soil_moisture_root"));
    auto it_wind_speed = import_state.fields.find(MapInput("wind_speed"));

    // Early return if critical fields are missing
    if (it_temp == import_state.fields.end() || it_lai == import_state.fields.end() || it_pardr == import_state.fields.end() ||
        it_pardf == import_state.fields.end() || it_suncos == import_state.fields.end()) {
        return;
    }

    auto& dv_temp = it_temp->second;
    auto& dv_lai = it_lai->second;
    auto& dv_pardr = it_pardr->second;
    auto& dv_pardf = it_pardf->second;
    auto& dv_suncos = it_suncos->second;

    // Sync required fields to host
    dv_temp.sync<Kokkos::HostSpace>();
    dv_lai.sync<Kokkos::HostSpace>();
    dv_pardr.sync<Kokkos::HostSpace>();
    dv_pardf.sync<Kokkos::HostSpace>();
    dv_suncos.sync<Kokkos::HostSpace>();

    int nx = static_cast<int>(dv_temp.extent(0));
    int ny = static_cast<int>(dv_temp.extent(1));
    int nz = static_cast<int>(dv_temp.extent(2));

    // Handle optional fields: lai_prev
    double* lai_prev_ptr = nullptr;
    if (it_lai_prev != import_state.fields.end()) {
        it_lai_prev->second.sync<Kokkos::HostSpace>();
        lai_prev_ptr = it_lai_prev->second.view_host().data();
    } else {
        // Use current LAI as fallback
        lai_prev_ptr = dv_lai.view_host().data();
    }

    // Handle optional fields: soil_moisture
    double* soil_moisture_ptr = nullptr;
    std::vector<double> dummy_sm;
    if (it_soil_moisture != import_state.fields.end()) {
        it_soil_moisture->second.sync<Kokkos::HostSpace>();
        soil_moisture_ptr = it_soil_moisture->second.view_host().data();
    } else {
        dummy_sm.assign(static_cast<size_t>(nx) * ny * nz, 1.0);
        soil_moisture_ptr = dummy_sm.data();
    }

    // Handle optional fields: wind_speed
    double* wind_speed_ptr = nullptr;
    std::vector<double> dummy_ws;
    if (it_wind_speed != import_state.fields.end()) {
        it_wind_speed->second.sync<Kokkos::HostSpace>();
        wind_speed_ptr = it_wind_speed->second.view_host().data();
    } else {
        dummy_ws.assign(static_cast<size_t>(nx) * ny * nz, 0.0);
        wind_speed_ptr = dummy_ws.data();
    }

    // Handle soil_nox_emissions from export state
    double* soil_nox_ptr = nullptr;
    std::vector<double> dummy_soil_nox;
    auto it_soil_nox = export_state.fields.find(MapOutput("soil_nox_emissions"));
    if (it_soil_nox != export_state.fields.end()) {
        it_soil_nox->second.sync<Kokkos::HostSpace>();
        soil_nox_ptr = it_soil_nox->second.view_host().data();
    } else {
        std::cerr << "Megan3FortranScheme: WARNING - soil_nox_emissions "
                  << "not found in export state, setting soil NO to zero\n";
        dummy_soil_nox.assign(static_cast<size_t>(nx) * ny * nz, 0.0);
        soil_nox_ptr = dummy_soil_nox.data();
    }

    // ---- Allocate flat output buffer for all mechanism species ----
    int total_output_size = num_mechanism_species_ * nx * ny * nz;
    std::vector<double> output_buffer(total_output_size, 0.0);

    // ---- Call Fortran subroutine ----
    run_megan3_fortran(dv_temp.view_host().data(), dv_lai.view_host().data(), lai_prev_ptr, dv_pardr.view_host().data(), dv_pardf.view_host().data(),
                       dv_suncos.view_host().data(), soil_moisture_ptr, wind_speed_ptr, soil_nox_ptr, output_buffer.data(), nx, ny, nz,
                       num_mechanism_species_, scale_factors_.data(), class_indices_.data(), mechanism_indices_.data(), molecular_weights_.data(),
                       num_mappings_, num_mechanism_species_);

    // ---- Copy output buffer to export state fields ----
    int cells_per_species = nx * ny * nz;
    for (int s = 0; s < num_mechanism_species_; ++s) {
        std::string field_name = export_field_names_[s];
        auto it_export = export_state.fields.find(MapOutput(field_name));
        if (it_export == export_state.fields.end()) {
            continue;
        }

        auto& dv_export = it_export->second;
        dv_export.sync<Kokkos::HostSpace>();

        double* export_ptr = dv_export.view_host().data();
        double* src_ptr = output_buffer.data() + s * cells_per_species;
        std::copy(src_ptr, src_ptr + cells_per_species, export_ptr);

        dv_export.modify<Kokkos::HostSpace>();
        dv_export.sync<Kokkos::DefaultExecutionSpace>();
    }
}

}  // namespace cece
