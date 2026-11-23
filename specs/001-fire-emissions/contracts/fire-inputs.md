# Proposed Normalized Fire Input Contract

**Status**: Version 1 design. Actual NOAA names/units require verified representative files.

## Container and Metadata

Use a manifest plus bounded-interval CF-style NetCDF files; the format does not imply loading all fields/times.

Manifest: schema/mode, decoder version, accepted source hashes and product/algorithm versions, original URLs or local IDs, acquisition/availability times, grid hash/CRS, applicable science/ancillary identities, interval coverage/files/hashes/completion, species/units, scaling and chemistry states.

Raw-source selection is a separate `source_manifest`; prepared runtime consumes only the normalized `input_manifest`. Record the resolved source-manifest hash, product temporal policy/profile, cutoff mode and per-interval cutoff/schedule hash as defined in the [configuration contract](./configuration.md). Preflight must not require a previously generated normalized manifest.

Cells need bounds and area in m2. Convert source km2 explicitly. UTC time bounds and supported calendar are mandatory. No invented precision; cited adapter metadata overrides are explicit and recorded.

## Derivation Inputs

Retain sensor contributions separately; this is not a pre-fused product.

| Quantity | Units/support | Required semantics |
|----------|---------------|--------------------|
| Sensor FRP | MW, sensor/analysis cell/time | Instantaneous versus interval-mean distinguished |
| Acquisition/time support | UTC timestamps/bounds | Preserve irregular timing before approved binning |
| Availability | UTC | Accept no input after cutoff |
| Observable/cloud fraction | 0..1 | Validity separate from zero |
| QA | Enumerated/masked | Preserve reasons, no default-good missing QA |
| Geometry | Explicit angle/length units | Viewing angle, footprint, parallax, overlap as profile requires |
| Land cover/ecoregion | Versioned IDs | Compatible factor/climatology definitions |
| Diurnal support | Normalized nonnegative fractions | Explicit local-solar-time bins |

Preparation may decode/project/index/deduplicate identical deliveries and perform declared linear footprint aggregation. If aggregation loses information needed by parallax/overlap/nonlinear correction, retain footprint input for the CECE provider. Do not fuse sensors or generate emissions in preparation.

## Product Inputs

Canonical bulk names: `fire_mass_<species>`, with identifiers:

```text
CO2 CO CH4 PM25 TPM SO2 OC BC NOX NH3 VOC
```

- Canonical quantity: kg/cell/interval.
- Source kg total: retain; kg/s mean rate: multiply by duration; kg/m2/s flux: multiply by source area and duration.
- Other units need validated explicit conversion; ambiguity fails.
- FRP/FRE/dry matter are optional diagnostics, not substitutes for missing emissions.
- Already target-specific products identify exact target species/units/profile and bypass bulk-to-target mapping.

## Temporal/Spatial Conservation

- `[start,end)` prevents boundary duplication.
- Product allocation selects `uniform`, `source_resolved` or `profile` according to the configuration contract. Preserve finer supplied intervals; reject incompatible or repeated profile weighting and record the selected policy/profile.
- Subinterval sums recover covered totals. Partial space/time coverage remains reported; do not renormalize away missing regions.
- Integrate power over actual support before dry-matter conversion.
- Map extensive mass/energy conservatively. If a remapper expects density, divide by source area, use the correct conservative operator, then reconstruct totals with target area; prove equivalence.
- Preserve `FireBulkInventory.component_ids` and `component_profiles` with component-resolved species mass until spatially varying chemistry factors apply. Component sums must recover bulk species totals. Map varying components separately before conservative target allocation/summation, or retain them separately through allocation; only invariant factors permit collapsing components first. Reject missing required composition metadata rather than average factors or choose a target cell's dominant biome.
- Outside regional RAVE coverage means unavailable, not zero-fire global coverage.

## Mapping and Output

- Bulk VOC mass is not mechanism composition; require fire profiles and declared mass/carbon tracking.
- Mapping factors declare mass, molar, carbon-molar or number bases.
- NOx identifies reference mass before NO/NO2 partition.
- PM25/TPM/OC/BC overlap; totals do not authorize adding constituents twice.
- OM/OC, bins/modes and number conversions require named factors and necessary molecular weights/densities/diameters.
- Default `FIRE_<target_species>` flux units are kg/m2/s where applicable. Required amount/number units must be genuine conversions, never relabeling.
- Default surface placement or existing configured vertical allocation preserves declared column budgets.

## Completeness and Reproducibility

- Commit ready intervals/manifests only after required fields/schema/checksums validate.
- Identity includes accepted sources, cutoff, science/scaling/chemistry profiles, grid and interval.
- Fixed and host-supplied cutoffs follow the configuration contract, including schedule/host agreement, nondecreasing forward cutoffs, acquisition/availability checks and age measured against cutoff. Causal requests cannot use acquisitions after interval end or an unverified reconstruction profile.
- Same accepted content in different delivery order yields equivalent outputs within declared tolerance.
- Changed source bytes invalidate cache hashes even under unchanged filenames.
- Restart/replay cannot add a second copy of committed interval totals.
- Preserve supplied uncertainty; do not claim derived uncertainty propagation without a scientific formulation.
