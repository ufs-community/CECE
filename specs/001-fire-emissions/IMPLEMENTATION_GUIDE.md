# Fire Emissions Implementation Guide

**Status**: T017, T018, T021 and T022 complete; T023 production chemistry remains blocked pending rights, independent budgets and explicit source-composition metadata

**Last Updated**: 2026-11-20

---

## Quick Links

| Resource | Location | Purpose |
|----------|----------|---------|
| **Product Samples** | `data/fire/product_samples/` | RAVE & GBBEPx test fixtures |
| **Sample Metadata** | `data/fire/product_samples_manifest.yaml` | Complete variable/dimension docs |
| **CB6 Factors** | `data/fire/cb6_fire_factors_manifest.yaml` | 42-factor chemistry mapping |
| **Asset Registry** | `data/fire/asset_registry.yaml` | Tracking all fire assets |
| **Completion Report** | `specs/001-fire-emissions/T017_T018_COMPLETION.md` | Detailed verification results |
| **Task Tracking** | `specs/001-fire-emissions/tasks.md` | T017–T027 status (marked ✅/🔄) |

---

## What You Need to Know

### Product Samples (T017 ✅)

**RAVE Sample**
```
File: data/fire/product_samples/RAVE-HrlyEmiss-13km_v2r0_20260701_sample.nc
Date: 2026-07-01T00:00:00Z (hourly)
Grid: 13 km CONUS (800×544 cells)
Species: 11 (CO2, CO, CH4, PM2.5, OC, BC, SO2, TPM, NH3, NOx, VOCs)
  → Unscaled + scaled variants available
Diagnostics: FRP_MEAN, FRP_SD, FRE, Cloud_Fraction
Dimensions: [time=1, grid_yt=544, grid_xt=800]
Size: 4.2 MB
SHA256: 0ac99097022d50bd9128af93df8268a3d53f96b70bad8d267cfde497ab264141
```

**GBBEPx Sample**
```
File: data/fire/product_samples/GBBEPx-all01GRID_v4r0_20230224_sample.nc
Date: 2023-02-24 (daily UTC)
Grid: Global 0.1° (3600×1801 cells)
Species: 8 (BC, CO, CO2, OC, PM2.5, SO2, NH3, NOx) [NO CH4/TPM/VOCs]
Diagnostics: MeanFRP, CloudPerc, FirePerc, QCAll, NumSensor
Dimensions: [Time=1, Latitude=1801, Longitude=3600]
Size: 526 MB
SHA256: 4dbcff118dd4bf78013d69a260c4700225464f1c0c3301839eb62d74ba40b56c
```

### CB6r5-AE7-AQ Chemistry Factors (T018 verified; production use blocked)

**Key Facts**
- **Source**: NOAA-OAR-ARL ufs-srweather-app commit `130beee95c0bef00a4cc8f24177e4d4db6787cda` (`aqm.rc` lines 290–333)
- **Rows**: 43 user-confirmed speciation factors, all matched to the pinned source
- **Sums**: PM2.5-referenced 1.0; CO-referenced 1.1247 (not a unit-sum partition)
- **Mechanism**: CB6r5-AE7-AQ (`aqm.rc` references the `cb6r5_ae7_aq` mechanism files)
- **Base Species**: User-confirmed CO- and PM2.5-referenced factors; preserve values without normalization
- **Remaining production gates**: Redistribution rights and independent target budgets are unverified

Do not normalize these values or use them for production mapping until the exact mechanism revision, coefficient semantics and independent budgets are verified.

**PM2.5 Species (17 total)**
```
Top 3 by listed PM2.5-referenced coefficient:
  1. POC  (Particulate Organic Carbon)      46.18%
  2. PNCOM (Primary Non-Carbon Organic)    32.32%
  3. PEC  (Particulate Elemental Carbon)    9.49%

Others: PCA, PCL, PFE, PK, PMG, PMN, PMOTHR, PNA, PNH4, PNO3, PSI, PSO4, PTI
```

**Gas Species (25 total)**
```
NOx: NO, NO2 (~90:10 partition)
SO2: SO2
NH3: NH3
CO:  CO
VOCs (20 species):
  - Aldehydes: CH4, ALD2, ALDX, FORM
  - Olefins: OLE, IOLE, ETHY
  - Aromatics: BENZ, TOL, XYL
  - Alkanes: ALK4, PRPA, ETH, ETHA, MEOH, ETOH
  - Ketones: KET
  - Terpenes: TERP
  - Other: ISOP, ACET, PAR, UNR
```

**Combustion Coefficient**: 0.368 kg/MJ (RAVE ATBD baseline)

### Dimensional Chain (T018 Reference)

```
Sensor Input (FRP):        FRP [MW/pixel]
           ↓
Time Step:                 dt [seconds]
           ↓
Fire Radiative Energy:     FRE [MJ] = FRP × dt
           ↓
Combustion Coefficient:    0.368 kg/MJ
           ↓
Dry Matter Mass:           DM [kg] = FRE × 0.368
           ↓
Species Emission Factor:   Factor [kg_species / kg_DM]
           ↓
Final Species Mass:        M [kg] = DM × Factor
```

**Example**: 1000 MW fire over 1 hour
- FRE = 1000 MW × 3600 s = 3,600,000 MJ
- DM = 3,600,000 MJ × 0.368 = 1,324,800 kg dry matter
- CO2 mass = 1,324,800 kg × 0.98 ≈ 1.3M kg CO2

---

## Next Steps (T021–T027)

### Immediate (T021 Decoder Implementation)

1. **Review Product Sample Metadata**
   - Open: `data/fire/product_samples_manifest.yaml`
   - Focus: variable names, dimensions, dtypes, units
   - Action: Use as reference for decoding logic

2. **Load & Parse Sample Files** (optional for validation)
   ```python
   import netCDF4 as nc

   rave = nc.Dataset('data/fire/product_samples/RAVE-HrlyEmiss-13km_v2r0_20260701_sample.nc')
   gbbepx = nc.Dataset('data/fire/product_samples/GBBEPx-all01GRID_v4r0_20230224_sample.nc')

   # Inspect structure
   print(rave.variables.keys())
   print(gbbepx.variables.keys())
   ```

3. **Implement Decoders** (T021 task)
   - **RAVE Decoder**: Handle (time, grid_yt, grid_xt) grid; support 11 species (unscaled + scaled)
   - **GBBEPx Decoder**: Handle (Time, Latitude, Longitude) grid; support 8-species reduced set
   - **Validation**: Verify species match manifest; check units/dtypes
   - **Reference Tests**: Compare decoded output against sample files

### Chemistry Mapping (T023 after T022)

T022 normalization is complete. T018 factor semantics are verified and the
values match the pinned CB6r5-AE7-AQ source. Preserve the CO-referenced total of 1.1247;
do not normalize the coefficients. T023 production chemistry remains blocked
until redistribution rights, independent target budgets, and product-to-target
source-composition metadata are resolved. Continue only with plumbing and
synthetic-profile tests that do not certify production chemistry.

### Test Cases (T024–T025)

1. **Configuration Examples**
   - Create: `examples/cece_config_fire_product.yaml`
   - Include: Both RAVE (11-species) and GBBEPx (8-species) cases
   - Reference: Actual samples from T017

2. **Conservation Tests** (T025)
   - Load actual sample data
   - Compute reference totals (mass, energy)
   - Run through allocation/regridding
   - Verify SC-002 tolerances (±5% per component)

3. **Sample Reference Values** (for validation)
   ```yaml
   # Compute from actual files:
   rave_total_co2_kg:        [from sample]
   rave_total_frp_mw_hours:  [from sample]
   gbbepx_total_co_kg:       [from sample]

   # Then verify after processing:
   # (output_total - reference_total) / reference_total <= 0.05
   ```

---

## Technical Considerations

### Grid Differences

| Property | RAVE | GBBEPx |
|----------|------|--------|
| **Coordinate Dims** | grid_yt, grid_xt | Latitude, Longitude |
| **Dims Order** | (time, grid_yt, grid_xt) | (Time, Latitude, Longitude) |
| **Resolution** | 13 km CONUS | 0.1° global |
| **Coverage** | North America | Global |
| **Temporal** | Hourly | Daily |

**Decoder Impact**: Both must normalize to common internal grid representation.

### Species Availability

**RAVE**: All 11 canonical species + scaled variants
```
Variables: CO2, CO, CH4, PM2.5, OC, BC, SO2, TPM, NH3, NOx, VOCs
         + CO2_scaled, CO_scaled, ... (scaled variants)
         + Diagnostics (FRP_MEAN, FRE, etc.)
```

**GBBEPx**: 8-species reduced set (missing CH4, TPM, VOCs)
```
Variables: BC, CO, CO2, OC, PM2.5, SO2, NH3, NOx
         + Diagnostics (MeanFRP, CloudPerc, etc.)
         - NO CH4, TPM, VOCs
```

**Validation**: T025 must reject requests for 11-species output from GBBEPx input (insufficient data).

### Units & Scaling

**RAVE Units** (from manifest): Varies by species; check manifest for each
**GBBEPx Units** (from manifest): Consistent; check manifest
**Output Units**: Must match configuration (typically kg/m²/s)
**Scaling Variants**: RAVE includes scaled versions; GBBEPx does not

---

## Common Pitfalls

### ❌ Don't

- Infer metadata from NetCDF attribute labels (use manifest)
- Assume species order is consistent across products
- Skip dimensional chain validation (FRP → FRE → DM → species)
- Forget to check for missing species in reduced GBBEPx set
- Use unverified combustion coefficient (must be 0.368 kg/MJ)

### ✅ Do

- Always reference `product_samples_manifest.yaml` for variable definitions
- Use actual sample files for decoder reference tests
- Validate species availability before allocation
- Document grid transformation logic (RAVE vs GBBEPx coordinates)
- Preserve mass conservation through all conversions (SC-002: ±5%)

---

## Verification Checklist

T021 decoder acceptance is complete: 15 focused tests pass against pinned
RAVE/GBBEPx actual-sample crops, including 24 consecutive RAVE hours and
independently summed species totals. T025 model-substep allocation/conservation
acceptance remains pending and is not a T021 completion gate.

Before marking T025 complete:
- [ ] 24-hour product cases run end-to-end
- [ ] Mass conservation verified (SC-002: ±5%)
- [ ] 11-species request rejected for GBBEPx input
- [ ] Multiple substeps per source interval tested
- [ ] Grid geometry changes handled correctly

---

## References

| Document | Purpose |
|----------|---------|
| [T017_T018_COMPLETION.md](./T017_T018_COMPLETION.md) | Detailed verification results |
| [product_samples_manifest.yaml](../../data/fire/product_samples_manifest.yaml) | Sample metadata & variables |
| [cb6_fire_factors_manifest.yaml](../../data/fire/cb6_fire_factors_manifest.yaml) | Chemistry factors & speciation |
| [asset_registry.yaml](../../data/fire/asset_registry.yaml) | Asset tracking & hashes |
| [plan.md](./plan.md) | Overall architecture |
| [spec.md](./spec.md) | Requirements & contracts |
| [data-model.md](./data-model.md) | Field definitions |

---

T021 decoding and T022 local normalization are complete. Next, assess T023's
allocation/mapping plumbing without enabling CB6 production factors; T025
model-substep conservation remains a separate acceptance task.
