# Briggs (1969) point-source plume rise

The native Briggs plume-rise method implements the
[NOAA standalone Briggs model ported from CMAQ 5.3.1](https://github.com/noaa-oar-arl/briggs_plumerise),
revision `4c1ebe6ff28941a4df727ed1af078e49778451ff`.
It computes one independent stack plume per horizontal column using
double-precision Kokkos kernels, with no Fortran implementation or runtime bridge.
It is available through the general `plume_rise` physics scheme and supports the
usual `input_mapping` and `output_mapping` options.

## Plume-rise method selection

The physics-scheme name is `plume_rise`. Its `method` option selects the
plume-rise algorithm independently of the CECE scheme lifecycle:

```yaml
physics_schemes:
  - name: plume_rise
    language: cpp
    options:
      method: briggs
```

`method` defaults to `briggs` if omitted. The old top-level physics-scheme name
`briggs` remains registered as a backwards-compatible alias and invokes the
same implementation. New configurations should use `plume_rise` with an
explicit `method`.

Plume-rise implementations are registered separately through
`PlumeRiseMethodRegistration<T>` and selected by `PlumeRiseScheme`. Each method
is a `PhysicsScheme` implementation that receives the same CECE import/export
states and configuration mappings. This keeps method-specific calculations
separate while sharing the physics-scheme interface, factory entry, field
mappings, and lifecycle. An unregistered or not-yet-implemented method fails
during initialization with a list of currently implemented methods; it does
not silently fall back to Briggs.

Currently registered method:

- `briggs` — implemented and validated for point-source stacks.

Reserved for future implementation (not yet available):

- `sofiev` — fire plume-rise approach based on fire radiative power and
  atmospheric boundary-layer/free-tropospheric stability inputs.
- `freitas` — vegetation-fire plume-rise parameterization.
- `grell_freitas` — retained as a distinct possible selector, not treated as
  an alias for `freitas`.

The comparison paper cited below evaluates **Briggs, Freitas, and Sofiev**.
It does not name a “Grell–Freitas plume-rise scheme”; Grell and Freitas is the
name of a separate scale- and aerosol-aware convective parameterization.
Accordingly `freitas` is the canonical selector for the paper’s three-scheme
comparison. `grell_freitas` is reserved only to avoid conflating the two
parameterizations and is not implemented.

The general framework does not assume that methods share identical inputs.
Briggs uses stack geometry, exit temperature and velocity, heat flux, and
layer meteorology; future fire methods can consume FRP and other method-specific
fields through the same configured mappings. Converting FRP to a Briggs heat
flux, where desired, belongs in an explicit upstream conversion or a documented
Briggs input adapter; it is not implicitly performed by this method selector.

## Field contract

All fields are CECE LayoutLeft views indexed `(i, j, k)`. Heights are meters
above local ground, not altitude above sea level. Layers are bottom-up.
There must be at least two layers.

| Import field | Units | Vertical extent |
|---|---|---|
| `layer_surface_height` | m; **upper face** of each layer | nz |
| `layer_center_height` | m; center inside each layer | nz |
| `pressure` | hPa at upper faces, as in the upstream fixture | nz |
| `temperature` | K | nz |
| `water_vapor_mixing_ratio` | kg/kg | nz |
| `eastward_wind`, `northward_wind` | m/s | nz |
| `sensible_heat_flux` | upstream numeric HFX; see below | 1 or nz |
| `mixing_height` | m | 1 or nz |
| `friction_velocity` | m/s | 1 or nz |
| `surface_temperature` | K | 1 or nz |
| `surface_pressure` | hPa | 1 or nz |
| `stack_diameter`, `stack_height` | m | 1 or nz |
| `stack_temperature` | K | 1 or nz |
| `stack_velocity` | m/s | 1 or nz |

Surface and stack fields use only `k=0` when supplied with full depth. All
horizontal extents must agree. No implicit horizontal broadcasting is performed.
All consumed inputs must be finite; temperatures, pressures and stack diameter must be
positive. Humidity, mixing height, friction velocity, stack height and exit
velocity must be nonnegative. Face heights must strictly increase, centers must
lie inside their layers, and pressure must not increase with height.
Missing fields, invalid shapes or values, and invalid computed heights throw
explicit exceptions before export values are changed.

| Export field | Units | Vertical extent |
|---|---|---|
| `plume_centerline_height` | m above ground | 1 or nz |
| `plume_fractions` | dimensionless | nz |

A full-depth height export repeats the same centerline at each level to support
CECE clients that allocate every output at grid depth. Fractions are nonnegative
and sum to one per column. Outputs are **overwritten**, not accumulated.
Height and fraction exports must be distinct storage and must not alias inputs.
The scheme does not multiply fractions by emissions or modify species fluxes:
the caller may use them to distribute a point-source total into emission layers.
Separate stacks sharing a horizontal cell must be evaluated separately and
combined using their emission-weighted fractions, not averaged stack properties.

## Compatibility and repaired defects

This is a port of the **standalone implementation**, not a new canonical
interpretation of all Briggs formulations.

- Ambient stack temperature and scalar wind speed use the nearest layer center.
  Ties select the lower layer. Wind used for rise is floored at 1 m/s.
- Virtual face temperatures use linear interpolation and top extrapolation.
- The upstream `DTHDZ` calculation **multiplies** the face potential-temperature
  difference by layer thickness. This dimensionally unusual expression is
  deliberately retained for reference compatibility; it is not silently replaced
  by a physical derivative.
- Upstream documentation labels HFX as W/m2, while the routine uses
  `GRAV * HFX / TA(1)` directly and comments describe kinematic flux in K m/s.
  CECE passes the numeric input through unchanged; the reference uses `25.0`.
  For physical applications supplying an energy flux, the caller must explicitly
  convert W/m2 to kinematic K m/s using air density and heat capacity.
  There is no hidden conversion in this scheme.
- Surface temperature and pressure are required for interface parity, but the
  upstream surface-potential-temperature calculation has no effect after its
  first-layer gradient is replaced by the second-layer value. That dead
  calculation is omitted.
- The upstream dummy arrays `ZF(0:)` and `PRES(0:)` are incorrectly indexed as
  `1:EMLAYS` for the supplied 35-element arrays. A bounds-checked original
  driver fails at its top-face interpolation. CECE indexes every fixture row
  consistently from zero: row 0 is the first layer's upper face, not ground.
- CECE computes stack-to-face distance for **every** layer, including the
  uninitialized upstream final `ZSTK` entry. Stack-layer lookup is defined for
  stacks below, within and above the domain.
- The nonpositive-buoyancy return in the upstream routine leaves fractions
  undefined. CECE still uses its momentum-only centerline, then computes fractions.
- Zero friction velocity uses the neutral formula's `10 * stack_height`
  limiting bound without division by zero.
- Turner plume bounds are `stack_height + 0.5 * rise` and
  `stack_height + 1.5 * rise`, where rise is centerline minus stack height.
  Despite upstream comments mentioning uniform pressure, its fraction formulas
  are geometric height overlaps, which CECE preserves.
- The top emission layer receives any plume portion above the domain, conserving
  the column total. Buoyant iterative rise stops at the last face; a stack above
  that face retains its stack height rather than acquiring negative rise.
  Momentum-only and momentum-bound rise bypass this clipping.
- The upstream low-level hot-stack adjustment is retained as written (including
  its subsequent stack-height/2/3 conversion); it is not reinterpreted.

## Example and validation

[cece_config_briggs.yaml](../examples/cece_config_briggs.yaml) selects
`plume_rise` with `method: briggs` and contains mappings,
the supplied 35-level profile path and all nine surface/stack numeric inputs.
Its `options.column_test` block is consumed by the native unit test, **not** by the normal
CECE standalone driver. Production use binds these imports and the two exports
through CECE's state interface and uses the same `physics_schemes` entry.

With the normal CECE dependencies available:

```sh
cmake -S . -B build -DCECE_BUILD_TESTING=ON
cmake --build build --target test_briggs -j2
OMP_NUM_THREADS=2 ctest --test-dir build -R '^BriggsTest\.' --output-on-failure
```

The profile fixture is byte-identical to the pinned upstream `input_profile.txt`.
Reference numbers were independently generated with GNU Fortran 13 using the
upstream routine, with the following **reference-only repairs**:

1. Change dummy arrays `ZF(0:)` and `PRES(0:)` to `ZF(:)` and `PRES(:)`.
2. Initialize `LPBL` and `LSTK` to 1 before their lookup loop.
3. Set `ZSTK(EMLAYS) = ZF(EMLAYS) - STKHT` before top-face extrapolation.
4. Replace the momentum-only `RETURN` with a jump to the `ZTOP` fraction block.

Compile the repaired routine and upstream driver with
`gfortran -fcheck=bounds -fdefault-real-8 plumerise_briggs_mod.F90 briggs_driver.F90`.
Dropping `-fdefault-real-8` reproduces the upstream single-precision arithmetic.
These reference files are not included in CECE or linked into its tests.

| Case (otherwise example inputs) | Double-precision centerline (m) |
|---|---:|
| Supplied example | 219.41083364358906 |
| HFX = -25 or 0 | 200.43017601095414 |
| Mixing height = 100 m | 204.60262911853181 |
| Stack temperature = 280 K (momentum only) | 376.23359809337614 |
| Both wind components = 0 | 228.74170673992165 |
| Synthetic neutral, HFX = 0 | 477.32459292859915 |
| Synthetic unstable, HFX = 25 | 474.44001884101391 |
| Synthetic stable, HFX = -25 | 464.96859893176463 |

Synthetic cases use 35 layers with upper faces `100*(k+1)` m, centers
`100*k+50` m, pressure 1000 hPa, temperature 300 K, zero humidity,
eastward wind 10 m/s, northward wind zero, and otherwise the example inputs.

The example's only nonzero fractions are layer 4:
`0.25398545808009954` and layer 5: `0.74601454191990046` (zero-based).
The single-precision reference is 219.410492 m with fractions
0.253992230 and 0.746007740. Tests compare **all** layers and all columns,
using absolute tolerance 1e-9 for double reference results and 5e-4 for the
single-precision reference, plus a 1e-12 column conservation check.
Additional tests cover repeated execution, mapping/factory integration,
independent columns, surface/full-depth output shapes, zero friction/exit
velocity, top-layer overflow, two-layer profiles, missing fields, invalid shapes,
nonfinite inputs, arithmetic overflow, aliased exports and invalid physical inputs.

## Attribution and permission

The method-selection design is motivated in part by Li et al. (2023),
[“Impacts of estimated plume rise on PM2.5 exceedance prediction during extreme
wildfire events: a comparison of three schemes (Briggs, Freitas, and Sofiev)”](https://doi.org/10.5194/acp-23-3083-2023),
*Atmospheric Chemistry and Physics*, 23, 3083–3101. The article compares
three schemes; CECE’s selector reserves those method names without claiming
the Freitas or Sofiev methods are implemented.

Reference: Briggs, G. A., 1969, *Plume Rise*, USAEC Critical Review Series,
TID-25075, NTIS, 81 pp. The standalone port credits Patrick Campbell (February
2021), adapted from CMAQ 5.3.1, with earlier RADM/SMOKE ancestry.

The upstream routine includes the following permission notice:

> The Community Multiscale Air Quality (CMAQ) system software is in
> continuous development by various groups and is based on information
> from these groups: Federal Government employees, contractors working
> within a United States Government contract, and non-Federal sources
> including research institutions. These groups give the Government
> permission to use, prepare derivative works of, and distribute copies
> of their work in the CMAQ system to the public and to permit others
> to do so. The United States Environmental Protection Agency
> therefore grants similar permission to use the CMAQ system software,
> but users are requested to provide copies of derivative works or
> products designed to operate in the CMAQ system to the United States
> Government without restrictions as to use by others. Software
> that is used with the CMAQ system but distributed under the GNU
> General Public License or the GNU Lesser General Public License is
> subject to their copyright restrictions.

The upstream also attributes modified SMOKE work to
Copyright (C) 2002, MCNC Environmental Modeling Center, All Rights Reserved,
and references its COPYRIGHT conditions. This native derivative preserves that
attribution and the CMAQ permission above. NOAA provides the scientific product
as-is, without implying government endorsement; see the upstream README for
its full disclaimer.
