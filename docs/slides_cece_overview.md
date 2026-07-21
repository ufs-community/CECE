# CECE: Community Emissions Computing Engine

## Presentation Slides

---

## Slide 1: What is CECE?

**Community Emissions Computing Engine**

- High-performance C++20 emissions compute component for Earth System Models
- Built on **Kokkos** for performance portability (CPUs and GPUs)
- Integrates with **ESMF/NUOPC** as a standard grid component
- Single YAML configuration file replaces multiple legacy config formats
- Modular physics engine with native C++ and Fortran plugin support

> CECE is the next-generation replacement for HEMCO and NEXUS emission processing systems.

---

## Slide 2: Why Replace HEMCO/NEXUS?

| Challenge | HEMCO/NEXUS | CECE |
|-----------|-------------|------|
| Hardware | CPU only | CPU + GPU via Kokkos |
| Configuration | Custom text format (`HEMCO_Config.rc`) | Standard YAML |
| Diagnostics | Separate `HEMCO_Diagn.rc` file | Integrated `diagnostics:` block |
| Temporal profiles | Inline slash-separated values | Named `temporal_profiles:` block |
| Vertical distribution | Column-13 keyword | Explicit `vdist:` block per layer |
| Scale factors | Separate section with numeric IDs | Named `scale_fields:` list per layer |
| Provenance | None | Built-in provenance tracking |
| Species registration | Recompile required | Runtime `AddSpecies()` API |
| Performance model | Sequential layer processing | Kernel fusion (single kernel per species) |

---

## Slide 3: Architecture & Key Improvements

```
┌─────────────────────────────────────────────────────┐
│                   ESMF / NUOPC                       │
│  ┌───────────┐   ┌──────────────┐   ┌───────────┐  │
│  │  Import   │   │     CECE     │   │  Export   │  │
│  │  State    │──▶│  Component   │──▶│  State    │  │
│  │ (meteo)   │   │              │   │ (emis)    │  │
│  └───────────┘   └──────┬───────┘   └───────────┘  │
└──────────────────────────┼──────────────────────────┘
                           │
              ┌────────────┼────────────┐
              ▼            ▼            ▼
     ┌──────────────┐ ┌────────┐ ┌──────────────┐
     │   Stacking   │ │ Physics│ │  Diagnostics │
     │   Engine     │ │Schemes │ │   Manager    │
     └──────┬───────┘ └────┬───┘ └──────────────┘
            │               │
            ▼               ▼
     ┌──────────────┐ ┌────────────────┐
     │ AMIO (Data   │ │ MEGAN, SeaSalt │
     │  Ingestion)  │ │ Dust, Lightning│
     └──────────────┘ └────────────────┘
```

**Lifecycle**: Initialize → Run (ingest → stack → physics → diagnostics → sync) → Finalize

### Key Improvements Over HEMCO

- **Kernel fusion** — Single optimized kernel per species → 4x speedup on GPU
- **GPU acceleration** — Same code on NVIDIA/AMD GPUs without modification
- **YAML configuration** — Human-readable, version-control friendly, automated migration tool
- **Named references** — Scale factors and masks by name, not numeric IDs
- **Provenance tracking** — Full record of layers, scales, and masks per calculation
- **Dynamic species** — Add species at runtime without recompilation

---

## Slide 4: The Stacking Engine — Overview

The **Stacking Engine** is the computational core of CECE.

**Purpose**: Combine multiple emission data layers into final emission fields for each species.

**Processing Phases**:

1. **Configuration Analysis** — Parse and validate emission layer configuration
2. **Field Binding** — Connect to data sources (AMIO streams, import fields)
3. **Hierarchy Processing** — Apply priority-based layer combination rules
4. **Kernel Fusion** — Generate optimized compute kernels
5. **Temporal Scaling** — Apply time-dependent scaling factors
6. **Vertical Distribution** — Map 2D emissions to 3D atmospheric grids
7. **Provenance Tracking** — Record complete calculation history

---

## Slide 5: Stacking Engine — Hierarchy System

Layers are organized by a **two-level hierarchy**:

1. **Categories** — Logical groupings (anthropogenic, biogenic, biomass_burning, etc.)
2. **Hierarchy Levels** — Numerical priorities within categories (higher = takes precedence)

```yaml
species:
  co:
    - field: "global_co_inventory"
      category: "anthropogenic"
      hierarchy: 1              # Base layer
      operation: "add"

    - field: "regional_co_override"
      category: "anthropogenic"
      hierarchy: 10             # Higher priority — replaces base
      operation: "replace"
      mask: "europe_mask"
```

**Result**: Regional inventory overrides global in masked region, preserving global elsewhere.

---

## Slide 6: Stacking Engine — Layer Operations

Four fundamental operations for combining emission layers:

| Operation | Behavior |
|-----------|----------|
| `add` | Accumulates layer emissions onto current value |
| `replace` | Overrides accumulated value with layer emissions (in masked region) |

Multiplicative scaling is achieved through `scale_fields` rather than a standalone operation.

Each layer can also apply:
- **Base scale factor** — Simple numerical multiplier
- **Scale fields** — Dynamic field-based scaling (temperature, LAI, PAR)
- **Geographical masks** — 2D/3D fields restricting spatial extent
- **Temporal profiles** — Diurnal, weekly, and seasonal cycles

---

## Slide 7: Stacking Engine — Temporal Scaling

Three cycle types applied as a product:

```
effective_scale = base_scale × diurnal × weekly × seasonal
```

```yaml
temporal_profiles:
  traffic_diurnal: [0.5, 0.3, 0.2, 0.3, 0.6, 1.2, 1.8, 1.5,
                    1.2, 1.0, 1.1, 1.2, 1.3, 1.2, 1.3, 1.5,
                    1.8, 2.0, 1.8, 1.5, 1.2, 1.0, 0.8, 0.6]

  weekday_pattern: [1.2, 1.3, 1.3, 1.3, 1.3, 0.8, 0.7]  # Sun-Sat

species:
  co:
    - field: "traffic_co"
      diurnal_cycle: "traffic_diurnal"
      weekly_cycle: "weekday_pattern"
      operation: "add"
```

---

## Slide 8: Stacking Engine — Vertical Distribution

Five methods for distributing 2D surface emissions into 3D volumes:

| Method | Description | Use Case |
|--------|-------------|----------|
| `SINGLE` | All emissions in one level | Point sources |
| `RANGE` | Even distribution over layer range | Industrial stacks |
| `PRESSURE` | Distribution by pressure bounds | Free troposphere |
| `HEIGHT` | Distribution by altitude bounds | Topography-dependent |
| `PBL` | Distribution within boundary layer | Surface anthropogenic |

```yaml
species:
  nox:
    - field: "aircraft_nox"
      vdist:
        method: height
        h_start: 8000.0    # 8 km
        h_end: 12000.0     # 12 km
      operation: "add"
```

All methods guarantee strict mass conservation: `∑(emissions_3d[i,j,:]) = emissions_2d[i,j]`

---

## Slide 9: Stacking Engine — Kernel Fusion

**Traditional approach** (HEMCO): Process each layer sequentially → multiple kernel launches, poor cache utilization.

**CECE approach**: Fuse all layers for a species into a **single optimized kernel**.

### Benefits:
- **Reduced memory bandwidth** — Minimizes data movement between memory and compute units
- **Lower kernel launch overhead** — One launch per species instead of N launches per layer
- **Better cache utilization** — Maximizes data reuse within kernels
- **Improved parallelization** — Better load balancing across GPU threads

### Performance Results:

| Grid Size | Hardware | Throughput |
|-----------|----------|------------|
| 1440×721×72 | 40-core Intel Xeon | ~50 species/sec |
| 1440×721×72 | NVIDIA V100 | ~200 species/sec |
| 3600×1801×72 | NVIDIA A100 | ~150 species/sec |

---

## Slide 10: Provenance Tracking

CECE records the complete scientific history of every emission calculation:

```yaml
# Provenance report excerpt
species: CO
time_context: hour=14 day_of_week=2 month=7
contributing_layers:
  - field: global_co_base
    operation: add
    hierarchy: 1
    category: anthropogenic
    effective_scale: 1.25
    masks: [land_mask]
  - field: regional_co_override
    operation: replace
    hierarchy: 10
    effective_scale: 0.85
    geographic_bounds: [lon: -125 to -65, lat: 25 to 50]
```

**Tracked**: Layer contributions, hierarchy application order, all scaling factors, operation history, data sources.

---

## Slide 11: Configuration — HEMCO vs CECE

### Before (HEMCO_Config.rc):
```
# ExtNr Name       File            Var  Time CRE Dim Unit  Species ScalIDs Cat Hier
0       MACCITY_CO data/MACCity.nc CO   2000 1   2   kg/m2 CO      1/2     1   1
```

### After (cece_config.yaml):
```yaml
species:
  co:
    - field: MACCITY_CO
      operation: add
      hierarchy: 1
      category: anthropogenic
      scale: 1.0
      scale_fields: [hourly_scalfact]

cece_data:
  streams:
    - name: MACCITY_CO
      file: data/MACCity.nc
```

**Migration tool**: `python3 scripts/hemco_to_cece.py HEMCO_Config.rc -o cece_config.yaml`

---

## Slide 12: Physics Schemes

CECE supports modular physics schemes in both C++ and Fortran:

| Scheme | Type | Description |
|--------|------|-------------|
| MEGAN | Biogenic | Biogenic VOC emissions (isoprene, terpenes) |
| Sea Salt | Natural | Wind-driven sea spray aerosol |
| Dust (Ginoux/FENGSHA) | Natural | Wind-blown mineral dust |
| Lightning NOx | Natural | NOx from lightning flashes |
| Soil NOx (BDSNP) | Natural | Soil microbial NOx |
| DMS | Natural | Dimethyl sulfide from ocean |
| Volcano | Natural | Volcanic SO2 emissions |

### Independent Refresh Intervals:
```yaml
physics_schemes:
  - name: "megan"
    refresh_interval_seconds: 300     # Every 5 min (fast-responding)
  - name: "sea_salt"
    refresh_interval_seconds: 1800    # Every 30 min
```

---

## Slide 13: Additional Capabilities

### Python Bindings
- High-level API via pybind11
- Zero-copy NumPy data transfer
- Automatic GIL release during computation

### Speciation Support
- Multiple chemical mechanisms: CB6, CRACMM, RACM2, SAPRC07
- YAML-based speciation mapping files

### Clock Refresh Intervals
- Independent timing for each component
- Fast schemes (biogenics) run every few minutes
- Slow data streams ingest hourly
- Reduces unnecessary computatio

### Diagnostics
- Integrated NetCDF output
- Configurable output intervals
- Intermediate and final variable capture

---

## Slide 14: Summary

| Aspect | What CECE Delivers |
|--------|-------------------|
| **Performance** | 4x speedup via kernel fusion + GPU acceleration |
| **Portability** | Same code on CPUs, NVIDIA GPUs, AMD GPUs |
| **Usability** | Clean YAML config, automated HEMCO migration tool |
| **Extensibility** | Runtime species registration, modular physics plugins |
| **Traceability** | Built-in provenance tracking for every calculation |
| **Integration** | ESMF/NUOPC compliant, AMIO data ingestion |
| **Flexibility** | Independent refresh intervals, multiple vertical distribution methods |

**CECE modernizes atmospheric emission processing for the exascale era.**

---
