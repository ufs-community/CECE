#ifndef ACES_HPP
#define ACES_HPP

/**
 * @file aces.hpp
 * @brief Core initialization and lifecycle management for the ACES component.
 */

#include "ESMC.h"
#include "NUOPC.h"
#include "aces/aces_kokkos_config.hpp"

namespace aces {

/**
 * @brief Initializes the ACES component.
 *
 * This function handles the initialization of Kokkos and any other necessary
 * libraries or state.
 *
 * @param comp The ESMF Grid Component.
 * @param importState The ESMF Import State.
 * @param exportState The ESMF Export State.
 * @param clock The ESMF Clock.
 * @param rc Return code pointer.
 */
void Initialize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState,
                ESMC_Clock* clock, int* rc);

/**
 * @brief Runs the ACES component.
 *
 * This function executes the main computational logic for the component.
 *
 * @param comp The ESMF Grid Component.
 * @param importState The ESMF Import State.
 * @param exportState The ESMF Export State.
 * @param clock The ESMF Clock.
 * @param rc Return code pointer.
 */
void Run(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock,
         int* rc);

/**
 * @brief Finalizes the ACES component.
 *
 * This function handles the cleanup and finalization of Kokkos and other
 * resources.
 *
 * @param comp The ESMF Grid Component.
 * @param importState The ESMF Import State.
 * @param exportState The ESMF Export State.
 * @param clock The ESMF Clock.
 * @param rc Return code pointer.
 */
void Finalize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock,
              int* rc);

}  // namespace aces

#endif  // ACES_HPP
