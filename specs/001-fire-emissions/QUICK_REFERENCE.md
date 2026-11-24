# Fire Emissions Quick Reference

**Last Updated**: 2026-11-20
**Investigation Phase Status**: ✅ COMPLETE
**Implementation Phase Status**: 🟢 READY TO BEGIN

---

## Critical Paths

### For T021 (Decoder Implementation)

**Must-Read Documents** (in order):
1. [T021_DECODER_QUICKSTART.md](./T021_DECODER_QUICKSTART.md) - Implementation guide
2. [product_samples_manifest.yaml](../../data/fire/product_samples_manifest.yaml) - Variable reference
3. [IMPLEMENTATION_GUIDE.md](./IMPLEMENTATION_GUIDE.md) - Best practices

**Reference Files**:
- RAVE sample: `data/fire/product_samples/RAVE-HrlyEmiss-13km_v2r0_20260701_sample.nc`
- GBBEPx sample: `data/fire/product_samples/GBBEPx-all01GRID_v4r0_20230224_sample.nc`

**Key Numbers**:
- RAVE: (time=1, grid_yt=544, grid_xt=800), 11 species
- GBBEPx: (Time=1, Latitude=1801, Longitude=3600), 8 species (NO CH4/TPM/VOCs)

**Test References**:
- File: `tests/test_fire_products.cpp`
- Manifest reference: Compute totals from actual sample files
- Tolerance: ±5% (SC-002)

---

### For T023 (Chemistry Mapping)

**Must-Read Documents**:
1. [cb6_fire_factors_manifest.yaml](../../data/fire/cb6_fire_factors_manifest.yaml) - Factor reference
2. [configuration.md](./contracts/configuration.md) - Product temporal allocation

**Key Numbers**:
- 43 transcribed coefficient rows (17 PM2.5-referenced + 26 CO-referenced, including CO)
- PM2.5-referenced sum: 1.0; CO-referenced sum: 1.1247
- Factor semantics and CB6r5-AE7-AQ configuration identity are verified; do not normalize
- Redistribution rights, independent budgets and production eligibility remain unresolved

**PM2.5 Top Contributors**:
- POC: 46.18%
- PNCOM: 32.32%
- PEC: 9.49%

**NOx Partition**:
- NO and NO2 factors are in a 9:1 ratio relative to CO
- This coefficient ratio is not an independently verified NOx mass partition

---

### For T025 (Conservation Tests)

**Test File**: `tests/test_fire_conservation.cpp`

**Reference Data Sources**:
- RAVE sample totals (compute from: `data/fire/product_samples/RAVE-HrlyEmiss-13km_v2r0_20260701_sample.nc`)
- GBBEPx sample totals (compute from: `data/fire/product_samples/GBBEPx-all01GRID_v4r0_20230224_sample.nc`)

**Tolerance**: ±5% per component (SC-002)

**Test Scenarios**:
1. Direct product allocation (no regridding)
2. Regridded allocation (geometry changes)
3. Multiple model substeps per source interval
4. GBBEPx 8-species → reject for 11-species output

---

## Quick Lookup Tables

### RAVE Sample Metadata

| Property | Value |
|----------|-------|
| **Filename** | RAVE-HrlyEmiss-13km_v2r0_20260701_sample.nc |
| **Path** | data/fire/product_samples/ |
| **Size** | 4.2 MB |
| **Date** | 2026-07-01 (hourly) |
| **Grid** | 13 km CONUS (800×544) |
| **Species** | 11 (all canonical) |
| **Total Variables** | 41 (species + scaled + diagnostics) |
| **Dimensions** | (time=1, grid_yt=544, grid_xt=800) |
| **SHA256** | 0ac99097022d50bd9128af93df8268a3d53f96b70bad8d267cfde497ab264141 |

### GBBEPx Sample Metadata

| Property | Value |
|----------|-------|
| **Filename** | GBBEPx-all01GRID_v4r0_20230224_sample.nc |
| **Path** | data/fire/product_samples/ |
| **Size** | 526 MB |
| **Date** | 2023-02-24 (daily) |
| **Grid** | Global 0.1° (3600×1801) |
| **Species** | 8 (reduced set) |
| **Total Variables** | 26 (species + diagnostics) |
| **Dimensions** | (Time=1, Latitude=1801, Longitude=3600) |
| **SHA256** | 4dbcff118dd4bf78013d69a260c4700225464f1c0c3301839eb62d74ba40b56c |

### CB6r5-AE7-AQ Chemistry Factors

| Category | Count | Examples |
|----------|-------|----------|
| **PM2.5 Species** | 17 | POC (46.18%), PNCOM (32.32%), PEC (9.49%) |
| **Gas Species** | 25 | NO, NO2, SO2, NH3, CO + 20 VOCs |
| **Total Factors** | 42 | All documented in manifest |
| **Combustion Coeff** | 0.368 | kg/MJ (from RAVE ATBD) |
| **Units** | KG/M2/S | Consistent with RAVE/GBBEPx |

---

## File Locations

### Product Samples
```
data/fire/product_samples/
  ├── RAVE-HrlyEmiss-13km_v2r0_20260701_sample.nc  (4.2 MB)
  ├── GBBEPx-all01GRID_v4r0_20230224_sample.nc    (526 MB)
  └── hashes.txt                                    (SHA256 verification)
```

### Manifests & Documentation
```
data/fire/
  ├── product_samples_manifest.yaml                (41+26 variables documented)
  ├── cb6_fire_factors_manifest.yaml               (42 factors documented)
  ├── asset_registry.yaml                          (asset tracking)
  └── cb6_fire_config.txt                          (aqm.rc excerpt)

specs/001-fire-emissions/
  ├── T017_T018_COMPLETION.md                      (verification report)
  ├── IMPLEMENTATION_GUIDE.md                      (decoder guide)
  ├── T021_DECODER_QUICKSTART.md                   (implementation guide)
  ├── QUICK_REFERENCE.md                           (this file)
  ├── tasks.md                                     (task tracking)
  ├── plan.md                                      (architecture)
  ├── spec.md                                      (requirements)
  └── data-model.md                                (field definitions)
```

### Implementation Files
```
src/core/
  ├── cece_fire_input.cpp                          (decoder implementations)
  └── physics/cece_fire.cpp                        (chemistry mapping)

tests/
  ├── test_fire_products.cpp                       (decoder unit tests)
  └── test_fire_conservation.cpp                   (conservation tests)

examples/
  └── cece_config_fire_product.yaml               (config examples)
```

---

## Species Reference

### RAVE (11 Species + Variants)

```
Canonical 11:
  CO2, CO, CH4, PM2.5, OC, BC, SO2, TPM, NH3, NOx, VOCs

Variants in Sample:
  CO2_scaled, CO_scaled, CH4_scaled, etc.

Diagnostics:
  FRP_MEAN, FRP_SD, FRE, Cloud_Fraction
```

### GBBEPx (8 Species)

```
Available 8:
  BC, CO, CO2, OC, PM2.5, SO2, NH3, NOx

NOT Available (decoder must reject):
  CH4, TPM, VOCs

Diagnostics:
  MeanFRP, CloudPerc, FirePerc, QCAll, NumSensor
```

### CB6r5-AE7-AQ Chemistry (42 Species)

**PM2.5 (17 species)**:
PAL, PCA, PCL, PEC, PFE, PK, PMG, PMN, PMOTHR, PNA, PNCOM, PNH4, PNO3, POC, PSI, PSO4, PTI

**Gas (25 species)**:
- SO2: SO2
- NH3: NH3
- NOx: NO, NO2
- CO: CO
- VOCs (20): CH4, ALD2, ALDX, FORM, OLE, IOLE, ETHY, BENZ, TOL, XYL, ALK4, PRPA, ETH, ETHA, MEOH, ETOH, KET, TERP, ISOP, ACET, PAR, UNR

---

## Grid Coordinate Systems

### RAVE

**Dimension Names**: grid_yt, grid_xt
**Type**: Lambert Conformal Projection
**Coverage**: 13 km CONUS only
**Shape**: 800 (xt) × 544 (yt)

**Action**:
- Read grid_yt and grid_xt coordinate arrays from NetCDF
- May need projection transformation to lat/lon
- Document grid_id as SHA256 hash of coordinates

### GBBEPx

**Dimension Names**: Latitude, Longitude
**Type**: Rectilinear Lat-Lon
**Coverage**: Global
**Shape**: 3600 (Longitude) × 1801 (Latitude)

**Action**:
- Read Latitude [-90, 90] and Longitude [-180, 180]
- Already in standard lat/lon coordinates
- Document grid_id as SHA256 hash of coordinates

---

## Validation Checklist

### T021 Completion Evidence
- [x] 15 focused decoder/product tests pass
- [x] 24 consecutive actual RAVE hours decode with contiguous intervals and stable grid identity
- [x] Species totals match independent sums from the raw product crops
- [x] Pinned GBBEPx reduced-species and dateline cases pass
- [ ] T025 model-substep allocation/conservation acceptance remains pending

### Before T023 Complete
- [ ] T022 normalized product outputs and transactional manifest commits are complete
- [ ] Required source-composition metadata is explicit and preserved
- [x] T018 factor semantics and pinned CB6r5-AE7-AQ configuration are verified
- [ ] Redistribution rights and independent CB6r5-AE7-AQ budget tests pass before production mapping
- [ ] Chemistry tests use independently verified semantics; no coefficient normalization or assumed NOx partition

### Before T025 Complete

- [ ] Reference totals computed from actual samples
- [ ] 24-hour product cases run end-to-end
- [ ] Mass conservation verified (SC-002: ±5%)
- [ ] Multiple substeps per source interval tested
- [ ] Grid geometry changes handled correctly
- [ ] GBBEPx 8-species → reject 11-species output
- [ ] All conservation tests pass

---

## Common Commands

### Load & Inspect Sample Files (Python)

```python
import netCDF4 as nc

# RAVE
rave = nc.Dataset('data/fire/product_samples/RAVE-HrlyEmiss-13km_v2r0_20260701_sample.nc')
print("RAVE dimensions:", list(rave.dimensions.keys()))
print("RAVE variables:", list(rave.variables.keys()))
print("CO2 shape:", rave.variables['CO2'].shape)
print("CO2 total:", rave.variables['CO2'][:].sum())

# GBBEPx
gbbepx = nc.Dataset('data/fire/product_samples/GBBEPx-all01GRID_v4r0_20230224_sample.nc')
print("GBBEPx dimensions:", list(gbbepx.dimensions.keys()))
print("GBBEPx variables:", list(gbbepx.variables.keys()))
print("CO shape:", gbbepx.variables['CO'].shape)
print("CO total:", gbbepx.variables['CO'][:].sum())
```

### Verify SHA256

```bash
cd data/fire/product_samples/
sha256sum -c hashes.txt
```

### View Manifest

```bash
cat data/fire/product_samples_manifest.yaml | head -50
cat data/fire/cb6_fire_factors_manifest.yaml | head -50
```

---

## Troubleshooting

| Problem | Cause | Solution |
|---------|-------|----------|
| NetCDF file not found | Wrong path | Use `data/fire/product_samples/...` exactly |
| Variable not found | Dimension name mismatch | Check manifest for exact names (grid_yt vs Latitude) |
| Wrong grid size | Reading wrong variable | Verify you're reading grid metadata, not data |
| Mass totals don't match | Unit conversion | Check units in manifest; apply factor if needed |
| CH4 not found in GBBEPx | Product limitation | Expected! GBBEPx is 8-species reduced set |
| Coordinates not read | File structure | Latitude/Longitude must exist in GBBEPx |

---

## Next Session Checklist

Before starting new session, have ready:
- [ ] Read T021_DECODER_QUICKSTART.md
- [ ] Read product_samples_manifest.yaml
- [ ] Understand RAVE vs GBBEPx differences
- [ ] Know CB6 factor semantics are verified, but production mapping is blocked by rights, budgets and source-composition requirements
- [ ] Know conservation tolerance (±5% SC-002)
- [ ] Know sample file locations and sizes
- [ ] Know species counts (11 RAVE, 8 GBBEPx, 43 CB6 coefficient rows)

---

## Key Dates

| Event | Date | Status |
|-------|------|--------|
| **T017 Completion** | 2026-11-20 | ✅ Complete |
| **T018 Completion** | 2026-11-20 | ✅ Complete |
| **T021 Target** | 2026-12-05 | ⏳ In Progress |
| **US1 MVP Target** | 2026-12-20 | ⏳ Upcoming |

---

**Keep this document open while implementing T021-T027!**

For detailed information, refer to the full documentation files linked above.
