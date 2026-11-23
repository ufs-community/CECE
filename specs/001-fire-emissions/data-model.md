# Data Model: CECE Fire Emissions

These are proposed contracts, not existing runtime types.

## Common Identity and Time

- `schema_version`: normalized input contract revision.
- `source_id`, `product_version`, `algorithm_version`, `delivery_id`, `content_sha256`: immutable provenance; acquisition time alone is not unique.
- `interval_start`, `interval_end`: half-open UTC timestamps; positive duration.
- `acquired_at`, `available_at`, `availability_cutoff`: accepted observations require availability at or before cutoff.
- `cutoff_mode`: `fixed` or `host_supplied`; both acquisition and availability must be at or before the explicit per-interval cutoff. Fixed mode reuses its configured UTC timestamp; host-supplied values come from the coupled host or the prepared provider's unique interval schedule. Forward cutoffs are nondecreasing, missing values fail, and observation age is cutoff minus acquisition time. The exact schedule and causal-context rules are defined in [the configuration contract](./contracts/configuration.md#availability-cutoff-and-reconstruction).
- `grid_id`, `grid_sha256`: dimensions, coordinates, bounds/areas and CRS; explicit longitude/date-line convention.

Product, algorithm, normalized-schema and scientific-profile versions are independent.

### Canonical Field Index

This index names the fields already described here so implementing tasks can cite exact `Entity.field` definitions. It does not change scientific scope or validation rules. Structured fields retain all constituent constraints described in their entity paragraphs below; they are not unvalidated free-form metadata.

| Entity | Canonical fields | Definition and constraints |
|--------|------------------|----------------------------|
| CommonIdentity | `schema_version`, `source_id`, `product_version`, `algorithm_version`, `delivery_id`, `content_sha256`, `interval_start`, `interval_end`, `acquired_at`, `available_at`, `availability_cutoff`, `cutoff_mode`, `grid_id`, `grid_sha256`, `grid_definition` | Common Identity and Time above; `grid_definition` contains dimensions, coordinates, positive areas, bounds, CRS and longitude/date-line convention. |
| FireObservation | `identity`, `sensor_platform`, `granule_pixel_key`, `footprint_geometry`, `parallax_status`, `frp_mw`, `quality_coverage`, `observation_status`, `source_reference` | FireObservation below; `identity` uses CommonIdentity; geometry includes viewing angles; quality/coverage includes cloud/observable fractions in [0,1] and saturation flags. |
| ScientificProfile | `identity_references`, `applicability`, `analysis_grid_cadence`, `overlap_quality_correction_rules`, `biome_ecoregion_maps`, `diurnal_cycles_time_basis`, `combustion_coefficient`, `species_factors`, `scaling`, `reconstruction_limits` | ScientificProfile below; identity includes version/hash/citations; coefficient kg/MJ and factors kg/kg; reconstruction limits include supported policies and maximum gap/age/window. |
| NormalizedSensorWindow | `profile_grid`, `interval_slots`, `sensor_power_time`, `coverage_quality`, `geometry_overlap`, `availability` | NormalizedSensorWindow below; slots/tiles are bounded independently of run length; sensor power/time stays separate by sensor and preserves profile-required geometry/support. |
| FireActivityInterval | `interval_grid_profile`, `power_support`, `fre_mj`, `coverage_contributions`, `reconstruction_method`, `oldest_source_age`, `quality_state`, `reason_flags` | FireActivityInterval below; MJ per cell, supported-energy conservation; quality_state is the four-value enum defined below. |
| ProductEmissionsInterval | `product_identity`, `interval_grid`, `species_availability`, `quantity_semantics`, `mass_kg`, `quality_coverage`, `optional_diagnostics`, `chemistry_state`, `target_identity`, `scaling_state` | ProductEmissionsInterval below; mass kg/cell/interval; explicit source quantity semantics, variant/version and optional diagnostics; chemistry/scaling states control bypass and duplicate conversion prevention. |
| FireBulkInventory | `species`, `mass_kg`, `component_ids`, `component_profiles`, `interval_grid_coverage_provenance`, `nox_reference_basis`, `voc_profile`, `particulate_overlap`, `optional_energy_dry_matter` | FireBulkInventory below; fixed eleven identifiers, selected-set completeness, distinct missing/invalid states, separately defined physical pools; component-resolved mass preserves spatially varying composition through chemistry. |
| FireTargetProfile | `identity`, `asset_hashes`, `typed_mappings`, `conversion_factors`, `nox_partition`, `oc_om_policy`, `source_disposition`, `budgets_tolerances` | FireTargetProfile below; conversion_factors includes needed molecular weights, carbon numbers and aerosol factors; source_disposition records mapped/diagnostic-only/excluded-with-reason. |
| FireRunProvenance | `accepted_hashes`, `cutoff`, `coverage_support`, `reconstruction_age`, `rejected_observations`, `remap_chemistry_budgets`, `performance_allocation` | FireRunProvenance below; replayable input/profile/grid identities, reconstructed fraction/oldest age, explicit units and bounded-memory diagnostic streaming. |
| IntervalLifecycle | `state`, `rejection_reason`, `cache_identity`, `revision_identity` | Relationships and State Transitions below; ready/commit validation, full cache identity, unconsumed updates and explicit historical revisions/restart semantics. |

## Entities

### FireObservation

Sensor/platform, granule/pixel keys; identity/times above; footprint, coordinates, viewing geometry, parallax status; FRP normalized to MW; QA, cloud/observable fraction, saturation and observed-fire/no-fire/missing status; original source reference.

**Validation**: Finite nonnegative accepted power. Invalid values are rejected or masked with reasons. Duplicate precedence is independent of file order. Zero means no fire only for a valid observation.

### ScientificProfile

Profile/version/hash/citations/applicability; analysis grid and cadence; overlap/quality/correction rules; versioned biome/ecoregion vocabularies and maps; normalized diurnal cycles and local-time basis; kg/MJ combustion coefficient; eleven species factors in kg/kg; optional scaling identity and applicability; reconstruction policies and maximum gap/age/window.

**Validation**: Requested classes covered, finite nonnegative factors, valid normalized cycles, no implicit unknown-biome mapping, compatible table versions, no repeated scaling.

`reconstruction_limits` identifies a verified retrospective or causal policy and its anchor-selection/integration rules and maximum gap/age/window. The RAVE retrospective baseline must not automatically enable a causal profile. Causal policy support requires independent verification; unsupported requests fail before integration.

### NormalizedSensorWindow

Profile/grid references, bounded interval slots; separate ABI/VIIRS power, times, coverage and QA; geometry and overlap provenance; availability information.

**Validation**: Compatible with analysis grid/cadence. Normalization cannot silently perform nonlinear correction or sensor fusion. Tile/slot capacity is independent of simulation length.

### FireActivityInterval

Interval/grid/profile, fused/reconstructed temporal power support, integrated MJ/cell, coverage/sensor contribution, reconstruction method and oldest source age.

Quality states: `observed`, `reconstructed`, `valid_zero`, `unavailable`, with reason flags.

**Validation**: Conserve supported energy; no duplicate contributions or partial intervals mislabeled complete. Unknown energy is not zero.

### ProductEmissionsInterval

Product/version/variant, interval/grid, species availability; original mass/rate/flux semantics; normalized kg/cell/interval; QA/coverage and optional FRP/FRE/dry matter; `chemistry_state` (`bulk` or `target_specific` with target identity); `scaling_state` (`unscaled` or scaling identity).

**Validation**: Already speciated products match the requested target and bypass chemistry mapping. Missing requested species fail unless an explicit reduced set is compatible. No FRP-to-mass calls.

### FireBulkInventory

Identifiers: `CO2`, `CO`, `CH4`, `PM25`, `TPM`, `SO2`, `OC`, `BC`, `NOX`, `NH3`, `VOC`; kg/cell/interval, grid/coverage/provenance; NOx reference basis and fire VOC-profile identity; particulate-overlap semantics. FRE/dry matter exist only when derived or actually supplied.

**Validation**: Selected species complete; missing and invalid are distinct. Compare each physical budget separately, never sum overlapping PM/OC/BC pools indiscriminately.

`component_ids` is a nonempty ordered set of unique source identifiers. `component_profiles` maps profiled components to versioned/hashed biome and fire-composition definitions; product inputs may instead list components in `unprofiled_component_ids` when those identities are unavailable. The two sets must be disjoint and cover every component. `mass_kg` retains the component dimension alongside species and cells until the last spatially varying chemistry coefficient has been applied; summing components recovers the reported bulk mass for each species. Unknown required composition is an error, not an averaged factor. Invariant product mappings may consume unprofiled components; composition-specific mappings may not. FRP-derived emissions still require applicable biome/ecoregion metadata before deriving emissions.

When chemistry varies by source component, map each component on its source support, conservatively allocate the resulting extensive target quantities, then sum compatible target quantities. Alternatively retain components through conservative allocation and apply their original coefficients separately before summation; no target-cell dominant-biome substitution is allowed. Only a verified spatially invariant mapping may collapse components and map after bulk allocation. Molecular/number outputs retain declared physical-budget conversion factors for conservation comparisons.

### FireTargetProfile

Exact AM4/CB6/CRACMM2/RACM2/GOCART2G version; species/fire-table hashes; typed source-to-target factors; molecular weights, carbon numbers or aerosol factors as needed; NOx partition; OC/OM policy; each source classified as mapped, diagnostic-only or excluded-with-reason; applicable conservation budgets/tolerances.

**Validation**: Reject ambiguous units, undefined species and missing factors. Unlike molar/carbon bases need not have factors summing to one; conservation uses the declared physical basis.

### FireRunProvenance

Accepted source/profile/grid hashes, cutoff, coverage and support; reconstructed fraction/oldest age; rejected observations/reasons; pre/post-remap and chemistry budgets; optional timing and rank/allocation information.

**Validation**: Replayable identities and policies. Diagnostics stream to disk instead of accumulating in RAM.

## Relationships and State Transitions

```text
discovered -> decoded -> validated -> normalized -> ready -> consumed -> evicted
                        |
                        `-> rejected(reason)

ready + derive_frp -> activity -> dry_matter -> bulk_inventory
ready + product    ------------------------> bulk_inventory
bulk_inventory (invariant mapping)
    -> conservative_allocation -> fire_mapping -> FIRE_exports
bulk_inventory (varying component mapping)
    -> per_component_fire_mapping -> conservative_target_allocation -> sum -> FIRE_exports
target_specific_product -> conservative_allocation -------> FIRE_exports
```

- Failed validation never yields a ready zero-filled interval.
- Missing context causes explicit error, deferred preparation or selected reconstruction, never silent completion.
- Output commits only after required fields/metadata/budgets validate; partial files cannot appear complete.
- Cache identity includes mode, grid/interval, accepted inputs, cutoff, quality rules, science/scaling/target profiles.
- Cache identity also includes the cutoff mode/schedule, raw-source selection and product temporal policy, including any temporal-profile hash. Prepared provider manifests retain these identities.
- Out-of-order delivery can update unconsumed windows after deduplication. Recomputing committed output needs an explicit new run/revision identity.
