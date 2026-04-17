/**
 * @file cece_speciation_engine.cpp
 * @brief Implementation of the speciation engine that converts 19 MEGAN3
 *        emission classes directly to mechanism-specific output species.
 *
 * Direct conversion pipeline:
 *   For each mechanism species s:
 *     output[s] = (Σ class_total[c] × scale_factor[c→s]) × MW[s]
 *
 * Scale factors, class indices, and mechanism indices are stored as parallel
 * device-side arrays (one entry per mapping). Molecular weights are stored
 * per mechanism species. Accumulation uses Kokkos::atomic_add for thread safety.
 *
 * @author CECE Team
 * @date 2024
 */

#include "cece/physics/cece_speciation_engine.hpp"

#include <Kokkos_Core.hpp>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace cece {

void SpeciationEngine::Initialize(const SpeciationConfig& config) {
    num_mappings_ = static_cast<int>(config.mappings.size());
    if (num_mappings_ == 0) {
        std::cerr << "SpeciationEngine: No mappings in config, nothing to initialize.\n";
        return;
    }

    // Build mechanism species name list and index lookup
    std::unordered_map<std::string, int> mechanism_index_map;
    mechanism_species_names_.clear();
    for (const auto& sp : config.species) {
        if (mechanism_index_map.find(sp.name) == mechanism_index_map.end()) {
            mechanism_index_map[sp.name] = static_cast<int>(mechanism_species_names_.size());
            mechanism_species_names_.push_back(sp.name);
        }
    }
    num_mechanism_species_ = static_cast<int>(mechanism_species_names_.size());

    // Build per-mechanism-species molecular weight array on host
    auto h_molecular_weights =
        Kokkos::create_mirror_view(Kokkos::View<double*, Kokkos::DefaultExecutionSpace>("molecular_weights", num_mechanism_species_));
    for (const auto& sp : config.species) {
        auto it = mechanism_index_map.find(sp.name);
        if (it != mechanism_index_map.end()) {
            h_molecular_weights(it->second) = sp.molecular_weight;
        }
    }

    // Flatten mappings into parallel host arrays
    auto h_scale_factors = Kokkos::create_mirror_view(Kokkos::View<double*, Kokkos::DefaultExecutionSpace>("scale_factors", num_mappings_));
    auto h_class_indices = Kokkos::create_mirror_view(Kokkos::View<int*, Kokkos::DefaultExecutionSpace>("class_indices", num_mappings_));
    auto h_mechanism_indices = Kokkos::create_mirror_view(Kokkos::View<int*, Kokkos::DefaultExecutionSpace>("mechanism_indices", num_mappings_));

    for (int m = 0; m < num_mappings_; ++m) {
        const auto& mapping = config.mappings[m];

        h_scale_factors(m) = mapping.scale_factor;
        h_class_indices(m) = static_cast<int>(mapping.emission_class);

        // Look up mechanism species index
        auto mech_it = mechanism_index_map.find(mapping.mechanism_species);
        if (mech_it != mechanism_index_map.end()) {
            h_mechanism_indices(m) = mech_it->second;
        } else {
            std::cerr << "SpeciationEngine: Mechanism species '" << mapping.mechanism_species
                      << "' not found in species list, defaulting to index 0.\n";
            h_mechanism_indices(m) = 0;
        }
    }

    // Allocate device views and deep copy from host mirrors
    scale_factors_ = Kokkos::View<double*, Kokkos::DefaultExecutionSpace>("speciation_scale_factors", num_mappings_);
    class_indices_ = Kokkos::View<int*, Kokkos::DefaultExecutionSpace>("speciation_class_indices", num_mappings_);
    mechanism_indices_ = Kokkos::View<int*, Kokkos::DefaultExecutionSpace>("speciation_mechanism_indices", num_mappings_);
    molecular_weights_ = Kokkos::View<double*, Kokkos::DefaultExecutionSpace>("speciation_molecular_weights", num_mechanism_species_);

    Kokkos::deep_copy(scale_factors_, h_scale_factors);
    Kokkos::deep_copy(class_indices_, h_class_indices);
    Kokkos::deep_copy(mechanism_indices_, h_mechanism_indices);
    Kokkos::deep_copy(molecular_weights_, h_molecular_weights);

    std::cout << "SpeciationEngine: Initialized with " << num_mappings_ << " mappings and " << num_mechanism_species_ << " mechanism species.\n";
}

void SpeciationEngine::Run(const Kokkos::View<const double**, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>& class_totals,
                           CeceExportState& export_state, int nx, int ny) {
    if (num_mappings_ == 0 || num_mechanism_species_ == 0) {
        return;
    }

    int num_cells = nx * ny;

    // Create temporary device view for mechanism species accumulation
    // Shape: (num_mechanism_species, num_cells)
    Kokkos::View<double**, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> mech_accum("speciation_mech_accum", num_mechanism_species_, num_cells);

    // Zero the accumulation buffer
    Kokkos::deep_copy(mech_accum, 0.0);

    // Capture device-side lookup tables for the kernel
    auto d_scale_factors = scale_factors_;
    auto d_class_indices = class_indices_;
    auto d_mechanism_indices = mechanism_indices_;
    auto d_molecular_weights = molecular_weights_;
    int n_mappings = num_mappings_;

    // Speciation kernel: for each grid cell and each mapping entry,
    // read the class total, apply scale_factor, and accumulate to the
    // mechanism species output using atomic_add.
    // MW is applied after accumulation (per mechanism species).
    Kokkos::parallel_for(
        "SpeciationKernel", Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<2>>({0, 0}, {num_cells, n_mappings}),
        KOKKOS_LAMBDA(int cell, int m) {
            int class_idx = d_class_indices(m);
            int mech_idx = d_mechanism_indices(m);
            double class_total = class_totals(class_idx, cell);
            double contribution = class_total * d_scale_factors(m);
            Kokkos::atomic_add(&mech_accum(mech_idx, cell), contribution);
        });

    Kokkos::fence();

    // Apply molecular weights per mechanism species
    int n_mech = num_mechanism_species_;
    Kokkos::parallel_for(
        "SpeciationApplyMW", Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<2>>({0, 0}, {n_mech, num_cells}),
        KOKKOS_LAMBDA(int sp, int cell) { mech_accum(sp, cell) *= d_molecular_weights(sp); });

    Kokkos::fence();

    // Copy results to export state fields
    // Each mechanism species gets written to "MEGAN_<species_name>"
    for (int sp = 0; sp < num_mechanism_species_; ++sp) {
        std::string field_name = "MEGAN_" + mechanism_species_names_[sp];
        auto it = export_state.fields.find(field_name);
        if (it == export_state.fields.end()) {
            std::cerr << "SpeciationEngine: Export field '" << field_name << "' not found in export state, skipping.\n";
            continue;
        }

        auto export_view = it->second.view_device();
        int sp_idx = sp;

        // Write accumulated values into the 3D export field (nx, ny, 1)
        Kokkos::parallel_for(
            "SpeciationExport_" + mechanism_species_names_[sp],
            Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<2>>({0, 0}, {nx, ny}), KOKKOS_LAMBDA(int i, int j) {
                int cell = i + j * nx;
                export_view(i, j, 0) = mech_accum(sp_idx, cell);
            });

        Kokkos::fence();
        it->second.modify_device();
    }
}

const std::vector<std::string>& SpeciationEngine::GetMechanismSpeciesNames() const {
    return mechanism_species_names_;
}

}  // namespace cece
