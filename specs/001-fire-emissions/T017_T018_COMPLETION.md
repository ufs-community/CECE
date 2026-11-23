# T017 and T018 Completion

**Date**: 2026-11-20  
**Status**: T017 complete; T018 factor semantics verified  
**Investigation Phase**: Product samples and CB6r5-AE7-AQ factor semantics are verified; production chemistry remains gated

---

## Executive Summary

T017 product sample acquisition is complete. The user confirmed that the CB6 coefficients are verified speciation factors referenced to CO and PM2.5. All 43 factors match the cited immutable NOAA AQMv8_p2 source revision. This resolves T018's factor-semantics blocker; it does not certify redistribution rights or independent target budgets, so production chemistry remains disabled.

### What Was Accomplished

**T017 - GBBEPx and RAVE Product Samples**
- ✅ RAVE v2r0 hourly sample acquired (2026-07-01, 13 km CONUS grid)
- ✅ GBBEPx v4r0 daily sample acquired (2023-02-24, 0.1° global grid)
- ✅ All 11 RAVE species verified (CO2, CO, CH4, PM2.5, OC, BC, SO2, TPM, NH3, NOx, VOCs)
- ✅ All 8 GBBEPx species verified (BC, CO, CO2, OC, PM2.5, SO2, NH3, NOx)
- ✅ NetCDF structures examined and documented
- ✅ SHA256 hashes computed for file integrity
- ✅ Manifest created with complete metadata

**T018 - CB6 Fire Chemistry Factors**
- User confirmed the 43 rows are verified CB6 speciation factors referenced to PM2.5 and CO.
- All 43 factors were matched to `aqm.rc` at commit `130beee95c0bef00a4cc8f24177e4d4db6787cda`; its SHA256 is recorded in the factor manifest.
- The PM2.5-referenced values sum to 1.0; the CO-referenced values sum to 1.1247.
- The factor semantics and CB6r5-AE7-AQ configuration identity are verified; preserve coefficients without normalization.
- Redistribution rights are unverified (upstream metadata reports `NOASSERTION`) and independent CB6r5-AE7-AQ budgets remain pending.
- The NO and NO2 entries have a 9:1 coefficient ratio relative to CO; this does not establish a NOx mass partition.
- The separate 0.368 kg/MJ combustion coefficient is not part of this table and is not certified for CB6 product mapping.
- Do not normalize these values or use them in production mapping.

---

## Repository Changes

### New Files Created

1. **data/fire/product_samples/**
   - `RAVE-HrlyEmiss-13km_v2r0_20260701_sample.nc` (4.2 MB)
   - `GBBEPx-all01GRID_v4r0_20230224_sample.nc` (526 MB)
   - `hashes.txt` (SHA256 verification)

2. **data/fire/**
   - `product_samples_manifest.yaml` - Complete RAVE/GBBEPx metadata
   - `cb6_fire_factors_manifest.yaml` - CB6r5-AE7-AQ fire speciation factors

### Modified Files

**data/fire/asset_registry.yaml**
- Product samples remain recorded; the CB6 asset is `in_progress` with its local excerpt hash and explicit verification blockers.

---

## Asset Details

### RAVE Hourly Sample (T017)

| Property | Value |
|----------|-------|
| **File** | RAVE-HrlyEmiss-13km_v2r0_20260701_sample.nc |
| **Size** | 4.2 MB |
| **Date** | 2026-07-01T00:00:00Z (hourly) |
| **Grid** | 13 km CONUS (800×544 cells) |
| **Species** | 11 (all canonical + unscaled/scaled variants) |
| **SHA256** | 0ac99097022d50bd9128af93df8268a3d53f96b70bad8d267cfde497ab264141 |
| **Status** | ✅ Verified |

### GBBEPx Daily Sample (T017)

| Property | Value |
|----------|-------|
| **File** | GBBEPx-all01GRID_v4r0_20230224_sample.nc |
| **Size** | 526 MB |
| **Date** | 2023-02-24 (daily UTC) |
| **Grid** | Global 0.1° (3600×1801 cells) |
| **Species** | 8 (reduced set) |
| **SHA256** | 4dbcff118dd4bf78013d69a260c4700225464f1c0c3301839eb62d74ba40b56c |
| **Status** | ✅ Verified |

### CB6r5-AE7-AQ Fire Factors (T018)

| Property | Value |
|----------|-------|
| **Source** | NOAA-OAR-ARL ufs-srweather-app/feature/aqmv8_p2/parm/aqm.rc |
| **Lines** | 290-333 |
| **PM2.5 Species** | 17 (PAL, PCA, PCL, PEC, PFE, PK, PMG, PMN, PMOTHR, PNA, PNCOM, PNH4, PNO3, POC, PSI, PSO4, PTI) |
| **CO-referenced rows** | 26, including CO (25 non-CO gas rows) |
| **Total factor rows** | 43 |
| **CO-referenced sum** | 1.1247; not a unit-sum partition |
| **Status** | Numeric values transcribed; semantics and production use unverified |

---

## Verification Results

### RAVE Sample Verification

```
✅ NetCDF structure verified
✅ Time dimension: 1 hour (hourly timestep)
✅ Grid dimensions: 800×544 (13 km CONUS)
✅ All 11 species present (unscaled + scaled variants)
✅ Diagnostics available: FRP_MEAN, FRP_SD, FRE, Cloud_Fraction
✅ File integrity: SHA256 computed and stored
✅ Product emission masses are retained independently of diagnostic FRP/FRE fields.
```

### GBBEPx Sample Verification

```
✅ NetCDF structure verified
✅ Time dimension: 1 day (daily composite)
✅ Grid dimensions: 3600×1801 (0.1° global)
✅ All 8 reduced species present
✅ Diagnostics available: MeanFRP, CloudPerc, FirePerc, QCAll, NumSensor
✅ File integrity: SHA256 computed and stored
✅ Grid coordinates: Latitude [-90, 90], Longitude [-180, 180]
✅ Note: GBBEPx is daily; RAVE is hourly (different temporal resolution expected)
```

### CB6r5-AE7-AQ Factors Verification

```
User confirmed that the 43 rows are CB6 speciation factors referenced to PM2.5 and CO.
All rows match the pinned AQMv8_p2 source file and recorded SHA256.
The PM2.5-referenced values sum to 1.0; the CO-referenced values sum to 1.1247.
Preserve the factors as written; do not normalize them by row sum.
Redistribution rights and independent CB6r5-AE7-AQ budget validation remain pending.
Do not treat the NO:NO2 coefficient ratio as a NOx mass partition.
Do not use these values in production mapping until remaining certification gates pass.
```

---

## What This Enables

### Immediate (T021-T027 US1 MVP Implementation)

T017 samples are available. T018 factor semantics are verified; production chemistry is not yet certified:

1. **Decoder Implementation** (T019 foundation already complete)
   - RAVE decoder can now consume real sample data
   - GBBEPx decoder can now target 8-species reduced set
   - Use verified NetCDF structure as specification

2. **Chemistry Mapping** (T023)
   - Factor semantics and the pinned CB6r5-AE7-AQ configuration are verified
   - Production mapping remains blocked by redistribution rights, independent budgets and explicit source-composition metadata
   - Preserve the CO-referenced coefficients as written; do not normalize them
   - Product mapping must require explicit source-composition metadata

3. **Conservation Testing** (T025 test cases)
   - Use actual RAVE sample to verify 11-species mass balance
   - Use actual GBBEPx sample for 8-species reduction validation
   - Verify SC-001/SC-002 conservation tolerances

### Next Phase (T021-T027 Implementation)

| Task | Now Enabled | Required Input |
|------|-------------|-----------------|
| T021 | Complete; 15/15 focused tests pass, including 24 consecutive RAVE hours | Pinned actual-sample crops and 24-hour reference series |
| T022 | Complete; local RAVE/GBBEPx outputs and rollback tests pass | Actual-sample decoder inputs |
| T023 | Blocked | Explicit composition metadata; production gates require redistribution rights and independent target budgets |
| T024 | Blocked | T023 and verified chemistry assets |
| T025 | Pending | 24-hour conservation acceptance |
| T026 | Pending | Verified product and chemistry scope |
| T027 | Pending | Reference data and acceptance results |

---

## Documentation Created

### Primary Manifests (in data/fire/)

1. **product_samples_manifest.yaml** (6.5 KB)
   - Complete RAVE sample metadata
   - Complete GBBEPx sample metadata
   - NetCDF structure documentation
   - Variable descriptions and dimensions
   - Verification results

2. **cb6_fire_factors_manifest.yaml** (7.1 KB)
   - 43 transcribed numeric rows (17 PM2.5-referenced, 26 CO-referenced)
   - Factor sums and unresolved semantics documented
   - Production mapping explicitly blocked pending verification

### Supporting Files

- **product_samples/hashes.txt** - SHA256 verification
- **cb6_fire_config.txt** - Original aqm.rc excerpt

---

## Key Findings

### RAVE (Hourly, 11 Species)

**Strengths**:
- All 11 canonical species present (complete set)
- Both unscaled and scaled variants (more options)
- Hourly resolution (better temporal coverage)
- Rich diagnostics (FRP, FRE, cloud fraction)

**Considerations**:
- 13 km grid (coarser than 0.03° specification)
- North America coverage (requires downscaling)
- 1 hour sample (verify multi-hour behavior)

### GBBEPx (Daily, 8 Species)

**Strengths**:
- 8-species reduced set (matches operational product)
- Global coverage (0.1° grid)
- Rich metadata (MeanFRP, CloudPerc, NumSensor)
- Daily resolution (operational standard)

**Considerations**:
- Missing 3 species from canonical 11 (CH4, TPM, VOCs)
- Daily resolution (less temporal detail than hourly RAVE)
- Larger file (526 MB vs 4.2 MB RAVE)

### CB6r5-AE7-AQ Chemistry

**Key Properties**:
- 43 transcribed factor rows (not production-certified)
- PM2.5 highly organic (POC 46%, PNCOM 32%)
- CO-referenced coefficients sum to 1.1247, so are not a unit-sum partition
- NO:NO2 coefficient ratio is not an independent NOx partition

**Confidence**:
- Numeric rows are user-confirmed and match the pinned NOAA AQMv8_p2 excerpt
- Source identity is pinned; source unit labels are preserved, while independent target-budget validation remains pending

---

## Recommendations for Implementation

### For T019 (Decoder Tests)

Use the sample files as reference data:
```python
# test_fire_products.cpp
static constexpr const char* RAVE_SAMPLE = "data/fire/product_samples/RAVE-HrlyEmiss-13km_v2r0_20260701_sample.nc";
static constexpr const char* GBBEPX_SAMPLE = "data/fire/product_samples/GBBEPx-all01GRID_v4r0_20230224_sample.nc";
```

### For T020 (Chemistry Mapping)

Do not load the extracted table as a production profile. First verify the coefficient basis, exact mechanism revision, target species and independent budgets; retain the values without normalization.

### For T025 (Conservation Tests)

Compute reference totals from actual samples:
```python
# test_fire_conservation.cpp
double rave_total_co2 = sum(nc_dataset['CO2'][:])  # kg
double rave_total_mass = sum_all_11_species()     # kg
double gbbepx_total_co2 = sum(nc_dataset['CO2'][:])  # kg
```

---

## Success Criteria Met

| Criterion | Status | Evidence |
|-----------|--------|----------|
| **T017: Product samples located** | ✅ | RAVE + GBBEPx files in repo |
| **T017: NetCDF structure verified** | ✅ | Manifest documents all variables |
| **T017: SHA256 hashes computed** | ✅ | Stored in product_samples/hashes.txt |
| **T017: Metadata documented** | ✅ | product_samples_manifest.yaml |
| **T018: CB6r5-AE7-AQ identified** | ✅ | Pinned `aqm.rc` mechanism-file references |
| **T018: Numeric rows transcribed** | ✅ | 43 rows and local source excerpt hash |
| **T018: Factor semantics verified** | ✅ | User confirmation; all rows matched to pinned source |
| **T018: Production certification** | ⏳ | Rights and independent budgets pending |
| **Asset registry updated** | ✅ | CB6 asset marked `in_progress` |
| **Manifests created** | ✅ | Chemistry caveats recorded |

---

## Status Summary

| Task | Status | Completion | Blockers | Next Steps |
|------|--------|-----------|----------|-----------|
| **T017** | ✅ COMPLETE | 100% | None | Use samples in T019-T025 |
| **T018** | ✅ Complete | User-confirmed semantics; source-pinned, row-matched factors | Rights and independent budgets remain production gates | Preserve factors; no normalization |
| **T030** | ⏳ In Progress | 20% | RAVE ATBD extraction | Extract algorithms & ancillary |

---

## Remaining Work

### For US1 MVP (T021-T027)

**Unblocked by T017**:
- T021 decoder implementation now passes 15 actual-sample tests, including a complete 24-hour RAVE reference series
- T025 can use the one-day GBBEPx sample and 24 hourly RAVE crops for model-substep allocation/conservation tests
- T021 decoder acceptance is complete; T025 remains a separate downstream gate

**Blocked**:
- T023 requires explicit composition metadata; production activation additionally requires rights and independent budget verification

**Complete**:
- T017 reference totals for the 24-hour RAVE crop are independently recorded in `rave-24h-manifest.yaml`
- T030 completion (for product metadata validation)

### For US2 Derivation (T031-T043)

**Waiting on T030** (RAVE algorithms + ancillary data)
- Once T030 complete, can implement sensor fusion
- CB6r5-AE7-AQ mapping still requires independent target-budget verification and redistribution rights

---

## Sign-Off

| Item | Status |
|------|--------|
| **Product files acquired** | ✅ RAVE + GBBEPx samples |
| **CB6 numeric rows transcribed** | ✅ 43 local rows; interpretation pending |
| **Manifests created** | ✅ Comprehensive documentation |
| **Asset registry updated** | ✅ CB6 remains `in_progress` |
| **Ready for decoder implementation** | ✅ Decoder code and sample tests implemented |
| **Ready for production chemistry mapping** | ❌ No; redistribution rights and independent budgets pending |

**Investigation Status**: Product samples and CB6 factor semantics verified; production certification remains gated

**Estimated Timeline to US1 MVP**: 2-3 weeks (T021-T027 implementation)

---

**Updated**: 2026-11-20  
**Investigation Phase**: T017 and T018 complete  
**Next Phase**: T023 may proceed after explicit source-composition metadata is supplied; CB6 production activation still requires rights and independent budget verification
