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
