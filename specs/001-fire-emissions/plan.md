# Implementation Plan: Online and Product-Based Fire Emissions

**Branch**: `develop` (actual Git branch, unchanged) | **Date**: 2026-11-19 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/001-fire-emissions/spec.md`

The setup helper reports feature identifier `001-fire-emissions` as `BRANCH`; Git confirms the actual branch remains `develop`. No branch hook is installed; this plan does not create or switch branches.

## Summary

Add one opt-in `fire` physics scheme with two exclusive input modes:

1. `product`: ingest versioned NOAA GBBEPx/RAVE emissions, preserve source interval mass, and bypass all FRP-to-mass calculations.
2. `derive_frp`: perform quality-aware ABI/VIIRS reconciliation, ecoregional diurnal reconstruction, FRE integration, dry-matter conversion, and eleven-species generation inside CECE.

Both feed fire-specific chemical mapping and existing stacking/vertical allocation. Keep network acquisition and file decoding outside physics kernels. Use existing import/export, diagnostic, Kokkos, CMake/CTest, and coupled-driver patterns; Python and NASA Earthaccess are not core runtime dependencies.

This delivery completes planning artifacts, not implementation. Scientific asset acquisition and per-target certification are explicit release gates, not silently filled defaults.

## Technical Context

**Language/Version**: C++20 core; existing Fortran/NUOPC coupling; Python >=3.10 optional preparation/testing tools.

**Primary Dependencies**: Existing Kokkos, ESMF, MPI, CONF, AMIO/NetCDF and logging. Optional xarray/h5netcdf for preparation, already represented by the cloud extra. Reuse conservative-remap machinery only after verifying extensive/intensive and interval semantics.

**Storage**: Versioned, CF-style NetCDF prepared inputs and immutable ancillary/profile files with content hashes. No new database or service.

**Testing**: Existing GTest/RapidCheck through CTest and pytest; offline synthetic fixtures first, separately enabled NOAA reference comparisons second.

**Target Platform**: Linux HPC, including Ursa Intel LLVM; standalone and UFS/NUOPC; Kokkos CPU baseline and supported accelerator builds.

**Project Type**: Scientific library/model-component extension and optional preparation CLI.

**Performance Goals**: Spec SC-001 through SC-008: conservative budgets at 1e-6 relative tolerance and bounded memory on 3600x1800 surface-grid cases. Record timings and absolute peak memory rather than invent a latency/RAM target.

**Constraints**: No network/file I/O inside `PhysicsScheme::Run`; no future-observation leakage or global-array replication per MPI rank; no double fire contribution, scaling, speciation, or particulate counting. Preserve fire-free behavior.

**Scale/Scope**: Eleven bulk species; five versioned target profiles; global GBBEPx and regional RAVE ingestion; ABI-footprint derivation and explicitly supported polar reconstruction. No new plume rise or forecast service.

## Constitution Check

*Gate: evaluated before research and again after design.*

The current [constitution](../../.specify/memory/constitution.md) is an unfilled template, not ratified governance. Its examples are not adopted principles.

| Gate | Before research | After design |
|------|-----------------|--------------|
| Existing lifecycle, typed config, logging, required-field checks | Pass: use existing mechanisms | Pass: one scheme and shared provider |
| Existing physics/speciation behavior | Pass: opt-in extension | Pass: separate fire basis/mapping and regression gates |
| Scientific provenance and dimensional correctness | Pass: require research | Pass as design; production profiles gated on verified assets |
| Offline/HPC behavior | Pass: separate acquisition | Pass: immutable inputs, local bounded windows, allocation diagnostics |
| No fabricated scientific support | Pass: unknowns explicit | Pass: version and five-profile acceptance matrix |

No constitutional violation or complexity exception is claimed.

## Project Structure

### Documentation (this feature)

```text
specs/001-fire-emissions/
|-- spec.md
|-- plan.md
|-- research.md
|-- data-model.md
|-- quickstart.md
|-- checklists/requirements.md
`-- contracts/
    |-- configuration.md
    `-- fire-inputs.md
```

`tasks.md` is deferred to `/speckit-tasks`.

### Source Code (repository root; proposed additions and integration points)

```text
include/cece/physics/cece_fire.hpp       # new scheme and typed science structures
src/core/physics/cece_fire.cpp           # fusion, conversion, fire mapping
include/cece/cece_fire_input.hpp         # new bounded input/provider contract
src/core/cece_fire_input.cpp             # shared pre-physics input lifecycle
src/core/cece_config_parser.cpp          # parse typed fire options
src/core/cece_config_validator.cpp       # cross-option and metadata validation
src/core/cece_core_initialize_p1.cpp     # lifecycle wiring as required
src/core/cece_core_initialize_p2.cpp
src/core/cece_core_run.cpp               # normalized input feed before physics
src/core/cece_core_finalize.cpp
src/python/config.py                    # matching public configuration
scripts/prepare_fire_inputs.py          # new decoding/preparation CLI
data/fire/                              # new ancillary/factor profile manifests
data/speciation/                        # new fire profiles, not MEGAN factors
examples/cece_config_fire_product.yaml   # new complete examples
examples/cece_config_fire_frp.yaml
scripts/ursa_fire_staged_run.slurm       # new explicit-resource offline example
tests/test_fire_*.cpp                   # new science/config/runtime tests
tests/test_prepare_fire_inputs.py
tests/data/fire/                        # small synthetic/permitted fixtures
CMakeLists.txt                          # explicit source registration
tests/CMakeLists.txt                    # cece_add_gtest registrations
docs/fire.md                            # new science/workflow guide
docs/configuration.md
docs/examples.md
docs/scripts.md
```

**Structure Decision**: Extend existing library organization. Start with one science implementation and one shared provider; split only when needed. Reuse `BasePhysicsScheme`, `PhysicsRegistration`, `RequireFields`, diagnostics/provenance, state views, and existing spatial/vertical infrastructure. Wire export registration and coupled import advertisement before initialization, not after the first step.

## Phase 0 - Research Decisions and Scientific Gates

See [research.md](./research.md) for evidence, alternatives, and limits.

1. Pin derivation profile `rave-atbd-2.1-unscaled` to NOAA RAVE ATBD v2.1, with document digest and independently transcribed ancillary/table inputs. EUM v2.2 describes an interface; Zenodo V2.0 is a separate data release. Do not equate these versions.
2. Product adapters pin actual sample metadata: GBBEPx file version, RAVE `PRODUCT_ALGORITHM_VERSION`, calendar, units, bounds, QA and scaled/unscaled field selection. No invented operational variable names.
3. Use a typed fire-species basis, not MEGAN's `EmissionClass`. Existing speciation uses MEGAN classes, amount fluxes, and a `MEGAN_` prefix; applying it directly to fire mass is incorrect.
4. Require verified fire composition and exact definitions for all five targets. AM4/GOCART2G are not aliases for an existing mapping. The GOCART2G header describes dust, not a fire-aerosol profile.
5. Fail explicit requests for uninstalled or uncertified profiles. Scientific asset gates are implementation prerequisites, not unresolved design choices.

## Phase 1 - Data Model and Interfaces

The [data model](./data-model.md), [configuration contract](./contracts/configuration.md), and [input contract](./contracts/fire-inputs.md) define:

- Half-open UTC intervals, sensor identity, QA/coverage, availability cutoff, bounded history, and scientific-profile identity.
- Shared normalized provider outside kernels, accepting prepared files or coupled imports with the same validated metadata.
- Separate raw `source_manifest` selection from normalized runtime `input_manifest`; preparation does not require an existing output. Product temporal allocation explicitly selects uniform, source-resolved or normalized-profile weights without flattening finer source intervals.
- Derivation inputs retaining sensor/time/geometry support on a profile-defined analysis grid. Fusion, nonlinear correction, reconstruction, and emission generation remain in CECE.
- Product interval masses plus optional diagnostic FRP. No conversion factors loaded or invoked.
- Pre-mapping bulk budgets, target outputs, and particulate categories that are not independent additive mass pools.
- Provenance and validity metadata retained through preparation and caching.

### Core Execution Order

1. Initialize: validate mode/profile/assets; create import/export descriptors and device tables; establish bounded window capacity.
2. Common pre-physics ingestion: read/accept normalized inputs, validate time/cutoff, deduplicate deliveries, enforce QA, and perform profile-required conservative aggregation on the analysis grid.
3. `FireScheme::Run`: derive activity/FRE/dry matter/species or accept product species. Apply scaling exactly once only when selected.
4. For spatially invariant chemistry, conservatively allocate extensive bulk masses then map chemistry. For varying chemistry, retain the inventory's explicit component IDs/profiles and mass dimension, map each source component then conservatively allocate and sum compatible extensive target quantities, or preserve separate components through allocation until their original factors apply. Never collapse components and substitute target-cell biome factors. Verify component sums and mapped physical budgets.
5. Divide by target area and interval seconds; convert to amount/number flux only with explicit factors; expose `FIRE_` fields.
6. Existing stacking and vertical allocation combine requested fields; diagnostics retain independent fire budgets.
7. Release/reuse completed temporal slots and tile buffers. Do not retain model history in RAM.

Do not regrid raw FRP to the model grid before nonlinear fusion when that changes the reference algorithm. Regional reference analyses use the documented 0.03-degree analysis grid; other footprints need a validated profile.

### Causal Versus Retrospective Reconstruction

Every interval has an explicit `fixed` or `host_supplied` cutoff. Prepared host-supplied runs use a unique interval schedule; coupled runs supply matching cutoff metadata before ingestion. Forward cutoffs are nondecreasing, no wall-clock default is permitted, and both acquisition and availability must precede cutoff. Observation age is cutoff minus acquisition time.

ATBD gap filling may require a later bracketing observation. Retrospective mode can use it only within cutoff/age/gap limits. Causal policies additionally forbid acquisitions after the modeled interval end and require an independently verified installed profile defining reconstruction/integration. The initial retrospective RAVE profile does not provide that certification. Unsupported causal requests fail, rather than invent a one-sided formula. Missing context may be retried during bounded acquisition but never silently commits an interval or advances model time. Default missing/stale behavior is error. See the [timing contract](./contracts/configuration.md#availability-cutoff-and-reconstruction).

## Delivery Sequence and Acceptance Gates

| Stage | Deliverable | Exit evidence |
|-------|-------------|---------------|
| 1 | Typed contracts, validators, synthetic fixtures, budget utilities | Reject invalid metadata, overlap, double scaling and missing profiles |
| 2 | GBBEPx/RAVE product adapters + one verified fire target | Preserve space/time mass; FRP cannot activate derivation |
| 3 | ABI/VIIRS preparation + CECE scientific pipeline | Dimensional tests and pinned RAVE comparisons |
| 4 | Five certified chemistry profiles | Fire mapping/conservation/exclusion tests per target |
| 5 | Coupled parity, offline and high-resolution workflows | SC-001 through SC-008, effective allocation and peak-memory evidence |

Stages 2 and 3 are independently useful. Full feature acceptance requires both modes and all five targets, not merely a working partial path.

## Validation Strategy

- Unit/property: duplicate permutation invariance, overlap reconciliation, QA versus zero, power/time/energy conversion, g/kg to kg/kg factors, normalized local-solar-time cycles, cloud/coverage thresholds, UTC/date-line/polar boundaries.
- Conservation: extensive remapping and partial-coverage budgets, interval subdivision/aggregation, spatially varying profiles, NOx mass basis, VOC carbon/mass declarations, PM/OC/BC non-additivity.
- Contracts: native/Python config parity, invalid schema, missing species, scaled products, already speciated products, future/stale inputs, cache mismatch, absent assets.
- Integration: shared files through standalone/coupled paths, non-fire stacking, vertical conservation, MPI decomposition equivalence and offline identity.
- Reference: per-variable manifests with algorithm/data versions, hashes, units, coverage differences and tolerances fixed before comparison. No large operational downloads in standard CI.
- Performance: measure 12/24-interval peak RSS and timings on the declared high-resolution grid, same window/tile capacity and profile set, fixed warm-up, no unbounded diagnostics history. Track per-rank and whole-job memory.
- Regression: new fire tests and affected config/speciation/stacking selectors first; then coupled parity and non-fire regressions. Escalate only for shared changes or failures.

See [quickstart.md](./quickstart.md) for proposed commands/evidence. This documentation-only delivery did not run unimplemented tests.

## Risks and Release Restrictions

- Real product metadata is required; the GBBEPx overview is not a decoder specification.
- Do not extrapolate CONUS coefficients/climatology globally without a validated profile.
- Published descriptions omit some implementation details; reference-package access may be necessary before operational-equivalence claims.
- VOC composition, NOx partitioning, AM4 definitions and aerosol conversion are scientific prerequisites, not arbitrary defaults.
- HPC access/policy and upstream latency vary; examples report actual allocations, not assumed partition internet access.
- The repository ignores `specs/` and `.specify/`. Artifacts are saved locally; no ignore rules, Git staging or commits are changed.

## Complexity Tracking

No exceptions. A fire mapper is justified by incompatible source bases/output conventions; a generic speciation-framework rewrite is unnecessary.
