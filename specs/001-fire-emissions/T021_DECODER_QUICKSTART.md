# T021 Decoder Implementation Quick Start

**Task**: T021 [US1] Implement product interval decoding for GBBEPx and RAVE
**Status**: 🟢 UNBLOCKED (T017 ✅ T018 ✅)
**Prerequisites**: T019 (product interval types) ✅
**Deadline**: Part of US1 MVP (2-3 weeks)

---

## What You're Implementing

**Goal**: Create decoders that read actual GBBEPx and RAVE NetCDF files, extract species masses, and normalize them to CECE's internal product interval format.

**Input Files**:
- `data/fire/product_samples/RAVE-HrlyEmiss-13km_v2r0_20260701_sample.nc` (reference)
- `data/fire/product_samples/GBBEPx-all01GRID_v4r0_20230224_sample.nc` (reference)

**Output**:
- Two decoder implementations in `src/core/cece_fire_input.cpp`
- Reference tests in `tests/test_fire_products.cpp`

---

## Critical Information

### Coordinate System Differences

**RAVE** uses:
```
Dimensions: (time, grid_yt, grid_xt)
Grid naming: grid_yt, grid_xt (not Latitude/Longitude)
Grid type: Lambert conformal on 13 km CONUS domain
Coordinates: Stored as projection coordinates, not lat/lon
```

**GBBEPx** uses:
```
Dimensions: (Time, Latitude, Longitude)
Grid naming: Latitude, Longitude (standard)
Grid type: Lat-lon rectilinear
Coordinates: [-180, 180] longitude, [-90, 90] latitude
```

**Action Required**: Both must normalize to common internal grid representation. Create transformation functions that:
1. Read grid coordinates from NetCDF
2. Convert to common reference system (or verify they're already compatible)
3. Store grid metadata for later use in allocation (T023)

### Species Availability

| Species | RAVE | GBBEPx | Note |
|---------|------|--------|------|
| CO2 | ✅ | ✅ | Present in both |
| CO | ✅ | ✅ | Present in both |
| CH4 | ✅ | ❌ | GBBEPx missing |
| PM2.5 | ✅ | ✅ | Present in both |
| OC | ✅ | ✅ | Present in both |
| BC | ✅ | ✅ | Present in both |
| SO2 | ✅ | ✅ | Present in both |
| TPM | ✅ | ❌ | GBBEPx missing |
| NH3 | ✅ | ✅ | Present in both |
| NOx | ✅ | ✅ | Present in both |
| VOCs | ✅ | ❌ | GBBEPx missing |

**Action Required**:
- Validation must check available species and reject requests for unavailable species
- For GBBEPx: Throw error if 11-species output requested (need CH4, TPM, VOCs)
- For RAVE: 11-species output always possible
- Document this in configuration contract

### Unit Consistency

**RAVE & GBBEPx Units**: Check `product_samples_manifest.yaml` for exact units
**Expected Dimension Chain**:
```
FRP [MW/pixel] × time_step [s]
  → FRE [MJ]
  → Dry Matter [kg] = FRE × 0.368 kg/MJ
  → Species Mass [kg] = DM × emission_factor
  → Final Output [kg/m²/s] (after regridding)
```

**Action Required**:
- Verify input file units match manifest
- Document unit conversions in decoder code
- Preserve units through allocation pipeline (T010 already handles this)

---

## Implementation Steps

### Step 1: Create Decoder Base Classes

**File**: `include/cece/cece_fire_input.hpp`

Create base decoder interface:
```cpp
class FireProductDecoder {
public:
  virtual ~FireProductDecoder() = default;

  // Open and validate product file
  virtual bool open(const std::string& filepath) = 0;

  // Get product metadata
  virtual ProductIdentity get_identity() = 0;
  virtual GridDefinition get_grid() = 0;
  virtual std::vector<std::string> get_available_species() = 0;

  // Extract data for one time interval
  virtual ProductEmissionsInterval decode_interval(size_t time_index) = 0;
};
```

### Step 2: Implement RAVE Decoder

**File**: `src/core/cece_fire_input.cpp`

```cpp
class RAVEDecoder : public FireProductDecoder {
private:
  std::unique_ptr<netCDF4::NcFile> file_;
  GridDefinition grid_;
  std::vector<std::string> species_;

public:
  bool open(const std::string& filepath) override {
    // 1. Open NetCDF file
    // 2. Verify it's RAVE (check attributes, version)
    // 3. Read grid definition (grid_yt, grid_xt)
    // 4. Enumerate available species (CO2, CO, CH4, PM2.5, etc.)
    // 5. Store metadata
    return true;
  }

  ProductEmissionsInterval decode_interval(size_t time_index) override {
    // 1. Read all species at time_index
    // 2. Extract coordinates (grid_yt, grid_xt values)
    // 3. Build ProductEmissionsInterval with:
    //    - product_identity (source, version, acquired_at)
    //    - interval_grid (dimensions, coordinates)
    //    - species_availability (all 11 species)
    //    - mass_kg (3D array for each species)
    //    - optional_diagnostics (FRP_MEAN, FRE, etc.)
    // 4. Return normalized interval
  }
};
```

**Key Methods to Implement**:

1. `open()`:
   ```cpp
   - file_ = std::make_unique<netCDF4::NcFile>(filepath, netCDF4::NcFile::read);
   - Check file->getAtt("Conventions") or similar for RAVE signature
   - Verify version == "v2r0" or similar
   - Read dimensions: time_dim = file->getDim("time")
   - Read grid: grid_yt = file->getVar("grid_yt");
   - species_ = { "CO2", "CO", "CH4", "PM2.5", "OC", "BC", "SO2", "TPM", "NH3", "NOx", "VOCs" }
   - Return true if all checks pass
   ```

2. `get_grid()`:
   ```cpp
   - Create GridDefinition with:
     - dimensions: (time=1, grid_yt=544, grid_xt=800)
     - coordinates: grid_yt and grid_xt values from NetCDF
     - grid_id: Compute SHA256 hash of grid definition
     - Apply RAVE projection if needed to get lat/lon bounds
   - Return GridDefinition
   ```

3. `decode_interval()`:
   ```cpp
   - For each species in species_:
     - Read variable at time_index: data = file->getVar(species_name)->getVar(...)
     - Convert to kg (handle scaled variants if needed)
     - Store in output mass_kg[species]
   - Extract diagnostics: FRP_MEAN, FRE, Cloud_Fraction
   - Build ProductEmissionsInterval
   - Return
   ```

### Step 3: Implement GBBEPx Decoder

**File**: `src/core/cece_fire_input.cpp`

```cpp
class GBBEPxDecoder : public FireProductDecoder {
private:
  std::unique_ptr<netCDF4::NcFile> file_;
  GridDefinition grid_;
  std::vector<std::string> species_;

public:
  bool open(const std::string& filepath) override {
    // 1. Open NetCDF file
    // 2. Verify it's GBBEPx (check attributes, version)
    // 3. Read grid definition (Latitude, Longitude)
    // 4. Enumerate available species (8-species reduced set)
    // 5. Store metadata
    return true;
  }

  ProductEmissionsInterval decode_interval(size_t time_index) override {
    // 1. Read 8 species at time_index (NO CH4/TPM/VOCs!)
    // 2. Extract coordinates (Latitude, Longitude arrays)
    // 3. Build ProductEmissionsInterval with:
    //    - product_identity (source, version, acquired_at)
    //    - interval_grid (Lat/Lon dimensions)
    //    - species_availability (8 species only)
    //    - mass_kg (3D array for each species)
    //    - optional_diagnostics (MeanFRP, CloudPerc, etc.)
    // 4. Return normalized interval
  }
};
```

**Key Differences from RAVE**:

1. `open()`:
   ```cpp
   - Verify version == "v4r0" or similar
   - Read dimensions: Time = file->getDim("Time") [note capital T]
   - Read grid: Latitude = file->getVar("Latitude");
   -            Longitude = file->getVar("Longitude");
   - species_ = { "BC", "CO", "CO2", "OC", "PM2.5", "SO2", "NH3", "NOx" }
   - NOTE: No CH4, TPM, VOCs in this product!
   ```

2. `get_grid()`:
   ```cpp
   - Create GridDefinition with:
     - dimensions: (Time=1, Latitude=1801, Longitude=3600)
     - coordinates: Latitude [-90, 90] and Longitude [-180, 180]
     - grid_id: Compute SHA256 hash of grid definition
   - Standard lat/lon grid, no projection needed
   - Return GridDefinition
   ```

3. `decode_interval()`:
   ```cpp
   - For each species in { "BC", "CO", "CO2", "OC", "PM2.5", "SO2", "NH3", "NOx" }:
     - Read variable at time_index: data = file->getVar(species_name)->getVar(...)
     - Convert to kg (usually already in correct units)
     - Store in output mass_kg[species]
   - Extract diagnostics: MeanFRP, CloudPerc, FirePerc, QCAll, NumSensor
   - Build ProductEmissionsInterval
   - Set species_availability to only 8 species (critical for validation later!)
   - Return
   ```

---

## Testing Strategy

### Unit Tests

**File**: `tests/test_fire_products.cpp`

```cpp
TEST(FireProductDecoderTest, RAVESampleOpensSuccessfully) {
  RAVEDecoder decoder;
  ASSERT_TRUE(decoder.open("data/fire/product_samples/RAVE-HrlyEmiss-13km_v2r0_20260701_sample.nc"));
}

TEST(FireProductDecoderTest, RAVEReturnsAllElevenSpecies) {
  RAVEDecoder decoder;
  decoder.open("data/fire/product_samples/RAVE-HrlyEmiss-13km_v2r0_20260701_sample.nc");

  auto species = decoder.get_available_species();
  ASSERT_EQ(species.size(), 11);
  EXPECT_TRUE(std::find(species.begin(), species.end(), "CO2") != species.end());
  EXPECT_TRUE(std::find(species.begin(), species.end(), "CH4") != species.end());
}

TEST(FireProductDecoderTest, RAVEDecodedIntervalHasCorrectDimensions) {
  RAVEDecoder decoder;
  decoder.open("data/fire/product_samples/RAVE-HrlyEmiss-13km_v2r0_20260701_sample.nc");

  auto interval = decoder.decode_interval(0);
  ASSERT_EQ(interval.interval_grid.dimensions["grid_yt"], 544);
  ASSERT_EQ(interval.interval_grid.dimensions["grid_xt"], 800);
}

TEST(FireProductDecoderTest, GBBEPxSampleOpensSuccessfully) {
  GBBEPxDecoder decoder;
  ASSERT_TRUE(decoder.open("data/fire/product_samples/GBBEPx-all01GRID_v4r0_20230224_sample.nc"));
}

TEST(FireProductDecoderTest, GBBEPxReturnsEightSpeciesOnly) {
  GBBEPxDecoder decoder;
  decoder.open("data/fire/product_samples/GBBEPx-all01GRID_v4r0_20230224_sample.nc");

  auto species = decoder.get_available_species();
  ASSERT_EQ(species.size(), 8);
  EXPECT_FALSE(std::find(species.begin(), species.end(), "CH4") != species.end());
  EXPECT_FALSE(std::find(species.begin(), species.end(), "TPM") != species.end());
  EXPECT_FALSE(std::find(species.begin(), species.end(), "VOCs") != species.end());
}
```

### Integration Tests

**File**: `tests/test_fire_conservation.cpp`

Add reference validation:
```cpp
TEST(FireProductConservationTest, RAVETotalMassMatchesReference) {
  RAVEDecoder decoder;
  decoder.open("data/fire/product_samples/RAVE-HrlyEmiss-13km_v2r0_20260701_sample.nc");

  auto interval = decoder.decode_interval(0);

  // Compute totals
  double total_co2 = 0.0;
  for (const auto& mass : interval.mass_kg["CO2"]) {
    total_co2 += mass;
  }

  // Reference value from product_samples_manifest.yaml
  double reference_co2 = 123456789.0;  // Replace with actual value

  // Allow ±5% tolerance (SC-002)
  EXPECT_NEAR(total_co2, reference_co2, reference_co2 * 0.05);
}
```

---

## Common Issues & Solutions

| Issue | Cause | Solution |
|-------|-------|----------|
| NetCDF file not found | Wrong path | Use absolute path: `data/fire/product_samples/...` |
| Variable not found | Dimension order mismatch | Check manifest for exact dimension names |
| Wrong grid dimensions | Reading wrong variable | Verify you're reading grid metadata, not data |
| Mass totals don't match | Unit conversion error | Check units in manifest; apply conversion if needed |
| CH4 not found in GBBEPx | Product limitation | Expected! GBBEPx only has 8 species |
| Grid coordinates not read | Missing Latitude/Longitude variables | These are required; verify they exist in file |

---

## Sign-Off Checklist

T021 is complete: all 15 focused product tests pass. The tests cover the
actual-sample RAVE and GBBEPx crops, 24 contiguous hourly RAVE intervals with
independently computed species totals, stable grid identity, embedded scaling,
and GBBEPx reduced-species/dateline behavior. T025's model-substep allocation
and conservation acceptance remains separate.

---

## Next Steps

1. **Implement T022**: Write verified product intervals and manifests transactionally.
2. **Proceed to T023 only after T022**: Preserve allocation semantics. T018 factor semantics and the CB6r5-AE7-AQ configuration identity are verified; production mapping remains blocked pending redistribution rights, independent budgets and explicit source-composition metadata.
3. **Implement T025**: Test model-substep allocation/conservation with actual samples.

---

## References

- **Sample Metadata**: `data/fire/product_samples_manifest.yaml`
- **Implementation Guide**: `specs/001-fire-emissions/IMPLEMENTATION_GUIDE.md`
- **Completion Report**: `specs/001-fire-emissions/T017_T018_COMPLETION.md`
- **Data Model**: `specs/001-fire-emissions/data-model.md`
- **Task Status**: `specs/001-fire-emissions/tasks.md`
