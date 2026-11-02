# Interface compatibility baseline

Exact source revisions are recorded in tests/data/interfaces/compatibility-lock.json.
UFS target is the pinned modern develop revision, selected rather than the old public-v2 release because it uses the current UFSATM/CCPP capgen integration. This is a development compatibility target, not a release certification.

CCPP framework: NCAR/ccpp-framework at the UFSATM-pinned revision. Physics: ufs-community/ccpp-physics at the corresponding revision. Inspect these sources rather than assuming a standalone SCM version is interchangeable.

Container: build CECE's Dockerfile as cece/cece-dev; the tag is a local build name and is not available from public Docker Hub. Configure BUILD_PYTHON_BINDINGS=ON explicitly. The baseline CMake package-copy list omits earthaccess_resolver.py and stream_bridge.py even though __init__.py imports both.

Reference checkout commands:

    git clone https://github.com/ufs-community/ufs-weather-model.git
    git checkout b1064dce9bf09144bf7f1e3b3956a396615e9f14
    git submodule update --init UFSATM
    git -C UFSATM submodule update --init ccpp/framework ccpp/physics

Initial verification uses the repository CPU image, bounded build parallelism, pytest against the built module, CTest native scheme references, and real CCPP metadata/cap generation. A UFS system build requires additional UFS dependencies and is reported separately from a generated suite harness.

## CCPP style reference

Follow bbakernoaa/CATChem PR #67 at d57c66a4da90525f7e393492276456c1cecede00. Its source-lock pins the same CCPP framework c4b7d9a as our UFS checkout. Match individually scheduled `_driver_register/init/run/final` entry points, one metadata properties block, Doxygen argument-table/htmlinclude links, `machine::kind_phys`, assumed-length error messages, explicit host inputs, host-target CMake integration and isolated SDF groups. Use correct `.html` links rather than reproducing typographical `.hml` links.

CECE caps publish emission fluxes, not prognostic tracer concentrations. They do not register duplicate host tracers or apply tendencies implicitly. A host may convert surface flux to layer tendencies at an explicit coupling boundary using its geometry, species basis and ownership rules. Such conversion is not part of this first flux-only cap milestone.

The reviewed MEGAN3/BDSNP kernels are stateless with explicit effective history inputs. CCPP uses a call-local executor (same native runtime as persistent Python), serializes native calls, and stages all output before publication. This deliberately trades allocation overhead for unambiguous block/thread state ownership. Future prognostic schemes must add host-visible state transfer before using that adapter; a hidden context alone is insufficient.
