# Feature Specification: Online and Product-Based Fire Emissions

**Feature Branch**: `develop` (existing branch; no branch-creation hook configured)

**Created**: 2026-11-19

**Status**: Draft, quality-validated; ready for implementation planning

**Input**: Add NOAA/UFS biomass-burning emissions to CECE using either online fusion of GOES-R ABI FRP, JPSS VIIRS 375 m FRP, and ecoregional diurnal climatologies, or ingestion of derived global GBBEPx/regional RAVE products. Support CO2, CO, CH4, PM2.5, TPM, SO2, OC, BC, NOx, NH3, and VOCs, with fire-specific mappings for AM4, CB6, CRACMM2, RACM2, and GOCART2G. The user confirmed CRACMM2 as the intended spelling.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Use existing NOAA fire products (Priority: P1)

As a UFS emissions user, I can select global GBBEPx or regional RAVE emissions, preserve their scientific meaning, and supply my model without repeating the satellite-fusion or fire-to-emissions calculation.

**Why this priority**: Provides an independently useful operational entry point and a reference for evaluating online derivation.

**Independent Test**: Supply a small, documented product sample and verify the resulting emissions on a different target grid over multiple model timesteps.

**Acceptance Scenarios**:

1. **Given** a supported GBBEPx daily product, **When** I request available species, **Then** CECE preserves their interval-integrated mass through time allocation and spatial remapping.
2. **Given** hourly RAVE emissions and associated FRP, **When** I select product mode, **Then** CECE uses supplied emissions without applying FRP-to-emissions coefficients or biome scaling again.
3. **Given** a product missing requested CH4, TPM, or VOCs, **When** validation runs, **Then** CECE reports the missing species and rejects the request unless I explicitly select a supported reduced species set.
4. **Given** overlapping product deliveries or scaled and unscaled variants, **When** inputs are selected, **Then** one explicitly identified version and variant contributes per covered interval, with no silent mixing or duplication.

---

### User Story 2 - Derive emissions from satellite FRP (Priority: P1)

As a fire-emissions scientist, I can derive emissions inside CECE by combining the temporal sampling of GOES-R ABI with the fine-spatial fire observations of JPSS VIIRS and ecoregion-specific diurnal information.

**Why this priority**: This is the new online scientific capability, independently testable without operational remote services.

**Independent Test**: Use synthetic and reference satellite scenes with known observation times, coverage, ecoregions, and energy totals to verify fused fire activity and all eleven emitted quantities.

**Acceptance Scenarios**:

1. **Given** quality-screened overlapping ABI and VIIRS observations, **When** I select the documented fusion method, **Then** CECE derives a single fire-activity estimate without adding duplicate representations of the same fire.
2. **Given** supported fire radiative energy, dry-matter conversion factors, and emission factors, **When** CECE derives emissions, **Then** all eleven requested quantities follow the documented dimensional conversions and coefficient version.
3. **Given** gaps or polar-only coverage, **When** the configured reconstruction policy is applicable, **Then** CECE uses normalized ecoregional diurnal information, preserves the supported energy total, and identifies reconstructed contributions.
4. **Given** delayed observations, **When** a model step is processed, **Then** only information available by the configured availability cutoff is used and its age is reported.

---

### User Story 3 - Supply different chemical mechanisms (Priority: P2)

As a chemistry-model user, I can select AM4, CB6, CRACMM2, RACM2, or GOCART2G and receive fire-specific gas and aerosol quantities in that target's required naming and units.

**Why this priority**: The same fire estimate must be scientifically usable by multiple UFS chemistry configurations.

**Independent Test**: Apply each target profile to a fixed fire-emission vector and compare its species, units, and explicitly declared conservation budgets with independently calculated expectations.

**Acceptance Scenarios**:

1. **Given** a validated profile for one requested target, **When** either input mode produces fire emissions, **Then** the same fire-specific mapping is applied exactly once.
2. **Given** bulk VOCs, NOx, OC, BC, PM2.5, and TPM, **When** mapping runs, **Then** explicit VOC composition, NOx mass basis and split, aerosol conventions, and overlapping particulate totals prevent double counting.
3. **Given** a missing target definition or unsupported species, **When** initialization runs, **Then** CECE reports the precise incompatibility instead of substituting another mechanism or dropping mass silently.

---

### User Story 4 - Run reproducibly on connected and offline systems (Priority: P2)

As an HPC/UFS operator, I can prepare inputs on an approved connected host and run offline, or consume available inputs during a connected run, with the same scientific result for identical inputs.

**Why this priority**: Forecast compute nodes may lack outbound network access, and high-resolution runs need predictable resource usage.

**Independent Test**: Run a fixed high-resolution case from staged inputs with outbound access disabled, compare with the same locally available inputs in online mode, and inspect peak memory and provenance.

**Acceptance Scenarios**:

1. **Given** complete staged inputs, **When** CECE runs without network access, **Then** all requested model steps complete without remote requests.
2. **Given** missing, stale, invalid, or out-of-coverage data, **When** an interval is processed, **Then** CECE follows an explicit fail-or-reconstruct policy and distinguishes unobserved conditions from observed zero fire.
3. **Given** a high-resolution example, **When** it runs for 24 intervals, **Then** working memory does not grow in proportion to previously processed intervals, and input, coefficient, and mapping versions are recoverable.

### Edge Cases

- ABI view overlap, VIIRS repeat deliveries, and repeated forecast updates must not count one observation twice.
- Cloud, missing granules, saturation, poor viewing geometry, invalid or negative FRP, and valid zero-fire observations have distinct meanings.
- GOES coverage is not global; outside its footprint, only an explicitly supported polar/climatological reconstruction may be used, with diagnostic coverage labels.
- Date-line and polar cells, partial source coverage, irregular observation times, UTC-day boundaries, and local-solar-time climatologies require explicit treatment.
- Time steps spanning multiple input intervals must conserve the supported total; missing intervals must not be bridged silently.
- Scaled versus unscaled RAVE, product-version changes, mass-versus-molar units, NOx reference mass, and PM/OC/BC overlap must be validated.
- Already mechanism-specific products must not be speciated a second time.
- A changed target grid, time window, coefficient set, or mapping must invalidate incompatible prepared inputs or cached results.
- Unsupported biomass regimes, absent ecoregions, missing chemical profiles, and incomplete product metadata must produce actionable errors.
- Forecast time beyond the latest available observations requires explicit, age-limited persistence/reconstruction, not access to future observations.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Users MUST explicitly select either satellite-FRP derivation or supplied-product emissions for a fire contribution. Selecting both for the same space-time contribution MUST fail rather than add duplicate fire emissions.
- **FR-002**: Product mode MUST support version-identified global GBBEPx and regional RAVE inputs, validate their coverage, species, interval bounds, units, and scaling status, and bypass satellite fusion and FRP-to-emission conversion.
- **FR-003**: The derivation mode MUST accept GOES-R ABI FRP, JPSS VIIRS 375 m fire-product FRP, observation geometry/coverage, quality information, acquisition and availability times, and versioned ecoregion and diurnal ancillary data.
- **FR-004**: A documented, versioned fusion method MUST reconcile overlapping observations and sensor footprints without duplicate energy, and distinguish observed, reconstructed, unavailable, and zero-fire contributions.
- **FR-005**: Diurnal reconstruction MUST specify local time convention, supported ecoregions, gap/coverage applicability, and normalization that preserves the supported interval's total fire radiative energy.
- **FR-006**: Derived emissions MUST support CO2, CO, CH4, PM2.5, TPM, SO2, OC, BC, NOx, NH3, and VOCs through traceable FRP-to-energy, energy-to-dry-matter, and dry-matter-to-emitted-mass calculations. Scientific coefficients and their units MUST be explicit and versioned.
- **FR-007**: Biome-specific scaling MUST be explicitly selected, recorded, and applied no more than once. Unscaled RAVE MUST be the default variant unless the user selects a documented scaled product/profile.
- **FR-008**: Both modes MUST conserve interval-integrated extensive quantities during spatial and temporal allocation; intensity or flux inputs MUST be converted using valid areas and durations, not treated as extensive totals.
- **FR-009**: Missing requested species MUST cause validation failure unless the user explicitly selects a compatible reduced species set. Product mode MUST NOT invent missing emissions from FRP.
- **FR-010**: Both modes MUST support versioned fire-specific target profiles for AM4, CB6, CRACMM2, RACM2, and GOCART2G, with verified target identities, species names, units, and mapping bases. Biogenic profiles MUST NOT be substituted for fire profiles.
- **FR-011**: Chemical mapping MUST declare VOC composition, NOx reference mass and partitioning, molecular-weight conversions, aerosol mass/number conventions, and treatment of overlapping particulate categories. Every source category MUST be accounted for as mapped, diagnostic-only, or explicitly excluded.
- **FR-012**: Users MUST select and document handling of missing observations, stale data, outside-footprint conditions, and forecast persistence, including maximum admissible ages. No missing input MAY silently become zero emissions.
- **FR-013**: Online runs MUST honor observation availability cutoffs and report input age. Identical accepted inputs, policies, and versions MUST produce reproducible outputs independently of delivery order.
- **FR-014**: Complete prepared inputs MUST permit offline execution without network calls. Connected acquisition MUST fail with actionable connectivity/authentication messages and bounded retries.
- **FR-015**: Diagnostics MUST expose fire activity, integrated energy, consumed dry matter where derived, pre-mapping species budgets, output budgets, quality/coverage status, reconstruction flags, source age, and all input/coefficient/mapping identities applicable to the selected mode.
- **FR-016**: Both standalone and UFS-coupled operation MUST accept the same scientific inputs and produce equivalent fire contributions for the same domain and timestep.
- **FR-017**: High-resolution execution MUST bound the working input window rather than retain every prior interval. Example workflows MUST state grid, duration, measured peak memory, requested resources, and effective allocated resources.
- **FR-018**: Default emissions MUST enter at the surface and reuse existing configured vertical allocation where requested. New plume-rise physics MUST NOT be required for this feature.
- **FR-019**: Existing non-fire schemes and existing fire-free configurations MUST retain their behavior. Fire emissions MUST remain separately identifiable before intentional combination with other inventories.

### Key Entities

- **Fire observation**: Sensor/product/version identity, footprint, acquisition and availability time, FRP and units, quality, coverage, and unique observation key.
- **Fire-activity interval**: Spatial support, time bounds, fused/reconstructed FRP or energy, provenance, coverage, age, and reconstruction status.
- **Scientific ancillary profile**: Ecoregion/biome definitions, normalized local-time cycles, dry-matter and species factors, scaling rules, uncertainty information when supplied, and citations.
- **Supplied fire-emissions interval**: Product/version/variant, grid and bounds, species quantities and units, ancillary FRP where present, and source metadata.
- **Fire-emissions inventory**: Eleven potential bulk species, mass basis, particulate relationships, domain/time support, and available species set.
- **Chemical target profile**: Exact target version, output species/units, fire-specific transformations, applicable conservation budgets, and excluded/diagnostic quantities.
- **Run provenance and budgets**: Accepted sources, availability cutoff, scientific/profile versions, reconstructed fractions, area/time coverage, and input/output totals.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Each mode completes a 24-hour test case, including at least two model timesteps per source interval in a temporal-allocation test, with no duplicate fire contribution.
- **SC-002**: Fully covered spatial and temporal allocation tests preserve integrated energy and each applicable emitted-mass budget within relative error 1e-6 for nonzero totals; zero-total synthetic cases remain zero within absolute tolerance 1e-12 in the declared comparison units.
- **SC-003**: All eleven derived species match independent dimensional-calculation fixtures within relative error 1e-6. NOAA reference cases meet per-variable numerical tolerances declared and approved before comparison, with all discrepancies reported rather than normalized away.
- **SC-004**: All five chemical targets pass fire-profile tests covering supported species, unit conversions, declared conservation budgets, exclusions, and duplicate-mapping prevention; unsupported configurations fail before time integration.
- **SC-005**: A 24-hour prepared-input case completes with network access disabled and agrees with identical accepted inputs in connected execution within relative error 1e-6 for nonzero emissions.
- **SC-006**: On a declared 3600 by 1800 surface-grid test, peak memory for 24 intervals is no more than 1.2 times that of an otherwise identical 12-interval case after warm-up; the example records absolute peaks, platform, profiles, and effective allocation. This is a bounded-memory criterion, not a promise of a particular RAM requirement.
- **SC-007**: Every missing-input, stale-input, duplicate, unavailable-species, unsupported-target, and double-scaling test produces its expected explicit error or documented, visibly flagged reconstruction.
- **SC-008**: Standalone and coupled test cases produce equivalent pre-mapping and mapped fire budgets within relative error 1e-6, while the existing non-fire regression suite retains its established tolerances.

## Assumptions

- CRACMM2 is the confirmed target name. AM4 and GOCART2G require exact, versioned chemistry/aerosol definitions; existing GOCART definitions are not assumed equivalent.
- This feature is inspired by NOAA GBBEPx/RAVE; it is not represented as an official operational reproduction without an approved scientific baseline and reference comparisons.
- The selected initial derivation baseline will be pinned to a documented RAVE algorithm revision and its compatible ancillary datasets. A modular boundary permits later scientific profiles, but the first release need not implement multiple fusion algorithms.
- Global coverage is provided directly by GBBEPx ingestion; online derivation outside ABI coverage depends on an explicitly documented polar/climatology policy, not synthetic global ABI observations.
- Product versions differ in species availability and grid. The NOAA GBBEPx page lists eight bulk emitted species; it does not establish support for all eleven requested here.
- Bulk VOC mass alone does not determine mechanism-resolved emissions. Scientifically sourced fire composition profiles are an external prerequisite.
- Observation cadence and delivery latency depend on upstream services. Initial scope reports age and enforces cutoffs but does not promise a fixed near-real-time service latency.
- Source access, redistribution rights, scientific tables, ancillary datasets, representative granules, and mechanism definitions must be obtained and versioned before their associated production profiles are accepted.
- Surface emissions and existing vertical allocation are in scope; new plume rise, fire spread, burned-area prediction, inverse estimation, data-service hosting, and operational forecasting infrastructure are not.
- The project constitution currently contains only template placeholders and imposes no ratified scientific principles. Existing repository conventions govern implementation until a constitution is adopted.

## Scientific References

- [NOAA GBBEPx product overview](https://www.ospo.noaa.gov/products/land/gbbepx/): global product grids, species, and ancillary fields; the overview contains differing sensor descriptions, so the exact version's files and documentation remain authoritative.
- [NOAA RAVE External User Manual](https://www.ospo.noaa.gov/resources/documents/pdf/RAVE_EUM.pdf): supplied user reference; the plan must pin a revision and verify algorithms and metadata against representative files.
- [RAVE V2.0 scientific dataset description](https://zenodo.org/records/17362980): eleven hourly species, ABI/VIIRS fusion, scaled/unscaled variants, ancillary coverage, and primary scientific citations.
- [Li et al. (2022), NOAA repository record](https://repository.library.noaa.gov/view/noaa/67902): hourly blended geostationary/polar fire-emissions method, Remote Sensing of Environment 281, 113237.
