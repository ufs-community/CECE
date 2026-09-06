#ifndef CECE_MEGAN3_UNITS_HPP
#define CECE_MEGAN3_UNITS_HPP

#include <Kokkos_Macros.hpp>

namespace cece {

/// Molecular weight of NO. Numerically, g/mol is equivalent to kg/kmol.
inline constexpr double kSoilNoMolecularWeightKgPerKmol = 30.01;

/// Convert a soil-NO mass flux [kg NO m-2 s-1] to an amount flux [kmol NO m-2
/// s-1].
KOKKOS_INLINE_FUNCTION
constexpr double SoilNoMassToAmountFlux(double mass_flux) {
    return mass_flux / kSoilNoMolecularWeightKgPerKmol;
}

}  // namespace cece

#endif  // CECE_MEGAN3_UNITS_HPP
