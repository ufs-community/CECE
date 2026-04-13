/**
 * @file cece_k14.hpp
 * @brief Native C++ implementation of the K14 (Kok et al., 2014) dust emission scheme.
 *
 * Implements the K14 dust emission algorithm using Kokkos for GPU-portable
 * parallel execution. Includes helper functions for Kok vertical dust flux,
 * MacKinnon drag partition, smooth roughness lookup, and Laurent erodibility.
 *
 * References:
 * - Kok, J.F., et al. (2012), An improved dust emission model, ACP, 12, 7413–7430.
 * - Kok, J.F., et al. (2014), An improved dust emission model — Part 2, ACP, 14, 13023–13041.
 * - MacKinnon, D.J., et al. (2004), A geomorphological approach, JGR, 109, F01013.
 * - Laurent, B., et al. (2008), Modeling mineral dust emissions, ACP, 8, 395–409.
 * - Fécan, F., et al. (1999), Annales Geophysicae, 17, 149–157.
 *
 * @author CECE Development Team
 * @date 2025
 * @version 1.0
 */
#ifndef CECE_K14_HPP
#define CECE_K14_HPP

#include "cece/physics_scheme.hpp"

#include <Kokkos_Core.hpp>

namespace cece {

/**
 * @class K14Scheme
 * @brief GPU-portable K14 (Kok et al., 2014) dust emission scheme using Kokkos.
 *
 * Implements the complete K14 algorithm using Kokkos for GPU-portable parallel
 * execution, including Shao threshold friction velocity, Zender gravimetric
 * moisture, Fécan moisture correction, clay parameterization (opt_clay),
 * MacKinnon drag partition, Laurent erodibility, IGBP vegetation masking,
 * and Kok (2012, 2014) vertical dust flux.
 */
class K14Scheme : public BasePhysicsScheme {
   public:
    K14Scheme() = default;
    ~K14Scheme() override = default;

    void Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;

   private:
    double ch_du_ = 0.8e-9;
    double f_w_ = 1.0;
    double f_c_ = 1.0;
    double uts_gamma_ = 1.65e-4;
    double undef_ = 1.0e15;
    double grav_ = 9.81;
    double von_karman_ = 0.4;
    int opt_clay_ = 0;
    int num_bins_ = 5;
    Kokkos::View<double*, Kokkos::DefaultExecutionSpace> bin_distribution_;
    bool has_custom_distribution_ = false;
};

}  // namespace cece

#endif  // CECE_K14_HPP
