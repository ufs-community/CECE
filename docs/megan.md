# MEGAN Biogenic Emissions

## Overview

CECE provides two MEGAN biogenic emission schemes:

- **`megan`** — Single-species isoprene scheme (original, ported from HEMCO)
- **`megan3`** — Full MEGAN3 multi-species, multi-class emission system with 19 emission classes, 5-layer canopy model, and chemical mechanism speciation

Both schemes coexist and can be selected independently via the YAML configuration.

---

## MEGAN (Single-Species Isoprene)

The original scheme computes isoprene emissions using activity factors for LAI, temperature (light-dependent and light-independent pathways), PAR (via PCEEA), leaf age, soil moisture, and CO₂ inhibition. Ported from HEMCO's `hcox_megan_mod.F90`.

### Registration Names

- Native C++: `"megan"`
- Fortran bridge: `"megan_fortran"`

### Configuration

```yaml
physics_schemes:
  - name: megan
    options:
      beta: 0.13
      ldf: 1.0
      aef: 1.0e-9
      co2_concentration: 400.0
```

### Import Fields

| Field Name | Units | Description |
| --- | --- | --- |
| `temperature` | K | Surface air temperature |
| `leaf_area_index` | m²/m² | Current month LAI |
| `leaf_area_index_prev` | m²/m² | Previous month LAI (optional) |
| `par_direct` | W/m² | Direct PAR |
| `par_diffuse` | W/m² | Diffuse PAR |
| `solar_cosine` | — | Cosine of solar zenith angle |
| `soil_moisture_root` | fraction | Root-zone soil moisture (optional) |

### Export Fields

| Field Name | Units | Description |
| --- | --- | --- |
| `isoprene_emissions` | kg/m²/s | Isoprene emission flux |

---

## MEGAN3 (Multi-Species, Multi-Class)

The full MEGAN3 scheme computes emissions for 19 biogenic emission classes, applies a comprehensive set of gamma factors including a 5-layer canopy model, and converts class totals to mechanism-specific output species via a configurable speciation engine.

### Registration Names

- Native C++: `"megan3"`
- Fortran bridge: `"megan3_fortran"`

### 19 Emission Classes

| Class | Description |
| --- | --- |
| ISOP | Isoprene |
| MBO | 2-methyl-3-buten-2-ol |
| MT_PINE | Monoterpenes (α-pinene type) |
| MT_ACYC | Monoterpenes (acyclic, e.g., myrcene) |
| MT_CAMP | Monoterpenes (camphene type) |
| MT_SABI | Monoterpenes (sabinene type) |
| MT_AROM | Monoterpenes (aromatic, e.g., p-cymene) |
| NO | Nitric oxide (from soil, via export state) |
| SQT_HR | Sesquiterpenes (high reactivity) |
| SQT_LR | Sesquiterpenes (low reactivity) |
| MEOH | Methanol |
| ACTO | Acetone |
| ETOH | Ethanol |
| ACID | Organic acids |
| LVOC | Low-volatility organic compounds |
| OXPROD | Oxidation products |
| STRESS | Stress-induced emissions |
| OTHER | Other VOCs |
| CO | Carbon monoxide |

### Gamma Factors

For each emission class, the scheme computes:

- **γ_T_LI** — Light-independent temperature response (exponential β formulation)
- **γ_T_LD** — Light-dependent temperature response (Guenther et al. 2012)
- **γ_PAR** — PAR response via PCEEA algorithm
- **γ_LAI** — Leaf area index correction
- **γ_age** — Leaf age (new/growing/mature/old fractions)
- **γ_SM** — Soil moisture
- **γ_CO₂** — CO₂ inhibition (Possell or Wilkinson)
- **γ_stress** — Wind/temperature/air quality stress (optional)

Combined via LDF partitioning:

```
emission[class] = NORM_FAC × AEF × γ_LAI × γ_age × γ_SM × γ_CO₂ × [(1-LDF)×γ_T_LI + LDF×γ_PAR×γ_T_LD] × γ_stress
```

### Canopy Model (MEGCANOPY)

A 5-layer Gaussian quadrature canopy model computes:
- Beer-Lambert PAR extinction through the canopy
- Sunlit/shaded leaf fractions at each layer
- Leaf temperature via energy balance
- Canopy-integrated emission activity factor

All light-dependent factors are zero when solar cosine ≤ 0 (nighttime).

### Speciation

After computing 19 class totals, the speciation engine converts them to mechanism-specific output species using scale factors from a YAML MAP file:

```
output[species] = (Σ class_total[c] × scale_factor[c→s]) × MW[s]
```

See [Speciation Configuration](#speciation-configuration) below for the YAML format.

### Soil NO Handling

The NO emission class reads `soil_nox_emissions` from the export state (produced by the BDSNP module or any other soil NO scheme). If the field is missing, a warning is logged and NO is set to zero.

### Configuration

```yaml
physics_schemes:
  - name: bdsnp
    options:
      soil_no_method: bdsnp
  - name: megan3
    options:
      mechanism_file: data/speciation/spc_cb6.yaml
      speciation_file: data/speciation/map_cb6.yaml
      speciation_dataset: MEGAN
      co2_concentration: 415.0
      co2_method: possell
      enable_wind_stress: false
      enable_temp_stress: false
      emission_classes:
        ISOP:
          ldf: 0.9996
          ct1: 95.0
          cleo: 2.0
          beta: 0.13
          anew: 0.05
          agro: 0.6
          amat: 1.0
          aold: 0.9
          default_aef: 1.0e-9
        MT_PINE:
          ldf: 0.10
          ct1: 80.0
          cleo: 1.83
          beta: 0.10
          anew: 2.0
          agro: 1.8
          amat: 1.0
          aold: 1.05
          default_aef: 3.0e-10
        # ... remaining classes
    input_mapping:
      temperature: T2M
      leaf_area_index: LAI
      par_direct: PARDR
      par_diffuse: PARDF
      solar_cosine: COSZS
      soil_moisture_root: GWETROOT
      wind_speed: U10M
    output_mapping:
      MEGAN_ISOP: ISOP_BIOG
      MEGAN_TERP: TERP_BIOG
```

### Import Fields

| Field Name | Units | Description |
| --- | --- | --- |
| `temperature` | K | Surface air temperature |
| `leaf_area_index` | m²/m² | Current month LAI |
| `leaf_area_index_prev` | m²/m² | Previous month LAI (optional) |
| `par_direct` | W/m² | Direct PAR |
| `par_diffuse` | W/m² | Diffuse PAR |
| `solar_cosine` | — | Cosine of solar zenith angle |
| `soil_moisture_root` | fraction | Root-zone soil moisture (optional) |
| `wind_speed` | m/s | Wind speed (optional, for stress) |
| `AEF_<CLASS>` | μg/m²/hr | Per-class gridded AEF (optional) |

### Export Fields

Dynamic — one field per mechanism species with `MEGAN_` prefix:

| Field Name | Units | Description |
| --- | --- | --- |
| `MEGAN_ISOP` | kg/m²/s | Isoprene (mechanism species) |
| `MEGAN_TERP` | kg/m²/s | Terpenes (mechanism species) |
| `MEGAN_<NAME>` | kg/m²/s | Other mechanism species |

---

## Speciation Configuration

MEGAN3 uses two YAML files for chemical mechanism speciation:

### SPC File (Mechanism Species)

Defines the target mechanism species and their molecular weights. Uses the MICM/OpenAtmos format:

```yaml
name: CB6_AE7
species:
  - name: ISOP
    molecular weight [kg mol-1]: 0.06812
  - name: TERP
    molecular weight [kg mol-1]: 0.13623
  - name: PAR
    molecular weight [kg mol-1]: 0.01443
  # ... up to 36 species for CB6
```

### MAP File (Speciation Mappings)

Defines how the 19 MEGAN emission classes map to mechanism species with per-class scale factors. Uses a dataset-oriented format that supports multiple emission sources:

```yaml
mechanism: CB6_AE7
datasets:
  MEGAN:
    ISOP:
      ISOP: 1.0
    TERP:
      MT_PINE: 0.5
      MT_ACYC: 0.3
      MT_CAMP: 0.1
      MT_SABI: 0.05
      MT_AROM: 0.05
    MEOH:
      MEOH: 1.0
    SESQ:
      SQT_HR: 0.7
      SQT_LR: 0.3
    "NO":
      "NO": 1.0
    CO:
      CO: 1.0
```

Each entry under a mechanism species name lists the contributing emission classes and their fractional scale factors. The speciation engine computes:

```
output[TERP] = (class_total[MT_PINE] × 0.5 + class_total[MT_ACYC] × 0.3 + ...) × MW[TERP]
```

### Supported Mechanisms

CECE ships with speciation files for:

| Mechanism | SPC File | MAP File |
| --- | --- | --- |
| CB6_AE7 | `data/speciation/spc_cb6.yaml` | `data/speciation/map_cb6.yaml` |
| RACM2 | `data/speciation/spc_racm2.yaml` | `data/speciation/map_racm2.yaml` |
| SAPRC07 | `data/speciation/spc_saprc07.yaml` | `data/speciation/map_saprc07.yaml` |
| CRACMM2 | `data/speciation/spc_cracmm.yaml` | `data/speciation/map_cracmm.yaml` |

To switch mechanisms at runtime, change `mechanism_file` and `speciation_file` in the YAML config — no recompilation needed.

### Adding a New Mechanism

1. Create an SPC file with species names and molecular weights (kg/mol)
2. Create a MAP file with a `MEGAN` dataset section mapping emission classes to your mechanism species
3. Point `mechanism_file` and `speciation_file` to your new files

---

## Scheme Ordering

BDSNP must run before MEGAN3 so that `soil_nox_emissions` is available:

```yaml
physics_schemes:
  - name: bdsnp        # Runs first, writes soil_nox_emissions
  - name: megan3       # Runs second, reads soil_nox_emissions for NO class
```
