# HEMCO to ACES Migration Guide

This guide walks HEMCO users through migrating their emission configurations to ACES.
ACES supports all HEMCO capabilities and adds new features including Kokkos GPU acceleration,
provenance tracking, and dynamic species registration.

## Table of Contents

1. [Overview of Differences](#overview-of-differences)
2. [Automated Conversion Tool](#automated-conversion-tool)
3. [Configuration Mapping Reference](#configuration-mapping-reference)
4. [Diagnostic Conversion](#diagnostic-conversion)
5. [Temporal Profiles](#temporal-profiles)
6. [Vertical Distribution](#vertical-distribution)
7. [Regional Inventories and Hierarchy](#regional-inventories-and-hierarchy)
8. [Scale Factors and Masks](#scale-factors-and-masks)
9. [New ACES Capabilities](#new-aces-capabilities)
10. [Troubleshooting](#troubleshooting)

---

## Overview of Differences

| Feature | HEMCO | ACES |
|---|---|---|
| Config format | `HEMCO_Config.rc` (custom text) | `aces_config.yaml` (YAML) |
| Diagnostics | `HEMCO_Diagn.rc` | `diagnostics:` block in YAML |
| Temporal profiles | Inline slash-separated values | `temporal_profiles:` block |
| Vertical distribution | Column 13 keyword | `vdist:` block per layer |
| Scale factors | Separate section with IDs | `scale_fields:` list per layer |
| Masks | Separate section with IDs | `mask:` list per layer |
| Performance | CPU only | CPU + GPU via Kokkos |
| Provenance | None | Built-in provenance tracking |
| Dynamic registration | Recompile required | Runtime `AddSpecies()` API |

---

## Automated Conversion Tool

The fastest way to migrate is the provided conversion script:

```bash
python3 scripts/hemco_to_aces.py HEMCO_Config.rc -o aces_config.yaml --diagn HEMCO_Diagn.rc
```

This converts:
- All base emissions to `species:` layers
- Scale factors to `scale_fields:` references or `temporal_profiles:`
- Masks to `mask:` references
- HEMCO_Diagn.rc variables to the `diagnostics:` block
- `$ROOT` path substitution

Review the output YAML and adjust file paths as needed before running ACES.

---

## Configuration Mapping Reference

### HEMCO Base Emission Entry

```
# HEMCO_Config.rc
# ExtNr Name       File                  Var   Time  CRE Dim Unit  Species ScalIDs Cat Hier
  0     MACCITY_CO data/MACCity.nc       CO    2000  1   2   kg/m2 CO      1/2     1   1
```

### Equivalent ACES YAML

```yaml
species:
  co:
    - field: MACCITY_CO
      operation: add
      hierarchy: 1
      category: anthropogenic
      scale: 1.0
      scale_fields: [hourly_scalfact]

aces_data:
  streams:
    - name: MACCITY_CO
      file: data/MACCity.nc
```

### Operation Mapping

| HEMCO | ACES |
|---|---|
| Hierarchy 1 (base) | `operation: add` |
| Hierarchy > 1 with override | `operation: replace` |

### Category Mapping

| HEMCO Cat | ACES Category |
|---|---|
| 1 | `anthropogenic` |
| 2 | `biogenic` |
| 3 | `biomass_burning` |
| 4 | `natural` |
| 5 | `aircraft` |
| 6 | `ship` |
| 7 | `soil` |
| 8 | `lightning` |
| 9 | `volcano` |

---

## Diagnostic Conversion

### HEMCO_Diagn.rc

```
# Name           Spec ExtNr Cat Hier Dim Unit     LongName
EmisNO_Total     NO   -1    -1  -1   2   kg/m2/s  NO_emission_flux_from_all_sectors
EmisCO_Anthro    CO   0     1   1    2   kg/m2/s  CO_anthropogenic_emissions
```

### Equivalent ACES YAML

```yaml
diagnostics:
  output_interval: 3600
  variables:
    - name: EmisNO_Total
      species: NO
      units: kg/m2/s
      long_name: NO_emission_flux_from_all_sectors
      dim: 2
    - name: EmisCO_Anthro
      species: CO
      units: kg/m2/s
      long_name: CO_anthropogenic_emissions
      dim: 2
```

---

## Temporal Profiles

### HEMCO Inline Diurnal Profile (24 values)

```
# ScalID Name         File                          Var  Time CRE Dim Unit Oper
1        HOURLY_SCALFACT 0.5/0.6/0.7/.../1.2/1.1  -    -    1   1   1    1
```

### Equivalent ACES YAML

```yaml
temporal_profiles:
  hourly_scalfact: [0.5, 0.6, 0.7, 0.8, 0.9, 1.0, 1.1, 1.2, 1.3, 1.2, 1.1, 1.0,
                    1.0, 1.1, 1.2, 1.3, 1.2, 1.1, 1.0, 0.9, 0.8, 0.7, 0.6, 0.5]

species:
  co:
    - field: MACCITY_CO
      operation: add
      hierarchy: 1
      scale: 1.0
      diurnal_cycle: hourly_scalfact
```

### Weekly Profile (7 values, Mon-Sun)

```yaml
temporal_profiles:
  weekly_nox: [1.0, 1.1, 1.1, 1.1, 1.1, 0.8, 0.7]

species:
  nox:
    - field: CEDS_NOx
      operation: add
      hierarchy: 1
      scale: 1.0
      weekly_cycle: weekly_nox
```

### Seasonal Profile (12 values, Jan-Dec)

```yaml
temporal_profiles:
  seasonal_biogenic: [0.3, 0.4, 0.6, 0.8, 1.0, 1.2, 1.3, 1.2, 1.0, 0.8, 0.5, 0.3]

species:
  isop:
    - field: MEGAN_ISOP
      operation: add
      hierarchy: 1
      scale: 1.0
      seasonal_cycle: seasonal_biogenic
```

All three cycle types can be combined — ACES applies them as a product:
`effective_scale = base_scale × diurnal_factor × weekly_factor × seasonal_factor`

---

## Vertical Distribution

### HEMCO Vertical Distribution Keywords

| HEMCO Keyword | ACES `vdist.method` |
|---|---|
| (none, 2D field) | `single` (layer 0) |
| `L(k)` | `single`, `layer_start: k-1` |
| `L(k1,k2)` | `range`, `layer_start: k1-1`, `layer_end: k2-1` |
| `P(p1,p2)` hPa | `pressure`, `p_start: p1*100`, `p_end: p2*100` (Pa) |
| `H(h1,h2)` m | `height`, `h_start: h1`, `h_end: h2` |
| `PBL` | `pbl` |

### Example: Pressure-Based Distribution

```yaml
species:
  aircraft_nox:
    - field: AEIC_NOx
      operation: add
      hierarchy: 1
      scale: 1.0
      vdist:
        method: pressure
        p_start: 20000.0   # 200 hPa in Pa
        p_end: 80000.0     # 800 hPa in Pa
```

### Example: PBL Distribution

```yaml
species:
  soil_nox:
    - field: SOILNOX
      operation: add
      hierarchy: 1
      scale: 1.0
      vdist:
        method: pbl
```

All vertical distribution methods conserve column mass within 1e-10 relative error.

---

## Regional Inventories and Hierarchy

HEMCO uses hierarchy levels to allow regional inventories to override global ones.
ACES replicates this with `hierarchy` and `operation: replace`.

### HEMCO Example

```
# Global inventory (hierarchy 1)
0  CEDS_CO  data/CEDS_CO.nc  CO  2015  1  2  kg/m2  CO  -  1  1
# European inventory overrides global in masked region (hierarchy 2)
0  EMEP_CO  data/EMEP_CO.nc  CO  2015  1  2  kg/m2  CO  500  1  2
```

Where ScalID 500 is a Europe mask.

### Equivalent ACES YAML

```yaml
species:
  co:
    - field: CEDS_CO
      operation: add
      hierarchy: 1
      category: anthropogenic
      scale: 1.0
    - field: EMEP_CO
      operation: replace
      hierarchy: 2
      category: anthropogenic
      scale: 1.0
      mask: mask_europe

aces_data:
  streams:
    - name: CEDS_CO
      file: data/CEDS_CO.nc
    - name: EMEP_CO
      file: data/EMEP_CO.nc
    - name: mask_europe
      file: data/Europe_mask.nc
```

The `replace` operation zeroes out lower-hierarchy contributions in the masked region
before adding the regional inventory value.

---

## Scale Factors and Masks

### HEMCO Scale Factor Section

```
# ScalID Name         File              Var    Time CRE Dim Unit Oper
1        HOURLY_SF    data/hourly.nc    HOURLY 2000 1   2   1    1
500      MASK_EUROPE  data/europe.nc    MASK   -    1   2   1    1
```

### Equivalent ACES YAML

```yaml
# Time-varying scale factor -> reference in scale_fields
species:
  co:
    - field: CEDS_CO
      operation: add
      hierarchy: 1
      scale: 1.0
      scale_fields: [hourly_sf]

# Geographical mask -> reference in mask
    - field: EMEP_CO
      operation: replace
      hierarchy: 2
      scale: 1.0
      mask: mask_europe

aces_data:
  streams:
    - name: hourly_sf
      file: data/hourly.nc
    - name: mask_europe
      file: data/europe.nc
```

Multiple scale factors and masks are supported as lists:

```yaml
scale_fields: [sf_temporal, sf_spatial]
mask: [mask_land, mask_europe]
```

---

## New ACES Capabilities

### GPU Acceleration

ACES runs all emission stacking on GPU via Kokkos with no code changes:

```bash
# Build with CUDA support
cmake -DACES_ENABLE_CUDA=ON ..
make -j4

# Run with GPU
ACES_DEVICE_ID=0 ./aces_nuopc_single_driver --config aces_config.yaml
```

### Emission Provenance Tracking

ACES records which layers contributed to each species at each timestep:

```cpp
// After StackingEngine::Execute()
const auto& prov = engine.GetProvenance();
const auto* co_prov = prov.GetProvenance("co");
std::cout << prov.FormatReport();
```

### Dynamic Species Registration

Add new species at runtime without recompilation:

```cpp
aces::EmissionLayer new_layer;
new_layer.field_name = "new_inventory";
new_layer.operation = "add";
new_layer.hierarchy = 1;
new_layer.scale = 1.0;

aces::AddSpecies(config, "new_species", {new_layer});
engine.AddSpecies("new_species");  // picks up from updated config
```

---

## Troubleshooting

### "Field not found" errors

Ensure the field name in `species[].field` matches the `name` in `aces_data.streams`
or the external name in `meteorology:`.

### Temporal profiles not applied

Check that `diurnal_cycle`, `weekly_cycle`, or `seasonal_cycle` names match keys in
`temporal_profiles:` exactly (case-sensitive).

### Vertical distribution not working

Ensure `vertical_grid:` is configured with the correct coordinate type (`fv3`, `mpas`, or `wrf`)
and that the required coordinate fields (`ak`, `bk`, `ps`, `height`, `hpbl`) are present in
the ESMF ImportState.

### Mass conservation errors

All vertical distribution methods conserve column mass. If you observe discrepancies,
verify that the source field is 2D (nz=1) when using `SINGLE`, `RANGE`, `PRESSURE`,
`HEIGHT`, or `PBL` methods.

### Performance below target

- Ensure Kokkos is built with OpenMP: `-DKokkos_ENABLE_OPENMP=ON`
- Set thread count: `export OMP_NUM_THREADS=16`
- For GPU: ensure CUDA toolkit matches Kokkos build configuration
- Run the benchmark: `python3 scripts/benchmark_hemco_vs_aces.py --build-dir build`
