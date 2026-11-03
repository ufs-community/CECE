// CMAQ-derived Briggs (1969) standalone algorithm:
// https://github.com/noaa-oar-arl/briggs_plumerise
// Upstream permission and compatibility notes: docs/briggs.md.
#include "cece/physics/cece_briggs.hpp"

#include <Kokkos_Array.hpp>
#include <Kokkos_MathematicalFunctions.hpp>
#include <array>
#include <stdexcept>

#include "cece/cece_physics_factory.hpp"

namespace cece {
namespace {

PhysicsRegistration<BriggsScheme> register_briggs("briggs");

enum Input { ZF, ZH, PRES, TA, QV, UW, VW, HFX, HMIX, USTAR, TSFC, PSFC, STKDM, STKHT, STKTK, STKVE, INPUT_COUNT };
constexpr std::array<const char*, INPUT_COUNT> names = {
    "layer_surface_height", "layer_center_height", "pressure", "temperature", "water_vapor_mixing_ratio", "eastward_wind",
    "northward_wind", "sensible_heat_flux", "mixing_height", "friction_velocity", "surface_temperature", "surface_pressure",
    "stack_diameter", "stack_height", "stack_temperature", "stack_velocity"};
constexpr double gravity = 9.80622;
constexpr double small = 3.0e-5;
constexpr double hcrit = 3.0e-6;
using InputView = Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>;

struct Column {
    Kokkos::Array<InputView, INPUT_COUNT> fields;
    int i, j, nz;

    KOKKOS_FUNCTION double get(Input field, int k = 0) const { return fields[field](i, j, k); }
    KOKKOS_FUNCTION double wind(int k) const {
        return Kokkos::sqrt(get(UW, k) * get(UW, k) + get(VW, k) * get(VW, k));
    }
    KOKKOS_FUNCTION double virtual_temperature(int k) const {
        const double q = get(QV, k);
        return get(TA, k) * (1.0 + 0.622 * q / (1.0 + q));
    }
    KOKKOS_FUNCTION double face_temperature(int k) const {
        const int other = k + 1 < nz ? k + 1 : k - 1;
        return virtual_temperature(k) + (virtual_temperature(other) - virtual_temperature(k)) *
                                           (get(ZF, k) - get(ZH, k)) / (get(ZH, other) - get(ZH, k));
    }
    KOKKOS_FUNCTION double gradient(int k) const {
        if (k == 0) k = 1;
        const double upper = face_temperature(k) * Kokkos::pow(1000.0 / get(PRES, k), 0.286);
        const double lower = face_temperature(k - 1) * Kokkos::pow(1000.0 / get(PRES, k - 1), 0.286);
        // Retain the standalone model's multiplication, not a derivative.
        return (get(ZF, k) - get(ZF, k - 1)) * (upper - lower);
    }
    KOKKOS_FUNCTION int nearest_center() const {
        int nearest = 0;
        for (int k = 1; k < nz; ++k) {
            if (Kokkos::fabs(get(STKHT) - get(ZH, k)) < Kokkos::fabs(get(STKHT) - get(ZH, nearest))) nearest = k;
        }
        return nearest;
    }
    KOKKOS_FUNCTION double buoyancy(int nearest) const {
        return 0.25 * gravity * (get(STKTK) - get(TA, nearest)) * get(STKVE) * get(STKDM) * get(STKDM) / get(STKTK);
    }
    KOKKOS_FUNCTION double momentum(int nearest) const {
        return 3.0 * get(STKDM) * get(STKVE) / Kokkos::fmax(wind(nearest), 1.0);
    }
};

KOKKOS_FUNCTION double neutral(double height, double buoyancy, double wind, double ustar) {
    if (ustar == 0.0) return 10.0 * height;
    const double ratio = buoyancy / (wind * ustar * ustar);
    return Kokkos::fmin(10.0 * height, 1.2 * Kokkos::pow(ratio, 0.6) * Kokkos::pow(height + 1.3 * ratio, 0.4));
}

KOKKOS_FUNCTION double stable(double buoyancy, double wind, double stability) {
    return 2.6 * Kokkos::pow(buoyancy / (wind * stability), 1.0 / 3.0);
}

KOKKOS_FUNCTION double unstable(double buoyancy, double wind) { return 30.0 * Kokkos::pow(buoyancy / wind, 0.6); }

KOKKOS_FUNCTION double centerline(const Column& c) {
    const double height = c.get(STKHT);
    const double diameter = c.get(STKDM);
    const double velocity = c.get(STKVE);
    const double exhaust = c.get(STKTK);
    const double ustar = c.get(USTAR);
    int stack_layer = c.nz - 1;
    for (int k = 0; k < c.nz; ++k) {
        if (height <= c.get(ZF, k)) {
            stack_layer = k;
            break;
        }
    }
    const int nearest = c.nearest_center();
    const double ambient = c.get(TA, nearest);
    const double wind_stack = Kokkos::fmax(c.wind(nearest), 1.0);
    double momentum = c.momentum(nearest);
    const double buoyancy = c.buoyancy(nearest);
    if (buoyancy <= 0.0) return height + Kokkos::fmax(momentum, 2.0);

    const double hstar = gravity * c.get(HFX) / c.get(TA);
    double stability = Kokkos::fmax(gravity * c.gradient(stack_layer) / ambient, small);
    double rise;
    int regime;
    if (hstar > hcrit) {
        double dh;
        if (c.get(HMIX) <= height) {
            if (c.gradient(stack_layer) > 0.001) {
                const double stable_momentum = 0.646 * Kokkos::pow(velocity * velocity * diameter * diameter /
                                                                     (exhaust * wind_stack), 1.0 / 3.0) *
                                               Kokkos::sqrt(ambient) / Kokkos::pow(c.gradient(stack_layer), 1.0 / 6.0);
                momentum = Kokkos::fmin(momentum, stable_momentum);
            }
            dh = stable(buoyancy, wind_stack, stability);
            regime = 3;
        } else {
            dh = unstable(buoyancy, wind_stack);
            regime = 1;
        }
        const double dhn = neutral(height, buoyancy, wind_stack, ustar);
        if (dhn < dh) {
            dh = dhn;
            regime = 2;
        }
        if (dh < momentum) {
            dh = momentum;
            regime = 4;
        }
        rise = 1.5 * dh;
    } else if (hstar < -hcrit || c.gradient(stack_layer) > 0.001) {
        const double dhn = 1.5 * neutral(height, buoyancy, wind_stack, ustar);
        rise = 1.5 * stable(buoyancy, wind_stack, stability);
        regime = 3;
        if (dhn < rise) {
            rise = dhn;
            regime = 2;
        }
    } else {
        rise = 1.5 * neutral(height, buoyancy, wind_stack, ustar);
        regime = 2;
    }

    if (regime != 4) {
        int layer = stack_layer;
        int iteration = 0;
        while (rise > c.get(ZF, layer) - height) {
            if (layer == c.nz - 1) {
                // A source above the domain remains in the top emission layer.
                rise = Kokkos::fmax(0.0, Kokkos::fmin(rise, c.get(ZF, layer) - height));
                break;
            }
            double residual;
            const double wind = iteration++ == 0 ? wind_stack : c.wind(layer);
            const double remaining = rise - (c.get(ZF, layer) - height);
            if (regime == 1) {
                residual = wind * Kokkos::pow(remaining / 45.0, 5.0 / 3.0);
            } else if (regime == 2) {
                residual = Kokkos::pow(remaining, 5.0 / 3.0) * wind * ustar * ustar /
                           (2.664 * Kokkos::pow(height + (2.0 / 3.0) * rise, 2.0 / 3.0));
            } else {
                residual = wind * stability * remaining * remaining * remaining / 59.319;
            }
            ++layer;
            const double next_wind = Kokkos::fmax(c.wind(layer), 1.0);
            stability = gravity * c.gradient(layer) / c.get(TA, layer);
            const double dhn = neutral(height, residual, next_wind, ustar);
            double dh = stability > small ? stable(residual, next_wind, stability) : unstable(residual, next_wind);
            regime = stability > small ? 3 : 1;
            if (dhn < dh) {
                dh = dhn;
                regime = 2;
            }
            rise = c.get(ZF, layer - 1) - height + 1.5 * dh;
        }
    }
    if (height + (2.0 / 3.0) * rise <= c.get(ZF) && exhaust > c.get(TA)) rise = c.get(ZH, 1);
    return height + (2.0 / 3.0) * rise;
}

}  // namespace

void BriggsScheme::Run(CeceImportState& import_state, CeceExportState& export_state) {
    Kokkos::Array<InputView, INPUT_COUNT> fields;
    for (int f = 0; f < INPUT_COUNT; ++f) {
        fields[f] = ResolveImport(names[f], import_state);
        RequireFields("Briggs", {{MapInput(names[f]), fields[f].data() != nullptr}});
    }
    auto height = ResolveExport("plume_centerline_height", export_state);
    auto fractions = ResolveExport("plume_fractions", export_state);
    RequireFields("Briggs", {{MapOutput("plume_centerline_height"), height.data() != nullptr},
                            {MapOutput("plume_fractions"), fractions.data() != nullptr}});
    if (height.data() == fractions.data()) throw std::invalid_argument("Briggs height and fraction exports must not alias");
    for (int f = 0; f < INPUT_COUNT; ++f) {
        if (fields[f].data() == height.data() || fields[f].data() == fractions.data()) {
            throw std::invalid_argument("Briggs exports must not alias input: " + MapInput(names[f]));
        }
    }
    const int nx = static_cast<int>(fields[ZF].extent(0));
    const int ny = static_cast<int>(fields[ZF].extent(1));
    const int nz = static_cast<int>(fields[ZF].extent(2));
    if (nx < 1 || ny < 1 || nz < 2) throw std::invalid_argument("Briggs requires nonempty columns with at least two layers");
    for (int f = 0; f < INPUT_COUNT; ++f) {
        const auto& v = fields[f];
        const bool depth_valid = f < HFX ? v.extent(2) == static_cast<std::size_t>(nz)
                                        : v.extent(2) == 1 || v.extent(2) == static_cast<std::size_t>(nz);
        if (v.extent(0) != static_cast<std::size_t>(nx) || v.extent(1) != static_cast<std::size_t>(ny) || !depth_valid) {
            throw std::invalid_argument("Briggs input shape mismatch: " + MapInput(names[f]));
        }
    }
    if (fractions.extent(0) != static_cast<std::size_t>(nx) || fractions.extent(1) != static_cast<std::size_t>(ny) ||
        fractions.extent(2) != static_cast<std::size_t>(nz) || height.extent(0) != static_cast<std::size_t>(nx) ||
        height.extent(1) != static_cast<std::size_t>(ny) || (height.extent(2) != 1 && height.extent(2) != static_cast<std::size_t>(nz))) {
        throw std::invalid_argument("Briggs output shape mismatch");
    }

    int invalid = 0;
    Kokkos::parallel_reduce(
        "BriggsValidate", Kokkos::MDRangePolicy<Kokkos::Rank<2>>({0, 0}, {nx, ny}),
        KOKKOS_LAMBDA(int i, int j, int& count) {
            const Column c{fields, i, j, nz};
            bool valid = true;
            for (int f = 0; f < INPUT_COUNT; ++f) {
                for (int k = 0; k < (f < HFX ? nz : 1); ++k) valid = valid && Kokkos::isfinite(fields[f](i, j, k));
            }
            for (int k = 0; k < nz; ++k) {
                const double bottom = k == 0 ? 0.0 : c.get(ZF, k - 1);
                valid = valid && c.get(ZF, k) > bottom && c.get(ZH, k) > bottom && c.get(ZH, k) < c.get(ZF, k) &&
                        c.get(PRES, k) > 0.0 && c.get(TA, k) > 0.0 && c.get(QV, k) >= 0.0;
                if (k > 0) valid = valid && c.get(PRES, k) <= c.get(PRES, k - 1);
                valid = valid && Kokkos::isfinite(c.wind(k)) && Kokkos::isfinite(c.gradient(k));
            }
            valid = valid && c.get(HMIX) >= 0.0 && c.get(USTAR) >= 0.0 && c.get(TSFC) > 0.0 && c.get(PSFC) > 0.0 &&
                    c.get(STKDM) > 0.0 && c.get(STKHT) >= 0.0 && c.get(STKTK) > 0.0 && c.get(STKVE) >= 0.0;
            const int nearest = c.nearest_center();
            valid = valid && Kokkos::isfinite(c.buoyancy(nearest)) && Kokkos::isfinite(c.momentum(nearest)) &&
                    Kokkos::isfinite(gravity * c.get(HFX) / c.get(TA));
            if (!valid) ++count;
        },
        invalid);
    if (invalid != 0) {
        throw std::invalid_argument("Briggs invalid meteorology, layer geometry or stack input in " + std::to_string(invalid) + " column(s)");
    }

    Kokkos::View<double**> centers("BriggsCenterlines", nx, ny);
    Kokkos::parallel_reduce(
        "BriggsCenterlines", Kokkos::MDRangePolicy<Kokkos::Rank<2>>({0, 0}, {nx, ny}),
        KOKKOS_LAMBDA(int i, int j, int& count) {
            const Column c{fields, i, j, nz};
            const double center = centerline(c);
            centers(i, j) = center;
            if (!Kokkos::isfinite(center) || center < c.get(STKHT) ||
                !Kokkos::isfinite(c.get(STKHT) + 1.5 * (center - c.get(STKHT)))) {
                ++count;
            }
        },
        invalid);
    if (invalid != 0) throw std::runtime_error("Briggs nonfinite or invalid computed plume height in " + std::to_string(invalid) + " column(s)");

    Kokkos::parallel_for(
        "BriggsPlumeFractions", Kokkos::MDRangePolicy<Kokkos::Rank<2>>({0, 0}, {nx, ny}), KOKKOS_LAMBDA(int i, int j) {
            const Column c{fields, i, j, nz};
            const double center = centers(i, j);
            for (std::size_t k = 0; k < height.extent(2); ++k) height(i, j, k) = center;
            const double rise = center - c.get(STKHT);
            const double bottom = c.get(STKHT) + 0.5 * rise;
            const double top = c.get(STKHT) + 1.5 * rise;
            int bottom_layer = nz - 1;
            for (int k = 0; k < nz; ++k) {
                fractions(i, j, k) = 0.0;
                if (bottom_layer == nz - 1 && bottom <= c.get(ZF, k)) bottom_layer = k;
            }
            if (rise <= 0.0 || bottom_layer == nz - 1) {
                fractions(i, j, bottom_layer) = 1.0;
            } else {
                for (int k = bottom_layer; k < nz; ++k) {
                    const double lower = k == bottom_layer ? bottom : c.get(ZF, k - 1);
                    const double upper = k == nz - 1 ? top : Kokkos::fmin(top, c.get(ZF, k));
                    fractions(i, j, k) = Kokkos::fmax(0.0, upper - lower) / rise;
                }
            }
        });
    Kokkos::fence();
    MarkModified("plume_centerline_height", export_state);
    MarkModified("plume_fractions", export_state);
}

}  // namespace cece
