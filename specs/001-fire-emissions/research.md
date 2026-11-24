# Research: NOAA/UFS Fire Emissions in CECE

**Date**: 2026-11-19
**Scope**: Repository interfaces and public primary sources. No production coefficients or runtime code installed.

## 1. Scientific Baseline

**Decision**: Implement one versioned unscaled derivation baseline, `rave-atbd-2.1-unscaled`, limited to documented applicability; later profiles require separate validation.

**Rationale**: [NOAA RAVE ATBD](https://www.ospo.noaa.gov/resources/documents/pdf/RAVE_ATBD.pdf) v2.1, sections 2.3.1-2.3.2, describes fusion, diurnal reconstruction and conversion. The [EUM](https://www.ospo.noaa.gov/resources/documents/pdf/RAVE_EUM.pdf) v2.2 (August 2024) describes interfaces. [Zenodo V2.0](https://zenodo.org/records/17362980) is a dataset release, not evidence that these revisions are identical.

**Alternatives considered**: Calling the implementation simply "RAVE" hides differences. Claiming GBBEPx algorithm parity is unjustified without full coefficient/variable documentation.

**Release gate**: Pin document digests, compatible ancillary assets, sample metadata and independent reference outputs. Verify transcribed formulas/tables before production activation.

## 2. Fusion and Diurnal Reconstruction

**Decision**: Preparation normalizes sensor inputs without performing the new science. CECE performs view-overlap selection, required corrections, temporal reconciliation, diurnal reconstruction and integration.

**Rationale**: ATBD section 2.3.1 describes parallax correction, ABI overlap handling using viewing geometry, aggregation to a 0.03-degree grid, VIIRS aggregation/cloud treatment and temporal combination. Section 2.3.1.3 describes land-cover/ecoregion cycles and short/long gaps. Summing all sensor observations is not equivalent.

**Alternatives considered**: Additive fusion double counts; arbitrary averaging and model-grid-first regridding can change energy/nonlinear corrections. Performing all science in Python does not meet the request for CECE-derived emissions.

**Release gate**: Obtain matching climatology and exact overlap/high-latitude rules. Research found descriptive high-latitude redistribution without a complete weighting equation; do not invent one. Pin geometry and temporal rules in reference fixtures.

## 3. Dimensional Chain and Coefficients

**Decision**: Canonical power MW, time seconds, energy MJ, dry matter kg, species interval mass kg; explicitly convert g/kg emission factors.

**Rationale**: ATBD section 2.3.2, equations 5-7, supports:

```text
FRE_MJ = integral(FRP_MW dt_seconds)
dry_matter_kg = FRE_MJ * combustion_coefficient_kg_per_MJ
species_mass_kg = dry_matter_kg * emission_factor_kg_per_kg
mass_flux_kg_m2_s = species_mass_kg / (area_m2 * interval_seconds)
```

The ATBD reports 0.368 kg/MJ and species/biome factors in Table 2. These are cited baseline facts, not universal constants or a GBBEPx specification. Independently verify transcription, biome meanings and applicability. Early tests use explicitly synthetic coefficients.

**Alternatives considered**: Implicit factors of 1000, treating FRP as energy, or molecular-weight multiplication on mass fields produce incorrect emissions.

**Release gate**: Verify the full eleven-species factor table and cloud/scaling rules against the pinned revision; no guessed numerical defaults.

## 4. Product Mode and Availability

**Decision**: Select one product version/variant and normalize source quantities to extensive interval mass; associated FRP is diagnostic-only.

**Rationale**: [GBBEPx](https://www.ospo.noaa.gov/products/land/gbbepx/) lists daily emissions and eight named bulk species. Its M-band/I-band sensor wording conflicts and does not specify enough units/variables to implement a decoder. [RAVE's dataset description](https://zenodo.org/records/17362980) documents eleven hourly species, 0.03-degree products and scaled/unscaled variants; unscaled estimates are recommended for most applications. EUM Table 1-6 describes product variables, including emissions in kg, but pinned granule metadata remains authoritative.

**Alternatives considered**: Assuming all GBBEPx versions include eleven species, inventing names/units from display labels, or recalculating supplied emissions from FRP violates mode separation.

**Release gate**: Inspect actual granule metadata, time bounds, QA/fill rules and algorithm versions. Missing optional diagnostics stay absent.

## 5. Fire-Specific Chemical Mapping

**Decision**: Introduce a fire basis and small mapper; share mechanism-file parsing/validation only where dimensions and semantics match.

**Rationale**: Existing [speciation configuration](../../include/cece/physics/cece_speciation_config.hpp) defines MEGAN classes. The [engine](../../src/core/physics/cece_speciation_engine.cpp) uses those classes, molecular-weight multiplication and `MEGAN_` exports. [CRACMM2](../../data/speciation/map_cracmm.yaml) confirms the corrected name, but MEGAN factors do not define fire composition. The [GOCART2G header](../../include/cece/physics/cece_gocart2g.hpp) describes dust, not fire mapping.

**Alternatives considered**: Reusing MEGAN factors, renaming GOCART, or distributing VOC equally invents chemistry. A general speciation rewrite adds avoidable risk.

**Release gate**: For each target obtain exact species/version, fire VOC composition, NOx reference mass/partition, OC/OM and aerosol bin/mode factors as applicable, units and declared budgets/exclusions. Validate AM4 and GOCART2G independently.

## 6. Time, Missing Data and Near-Real-Time Operation

**Decision**: Half-open UTC bounds; explicit fixed or host-supplied per-interval cutoff, with an immutable schedule for prepared inputs; observation age measured as cutoff minus acquisition. Forward cutoffs are nondecreasing, and acquisition/availability must be at or before cutoff. Default error on unavailable required data. Retrospective and causal reconstruction are distinct; causal profiles must also forbid acquisitions after interval end and be independently verified before activation. No causal formula is inferred from the retrospective baseline.

**Rationale**: Bracketing reconstruction can use observations acquired after an interval. That is not inherently causal online skill. Local-solar-time cycles must not silently use the repository's civil-time offset service.

**Alternatives considered**: Unlimited persistence, zero under cloud, or unrestricted retrospective archives conceals missing data and forecast leakage.

**Release gate**: Cutoff fixtures and explicit observed/reconstructed/unavailable/valid-zero provenance, age and reconstruction diagnostics.

## 6a. Preparation and Component-Preserving Allocation

**Decision**: Raw-source selection is a separate manifest from prepared runtime inputs. Product time policy is explicitly uniform, source-resolved or versioned normalized-profile weighting, preserving finer source intervals and rejecting repeated diurnal weighting.

**Rationale**: Preparation must be possible before any normalized outputs exist. Temporal assumptions must be reproducible rather than inferred from model timestep.

**Decision**: Preserve component IDs, profile identities and component-resolved mass through spatially varying chemistry. Either map components before conservative target allocation or keep them separate through allocation until their original factors apply.

**Alternatives considered**: Collapsing mixed-biome mass before chemistry and choosing a target-cell dominant biome changes chemical budgets. Only verified invariant coefficients permit mapping after collapsed bulk remapping.

**Release gate**: Missing-source/preflight tests, finer-cadence preservation, duplicate-profile rejection, and mixed-biome fixtures with independent target-species budgets.

## 7. Repository Integration and Memory

**Decision**: Opt-in `fire` registration using [BasePhysicsScheme](../../include/cece/physics_scheme.hpp), bounded common pre-physics provider and existing import/export/diagnostic/vertical pathways. No I/O inside science kernels.

**Rationale**: [MEGAN3](../../src/core/physics/cece_megan3.cpp) demonstrates registration and required fields. [Ingestion](../../src/core/cece_data_ingestor.cpp) supports band-local data. [Vertical distribution](../../docs/vertical_distribution.md) already supports established allocation methods. Existing CMake uses explicit sources and `cece_add_gtest`.

**Alternatives considered**: A standalone-only subprocess does not establish coupled parity; replicated global histories defeat memory bounds.

**Release gate**: Lifecycle/advertisement tests, one/multirank parity, 12/24-interval memory scaling and actual Slurm allocation evidence.

## Research Completion and Asset Dependencies

Design choices are resolved; no user-scope clarification remains. Mandatory implementation inputs still to obtain:

1. Pinned NOAA documents, independently verified factors and matching ancillaries.
2. Actual supported GBBEPx/RAVE granules and metadata.
3. ABI/VIIRS quality/geometry/availability fixtures.
4. Fire composition and exact definitions for all five chemistry targets.
5. Reference outputs and per-variable tolerances approved before comparison.

Synthetic/interface implementation may proceed, but production support and NOAA operational-parity claims require these gates.
