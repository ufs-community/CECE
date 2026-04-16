#ifndef CECE_SPECIATION_ENGINE_HPP
#define CECE_SPECIATION_ENGINE_HPP

/**
 * @file cece_speciation_engine.hpp
 * @brief Speciation engine that converts 19 MEGAN3 emission classes directly
 *        to mechanism-specific output species via per-class scale factors.
 *
 * Performs the direct conversion pipeline:
 *   For each mechanism species s:
 *     output[s] = (Σ class_total[c] × scale_factor[c→s]) × MW[s]
 *
 * This replaces the previous three-stage pipeline (19 classes → 201 speciated
 * → mechanism) with a simpler two-stage pipeline.
 *
 * All lookup tables are stored as device-side Kokkos::Views for GPU-portable
 * parallel execution.
 */

#include <Kokkos_Core.hpp>

#include <string>
#include <vector>

#include "cece/cece_state.hpp"
#include "cece/physics/cece_speciation_config.hpp"

namespace cece {

/**
 * @class SpeciationEngine
 * @brief Converts emission class totals to mechanism-specific species using
 *        device-side lookup tables and Kokkos kernels.
 */
class SpeciationEngine {
   public:
    /**
     * @brief Initialize from parsed speciation config; builds device-side lookup tables.
     * @param config A validated SpeciationConfig containing mechanism species,
     *              speciation mappings with emission classes and scale factors.
     */
    void Initialize(const SpeciationConfig& config);

    /**
     * @brief Run the speciation pipeline on a 2D grid.
     *
     * For each mechanism species s, computes:
     *   output[s] = (Σ class_total[c] × scale_factor[c→s]) × MW[s]
     *
     * @param class_totals Device-side view of shape (num_classes, nx*ny) containing
     *                     the 19 emission class totals per grid cell.
     * @param export_state The export state to write mechanism species fields into,
     *                     using the "MEGAN_" prefix convention.
     * @param nx Number of grid cells in the x-dimension.
     * @param ny Number of grid cells in the y-dimension.
     */
    void Run(
        const Kokkos::View<const double**, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>& class_totals,
        CeceExportState& export_state,
        int nx, int ny);

    /**
     * @brief Returns the list of mechanism species names for dynamic field registration.
     * @return A const reference to the vector of mechanism species names.
     */
    [[nodiscard]] const std::vector<std::string>& GetMechanismSpeciesNames() const;

   private:
    /// @name Device-side lookup tables for speciation (one entry per mapping)
    /// @{

    /// Scale factors for each mapping entry (class→mechanism contribution).
    Kokkos::View<double*, Kokkos::DefaultExecutionSpace> scale_factors_;

    /// Emission class index (0–18) for each mapping entry.
    Kokkos::View<int*, Kokkos::DefaultExecutionSpace> class_indices_;

    /// Target mechanism species index for each mapping entry.
    Kokkos::View<int*, Kokkos::DefaultExecutionSpace> mechanism_indices_;

    /// @}

    /// Per-mechanism-species molecular weights (g/mol), one entry per species.
    Kokkos::View<double*, Kokkos::DefaultExecutionSpace> molecular_weights_;

    /// Host-side list of mechanism species names (e.g., "ISOP", "TERP", "PAR").
    std::vector<std::string> mechanism_species_names_;

    /// Total number of speciation mapping entries.
    int num_mappings_ = 0;

    /// Total number of unique mechanism species in the loaded configuration.
    int num_mechanism_species_ = 0;
};

}  // namespace cece

#endif  // CECE_SPECIATION_ENGINE_HPP
