/**
 * @file cece_fengsha.hpp
 * @brief Native C++ implementation of the FENGSHA dust emission scheme.
 * @author CECE Development Team
 * @date 2025
 * @version 1.0
 */
#ifndef CECE_FENGSHA_HPP
#define CECE_FENGSHA_HPP

#include "cece/physics_scheme.hpp"

#include <Kokkos_Core.hpp>

namespace cece {

/**
 * @class FengshaScheme
 * @brief Native C++ implementation of the FENGSHA dust emission scheme.
 *
 * Implements the FENGSHA algorithm using Kokkos for GPU-portable parallel
 * execution, including Fécan moisture correction, MB95 flux ratio, and
 * per-bin emission distribution.
 */
class FengshaScheme : public BasePhysicsScheme {
   public:
    FengshaScheme() = default;
    ~FengshaScheme() override = default;

    void Initialize(const YAML::Node& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;

   private:
    double alpha_ = 1.0;
    double gamma_ = 1.0;
    double kvhmax_ = 2.45e-4;
    double grav_ = 9.81;
    double drylimit_factor_ = 1.0;
    int num_bins_ = 5;
    Kokkos::View<double*, Kokkos::DefaultExecutionSpace> bin_distribution_;
    bool has_custom_distribution_ = false;
};

}  // namespace cece

#endif  // CECE_FENGSHA_HPP
