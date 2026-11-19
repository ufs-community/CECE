// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <Kokkos_Core.hpp>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace cece::test::fire {

inline constexpr std::array<std::string_view, 11> kSpecies = {"CO2", "CO", "CH4", "PM25", "TPM", "SO2", "OC", "BC", "NOX", "NH3", "VOC"};
inline constexpr double kRelativeTolerance = 1.0e-6;
inline constexpr double kZeroAbsoluteTolerance = 1.0e-12;

struct SyntheticFactors {
    double combustion_kg_per_mj;
    std::array<double, kSpecies.size()> emission_kg_per_kg;
};

inline constexpr SyntheticFactors kSyntheticFactors{0.5, {0.01, 0.02, 0.03, 0.04, 0.05, 0.06, 0.07, 0.08, 0.09, 0.10, 0.11}};
inline constexpr std::array<double, kSpecies.size()> kExpectedMassKg{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

struct SmallGrid {
    std::size_t nx;
    std::size_t ny;
    Kokkos::View<double**> area_m2;
};

inline SmallGrid MakeGrid(std::size_t nx, std::size_t ny, const std::vector<double>& areas_m2) {
    if (nx == 0 || ny == 0 || nx > areas_m2.size() / ny || nx * ny != areas_m2.size()) {
        throw std::invalid_argument("Synthetic fire grid dimensions must match its nonempty area array");
    }
    for (double area : areas_m2) {
        if (!std::isfinite(area) || area <= 0.0) {
            throw std::invalid_argument("Synthetic fire grid areas must be finite and positive");
        }
    }
    SmallGrid grid{nx, ny, Kokkos::View<double**>("synthetic_fire_area", ny, nx)};
    auto host = Kokkos::create_mirror_view(grid.area_m2);
    for (std::size_t j = 0; j < ny; ++j) {
        for (std::size_t i = 0; i < nx; ++i) {
            host(j, i) = areas_m2[j * nx + i];
        }
    }
    Kokkos::deep_copy(grid.area_m2, host);
    return grid;
}

inline Kokkos::View<double***> MakeMassField(const SmallGrid& grid, const std::vector<double>& species_cell_mass_kg) {
    const std::size_t cells = grid.nx * grid.ny;
    if (cells == 0 || species_cell_mass_kg.size() / cells != kSpecies.size() || species_cell_mass_kg.size() % cells != 0) {
        throw std::invalid_argument("Synthetic fire mass array must contain every species and cell");
    }
    for (double mass : species_cell_mass_kg) {
        if (!std::isfinite(mass) || mass < 0.0) {
            throw std::invalid_argument("Synthetic fire mass must be finite and nonnegative");
        }
    }
    Kokkos::View<double***> mass("synthetic_fire_mass", kSpecies.size(), grid.ny, grid.nx);
    auto host = Kokkos::create_mirror_view(mass);
    for (std::size_t s = 0; s < kSpecies.size(); ++s) {
        for (std::size_t j = 0; j < grid.ny; ++j) {
            for (std::size_t i = 0; i < grid.nx; ++i) {
                host(s, j, i) = species_cell_mass_kg[s * cells + j * grid.nx + i];
            }
        }
    }
    Kokkos::deep_copy(mass, host);
    return mass;
}

inline double SpeciesBudgetKg(const Kokkos::View<double***>& mass, std::size_t species) {
    if (mass.extent(0) != kSpecies.size() || species >= kSpecies.size()) {
        throw std::invalid_argument("Synthetic fire budget requires a valid species index and complete mass field");
    }
    const auto host = Kokkos::create_mirror_view_and_copy(Kokkos::HostSpace{}, mass);
    double budget = 0.0;
    for (std::size_t j = 0; j < mass.extent(1); ++j) {
        for (std::size_t i = 0; i < mass.extent(2); ++i) {
            budget += host(species, j, i);
        }
    }
    return budget;
}

}  // namespace cece::test::fire
