# Per-scheme UFS CCPP flux interfaces

Style reference: [CATChem PR #67](https://github.com/bbakernoaa/CATChem/pull/67),
head d57c66a. Compatibility revisions are recorded in
`tests/data/interfaces/compatibility-lock.json`; the framework matches that PR.

Caps live in `src/driver/ccpp` and expose `_driver_register`, `_init`, `_run`,
and `_final`. Each metadata file has one table-properties block and corresponding
Fortran argument-table/htmlinclude links. Caps use the host `machine::kind_phys`,
explicit scientific arguments and `errmsg/errflg`; no exception crosses the C ABI.

Current caps are the native demonstration, MEGAN3 and BDSNP YL95. Each runs only
its selected scheme. MEGAN3 takes explicit effective histories, soil-NO flux and
existing speciation file paths. Its 36-species CB6 output uses the versioned index
manifest `tests/data/interfaces/megan3-cb6-species-v1.json`. Count and ordered names
are checked at runtime; an incompatible mechanism fails before host publication.
Other mechanisms require separately validated contracts. Full effective-input
BDSNP is available to Python but does not yet have a reviewed CCPP cap.

CECE returns emission mass fluxes in kg species m-2 s-1. It does not advance host
tracer concentrations, register duplicate tracers, or apply a tendency implicitly.
A host may map fluxes into its constituent registry and apply its own explicit
mass/basis/geometry conversion. The example kernel has arbitrary caller units.

Keep schemes in dedicated SDF groups at explicit host coupling points. The test
suite has separate native and production groups; production orders BDSNP before
MEGAN3. Do not route all physics through a monolithic CECE run when a suite selected
one scheme, and do not duplicate prescribed-surface-forcing scheme variants.

## Lifecycle and failure rules

The reviewed production kernels are stateless given their explicit effective inputs.
The CCPP adapter therefore creates a call-local execution context, copies inputs,
runs the same `PhysicsScheme` implementation as Python, stages outputs, and destroys
the context. No hidden block history or global column indexing survives the call.
This favors correctness for interleaved/unequal blocks over allocation performance.
Future evolving schemes need explicit host-owned restart transfer before support.

Opaque C ABI handles are monotonically allocated and never reused; destroyed handles
are rejected. C ABI calls are serialized for safe registry/runtime ownership. There
is no claim of concurrent native CCPP execution. Internally, Kokkos kernels retain
normal parallelism. Initial interop uses double-precision host buffers and documented
column/axis Fortran order; device-pointer interop is not implemented.

No output buffer is changed on validation or computation failure within a scheme.
Earlier successfully completed schemes in a suite group are not rolled back.
Individual scheme `_final` hooks do not finalize Kokkos: the application owns runtime
shutdown. `cece_scheme_finalize_runtime()` is an explicit application-exit operation,
requires no active contexts and does not finalize a host-initialized runtime.

## Build options

```sh
cmake -S . -B build \
  -DCECE_BUILD_CCPP=ON \
  -DCECE_CCPP_HOST_MODULE_DIR=/path/to/ufs/fortran/modules
```

Alternatively set `CECE_CCPP_HOST_TARGET` to an existing CMake target exporting host
modules/libraries. The CCPP library links the native core, not `cece_driver` or its
AMIO/AXIS/DAGR/ESMF ingestion dependencies. Current top-level configuration still
resolves the repository development dependencies.

For generated-suite testing in Docker:

```sh
cmake -S . -B build \
  -DBUILD_PYTHON_BINDINGS=ON \
  -DCECE_BUILD_CCPP=ON \
  -DCECE_CCPP_TEST_STUBS=ON \
  -DCECE_CCPP_FRAMEWORK_DIR=/path/to/pinned/ccpp/framework
cmake --build build --target test_cece_ccpp_suite -j2
ctest --test-dir build -R ccpp_generated_suite --output-on-failure
```

`CECE_CCPP_TEST_STUBS` supplies only the test host's `machine` precision module;
it does not substitute scheme science or the CCPP framework. The fixture uses the
actual pinned validator and cap generator, compiles generated caps and executes a
suite. `xmllint` is required. Cross-interface pytest compares all 36 MEGAN3 fluxes
and soil NO with Python at relative tolerance 2e-12 and absolute tolerance 1e-27.

This establishes generated-framework compatibility, not a completed UFS system
integration or real atmospheric SCM case certification. Those require the selected
host's field mapping, forcing and full toolchain verification.
