# Sea Salt (GEOS-12)

## Overview

Computes size-resolved sea salt aerosol emissions over open ocean using the
GEOS-12 (GEOS-Chem 2012) scheme: the Gong (2003) source function with the
Jaeglé et al. (2011) sea surface temperature (SST) correction, an optional
Fan & Toon (2011) Weibull wind-speed correction, and a Great Lakes / Caspian
Sea deep-lake mask. Each GOCART size bin is sub-integrated over `NR = 10`
linear dry-radius sub-bins and published as its own mass and number emission
field. It is a GPU-portable native Kokkos scheme.

References:
- Gong, S.L. (2003), A parameterization of sea-salt aerosol source function for sub- and super-micron particles, *Global Biogeochem. Cycles*, 17(4).
- Jaeglé, L., et al. (2011), Global distribution of sea salt aerosols: new constraints from in situ and remote sensing observations, *Atmos. Chem. Phys.*, 11.
- Fan, T., and Toon, O.B. (2011), Modeling sea-salt aerosol in a coupled climate and sectional microphysical model, *Atmos. Chem. Phys.*, 11.

## Registration Name

- Native C++ (Kokkos): `"sea_salt_geos12"`

## Configuration Parameters

| YAML Key | Type | Default | Description |
| --- | --- | --- | --- |
| `mechanism_file` | string | `data/speciation/spc_cb6_gocart.yaml` | MICM mechanism file providing per-bin aerosol properties (density, lower/upper/effective dry radius). |
| `speciation_file` | string | `data/speciation/map_cb6_gocart.yaml` | Aerosol map selecting which bins to emit and their per-bin scale. |
| `speciation_dataset` | string | `SEASALT` | Dataset within the map that selects the sea salt bins. |
| `weibull_flag` | bool | `false` | Enable the Fan & Toon (2011) Weibull wind-speed correction. |
| `scale_factor` | double | `1.0` | Global tuning scale factor. |

Per-bin size properties (density, dry-radius ranges) come from the mechanism
file, not from individual YAML keys. The default GOCART dataset defines five
bins (`seas1`..`seas5`).

## Import Fields

Resolved through `input_mapping`. `LAT`/`LON` are published by the driver from
the target grid.

| Internal Name | Units | Description |
| --- | --- | --- |
| `frocean` | 1 | Ocean fraction |
| `frseaice` | 1 | Sea-ice fraction |
| `lat` | deg | Latitude |
| `lon` | deg | Longitude |
| `sst` | K | Sea surface temperature |
| `u10m` | m/s | 10-m eastward wind |
| `v10m` | m/s | 10-m northward wind |
| `ustar` | m/s | Friction velocity (drives the Gong source function) |

## Export Fields

One field per named size bin, plus column totals.

| Field Name | Units | Description |
| --- | --- | --- |
| `seasalt_mass_<bin>` | kg/m²/s | Mass emission flux per bin (e.g. `seasalt_mass_seas1`) |
| `seasalt_number_<bin>` | #/m²/s | Number emission flux per bin |
| `seasalt_mass_total` | kg/m²/s | Sum of per-bin mass emissions |
| `seasalt_number_total` | #/m²/s | Sum of per-bin number emissions |

The static per-bin effective dry radius is exposed as the
`seasalt_effective_radius` diagnostic.

## Algorithm

For each ocean cell (`frocean - frseaice > 0`):

1. Compute the 10-m wind speed and the optional Weibull correction factor
   (uses the incomplete gamma function; cells whose evaluation overflows are
   skipped).
2. Apply the Jaeglé et al. (2011) SST correction (skin temperature clamped to
   [-0.1, 36] °C, result clamped to [0, 7]).
3. Apply the Great Lakes (lat 40.5-50, lon 267-285 °E) and Caspian Sea
   (lat 35-48, lon 45-56 °E) deep-lake mask.
4. For each bin, sub-integrate the Gong (2003) source function over `NR = 10`
   dry-radius sub-bins using the wet radius at 80% RH (`R80FAC = 1.65`),
   accumulating both number and mass (mass uses the per-bin density).
5. Scale by ocean fraction × deep-lake mask × Weibull × SST correction ×
   `scale_factor`, and clamp to non-negative.

## YAML Configuration Example

```yaml
physics_schemes:
  - name: sea_salt_geos12
    language: cpp
    options:
      mechanism_file: data/speciation/spc_cb6_gocart.yaml
      speciation_file: data/speciation/map_cb6_gocart.yaml
      speciation_dataset: SEASALT
      weibull_flag: false
      scale_factor: 1.0
      input_mapping:
        frocean: FROCEAN
        frseaice: FRSEAICE
        lat: LAT
        lon: LON
        sst: SST
        u10m: U10M
        v10m: V10M
        ustar: USTAR
      output_mapping:
        seasalt_mass_total: SS_MASS_TOTAL
        seasalt_number_total: SS_NUMB_TOTAL
```

## Implementation Notes

- GPU-portable native Kokkos scheme; the emission kernel runs on the device and
  writes per-bin fields directly (no host round-trip).
- Per-bin properties and the aerosol map are loaded once during `Initialize`
  via the shared speciation loader; only the emission kernel runs per timestep.
- The former Fortran bridge (`sea_salt_geos12_fortran`) and the legacy
  `sea_salt` / `sea_salt_fortran` schemes have been retired.
