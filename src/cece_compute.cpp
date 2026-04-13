#include "cece/cece_compute.hpp"

#include <Kokkos_Core.hpp>

#include "cece/cece_stacking_engine.hpp"

/**
 * @file cece_compute.cpp
 * @brief Legacy entry point for the emissions compute engine.
 */

namespace cece {

/**
 * @brief Legacy wrapper for performing emission computation.
 *
 * This function now utilizes the optimized StackingEngine to perform the work.
 *
 * @param config The CECE configuration.
 * @param resolver A FieldResolver to bridge between compute and data sources.
 * @param nx Size of the first grid dimension.
 * @param ny Size of the second grid dimension.
 * @param nz Size of the third grid dimension.
 * @param default_mask Unused in the new implementation (kept for API compatibility).
 * @param hour Current hour.
 * @param day_of_week Current day of week.
 */
void ComputeEmissions(
    const CeceConfig& config, FieldResolver& resolver, int nx, int ny, int nz,
    const Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>& default_mask,
    int hour, int day_of_week, int month, StackingEngine* engine) {
    if (engine != nullptr) {
        engine->Execute(resolver, nx, ny, nz, default_mask, hour, day_of_week, month);
    } else {
        StackingEngine stack_engine(config);
        stack_engine.Execute(resolver, nx, ny, nz, default_mask, hour, day_of_week, month);
    }
}

}  // namespace cece
