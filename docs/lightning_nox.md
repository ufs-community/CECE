# Lightning NOx

## Overview

Computes lightning-produced nitrogen oxide (NOx) emissions based on convective cloud top height and empirical flash rate–yield relationships. Lightning is a significant natural source of NOx in the middle and upper troposphere, affecting ozone chemistry and atmospheric composition.

The flash rate is parameterized as a power law of cloud top height, with separate NOx yield factors for land and ocean regions.

References:
- Price, C., et al. (1997), Vertical distributions of lightning NOx for use in regional and global chemical transport models, *JGR*, 102(D5), 5943–5941.

## Registration Names

- Native C++: `"lightning"`
- Fortran bridge: `"lightning_fortran"` (legacy behavior; the diagnostics and corrections below apply only to native C++)

## Configuration Parameters

| YAML Key | Type | Default | Description |
| --- | --- | --- | --- |
| `yield_land` | double | 3.011e26 | NOx yield factor for land regions [molecules/flash] |
| `yield_ocean` | double | 1.566e26 | NOx yield factor for ocean regions [molecules/flash] |
| `flash_rate_coeff` | double | 3.44e-5 | Flash rate coefficient for cloud height in km; the default Price-style convention is flashes/minute/cell |
| `flash_rate_power` | double | 4.9 | Flash rate power-law exponent |
| `flash_rate_time_unit` | string | `minutes` | Time unit of the configured power-law coefficient: `minutes` or `seconds`. Exports are always normalized to seconds. |

Yields and the coefficient must be finite and nonnegative; the exponent must be finite and positive. The default yields are 500 mol NO/flash over land and approximately 260.0465 mol NO/flash over ocean, using Avogadro's number `6.022e23`.

## Import Fields

| Field Name | Units | Description |
| --- | --- | --- |
| `cloud_top_height` | m | Convective cloud top height |
| `land_mask` | dimensionless | Land fraction (>0.5 = land); optional |
| `cell_area` | m2 | Actual horizontal cell area; required only when `lightning_flash_density` is requested. Every cell must have a finite positive area. |

These column inputs are read at vertical index 0. Horizontal dimensions must match the emission field. Use `input_mapping` to bind different external field names.

## Export Fields

| Field Name | Units | Description |
| --- | --- | --- |
| `lightning_nox_emissions` | kg NO/s/cell | NO-equivalent mass production, distributed uniformly across vertical levels; required |
| `lightning_flash_rate` | flashes/s/cell | Optional instantaneous column flash rate |
| `lightning_flash_density` | flashes/km2/hour | Optional instantaneous column flash density |
| `lightning_nox_efficiency` | mol NO/flash | Optional production efficiency diagnosed from the scheme's own NO production divided by its flash rate |
| `lightning_nox_production` | kg NO/s/cell | Optional column-integrated NO production, before vertical distribution |

The diagnostics are export fields, not diagnostic-manager registrations. The standalone driver allocates them when listed in `output.fields`. Use `output_mapping` for external names, and advertise physics-owned fields with empty `species` layer lists so the stacking engine does not overwrite them.

Column diagnostics occupy vertical index 0 only. Other levels are zero for rate, density, and production, and NaN for efficiency. Zero, negative, or nonfinite cloud height produces zero flashes and zero NO production. Efficiency is NaN when the flash rate is zero. Each call overwrites the emission and diagnostic fields; these are instantaneous rates, not timestep totals or accumulated emissions. A scheme should own its export names; combine separate source fields through the stacking engine instead of sharing one lightning output across schemes.

## Algorithm

1. Convert cloud top height to km: `h_km = h / 1000`.
2. Compute flashes/s/cell: `F = coeff * h_km^power / T`, where `T=60` for `minutes` and `T=1` for `seconds`.
3. Select the molecules/flash yield based on the land mask.
4. Compute column NO production: `Q = F * yield_factor / 6.022e23 * 0.030` kg NO/s/cell.
5. Compute flash density: `D = F * 3600 * 1e6 / cell_area` flashes/km2/hour.
6. Diagnose efficiency: `E = Q / (0.030 * F)` mol NO/flash, or NaN without flashes.
7. Distribute uniformly across vertical levels: `level_yield = Q / nz`. The vertical sum equals `Q`.

The emission field is cell-integrated mass rate, not kg/m2/s. Divide `Q` by actual cell area when a downstream consumer needs an areal flux. Integrate flash rate over time for flash counts; compute aggregated efficiency as total produced moles divided by total flashes, not an unweighted mean of cell efficiencies.

## YAML Configuration Example

```yaml
physics_schemes:
  - name: lightning
    language: cpp
    options:
      yield_land: 3.011e26
      yield_ocean: 1.566e26
      flash_rate_coeff: 3.44e-5
      flash_rate_power: 4.9
      flash_rate_time_unit: minutes
```

## Synthetic Diagnostic Example

Run from the repository root with a configured CECE build and Python dependencies `numpy`, `xarray`, and `h5netcdf`:

```bash
python -m pip install numpy xarray h5netcdf
python tools/lightning_diagnostics_example.py generate
cmake --build build --target cece_standalone_driver test_physics
./build/test_physics --gtest_filter='PhysicsTest.Lightning*'
./build/cece_standalone_driver examples/cece_config_lightning_diagnostics.yaml
python tools/lightning_diagnostics_example.py check
```

The [configuration](../examples/cece_config_lightning_diagnostics.yaml) maps the exports to `LIGHTNING_NO`, `FLASH_RATE`, `FLASH_DENSITY`, `NO_PER_FLASH`, and `NO_PRODUCTION`. It uses a 4x2x3 grid with cloud heights 0, 5, 10, and 15 km, one land row and one ocean row. The generator computes spherical cell areas from explicit one-degree cell boundaries. The second timestep is clear sky to catch stale or accumulating output. No external meteorological download is needed.

At 10 km, the default flash rate is approximately `0.04554148546` flashes/s/cell. Land efficiency is 500 mol NO/flash and ocean efficiency is approximately 260.0465 mol NO/flash. The checker verifies these relationships, area-normalized density, all three vertical NO levels, output units, and clear-sky resetting. The standalone writer labels each result with the end-of-step time, so inputs at 00:00 and 01:00 produce files labeled 01:00 and 02:00. Use a clean output directory; the checker expects exactly two `lightning_*.nc` files.

## Compatibility Notes

The previous native implementation divided cell-integrated production by an unexplained `1e6`, treated the raw coefficient as a per-second rate, and added into the previous emission field. These are now corrected. With default minute-based coefficients, positive-column NO production increases by `1e6/60` relative to the old single-call result. Set `flash_rate_time_unit: seconds` only for a coefficient calibrated in flashes/s; it does not restore the erroneous mass conversion.

The Fortran bridge is unchanged and is no longer used as the native lightning correctness oracle. Independent native tests check molecular-to-molar conversion, time units, land/ocean yields, mappings, density, vertical conservation, repeated runs, invalid areas, and empty MPI bands. Uniform vertical distribution remains a simple proxy, not a resolved lightning channel profile. If `land_mask` is absent, all cells are treated as land.
