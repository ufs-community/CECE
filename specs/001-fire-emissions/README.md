# Fire Emissions Feature Implementation

**Current Phase**: 🟢 Ready for Implementation (T021-T027)  
**Investigation Phase**: ✅ Complete (T017-T018)  
**Last Updated**: 2026-11-20

---

## Quick Navigation

| Need | Document | Purpose |
|------|----------|---------|
| **One-page reference** | [QUICK_REFERENCE.md](./QUICK_REFERENCE.md) | Lookup tables, file locations, checklists |
| **Decoder implementation** | [T021_DECODER_QUICKSTART.md](./T021_DECODER_QUICKSTART.md) | Detailed guide for T021 implementation |
| **Implementation tips** | [IMPLEMENTATION_GUIDE.md](./IMPLEMENTATION_GUIDE.md) | Best practices, common pitfalls, dimensional chain |
| **Verification report** | [T017_T018_COMPLETION.md](./T017_T018_COMPLETION.md) | Detailed completion report with all verification |
| **Task tracking** | [tasks.md](./tasks.md) | Full task list (T001-T043) with status |
| **Architecture** | [plan.md](./plan.md) | Overall design and approach |
| **Requirements** | [spec.md](./spec.md) | Full specification with contracts |
| **Data model** | [data-model.md](./data-model.md) | Field definitions and validation rules |
| **Configuration** | [contracts/configuration.md](./contracts/configuration.md) | Config schema and options |

---

## What's Complete

### Investigation Phase ✅

**T017 - Product Sample Acquisition** ✅
- RAVE v2r0 hourly sample (13 km CONUS, 11 species, 4.2 MB)
- GBBEPx v4r0 daily sample (0.1° global, 8 species, 526 MB)
- Both stored in `data/fire/product_samples/` with SHA256 verification
- Complete manifests with all variable documentation

**T018 - CB6 Chemistry Factors** ✅ Factor semantics verified
- User confirmed the 43 factors are CB6 speciation coefficients referenced to PM2.5 and CO
- All rows match the pinned AQMv8_p2 source revision; its config identifies `cb6r5_ae7_aq`
- Preserve the PM2.5 coefficient sum of 1.0 and CO-referenced sum of 1.1247 without normalization
- Redistribution rights and independent budgets remain unverified; production mapping remains blocked

### Foundation Phases ✅

**Phase 1: Setup** ✅ (T001-T003)
- Fixture schema and manifest conventions
- Fire test helper framework
- CMake registration and CTest discovery

**Phase 2: Foundation** ✅ (T004-T015)
- Typed identity/metadata (T004)
- Grids and inventories (T005)
- Configuration (T006-T008)
- Lifecycle and allocation (T009-T010)
- Profiles and mapping (T011-T012)
- Validation and CLI (T013-T015)

---

## What's Next

### Ready to Implement Now 🟢

**T021 - Decoder Implementation (Complete)**
- RAVE v2r0 and GBBEPx v4r0 decoders pass 15 focused tests, including 24 consecutive actual RAVE hours and pinned GBBEPx crops
- T025's model-substep allocation/conservation tests remain a separate downstream gate
- Reference: [T021_DECODER_QUICKSTART.md](./T021_DECODER_QUICKSTART.md)

**T022 - Normalized Product Preparation**
- Complete: actual RAVE/GBBEPx sample normalization, per-interval outputs, checksummed manifest, and failure cleanup

**T023 - Chemistry Mapping**
- Follows T022; allocation/mapping needs explicit source-composition metadata
- T018 factor semantics are verified; CB6 mapping remains blocked pending redistribution rights, independent budgets and explicit source-composition metadata

**T024-T027 - Integration & Testing** (1-2 weeks, after T023)
- Configuration examples with real products
- Conservation tests with actual samples
- Documentation distinguishes verified product decoding from pending chemistry certification
- Acceptance recording

---

## Asset Reference

### Product Samples

Located in `data/fire/product_samples/`:

```
RAVE-HrlyEmiss-13km_v2r0_20260701_sample.nc  (4.2 MB)
  - 11 canonical fire species
  - Unscaled + scaled variants
  - 41 total variables
  - Dimensions: (time=1, grid_yt=544, grid_xt=800)
  - SHA256: 0ac99097...

GBBEPx-all01GRID_v4r0_20230224_sample.nc  (526 MB)
  - 8 reduced species (no CH4/TPM/VOCs)
  - 26 total variables
  - Dimensions: (Time=1, Latitude=1801, Longitude=3600)
  - SHA256: 4dbcff11...

hashes.txt
  - SHA256 verification for reproducibility
```

### Documentation

Located in `data/fire/`:

```
product_samples_manifest.yaml
  - 41 RAVE variables documented
  - 26 GBBEPx variables documented
  - All dimensions, dtypes, units

cb6_fire_factors_manifest.yaml
  - 43 source-matched and user-confirmed coefficient rows
  - PM2.5 coefficients sum to 1.0; CO-referenced coefficients sum to 1.1247
  - Factor semantics verified; rights, independent budgets and production eligibility remain unresolved

asset_registry.yaml
  - Product samples are recorded; CB6 factor semantics are verified
  - Production activation remains blocked by rights and independent budget tests
```

---

## Key Numbers

### RAVE Sample
- **Species**: 11 (all canonical)
- **Grid**: 13 km CONUS (800×544 cells)
- **Time**: Hourly (2026-07-01)
- **Size**: 4.2 MB
- **Variables**: 41 (11 species + scaled + diagnostics)

### GBBEPx Sample
- **Species**: 8 (reduced set, no CH4/TPM/VOCs)
- **Grid**: Global 0.1° (3600×1801 cells)
- **Time**: Daily (2023-02-24)
- **Size**: 526 MB
- **Variables**: 26 (8 species + diagnostics)

### CB6r5-AE7-AQ Chemistry
- **PM2.5 Species**: 17 (POC 46%, PNCOM 32%, PEC 9%, others 13%)
- **CO-referenced rows**: 26, including the CO reference (25 non-CO gas rows)
- **Total factor rows**: 43
- **CO-referenced sum**: 1.1247; not a unit-sum mass partition
- **Status**: Not production eligible; see factor manifest for blockers

---

## Critical Differences

### Grid Coordinates

| Property | RAVE | GBBEPx |
|----------|------|--------|
| **Dims** | grid_yt, grid_xt | Latitude, Longitude |
| **Type** | Lambert Conformal | Rectilinear Lat-Lon |
| **Coverage** | 13 km CONUS | Global 0.1° |
| **Shape** | 800×544 | 3600×1801 |

### Species Availability

| Species | RAVE | GBBEPx |
|---------|------|--------|
| CO2, CO, PM2.5, OC, BC, SO2, NH3, NOx | ✅ | ✅ |
| CH4, TPM, VOCs | ✅ | ❌ |

**Important**: GBBEPx decoder must reject requests for 11-species output (insufficient data).

---

## Validation Checklist

### T021 Completion Evidence
- 15/15 focused product tests pass, including the complete 24-hour RAVE reference series
- The 24 hourly crops retain individual source names/hashes and independently summed species totals
- GBBEPx daily and dateline crops validate reduced species and grid handling
- T025 model-substep allocation/conservation acceptance is still pending

### Before T023 Complete
- [x] CB6r5-AE7-AQ factor semantics and configuration identity verified
- [ ] 17 PM2.5 mapping working
- [ ] 26 CO-referenced rows mapped with an explicit non-partition budget, if confirmed by the source
- [ ] Required composition, NOx, VOC and aerosol metadata supplied

### Before T025 Complete
- [ ] 24-hour product cases run
- [ ] Mass conservation verified (±5%)
- [ ] Multiple substeps tested
- [ ] All conservation tests pass

---

## Timeline

| Phase | Tasks | Status | Duration |
|-------|-------|--------|----------|
| **Investigation** | T017-T018 | ✅ Complete | — |
| **Implementation** | T021-T027 | 🟢 Ready | 2-3 weeks |
| **US1 MVP** | Decoders + Chemistry + Tests | ⏳ Upcoming | ~2 weeks |
| **US2 Derivation** | Sensor fusion | ⏳ After US1 | — |

---

## Documentation Standards

All implementation documents follow this structure:

1. **Quick Summary** (1-2 paragraphs)
2. **Critical Information** (key numbers, differences, gotchas)
3. **Implementation Steps** (numbered, executable)
4. **Testing Strategy** (unit tests, integration tests, reference validation)
5. **Common Issues** (problems and solutions)
6. **Sign-Off Checklist** (completion criteria)

See any document in this directory for examples.

---

## For Implementation Teams

### Starting T021 (Decoder Implementation)

1. Read: [T021_DECODER_QUICKSTART.md](./T021_DECODER_QUICKSTART.md)
2. Reference: `data/fire/product_samples_manifest.yaml`
3. Load: `data/fire/product_samples/*.nc` (use in tests)
4. Implement: `src/core/cece_fire_input.cpp`
5. Test: `tests/test_fire_products.cpp`
6. Validate: Reference totals from actual samples

### Starting T023 (Chemistry Mapping)

1. Read: [configuration.md](./contracts/configuration.md) (product temporal allocation)
2. Reference: `data/fire/cb6_fire_factors_manifest.yaml`
3. Verify the coefficient basis and exact mechanism definition before mapping
4. Preserve explicit product composition and source interval semantics
5. Do not normalize the CO-referenced coefficients or infer FRP conversion

### Starting T025 (Conservation Tests)

1. Load: `data/fire/product_samples/*.nc` for reference totals
2. Test: 24-hour product cases end-to-end
3. Validate: ±5% mass conservation (SC-002)
4. Check: Multiple substeps, geometry changes, edge cases

---

## Common Questions

**Q: Where are the actual product files?**  
A: `data/fire/product_samples/` - RAVE (4.2 MB) and GBBEPx (526 MB)

**Q: Where do I find variable documentation?**  
A: `data/fire/product_samples_manifest.yaml` - all 41 RAVE + 26 GBBEPx variables

**Q: Where are the CB6 factors?**  
A: `data/fire/cb6_fire_factors_manifest.yaml` - 43 transcribed rows; their semantics are not yet production-verified

**Q: What's the combustion coefficient?**  
A: 0.368 kg/MJ is a separate RAVE ATBD claim and is not part of the CB6 product-factor mapping.

**Q: How do I validate my implementation?**  
A: Use reference totals from actual samples; ±5% tolerance (SC-002)

**Q: Why does GBBEPx only have 8 species?**  
A: Product limitation - it doesn't include CH4, TPM, or VOCs

**Q: How long will T021-T027 take?**  
A: 2-3 weeks total (1-2 weeks for T021, 1 week for T023, 3-5 days each for T024-T027)

---

## Next Session Reminder

Before next session, have:
- [ ] Read QUICK_REFERENCE.md (5 min)
- [ ] Read T021_DECODER_QUICKSTART.md (20 min)
- [ ] Located product sample files (verify they exist)
- [ ] Located manifest files (product_samples_manifest.yaml, cb6_fire_factors_manifest.yaml)
- [ ] Understood RAVE vs GBBEPx differences
- [ ] Ready to implement RAVE decoder

---

**Status**: Investigation complete. Implementation ready. All assets staged. Proceed with T021.

For questions, refer to [QUICK_REFERENCE.md](./QUICK_REFERENCE.md) or [IMPLEMENTATION_GUIDE.md](./IMPLEMENTATION_GUIDE.md).
