# Tasks: Online and Product-Based Fire Emissions

**Input**: Design documents in `specs/001-fire-emissions/`: [spec.md](./spec.md), [plan.md](./plan.md), [research.md](./research.md), [data-model.md](./data-model.md), [contracts/](./contracts/), and [quickstart.md](./quickstart.md).

**Prerequisites**: Read the design documents before implementation. The constitution is an unfilled template, not ratified governance. No extension hooks are currently configured.

**Tests**: Included because the specification explicitly requires scientific fixtures, conservation, error-policy, offline, memory, and coupled/non-fire regression tests. Author test expectations before the corresponding implementation and establish failures without leaving the shared build unbuildable; missing compilation targets are not scientific test evidence.

**Organization**: Four independently testable user stories, in specification priority order. Foundation provides the shared typed provider and mapping machinery; story tasks add actual science/products/profiles. Checkboxes track implementation evidence; blocked requirements remain unchecked with their missing prerequisites recorded.

## Current Status (2026-11-23)

| Tasks | Checkbox status | Current scope |
|-------|-----------------|---------------|
| T001-T014 | 14 checked | Shared foundation and synthetic lifecycle/mapping contracts; not production science. |
| T015-T027 | 13 checked | Local RAVE v2r0/GBBEPx v4r0 product path and nonproduction examples; production chemistry acceptance remains gated. |
| T028-T043 | 16 open | FRP derivation; T028-T030 have partial tests/evidence, but sensor-to-emissions science and reconstruction are not implemented. |
| T044-T055 | 12 open | Five-target profile certification and both-mode validation. |
| T056-T066 | 11 open | Connected/offline, coupled and measured HPC acceptance. |
| T067-T071 | 5 open | Cross-story release documentation and acceptance. |
| T072-T082 | 11 open | Product/chemistry convergence, ancillary ingestion and host integration. |

Total: **27 checked, 55 open**. Checked counts report the recorded task
scope, not independent revalidation of every historical claim or production
readiness. Earlier checkpoint paragraphs below are historical snapshots; their
references to then-pending work must not override later task checkboxes.
The feature's specification and task files are now Git-trackable so this
product-focused branch can preserve the deferred derivation plan for a later
branch. They remain uncommitted until included in a Git commit.

**Branch scope**: Phases 1-3 and the product-specific profile work below are
the current product-mode focus. Phase 4 onward, including T028-T030 and raw
satellite-FRP derivation, stays in place as a deferred plan for a later branch;
this is logical separation, not a history rewrite or removal of implemented
work. Phase 5-7 tasks shared by product mode and derivation remain open until
their full stated scope is satisfied.

Most recent product verification: the `test_fire_speciation`,
`test_fire_products`, `test_fire_runtime`, and `test_fire_conservation` CTest
selection passed 68/68 cases in `build-fire-t024-session`. Both proposed Stage 1
standalone examples also ran against the pinned full RAVE and GBBEPx samples;
the six finite, nonnegative flux fields had the expected units and matched
separately generated native-product outputs times the proposed factors at
relative tolerance 1e-6 (absolute 1e-20). This is not a production chemistry,
CATChem host, independent NOAA or FRP-science acceptance.

Product checkpoint recheck (2026-11-23): 93/93 C++ config, product,
speciation, runtime, conservation and science cases and 91/91 Python config
and preparation cases passed. The CB6 generator `--check` passed. Full pinned
RAVE v2r0 and GBBEPx v4r0 samples were normalized and all six native CB6,
factor CB6 and proposed GOCART-2G Stage 1 standalone runs completed on the
72x46 grid. All output fields had expected dimensions, finite nonnegative
values and `kg m-2 s-1` units. For each product, the six Stage 1 fields and
26 CO-referenced CB6 fields matched native output times their factors; the
17 PM2.5-referenced fields matched their POC-relative factor ratios. This
last comparison is not an independent PM2.5 budget; comparisons used relative
tolerance 1e-6 and absolute tolerance 1e-20. The conservation selection also
exercised 24 RAVE hourly crops and the GBBEPx daily crop over two substeps.
Commands:

```sh
OMP_NUM_THREADS=2 OMP_PROC_BIND=false ctest --test-dir build-fire-t024-session --output-on-failure -R '^test_fire_(config|products|runtime|conservation|speciation|science)\.'
CECE_FIRE_PRODUCT_NORMALIZER=/workspaces/CECE/build-fire-t024-session/cece_fire_product_normalize PYTHONPATH=/workspaces/CECE/scripts python -m pytest -q tests/test_fire_config.py tests/test_prepare_fire_inputs.py
python scripts/generate_fire_cb6_examples.py --check
```

These runs use invariant factors and did not ingest VIIRS AST. The candidate
five-biome crosswalk passed synthetic class tests. The separate three-component
product splitter now treats class 17 as water: zero-mass cells receive no
component, and positive selected-species mass raises an error. Real aligned
ancillary ingestion and the remaining class policies still need T075/T076.
The T001-T027 checkpoint remains nonproduction product-mode evidence, not
completion of biome-dependent profiles, CATChem integration or chemistry
certification.

## Format: `[ID] [P?] [Story] Description`

- `[P]` marks disjoint-file work eligible to overlap only after its stated prerequisites. It never overrides dependencies.
- `[US1]` through `[US4]` identify the specification stories. Setup/foundation/final tasks have no story label.
- Paths are repository-relative. Proposed files must be created by their implementing task, not assumed to exist.
- `data-model.md` citations resolve to the Canonical Field Index and corresponding entity validation paragraphs. Constraints are inherited rather than copied into each task.
- Scientific-asset tasks must record source/version/digest/rights and verification evidence. If access or scientific details are unavailable, mark the task blocked and explain the missing input; never fabricate factors, silently skip certification or substitute biogenic profiles.

## Path Conventions

Follow the existing single-library layout: `include/cece/`, `src/core/`, `src/python/`, `scripts/`, `data/`, `tests/`, `examples/`, and `docs/`. Test registration uses existing `cece_add_gtest`, not new test tooling.

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Establish reproducible fixtures, build registration and scientific acceptance tracking without changing runtime behavior.

- [X] T001 Create synthetic fixture and scientific-asset manifest conventions in `tests/data/fire/fixture_schema.yaml` and `data/fire/asset_registry.yaml`, recording independent expected values, permitted redistribution, immutable source references, and a production-versus-synthetic distinction; enumerate required product, sensor, ancillary and five-target assets from `research.md`.
- [X] T002 [P] Create the shared fire test helper in `tests/support/cece_fire_test_fixtures.hpp` for small grids, interval budgets and configurable synthetic factors, using existing Kokkos/MPI harness patterns; do not encode production coefficients.
- [X] T003 After T001-T002, register the new fire source/test files as they are introduced in `CMakeLists.txt` and `tests/CMakeLists.txt`, maintaining a buildable scaffold and the `test_fire_config`, `test_fire_science`, `test_fire_speciation`, and `test_fire_runtime` selectors required by `quickstart.md`; verify nonempty CTest discovery.

Setup verification: restored pinned submodules and used the repository CI image
`jcsda/docker-gnu-openmpi-dev:1.9` after host configuration failed for missing
Kokkos. All four fire selectors built and discovered six fixture-only cases.
Those six and fifteen existing `KokkosConfigurationTest` cases passed together
(21/21). Fixture JSON Schema, five negative schema cases and registry SHA256 were
also checked. This is setup evidence, not the T014 foundation checkpoint or
scientific/NOAA certification. No new runtime source existed at this checkpoint.

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Shared contracts and lifecycle required before story implementation. Production science assets are story-specific gates, not a reason to block all synthetic infrastructure work.

- [X] T004 Implement typed immutable identity/time metadata in `include/cece/cece_fire_input.hpp` and `src/core/cece_fire_input.cpp`, citing `data-model.md`: `CommonIdentity.schema_version`, `CommonIdentity.source_id`, `CommonIdentity.product_version`, `CommonIdentity.algorithm_version`, `CommonIdentity.delivery_id`, `CommonIdentity.content_sha256`, `CommonIdentity.interval_start`, `CommonIdentity.interval_end`, `CommonIdentity.acquired_at`, `CommonIdentity.available_at`, `CommonIdentity.availability_cutoff`, and `CommonIdentity.cutoff_mode`.
- [X] T005 Extend the shared input types and validation in `include/cece/cece_fire_input.hpp` and `src/core/cece_fire_input.cpp` for grids and component-resolved bulk inventories, citing `data-model.md`: `CommonIdentity.grid_id`, `CommonIdentity.grid_sha256`, `CommonIdentity.grid_definition`, `FireBulkInventory.species`, `FireBulkInventory.mass_kg`, `FireBulkInventory.component_ids`, `FireBulkInventory.component_profiles`, `FireBulkInventory.interval_grid_coverage_provenance`, `FireBulkInventory.nox_reference_basis`, `FireBulkInventory.voc_profile`, `FireBulkInventory.particulate_overlap`, and `FireBulkInventory.optional_energy_dry_matter`.
- [X] T006 Add mode/provider/profile/scaling options, separate raw `source_manifest` and prepared `input_manifest`, product temporal allocation/profile options, fixed/host-supplied cutoff and schedule options, and reconstruction gap/age settings to `include/cece/cece_config.hpp`, `src/core/cece_config_parser.cpp`, and `src/core/cece_config_validator.cpp` according to `contracts/configuration.md`; reject unknown options, duplicate overlapping fire contributions, incompatible manifests/time policies and inconsistent already-speciated/product/derivation selections before time integration.
- [X] T007 [P] After T006's configuration contract is fixed, mirror its public representation and validation in `src/python/config.py` and `scripts/validate_cece_config.py`; preserve existing field/path/localization conventions and fire-free configuration behavior.
- [X] T008 [P] After T006's contract is fixed, add native configuration and identity validation tests in `tests/test_fire_config.cpp` covering both modes, source/output manifest separation, temporal-policy combinations, fixed/host-supplied cutoffs and schedules, required assets, unknown options, grid/time/unit errors, missing fields, and conflicting contributions; compare native/Python acceptance using fixture cases in `tests/data/fire/config_cases.yaml`.
- [X] T009 Implement a bounded prepared/coupled provider interface, ready/rejected/consumed/evicted transitions and source-hash validation in `include/cece/cece_fire_input.hpp` and `src/core/cece_fire_input.cpp`, citing `data-model.md`: `IntervalLifecycle.state`, `IntervalLifecycle.rejection_reason`, `IntervalLifecycle.cache_identity`, and `IntervalLifecycle.revision_identity`; actual product/sensor decoders are added in story phases.
- [X] T010 Implement interval-overlap allocation, conservative extensive-versus-density remap adapters and unit-tagged component/total budget helpers in `src/core/cece_fire_input.cpp`, reusing existing CECE regridding/time machinery and preserving component identity until its factors apply; add synthetic partial-coverage, UTC-boundary, date-line, polar and mixed-composition allocation cases in `tests/test_fire_conservation.cpp` with SC-002 tolerances.
- [X] T011 Define and load typed fire target profiles in `include/cece/physics/cece_fire.hpp` and `src/core/physics/cece_fire.cpp`, citing `data-model.md`: `FireTargetProfile.identity`, `FireTargetProfile.asset_hashes`, `FireTargetProfile.typed_mappings`, `FireTargetProfile.conversion_factors`, `FireTargetProfile.nox_partition`, `FireTargetProfile.oc_om_policy`, `FireTargetProfile.source_disposition`, and `FireTargetProfile.budgets_tolerances`; reuse only dimensionally compatible existing parsing, not MEGAN classes/factors.
- [X] T012 Implement the shared typed fire-mapping kernel and register opt-in `FireScheme` in `src/core/physics/cece_fire.cpp`, producing isolated `FIRE_` fields from bulk inventories or bypassing mapping for exact-target inputs; exercise a clearly synthetic target in `tests/test_fire_speciation.cpp`, with real production target assets deferred to story gates.
- [X] T013 Wire prepared/coupled provider initialization, pre-physics field population, export descriptors and cleanup into `src/core/cece_core_initialize_p1.cpp`, `src/core/cece_core_initialize_p2.cpp`, `src/core/cece_core_run.cpp`, and `src/core/cece_core_finalize.cpp`, using `src/core/cece_core_field_helpers.cpp` and existing driver/cap registration surfaces as needed; keep I/O outside `FireScheme::Run` and preserve opt-out behavior.
- [X] T014 Add lifecycle/error-propagation and fire-disabled regression cases in `tests/test_fire_runtime.cpp`; run T008/T010/T012 cases together, assert no empty test selections, and record the passing foundation checkpoint in `tests/data/fire/foundation_acceptance.yaml` before user-story runtime work.

**Checkpoint**: Foundation is buildable and validated with synthetic inputs/targets. It does not claim GBBEPx/RAVE scientific decoding or any certified production chemistry target.

Partial foundation verification (T004-T005 only): identity contract tests first
failed to link before implementation; after implementation, all four fire
selectors discover 18 cases. Those 18 and 36 existing clock/Kokkos cases pass
together (54/54) in the repository CI image. Host GCC also accepts the new input
source with `-std=c++20 -Wall -Wextra -Wpedantic -Werror -fsyntax-only`.
Formatting and documentation validation pass. Grid support is explicitly
geographic quadrilaterals; actual content hashing, profile certification,
providers, kernels and lifecycle wiring remain pending.
This is not T014 acceptance.

Configuration verification (T006-T008): native parser and validator share typed
fire option validation; Python preserves its existing options dictionary and the
CLI reuses that validation without bypassing the runtime-registration gate.
The red native acceptance test failed before implementation. The final CI-image
build passes 72/72 CTest entries covering all fire selectors, existing driver
configuration, Python configuration and CLI tests (including setup fixtures).
Native and Python tests independently exercise the same 68 acceptance cases;
both also reject manifest symlink aliases. Formatting, Python lint and related
documentation validation pass. Only declaration validation is complete:
asset/schedule contents and runtime metadata validation belong to the pending
provider/profile tasks.

Provider/allocation verification (T009-T010): the shared provider enforces fixed
slot/cell/component/metadata bounds, ready/rejected/consumed/evicted states,
full policy/profile/content cache identities, explicit run/revision identity and
restorable committed-end/cutoff watermarks. OpenSSL Crypto is a required system
build dependency; streaming file hashing and coupled-byte hashing agree with
known SHA256 vectors, reject changed same-path content, and do not reopen raw
archives during prepared staging. Provider contract tests were authored before
implementation; the initial build failed on the missing API (not scientific
reference evidence).

The allocation helper uses AXIS first-order density operators with explicit
DstArea/FracArea semantics and common physical/operator area validation. It
retains components, source hashes and chemical conventions; optional FRE and dry
matter retain separate unit-tagged budgets. Tests cover partial spatial/temporal
support, half-open UTC boundaries, real AXIS longitude/polar operators, mixed
component mapping-order equivalence and unavailable versus valid-zero states.
Final CI-image verification passes 142/142 CTest entries: all fire selectors,
existing driver/Python configuration and CLI, clock/Kokkos, band-boundary and
regrid-conservation regressions, including Python setup fixtures. C++ and CMake
format checks, diff hygiene and related documentation validation pass. Bounds
are logical per-window limits, not measured high-resolution RSS guarantees.
Actual decoder/host bindings, output/checkpoint atomicity, profile certification
and runtime integration remain pending; this is not T014 acceptance.
T011/T012 synthetic foundation verification: 37/37 fire config, profile/mapping
and runtime CTest selectors pass, including registered bulk mapping, mass-flux
export and exact-target bypass. This does not certify production assets or
complete CECE driver/provider wiring; it is not T014 acceptance.

T013/T014 foundation acceptance: registered a typed prepared/coupled runtime
adapter factory, initialized adapters after grid setup, prepared borrowed
inventories before `FireScheme::Run`, committed only after export synchronization,
aborted failed intervals, validated standalone/NUOPC fire output descriptors,
and finalized adapters with surfaced cleanup errors. The test-only adapter
verified successful commit, preparation/commit failures and fire-disabled
behavior. The combined fire/config CTest selection discovered and passed 59/59
cases with no empty selection; all 17 NUOPC coupling configuration tests also
passed directly, including fire export-key registration and missing-export
rejection. Details are recorded in
`tests/data/fire/foundation_acceptance.yaml`. The prepared-product adapter is
registered and consumes normalized product intervals; a generic coupled-source
adapter, FRP derivation science, production assets and Ursa execution remain
unavailable. Product contract tests and in-memory validation/normalization
(T015, T019, T020) are complete using synthetic identities. T016's
preflight/manifest contract tests are complete. T021 decoders and T022 local
product normalization are complete against pinned actual NOAA sample crops.
T018 factor semantics are verified against the pinned CB6r5-AE7-AQ source.
T023 is complete: the adapter loads and verifies temporal profiles, supports
UTC/local-solar allocation, conservatively remaps to standalone rectilinear
target grids, and preserves component profiles through composition-dependent
mapping. T024/T025 are complete for nonproduction examples: all four RAVE and
GBBEPx native/factor configurations passed preflight, normalization, standalone
execution and output-unit checks. The focused fire C++ suites pass 84/84 tests;
Python configuration and preparation tests pass 71 and 19 cases respectively.
T027 is complete as a nonproduction acceptance record at
`tests/data/fire/product_acceptance.yaml`. Overall US1/production acceptance
remains gated on permitted redistribution, independent gas/aerosol budgets
and deployed-mechanism compatibility.

Validation build retained at `build-fire-ci`. To avoid expensive unused dependency
instantiations on this cached build, configure with
`-U 'KokkosKernels_INST_*' -DKokkosKernels_ADD_DEFAULT_ETI=OFF`.
Fetched KokkosKernels also creates a legacy uppercase cache option that breaks
later reconfiguration; the verified cache-only workaround is
`-U KOKKOSKERNELS_ENABLE_SUPERNODAL_SPTRSV -DKokkosKernels_ENABLE_SUPERNODAL_SPTRSV=OFF`.
No dependency source or tracked build defaults were changed for these workarounds.

## Phase 3: User Story 1 - Use Existing NOAA Fire Products (Priority: P1) - MVP

**Goal**: Ingest global daily GBBEPx and regional hourly RAVE emissions without re-deriving them, preserving source mass and supplying one verified target.

**Independent Test**: Run permitted small product samples through a different target grid and multiple substeps, assert SC-001/SC-002 budgets, product version/variant identity, missing-species rejection, and no FRP conversion.

### Tests for User Story 1

- [X] T015 [P] [US1] Add product contract tests in `tests/test_fire_products.cpp` for daily/hourly interval handling, unavailable species, version/variant overlap, scaled data, target-specific bypass and diagnostic-only FRP; use small synthetic decoder metadata until real samples pass T017.
- [X] T016 [P] [US1] Add preparation CLI tests in `tests/test_prepare_fire_inputs.py` for raw-source selection, preflight before normalized outputs exist, source/output path collision, metadata/priority conflicts, temporal-profile compatibility, cutoff schedule completeness, exit codes, existing-manifest mismatch, atomic writes and overwrite behavior from both contracts; mock remote access and prohibit large test downloads.

### Implementation for User Story 1

- [x] T017 [P] [US1] ✅ **COMPLETE** — Acquired, verified, and pinned representative GBBEPx/RAVE samples with authoritative metadata. RAVE v2r0 hourly sample (2026-07-01, 13 km CONUS grid, all 11 species) and GBBEPx v4r0 daily sample (2023-02-24, 0.1° global, 8 species) stored in `data/fire/product_samples/` with SHA256 hashes. Complete manifests in `product_samples_manifest.yaml` document all 41 RAVE and 26 GBBEPx variables, dimensions, dtypes, and diagnostics. See `T017_T018_COMPLETION.md` for verification details.
- [X] T018 [P] [US1] **COMPLETE (factor verification)** — The user confirmed the coefficients are verified CB6 speciation factors referenced to CO and PM2.5. All 43 rows match the immutable AQMv8_p2 `aqm.rc` excerpt at commit `130beee95c0bef00a4cc8f24177e4d4db6787cda` (source SHA256 recorded in `cb6_fire_factors_manifest.yaml`); the pinned configuration identifies `cb6r5_ae7_aq`. Preserve coefficients without normalization. Production activation remains separately gated on redistribution rights and independent CB6r5-AE7-AQ budget tests; see the asset registry and factor manifest.
- [X] T019 [US1] Implement product interval decoding/validation types in `include/cece/cece_fire_input.hpp` and `src/core/cece_fire_input.cpp`, citing `data-model.md`: `ProductEmissionsInterval.product_identity`, `ProductEmissionsInterval.interval_grid`, `ProductEmissionsInterval.species_availability`, `ProductEmissionsInterval.quantity_semantics`, `ProductEmissionsInterval.mass_kg`, and `ProductEmissionsInterval.quality_coverage`.
- [X] T020 [US1] Implement optional diagnostics and conversion-bypass/scaling guards in `src/core/cece_fire_input.cpp` and `src/core/physics/cece_fire.cpp`, citing `data-model.md`: `ProductEmissionsInterval.optional_diagnostics`, `ProductEmissionsInterval.chemistry_state`, `ProductEmissionsInterval.target_identity`, and `ProductEmissionsInterval.scaling_state`; prove with T015 spies/counters that supplied FRP cannot activate dry-matter/species generation.
- [X] T021 [US1] **COMPLETE** — RAVE v2r0 and GBBEPx v4r0 NetCDF decoders validate explicit cutoffs, product metadata, units, grid geometry and interval masses in `src/core/cece_fire_product_decoder.cpp`. Tests use pinned actual-sample crops, including 24 consecutive RAVE hours, stable geometry/hash checks and independently summed species totals; all 15 `test_fire_products` cases pass. T025 model-substep allocation/conservation remains a separate downstream gate.
- [X] T022 [US1] Implement local bulk RAVE v2r0 and GBBEPx v4r0 normalization in `scripts/prepare_fire_inputs.py` and `src/core/cece_fire_product_writer.cpp`; write checksummed CF-style interval files and commit the manifest only after all selected intervals validate. Failure tests prove partial output cleanup and preservation of prior manifests/files. Remote acquisition and target-specific inputs remain unsupported.
- [X] T023 [US1] Complete prepared-product runtime wiring, versioned temporal-profile file loading, UTC/local-solar allocation, and composition-dependent mapping. Preserve finer source intervals, component identities when supplied, coverage, and conservative mass budgets; require composition identities only for composition-specific mappings. Invariant Phase 3 product mappings accept explicitly unprofiled components; FRP-derived inputs still require biome/composition identities. Synthetic adapter/allocator/mapping tests cover the execution path. Do not enable CB6 production mapping until redistribution rights and independent CB6r5-AE7-AQ budgets are verified.
- [X] T024 [US1] **COMPLETE (nonproduction examples)** — `scripts/generate_fire_cb6_examples.py` generates the hash-pinned 43-row factor profile and fire-specific target definitions from the verified factor manifest, plus direct-native and factor-mode configs/source manifests for hourly RAVE and daily GBBEPx. All four configs passed manifest-level preflight and normalization, and all four standalone runs wrote finite `kg m-2 s-1` outputs. Product mode does not require component identities for invariant mappings. Keep production eligibility false; rights and independent budgets remain production gates, not prerequisites for these demos.
- [X] T025 [US1] Added actual-sample allocation/conservation tests for 24 hourly RAVE intervals and the GBBEPx daily interval, each split into two substeps with alternating target geometry, and retained scaled/unscaled and missing-species decoder checks. The focused config, speciation, product, runtime and conservation C++ suites pass (84/84), including actual-sample SC-002 budgets.
- [X] T026 [P] Documented supported product revisions, units, species limits, CB6 factor provenance and non-normalized sums, RAVE scaling variants, output/vertical allocation semantics, and no-FRP-conversion behavior in `docs/fire.md`.
- [X] T027 [US1] Acceptance record at `tests/data/fire/product_acceptance.yaml` records the four preflighted, normalized and executed nonproduction configurations and passing conservation tests. Overall US1/production acceptance remains pending permitted CB6 redistribution, independent gas/aerosol budgets and deployed-mechanism compatibility; the MVP and production CB6 support are not complete before those gates are resolved.

**Checkpoint**: Product decoders, preparation and prepared-runtime allocation are implemented. Phase 3 examples do not require biome/composition identities for invariant CO/PM25 factor mappings. End-to-end nonproduction examples and conservation are verified; production CB6 acceptance remains pending T027 gates.

## Phase 4: User Story 2 - Derive Emissions from Satellite FRP (Priority: P1)

**Goal**: Derive eleven-species fire mass inside CECE from ABI/VIIRS and versioned ecoregional diurnal data without duplicate energy or future-data leakage.

**Independent Test**: Known synthetic sensor scenes plus independently sourced NOAA reference scenes validate fusion, reconstruction, energy/dry-matter conversion and all eleven species. Use the foundation's synthetic target for isolated science tests; production target use additionally requires its verified profile.

### Tests for User Story 2

- [ ] T028 [P] [US2] Add science tests in `tests/test_fire_science.cpp` covering dimensional chains, independent expected eleven-species values, sensor-overlap permutation invariance, QA/zero distinctions, irregular sampling and cloud/geometry edge cases; use declared synthetic factors first.
- [ ] T029 [P] [US2] Add policy tests in `tests/test_fire_reconstruction.cpp` covering ecoregion/biome gaps, local-solar-time cycles, UTC boundaries, cutoff-minus-acquisition age, schedule/host disagreement, missing/backward cutoffs, late availability, retrospective brackets and causal post-interval acquisitions, unsupported causal profiles, gap/age limits and outside-ABI/polar coverage.

  **T028 status: partial, not complete.** The six science cases currently
  exercise synthetic dimensional arithmetic, product-flux normalization,
  an ATBD-based illustrative forest calculation, approximate swath geometry,
  and a standalone candidate VIIRS IGBP17-to-five-biome crosswalk. The
  arithmetic tests do not call an FRP derivation kernel; no sensor fusion,
  overlap-permutation, irregular-sampling or cloud-correction science is
  exercised. Remaining work: define independently calculated synthetic sensor
  scenarios and test the actual observation/fusion/FRE/eleven-species
  interfaces, including missing versus valid-zero and geometry/cloud edges.
  The selected RAVE v2.1 baseline and user-selected VIIRS AST substitution
  must retain separate provenance and a validated aggregation policy.

  **T029 status: partial, not complete.** The three reconstruction-file
  cases test shared identity/provider age, availability, UTC boundaries and
  monotonic cutoff behavior, not FRP reconstruction. Product-runtime tests
  separately cover some schedule disagreement/missing entries. Remaining work:
  exercise actual ecoregion/biome lookup, local-solar cycles, retrospective
  brackets, gap/age limits, missing/late context, unsupported causal policies
  and outside-ABI/polar behavior. No independently verified causal RAVE
  policy is installed.

  **Next steps and gates:** T028/T029 can advance using declared synthetic
  factors, independently calculated expectations and a synthetic policy;
  they do not need to wait for NOAA approval. Define the observation, profile
  and bounded-window contracts (T031-T035), then implement/test the
  sensor/reconstruction and conversion path (T036-T039) in dependency order.
  Production activation and NOAA comparison require T030: obtain compatible
  v2.1 FRP climatology/burn-duration and ancillary tables or verified exact
  reconstruction methods; validate VIIRS AST crosswalk, source-grid
  aggregation, EPA ecoregion/code alignment and time basis; resolve cloud,
  quality, calibration and high-latitude rules; obtain independent paired
  ABI/VIIRS scenes and expected outputs, rights, hashes and preapproved
  per-variable tolerances. The NOAA static package and independent paired
  scenes have not been obtained; no access request was submitted.

  Partial T028/T029: synthetic eleven-species flux normalization and
  valid-zero/unavailable handling exercise the product contract, and identity
  and provider tests check UTC boundaries, age, late availability, backward
  cutoffs and post-interval acquisitions. A prepared-product test checks
  schedule disagreement and missing interval entries. These are not FRP
  fusion, causal-policy or reconstruction tests; those interfaces and the
  remaining cases are still absent.
  T028 additionally has an ATBD-based, nonproduction forest arithmetic
  illustration of VIIRS precedence, six 10-minute slots, the 0.368 kg/MJ
  coefficient and all eleven transcribed mean factors. This only checks
  fixture consistency, not CECE derivation or verified deployed tables.
  GBBEPx V5 is a separate requested reference: accessible ATBD excerpts describe
  direct QFED-calibrated VIIRS FRP-to-emissions, not the RAVE FRE/dry-matter
  chain. The complete V5 document returned HTTP 403; equations, coefficients,
  sample and independent references remain unverified. See
  `data/fire/gbbepx_v5_atbd_manifest.yaml`; do not infer V5 from V4r0.
  The user-supplied NOAA NCEI annual VIIRS IGBP17 source is available: one
  2019 geographic file's full SHA256 and NetCDF metadata are recorded in that
  manifest. It is selected for T075/T076 evaluation, not a verified V5 biome crosswalk
  or a substitute for RAVE's MODIS/ecoregion/climatology assets. Its coordinates
  denote pixel upper-left corners, not centers; fill, aggregation and data
  availability policies still need verification. The supplied V5 PDF again
  returned HTTP 403. Existing T028/T029 cases pass, but the missing derivation
  and reconstruction coverage remains outstanding.
  A user-supplied four-page V5 methodology summary was inspected and hashed.
  It does not include the full V5 coefficient matrix; its six-species
  reference tables match the older V3.1 ATBD. It also labels class 0 as water,
  while the inspected VIIRS file declares 0 as fill, and its coefficient unit
  equivalence differs by a factor of 1000. See the V5 manifest for the
  provenance and review gates; do not activate or substitute these values.
  The user requested V3 information for missing V5 details. V3.1 supplies
  six continental coefficients but neither NH3/NOx nor separate biome
  coefficients, and its table does not establish the summary's numerical
  unit scale. Any V3-based substitute must be labeled a separate provisional
  proxy; it cannot complete the eleven-species RAVE tests or certify V5.
  The user selected a separate nonproduction runtime proxy using raw VIIRS
  and stated a V3 table scale of 10^-12 kg/J per table unit; independent
  verification of that scale is still pending. A real NOAA-20 v1r3 I-band
  fire granule and matching geolocation file were inspected and hashed in
  the V5 manifest. They contain no detected fires and do not provide
  independently verified cloud-covered-land area or full-pixel area rules.
  Raw decoding, safe gridding/QA and runtime integration remain unimplemented.
  An experimental spherical finite-difference swath-area helper has synthetic
  date-line/deletion/invalid-geometry tests. The science, reconstruction and
  config selectors passed 27/27 before the later crosswalk test was added.
  The helper is not an operational footprint, cloud/land mask, or runtime adapter.
  The supplied RAVE references were inspected and hashes recorded in the
  RAVE manifest. Li et al. 2022 describes five-minute reconstruction and eight
  species; ATBD v2.1 describes 10-minute reconstruction, different long-gap
  durations and contemporaneous pairing, and an eleven-row factors table with
  an eight-species caption. The 2024 EUM is v2.2. The supplied STAR index
  lists ABI fire detection ATBDs, not RAVE v1.0/v2.0 emissions ATBDs.
  Neither source establishes VIIRS IGBP17 as equivalent to the MODIS-based
  ancillary. The user selected ATBD v2.1 as the baseline. A reprocessed 2020
  daily RAVE v2r0 output was selectively retrieved from the Zenodo archive;
  its inner archive and NetCDF hashes and metadata are recorded in the
  reference manifest. One forest cell's eleven unscaled species are
  approximately consistent with the ATBD's FRE conversion. This exploratory
  comparison is not an approved acceptance test: the output is not paired
  with sensor scenes, and v2.1 compatibility has not been established.
  The user selected VIIRS AST IGBP17 for land cover, while its crosswalk
  and aggregation remain unvalidated. AST's surface-type climatology is
  not the FRP diurnal climatology and does not supply EPA ecoregions.
  The EPA North American Level I vector archive was downloaded, integrity-
  checked and hashed as a candidate in the RAVE manifest; it has 15 region
  codes plus water, not the 11 climatology regions described in the ATBD.
  Rasterization, code alignment, compatible FRP/burn-duration tables and
  deployed RAVE ancillary equivalence remain unresolved.
  A standalone candidate VIIRS IGBP17 five-biome crosswalk now has tests for
  all mapped classes and explicit rejection of fill, water, other unmapped
  classes and out-of-range values including 18 and 20. It accepts a numeric
  code, not dataset metadata, and does not by itself reject an IGBP20 file
  containing overlapping class codes 1-17. It is not connected to runtime or
  verified against a deployed ancillary. The user-proposed NOAA access route and
  Google science portal were checked; the latter requires sign-in, and no
  request or table download occurred. ATBD v2.1 uses 2-hour/1-hour gap
  buffers, while the supplied 1-hour/30-minute description is from the
  older paper. EUM low-quality labels do not establish training exclusions.

### Implementation for User Story 2

- [ ] T030 [P] [US2] Pin RAVE ATBD v2.1, compatible geometry/quality rules, land-cover/ecoregion/climatology assets and independent ABI/VIIRS reference scenes in `data/fire/rave_atbd_2_1_manifest.yaml` and `tests/data/fire/frp_reference_manifest.yaml`; record geographic applicability, rights, hashes, per-variable tolerances approved before comparison, and block undocumented high-latitude rules instead of inventing them.

  Partial T030 evidence: the NOAA OSPO ATBD v2.1 document was inspected and
  SHA256-pinned in `data/fire/rave_atbd_2_1_manifest.yaml`. Its cloud-threshold
  descriptions disagree and its Table 2 caption conflicts with the number of
  rows; independently verified algorithm/assets, scenes, redistribution rights
  and approved tolerances remain blocked. No production profile is installed.
  A CC-BY-4.0 Zenodo reprocessed RAVE output archive was identified; one
  nested v2r0 day was inspected and hashed as recorded in the reference
  manifest. The full archive was not independently hashed; compatibility with v2.1,
  paired sensor scenes and approved tolerances remain unverified.
- [ ] T031 [US2] Implement scientific profile loading/validation in `src/core/physics/cece_fire.cpp`, citing `data-model.md`: `ScientificProfile.identity_references`, `ScientificProfile.applicability`, `ScientificProfile.analysis_grid_cadence`, `ScientificProfile.overlap_quality_correction_rules`, and `ScientificProfile.biome_ecoregion_maps`.
- [ ] T032 [US2] Complete scientific table loading/device preparation in `src/core/physics/cece_fire.cpp`, citing `data-model.md`: `ScientificProfile.diurnal_cycles_time_basis`, `ScientificProfile.combustion_coefficient`, `ScientificProfile.species_factors`, `ScientificProfile.scaling`, and `ScientificProfile.reconstruction_limits`; production activation depends on T030's independent verification.
- [ ] T033 [US2] Define and validate observations in `include/cece/cece_fire_input.hpp` and `src/core/cece_fire_input.cpp`, citing `data-model.md`: `FireObservation.identity`, `FireObservation.sensor_platform`, `FireObservation.granule_pixel_key`, `FireObservation.footprint_geometry`, `FireObservation.parallax_status`, `FireObservation.frp_mw`, `FireObservation.quality_coverage`, `FireObservation.observation_status`, and `FireObservation.source_reference`.
- [ ] T034 [US2] Implement sensor decoding/preparation and contract tests in `scripts/prepare_fire_inputs.py` and `tests/test_prepare_fire_inputs.py`, using the separate raw `source_manifest` contract from T006 (reuse T021's parser if available, otherwise implement the shared parser here without adding a product-mode dependency); accept ABI/VIIRS 375 m data and retain footprint/composition/time/availability information and interval cutoff identities without fusion, nonlinear corrections or emissions generation.
- [ ] T035 [US2] Implement bounded sensor windows in `include/cece/cece_fire_input.hpp` and `src/core/cece_fire_input.cpp`, citing `data-model.md`: `NormalizedSensorWindow.profile_grid`, `NormalizedSensorWindow.interval_slots`, `NormalizedSensorWindow.sensor_power_time`, `NormalizedSensorWindow.coverage_quality`, `NormalizedSensorWindow.geometry_overlap`, and `NormalizedSensorWindow.availability`; honor profile context without global history replication.
- [ ] T036 [US2] After T030-T035, implement profile-defined parallax/overlap, viewing-angle selection, sensor quality/cloud corrections and ABI/VIIRS temporal reconciliation in `src/core/cece_fire_input.cpp` and `src/core/physics/cece_fire.cpp`; maintain algorithm ordering on the analysis grid, not arbitrary pre-regridded model cells.
- [ ] T037 [US2] Implement local-solar-time reconstruction and fixed/host-supplied cutoff validation in `src/core/physics/cece_fire.cpp` and `src/core/cece_fire_input.cpp` according to `contracts/configuration.md#availability-cutoff-and-reconstruction`; implement verified retrospective rules, reject uninstalled/unverified causal profiles and missing context, and make T029 pass without inventing one-sided/global formulas or implicit time advancement.
- [ ] T038 [US2] Implement integrated activity/status output in `include/cece/physics/cece_fire.hpp` and `src/core/physics/cece_fire.cpp`, citing `data-model.md`: `FireActivityInterval.interval_grid_profile`, `FireActivityInterval.power_support`, `FireActivityInterval.fre_mj`, `FireActivityInterval.coverage_contributions`, `FireActivityInterval.reconstruction_method`, `FireActivityInterval.oldest_source_age`, `FireActivityInterval.quality_state`, and `FireActivityInterval.reason_flags`.
- [ ] T039 [US2] Implement Kokkos FRE-to-dry-matter-to-eleven-species conversion and exactly-once optional scaling in `src/core/physics/cece_fire.cpp`, retaining T005's component IDs/profiles and mass dimension; apply varying chemistry per source component before conservative target allocation/summation, or keep components separate through allocation until their original factors apply; permit collapsed bulk allocation before mapping only for verified invariant coefficients and verify component/total physical budgets.
- [ ] T040 [US2] Add a complete derivation example with separate source/output manifests, pinned profile, explicit cutoff mode, observation-age limit and any required reconstruction gap/policy in `examples/cece_config_fire_frp.yaml`; add equivalent fixed and host-supplied prepared/coupled-normalized synthetic cases to `tests/test_fire_runtime.cpp`, with interval schedules or host metadata as appropriate.
- [ ] T041 [US2] Run 24-hour derivation cases and NOAA comparisons in `tests/test_fire_science.cpp` and `tests/test_fire_reconstruction.cpp`; require independent eleven-species relative errors <=1e-6 and source-specific preapproved NOAA tolerances, reporting differences without tuning expected values after the fact.
- [ ] T042 [US2] Document scientific equations, applicable coefficients/regions, correction order, causal limits and unsupported polar/global behavior in `docs/fire.md`, distinguishing algorithm revision, EUM revision and dataset release.
- [ ] T043 [US2] Record SC-001/SC-002/SC-003 and error-policy evidence with scientific asset hashes in `tests/data/fire/frp_acceptance.yaml`; run US2 independently of product adapters using synthetic normalized observations, then require T030 assets for production-science acceptance.

**Checkpoint**: CECE performs derivation, not Python preparation. Coverage/reconstruction and coefficient provenance are inspectable; absent scientific rules remain explicit blockers.

## Phase 5: User Story 3 - Supply Different Chemical Mechanisms (Priority: P2)

**Goal**: Certify five fire-specific chemistry targets for either input mode without reusing biogenic factors or double-counting particulate pools.

**Independent Test**: Feed a fixed bulk fire vector directly into the shared mapper; independently check each target's species/units, declared budgets/exclusions, NOx/VOC/aerosol factors and bypass behavior without remote data.

### Tests for User Story 3

- [ ] T044 [P] [US3] Extend `tests/test_fire_speciation.cpp` with independent five-target mapping expectations, mass/molar/carbon/number bases, molecular-weight/aerosol conversions, missing profile/species errors, source dispositions, double-speciation rejection and PM overlap; add a two-component mixed-biome fixture with different VOC factors proving component-preserving paths agree and collapsed-mass/target-biome substitution is rejected.
- [ ] T045 [P] [US3] Define fire-profile certification and evidence structure in `tests/data/fire/target_reference_manifest.yaml`, including exact target revisions, source citations, independently computed budgets and fixed comparison tolerances; synthetic profiles must be visibly distinct from scientific references.

### Implementation for User Story 3

- [ ] T046 [P] [US3] Obtain and verify the exact AM4 target definitions/fire factors in `data/speciation/fire_am4.yaml` and `data/fire/am4_profile_manifest.yaml`, including fire VOC composition, NOx basis, species dispositions and any aerosol mapping conventions.
- [ ] T047 [P] [US3] Obtain and verify exact CRACMM2 fire definitions/factors in `data/speciation/fire_cracmm2.yaml` and `data/fire/cracmm2_profile_manifest.yaml`, checking compatibility with existing CRACMM2 identity but not inheriting MEGAN factors.
- [ ] T048 [P] [US3] Obtain and verify exact RACM2 fire definitions/factors in `data/speciation/fire_racm2.yaml` and `data/fire/racm2_profile_manifest.yaml`, including independent composition/conversion budgets.
- [ ] T049 [P] [US3] Obtain and verify exact GOCART2G fire-aerosol definitions/factors in `data/speciation/fire_gocart2g.yaml` and `data/fire/gocart2g_profile_manifest.yaml`, explicitly separating these from existing GOCART biogenic maps and GOCART2G dust; identify non-aerosol source quantities as mapped, diagnostic-only or excluded with reason.
- [ ] T050 [US3] Certify the CB6 target in `data/speciation/fire_cb6.yaml` and `data/fire/cb6_profile_manifest.yaml`, reusing T018 if complete or performing its asset acquisition here otherwise; distinguish this independent vector-profile certification from US1 product integration.
- [ ] T051 [US3] Extend the shared mapper in `src/core/physics/cece_fire.cpp` only where certified target semantics require it, retaining all `FireTargetProfile` constraints cited by T011 and explicit target-specific bypass; reject missing/uncertified factors rather than using fallback profiles.
- [ ] T052 [US3] Wire target field names/units and dynamic export/stacking declarations through `src/core/cece_core_field_helpers.cpp`, `src/core/cece_core_initialize_p1.cpp`, and `src/core/cece_core_initialize_p2.cpp`, using existing driver/cap field advertisement surfaces as necessary; surface outputs before first-step binding without changing non-fire names.
- [ ] T053 [US3] After profile certification, add both-mode/five-target runtime coverage in `tests/test_fire_runtime.cpp` and the existing NUOPC field dictionary fixture `tests/nuopc_coupling/test_field_dictionary.yaml`, including default surface placement, configured vertical conservation and missing export failures.
- [ ] T054 [US3] Publish the exact support matrix, species/units, fire-profile references, VOC/NOx/aerosol conventions and explicit exclusions in `docs/fire.md` and `docs/configuration.md`; mark absent assets unsupported rather than complete.
- [ ] T055 [US3] Run T044 and both-mode checks after US1/US2 integration, recording all five target certifications and SC-004 evidence in `tests/data/fire/target_acceptance.yaml`; run existing speciation/MEGAN and stacking regressions to prove biogenic behavior is unchanged.

**Checkpoint**: Five targets are independently certified from bulk fixtures and exercised through both source modes; no partial set is described as full feature support.

## Phase 6: User Story 4 - Run Reproducibly on Connected and Offline Systems (Priority: P2)

**Goal**: Reproducible prepared/connected operation, common coupled/standalone science, bounded high-resolution memory and truthful HPC resource diagnostics.

**Independent Test**: Execute a 24-hour case offline and with identical accepted connected inputs, compare budgets and provenance; measure 12/24-interval memory scaling and actual Slurm allocation on the fixed high-resolution grid.

### Tests for User Story 4

- [ ] T056 [P] [US4] Add offline/acquisition tests in `tests/test_fire_offline.py` for network-disabled execution, identical accepted input sets, bounded retry/authentication errors, changed content under the same filename, out-of-order deliveries, partial writes and restart/replay without duplicate totals.
- [ ] T057 [P] [US4] Add common-lifecycle and coupled/standalone/MPI parity cases in `tests/test_fire_coupling.cpp` and `tests/nuopc_coupling/test_fire_coupling.sh`, checking scientific input identity and SC-008 budgets without relying on standalone-only subprocess ingestion.

### Implementation for User Story 4

- [ ] T058 [US4] Implement replayable streaming diagnostics/provenance in `src/core/cece_fire_input.cpp`, `src/core/cece_provenance.cpp`, and `src/core/cece_diagnostic_manager.cpp`, citing `data-model.md`: `FireRunProvenance.accepted_hashes`, `FireRunProvenance.cutoff`, `FireRunProvenance.coverage_support`, `FireRunProvenance.reconstruction_age`, `FireRunProvenance.rejected_observations`, `FireRunProvenance.remap_chemistry_budgets`, and `FireRunProvenance.performance_allocation`; emit no fabricated product-mode dry-matter diagnostics.
- [ ] T059 [US4] Add verified provider-specific connected acquisition to `scripts/prepare_fire_inputs.py`, using the source manifest identities and T017/T030 source contracts; enforce bounded retries, actionable network/auth errors, atomic commits and no network calls for prepared mode, without assuming NASA Earthaccess hosts NOAA assets.
- [ ] T060 [US4] Complete common refresh, per-interval cutoff enforcement and restart/cache/revision handling in `src/core/cece_fire_input.cpp` and `src/core/cece_core_run.cpp`, preserving T009 and the availability contract; reject missing/backward host cutoffs, enforce schedule agreement and age semantics, include source/time-policy/cutoff schedule hashes in cache identity, and test causal ingestion separately from historical reprocessing in `tests/test_fire_offline.py`.
- [ ] T061 [US4] Finish host field advertisement/binding and provider parity in `src/core/cece_core_initialize_p1.cpp`, `src/core/cece_core_initialize_p2.cpp`, and `tests/nuopc_coupling/test_fire_coupling.sh`; update existing cap/driver field metadata surfaces discovered during implementation rather than add a standalone-only workaround.
- [ ] T062 [US4] Implement tile/slot reuse and bounded diagnostic/output buffering in `src/core/cece_fire_input.cpp` and `src/core/physics/cece_fire.cpp`, using existing band-local infrastructure and preserving nonlinear-analysis-grid science; remove any introduced per-rank global history copies and validate one/multirank budgets.
- [ ] T063 [P] [US4] After T024/T040 define runnable configurations, create `scripts/ursa_fire_staged_run.slurm` with `cece_ursa.intelllvm`, `I_MPI_F90=ifx`, explicit resource requests and in-job allocation logging; separate connected preparation from compute execution, isolate optional Python after module loads and fail early on incompatible effective memory/CPU allocation.
- [ ] T064 [P] [US4] After T062, add `scripts/benchmark_fire_memory.sh` and `tests/data/fire/high_resolution_cases.yaml` for otherwise identical 3600x1800 12/24-interval runs, recording warm-up, profiles, ranks/threads, window sizes, timing, rank/job peak memory and allocation; assert the SC-006 ratio <=1.2 rather than assume 32G suffices.
- [ ] T065 [US4] Run T056/T057, 24-hour network-disabled comparisons and approved-host high-resolution runs from T063/T064, recording SC-005/SC-006/SC-008, raw measurement locations and platform evidence in `tests/data/fire/hpc_acceptance.yaml`; unavailable Ursa access blocks measured acceptance, not synthetic development.
- [ ] T066 [US4] Document connected/staged setup, causal cutoff/age policy, actual resource checks, provenance/replay, and interpreter/network/OOM troubleshooting in `docs/scripts.md` and `docs/fire.md`, using T065 measurements rather than inherited partition-access assumptions.

**Checkpoint**: Scientific outputs are reproducible across providers/coupling, offline runs issue no requests, and measured memory satisfies the specified scaling criterion on a documented platform.

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Complete cross-story acceptance, documentation and regression evidence without unrelated refactoring.

- [ ] T067 [P] After all story interfaces stabilize, reconcile implemented keys/commands and measured workflows with `specs/001-fire-emissions/contracts/configuration.md`, `specs/001-fire-emissions/contracts/fire-inputs.md`, and `specs/001-fire-emissions/quickstart.md`, removing design-only warnings only for verified implemented functionality.
- [ ] T068 [P] After T026/T042/T054/T066, link the fire guide and complete examples from `docs/examples.md`, `docs/configuration.md`, `docs/physics_scheme_development.md`, and `examples/README.md`; document supported/unsupported product/target versions and make no operational-parity claim beyond evidence.
- [ ] T069 Run the combined targeted native/Python fire selectors and affected config, speciation, non-fire physics, stacking, regridding, vertical and coupled parity regressions registered in `tests/CMakeLists.txt`; record discovery, commands and results in `tests/data/fire/regression_acceptance.yaml`, escalating to the full suite only for shared changes or targeted failures.
- [ ] T070 Re-run every executable scenario in `specs/001-fire-emissions/quickstart.md` against the pinned assets and compile final FR-001 through FR-019 and SC-001 through SC-008 evidence in `tests/data/fire/release_acceptance.yaml`; missing references, uncertified targets, unsupported causal science or absent HPC measurements remain explicit release blockers.
- [ ] T071 Review changed runtime/build/documentation surfaces against `specs/001-fire-emissions/plan.md`, remove only task-owned scratch inputs, verify no scientific assets/secrets are accidentally staged, and update final supported scope and remaining blockers in `docs/fire.md`; preserve the existing ignore rules for local planning artifacts unless the user separately requests versioning them.

## Dependencies & Execution Order

### Phase Dependencies

```text
Phase 1: T001 + T002 -> T003
                           |
Phase 2: T004 -> T005 -> T006 -> (T007 || T008)
                           |             |
                           +-> T009 -> T010 -> T011 -> T012 -> T013
                                                              |
                                                all foundation -> T014
                                                              |
                              +-------------------------------+------------------+
                              |                               |                  |
                         US1 product                     US2 derivation     US3 vector profiles
                              |                               |                  |
                              +-------------------------------+------------------+
                                                              |
                                       US3 both-mode checks + US4 integration
                                                              |
                                                Phase 7 release acceptance
```

The diagram expresses prerequisites, not permission to edit shared files concurrently. Core/provider/config/build files have one integration owner or serialized edits. Task-specific dependencies below take precedence over phase numbering when optional work is scheduled in parallel.

### Within-Story Dependencies

| Story | Required execution order |
|-------|--------------------------|
| US1 | T015/T016 author tests; T017 and T018 may proceed independently. T019 -> T020; T021 needs T016/T017; T022 needs T021; T023 needs T019/T020/T022; T024 needs T018/T023; T025 needs T015/T017/T024; T026 needs T018/T024; T027 needs T025/T026. T021 decoder acceptance does not wait on downstream T025. |
| US2 | T028/T029/T030 may begin after foundation. T031 -> T032; T033 -> T034; T035 needs T031-T034; T036 needs T030-T035; T037 needs T029/T036; T038 needs T037; T039 needs T032/T038; T040 needs T034/T039; T041 needs T028/T029/T030/T040; T042 needs T041; T043 needs T041/T042. |
| US3 | T044/T045 and T046-T049 may overlap after foundation in disjoint files. T050 reuses T018 or obtains the same verified assets, never duplicates acquisition. T051 needs T044-T050; T052 needs T051; T053 needs T052 and both source modes (T025/T041); T054 needs T053; T055 needs T045/T053/T054 and actual five-target reference assets. |
| US4 | T056/T057 may be authored against fixture providers after foundation. T058 -> T059 -> T060; T059 needs T017/T030 source contracts; T060 integration needs T025/T041. T061 needs T053/T057/T060; T062 needs T058/T061; T063 needs T024/T040; T064 needs T062; T065 needs T055-T064 completed; T066 needs T065. |
| Final | T067 needs all implemented story contracts; T068 needs story docs; T069 needs all story checkpoints; T070 needs T067-T069; T071 needs T070. |

### Scientific and External Prerequisites

- **Product support**: T017 blocks real GBBEPx/RAVE decoder acceptance, not synthetic contracts.
- **Initial production chemistry**: T018 factor semantics and the CB6r5-AE7-AQ configuration identity are verified, but production MVP remains gated by redistribution rights, independent CB6r5-AE7-AQ budgets and explicit source-composition metadata. US2 science and US3 vector tests can use visibly synthetic fixtures independently.
- **Derivation science**: T030 blocks real algorithm/profile activation and NOAA comparison; undocumented fusion details or incompatible ancillary assets cannot be guessed.
- **All targets**: T046-T050 block five-target certification. Profile verification includes independent factor/reference checks, not just parsing.
- **HPC execution**: T065 requires permitted execution access and effective memory allocation. Without it, report acceptance pending rather than invent results.

## Parallel Execution Examples

### User Story 1

After foundation, author T015 (`tests/test_fire_products.cpp`) and T016 (`tests/test_prepare_fire_inputs.py`) concurrently. T017 product-reference acquisition and T018 CB6 asset verification use disjoint manifests and can also overlap. Serialize provider/preparation implementation edits.

### User Story 2

After foundation, author T028 (`tests/test_fire_science.cpp`) and T029 (`tests/test_fire_reconstruction.cpp`) while T030 acquires documented scientific assets. Do not let tests manufacture authoritative coefficients or silently replace blocked references.

### User Story 3

After T045 establishes the reference evidence format, T046-T049 can verify AM4/CRACMM2/RACM2/GOCART2G assets in separate files. T044 test authoring is independent of these asset writes. One owner integrates kernel changes in T051.

### User Story 4

Author T056 (`tests/test_fire_offline.py`) and T057 (`tests/test_fire_coupling.cpp` and its shell runner) concurrently after foundation. Once their respective prerequisites hold, T063 Ursa script and T064 memory runner can overlap because their files differ; T065 awaits both.

## Requirement and Outcome Coverage

| Specification | Implementing tasks and acceptance |
|---------------|-----------------------------------|
| FR-001 | T006, T008, T020, T023; T025/T069 |
| FR-002 | T017, T019-T023; T025/T027 |
| FR-003 | T030-T035; T028/T034/T041 |
| FR-004 | T033, T036, T038; T028/T041 |
| FR-005 | T031-T032, T037; T029/T041 |
| FR-006 | T032, T038-T039; T028/T041/T043 |
| FR-007 | T006, T020, T032, T039; T015/T025/T044 |
| FR-008 | T005, T010, T023, T038-T039; T025/T041 |
| FR-009 | T006, T019-T021; T015/T025 |
| FR-010 | T011-T012, T018, T046-T052; T044/T055 |
| FR-011 | T005, T011-T012, T018, T046-T051; T044/T055 |
| FR-012 | T006, T009, T033, T035, T037, T060; T029/T056 |
| FR-013 | T004, T009, T035, T037, T058, T060; T028/T029/T056 |
| FR-014 | T021-T022, T059, T063; T016/T056/T065 |
| FR-015 | T020, T038-T039, T058; T041/T055/T065 |
| FR-016 | T013, T052-T053, T061; T057/T065 |
| FR-017 | T009, T035, T058, T062-T064; T065 |
| FR-018 | T024, T052-T053; T055/T069 |
| FR-019 | T006-T008, T012-T014, T023, T052; T055/T069 |
| SC-001 | T025, T041, T043 |
| SC-002 | T010, T025, T041 |
| SC-003 | T028, T030, T032, T039, T041 |
| SC-004 | T044-T055 |
| SC-005 | T056, T059-T060, T065 |
| SC-006 | T062-T065 |
| SC-007 | T008, T015-T016, T028-T029, T044, T056, T069 |
| SC-008 | T053, T055, T057, T061, T065, T069 |

## Implementation Strategy

### MVP First (User Story 1)

Complete setup/foundation and US1, including real sample metadata and verified CB6 fire factors. Demonstrate GBBEPx/RAVE product ingestion, missing-species safeguards, mass conservation, no redundant FRP science, surface/vertical allocation and provenance. Stop and validate before claiming an operational MVP.

### Incremental Delivery

1. Foundation: typed scientific contracts and passing synthetic machinery, no production claims.
2. US1: usable product-mode MVP with one verified target.
3. US2: CECE-derived sensor fusion/emissions, first synthetic then scientifically verified.
4. US3: certify all five targets and verify both source modes.
5. US4: complete connected/offline, coupled/MPI and measured HPC acceptance.
6. Final: reconcile docs/contracts and verify every requirement/outcome with persistent evidence.

### Parallel Team Strategy

Keep one owner for shared C++ provider/science/config/build edits. Delegate only self-contained tests, independent product/science asset verification or disjoint target profiles when appropriate. US1, US2 and US3 can be tested independently after foundation, but shared-file integration and full release gates are explicitly ordered above.

## Original Generation Validation (T001-T071)

- 71 tasks: setup 3, foundation 11, US1 13, US2 16, US3 12, US4 11, final 5.
- Tasks were originally generated unchecked and retain sequential IDs, story labels only where required, and explicit file paths; current completion/blocker status is recorded inline.
- Canonical field citations cover every indexed constrained field in `data-model.md`; scientific constraints are inherited from it, not replaced by task-local guesses.
- Every story has independent tests, implementation tasks, acceptance checkpoint, dependency order and concrete parallel examples.
- This file defines work; it does not assert that scientific assets exist, tests have passed, profiles are certified or HPC measurements have been obtained.

## Phase 8: Convergence

The user supplied `ufs-chem-rave-fire-emissions-spec.md` and
`ufs-chem-gbbepx-fire-emissions-spec.md` as proposed UFS-Chem/CATChem contracts.
The tasks below extend, rather than replace, T017-T027, T044-T055 and
T056-T071. The values in the tables below preserve the requested proposals;
they are **not independently verified production science**. Existing checked
tasks retain their original scope and evidence. In-progress local changes,
including the synthetic IGBP fixture and splitter, are not acceptance evidence.

- [ ] T072 [US1] Reconcile the attached RAVE and GBBEPx source contracts with pinned actual files and authoritative revision-specific documentation in `data/fire/product_samples_manifest.yaml`, `src/core/cece_fire_product_decoder.cpp` and `tests/test_fire_products.cpp`: validate names, units, bounds, intervals, grid geometry, availability, FRP semantics, NOx reference basis and land-cover presence. In particular, the implemented RAVE v2r0 decoder reads species as `kg` per interval, while the proposal describes `kg hr-1`; the pinned GBBEPx v4r0 sample has 1801 latitude points, not the proposed 1800. Never infer v005 support or identical `IGBP` variables from the proposal: obtain and pin representative permitted metadata/samples before implementing and testing any distinct V5 adapter. Preserve v2r0/v4r0 behavior and reject unsupported revisions and mismatched units explicitly per FR-002/FR-009 and attached source contracts (partial).
- [ ] T073 [US3] Extend the T044/T045/T049 target-profile work and `tests/test_fire_speciation.cpp` to represent the proposed UFS-Chem GOCART-2G Stage 1 mapping for both products: BC -> BCphobic 0.80 and BCphilic 0.20; OC -> BRCphobic 0.50 and BRCphilic 0.50; SO2 and NH3 -> their named targets at 1.00. Declare PM2.5/TPM overlap and each remaining source disposition, prohibit anthropogenic-OC routing and duplicate PM2.5 emissions, and test conservation and invalid profiles. Verify exact deployed tracer definitions, OC-to-BRC mass convention, factor sources, rights and independent budgets before production certification; the existing `data/fire/ufschem_gocart2g_species.yaml` is proposed, not certification per FR-010/FR-011 and attached Stage 1 contracts (missing).
- [ ] T074 [US3] Extend T044-T046/T051 with separate versioned RAVE-vs-GBBEPx AM4.1 + GOCART-2G fire profiles in `data/speciation/`, `data/fire/` and `tests/test_fire_speciation.cpp`, using the Stage 2 contract table below. RAVE VOC factors multiply native bulk VOC mass; GBBEPx ratios multiply CO mass as an explicitly selected target-specific empirical mapping, not invented bulk VOC observations or FRP-derived emissions. Map CO directly, map RAVE CH4 only when its target is enabled, and never fabricate GBBEPx CH4. Verify NOx is NO2-equivalent before using NO = 0.90 * 30.01 / 46.01 and NO2 = 0.10; test nitrogen-equivalent rather than total molecular mass conservation. Retain coefficients without normalization, document excluded/unrepresented VOC and PM pools, and require independent deployed-AM4.1 species/factor/basis, source, rights and budget verification before production enablement. Reject incompatible source/profile combinations per FR-002/FR-009/FR-010/FR-011 and attached Stage 2 contracts (missing).

  Partial T073 evidence: `data/fire/ufschem_gocart2g_stage1_profile.yaml`
  declares the six proposed mass targets, excludes PM25/TPM and all other
  unselected species, and hash-pins the proposed factors and target list. A
  focused test checks both source labels, expected split masses and rejection
  of a PM25 request. Both full-sample standalone configurations ran and were
  checked against native product fluxes as recorded above. The mapping is
  nonproduction; factor provenance, tracer meaning, OC/BRC basis, rights and
  independent deployed-mechanism budgets remain unverified. Full T073 invalid
  profile and host acceptance requirements remain open.

  T074 blocker: prepared product inventory components have neither verified
  composition identities nor a NOx basis. In addition, the prepared runtime
  puts the delivery/source-manifest identifier in the bulk identity and does
  not retain `product_id`, so a product-specific profile cannot safely enforce
  product compatibility at the mapper. The loader now permits direct and
  composition-specific mappings of one source only for independent
  coefficients and distinct target names; synthetic mixed-component tests
  verify direct and component-dependent budgets and reject unresolved
  compositions, shared target names and partition semantics.
  Preserve product identity through all inventory/allocation/cache paths, add
  and test a strict product/version guard, and complete verified source-grid
  biome/NOx metadata before enabling real Stage 2 runs. No AM4.1 runtime
  profiles are claimed complete.
- [ ] T075 [US1] Specify and complete the small `tests/data/fire/synthetic_igbp_map.yaml` fixture and its tests in `tests/test_fire_speciation.cpp` and `tests/test_prepare_fire_inputs.py`: retain its explicitly synthetic 2x2 GBBEPx-crop coordinates and class IDs 1, 10, 12 and 14, add a separate deterministic all-17-class case plus missing, invalid, water and misaligned-grid cases, and define schema/version, axis order, coordinate convention, grid identity and hash as needed by the actual loader. Validate mappings of classes 1-5 to forest, 12 and 14 to cropland, and supported 6-11 to grassland/savanna; independently establish the treatment of 13, 15 and 16 and reject or explicitly mask AST class 17 (water), rather than treating it as grassland. Check component sums and mixed-biome results before conservative allocation. The proposed missing-value fallback to class 10 must never silently turn unavailable or invalid data into observed land cover: specify an explicit, recorded policy and its applicability, default to error where required by existing FR-012, and test both zero-fire and active-fire cells. Reuse and validate the in-progress `SplitFireProductByIgbp` rather than duplicating it per plan: component-preserving chemistry and attached IGBP contracts (partial).
- [ ] T076 [US1] Design and implement bounded, provenance-checked ancillary ingestion/alignment in `scripts/prepare_fire_inputs.py`, `src/core/cece_fire_input.cpp` and the prepared input contract for GBBEPx and RAVE when source-grid biome identity is required. Evaluate the user-provided `https://noaa-oar-arl-nacc-pds.s3.amazonaws.com/inputs/geo-files/canopy_vegetation_type.0.01.nc` as a candidate, not an assumed IGBP class raster: inspect its actual variable schema, class convention, time, coordinates, fill values, license and provenance, and pin immutable identity/checksum and conversion/crosswalk only if independently verified. Its 2026-11-20 HTTP metadata reported 5,184,876,322 bytes; use a documented bounded crop/streaming or pre-staged input rather than a full download in tests. Define and test reproducible categorical 0.01-degree-to-product-grid aggregation, ties, coastline/water, missing coverage, longitude wrap and grid fingerprint alignment; if the dataset contains fractions or a non-IGBP classification, require a scientifically verified mapping rather than guessing. Preserve source-grid composition components and hash ancillary/crosswalk/policy in provenance and cache identity; do not infer biome from target cells. If suitable data or conversion is unavailable, block production use explicitly per FR-008/FR-010/FR-011 and attached ancillary contracts (partial).

  Partial T075/T076 evidence: the synthetic test checks all 17 existing
  assignments and conservation and rejects missing classes for active and
  valid-zero cells. Class 17 now represents water: synthetic tests check both
  zero-emission water and rejection of positive emissions over water, including
  mixed-species and mixed-biome mapping. The speciation, science, product and
  conservation CTest selection passed 52/52 cases. A bounded candidate NetCDF header
  describes 2020 VIIRS IGBP and is recorded in the ATBD manifest, but its data,
  coordinates, rights, full digest and compatibility remain unverified. No
  ancillary ingestion or verified aggregation has been implemented. The
  separately selected NCEI VIIRS AST IGBP17 2019 file has a verified full
  digest and inspected metadata in `gbbepx_v5_atbd_manifest.yaml`; its
  source-grid aggregation and deployed RAVE compatibility are still unverified.
  The existing three-component product splitter and the separate five-biome
  RAVE candidate crosswalk have different scopes and class policies; neither
  proves the other's scientific validity.
- [ ] T077 [US1] Extend the preparation/output and remap paths in `scripts/prepare_fire_inputs.py`, `src/core/cece_fire_product_writer.cpp` and the existing AXIS/ESMF adapters for actual FV3 regional tile7 and global C96/C384 grid definitions, using validated supergrid centers/corners, areas, orientation and masks. Map composition-specific masses on the source grid or preserve source components through remap; use first-order conservative remapping for chemical flux/mass with weights keyed by source/target geometry, masks and method. Reuse validated weights across hours and both chemistry stages and invalidate on changed identities. Produce the requested hourly RAVE and daily GBBEPx tile file names, target field units `kg m-2 s-1` and CF Gregorian time coordinates/bounds via a supported CECE/CATChem interface; test date line, poles, partial overlap, tile indexing and mass over the same covered domain. Do not add a mandatory xESMF dependency if existing operators satisfy the contract per FR-008/FR-016/SC-002 and attached spatial/output contracts (partial).
- [ ] T078 [US1] Specify and implement `FRP_MEAN`/`MeanFRP` diagnostic handoff in `src/core/cece_fire_product_decoder.cpp`, `src/core/cece_fire_product_writer.cpp`, the prepared/coupled runtime and host field metadata: validate source units/support, remap to target `frp` in MW using separately keyed nearest-source-to-destination weights, preserve masks/coverage, and expose it for an existing CATChem inline-plume-rise consumer without applying FRP-to-emissions conversion or introducing plume-rise physics. Verify that nearest-neighbor and source FRP support are appropriate for the deployed consumer, and explicitly reject unsupported or missing required FRP; test provenance, peak selection, missing data and no double conversion per FR-002/FR-015/FR-018 and attached FRP contracts (partial).
- [ ] T079 [US1] Extend the T023 versioned temporal-profile path and `tests/test_fire_conservation.cpp`/`tests/test_fire_runtime.cpp` with an explicitly selected GBBEPx local-solar Gaussian profile (proposed peak 14.0 hours, width 3.5 hours), with a documented periodic definition, interval integration and normalization per source UTC day and longitude. Prove arbitrary substeps and date-line/UTC boundaries recover each daily source-cell mass and cannot apply the profile twice; reject invalid widths, missing profile identity and use on already hourly RAVE. Treat the parameters as a proposed nonproduction profile until independently verified, retain existing uniform/source-resolved options and preserve SC-002 per FR-008 and attached GBBEPx diurnal contract (partial).
- [ ] T080 [US3] Integrate the two-stage file/host contract with existing CECE configuration, prepared/target-specific ingestion and NUOPC/CATChem export bindings in `src/core/cece_core_initialize_p1.cpp`, `src/core/cece_core_initialize_p2.cpp`, `src/core/cece_fire_prepared_runtime.cpp`, `examples/` and `tests/nuopc_coupling/`. Define one product-specific output containing both stage field sets, stable target identities and a supported Stage 1 subset versus Stage 2 selection; preserve Stage 1 fields and cached geometry when switching, prevent double speciation/scaling, and fail early on incompatible field dictionaries or missing exports. Translate the illustrative attached YAML into actually supported CECE/CATChem configuration rather than claiming its `emissions` syntax already works. Coordinate with T052/T053/T061 and keep non-fire configurations unchanged per FR-016/FR-018/FR-019 and attached two-stage contracts (missing).
- [ ] T081 [US1] After T072-T080 and the relevant T025/T044/T053/T057 prerequisites, add independently computed source-vector, mixed-biome, FV3 coverage and end-to-end acceptance fixtures in `tests/data/fire/` and registered fire tests. Check all proposed factors and source dispositions, RAVE 24 hourly intervals and GBBEPx daily allocation with at least two substeps, `kg m-2 s-1` fields, CF time/filename, FRP handoff, no PM double count or FRP conversion, and both stage bindings using identical product-specific files. For the same covered domain require RAVE per-species relative remap error <0.1% and GBBEPx CO/BC <0.5%, while also meeting the stricter SC-002 1e-6 fully covered test and 1e-12 zero-total criterion. Run both 24-hour UFS-Chem/CATChem stage smoke tests when the host and certified profiles are available; record exact commands, grid, input/profile hashes, units, budgets and outcomes, or mark each unavailable external prerequisite blocked without claiming acceptance per SC-001/SC-002/SC-004/SC-008 and attached acceptance criteria (missing).
- [ ] T082 [US3] Reconcile `docs/fire.md`, `docs/configuration.md`, `specs/001-fire-emissions/contracts/` and `specs/001-fire-emissions/quickstart.md` after implementation with the actual supported product revisions, two-stage fields, source-specific factors/bases, biome map generation and provenance, missing-data policy, diurnal normalization, FV3/FRP semantics and CATChem host requirements. Distinguish synthetic fixtures, proposed mappings and certified production profiles; document unresolved deviations from the attached examples and coordinate with T054/T067/T068 rather than claiming operational parity per FR-010/FR-011/FR-015 and attached contracts (missing).

### Proposed Stage 2 coefficient contract

Columns are forest, grassland/savanna, cropland. RAVE entries are kg target
species per kg bulk VOC; GBBEPx entries are kg target species per kg CO.
These are distinct source-dependent mappings, not interchangeable profiles.

| Target | RAVE forest | RAVE grass | RAVE crop | GBBEPx forest | GBBEPx grass | GBBEPx crop |
|--------|-------------|------------|-----------|---------------|--------------|-------------|
| CH2O | 0.155 | 0.130 | 0.140 | 0.0220 | 0.0140 | 0.0185 |
| ALD2 | 0.125 | 0.105 | 0.115 | 0.0175 | 0.0110 | 0.0150 |
| ACET | 0.080 | 0.070 | 0.065 | 0.0110 | 0.0075 | 0.0085 |
| MEK | 0.050 | 0.040 | 0.045 | 0.0070 | 0.0045 | 0.0060 |
| PRPE | 0.115 | 0.150 | 0.145 | 0.0160 | 0.0160 | 0.0190 |
| C2H6 | 0.060 | 0.075 | 0.070 | 0.0085 | 0.0080 | 0.0090 |
| C3H8 | 0.025 | 0.030 | 0.025 | 0.0035 | 0.0032 | 0.0033 |
| ALK4 | 0.035 | 0.045 | 0.045 | 0.0050 | 0.0048 | 0.0060 |
| ISOP | 0.020 | 0.010 | 0.010 | 0.0028 | 0.0011 | 0.0013 |
| BENZ | 0.040 | 0.050 | 0.045 | 0.0056 | 0.0053 | 0.0060 |
| TOLU | 0.035 | 0.035 | 0.035 | 0.0049 | 0.0037 | 0.0046 |
| XYLE | 0.025 | 0.025 | 0.025 | 0.0035 | 0.0027 | 0.0033 |
| HCOOH | 0.080 | 0.080 | 0.085 | 0.0112 | 0.0085 | 0.0112 |
| ACTA | 0.075 | 0.075 | 0.080 | 0.0105 | 0.0080 | 0.0105 |

Use the exact NO formula, not the different rounded approximations in the
attachments. The attached references to Akagi et al. (2011) and Andreae (2019)
do not by themselves establish that these particular rows, bases or mechanism
lumps are verified.

**Ordering and gates**: T072 precedes source-specific production support;
T075 precedes T076 integration; T073, T074 and T076 precede mapped-output
integration in T077/T080; T077-T079 precede T080/T081; T081 precedes final
certification and T082 documentation claims. Synthetic work can proceed
without downloading the large ancillary or granting production eligibility.
Existing T046/T049/T053/T055 and final release gates remain in force.
