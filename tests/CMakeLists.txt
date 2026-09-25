# SPDX-License-Identifier: Apache-2.0
# CECE test suite. Entered from the top-level CMakeLists.txt under
# CECE_BUILD_TESTING; registration helpers live in cmake/CeceTesting.cmake.

# Test binaries land at the build root (where scripts and docs expect them)
# and run from there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# --- Test dependencies -------------------------------------------------------

# GTest
find_package(GTest QUIET)

if(TARGET GTest::gtest AND TARGET GTest::gtest_main)
  cece_register_dep("GTest" "system (${GTest_DIR})")
elseif(CECE_FETCH_DEPS)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  cece_register_dep("GTest" "FetchContent (v1.15.2)")
else()
  message(
    FATAL_ERROR
    "GTest not found via find_package() and CECE_FETCH_DEPS=OFF. Install GTest or configure with -DCECE_BUILD_TESTING=OFF."
  )
endif()

# RapidCheck (Property-Based Testing)
find_package(rapidcheck CONFIG QUIET)

if(TARGET rapidcheck)
  if(NOT TARGET rapidcheck_gtest)
    # Installed RapidCheck packages do not always export the header-only gtest
    # extra as its own target; the extras headers ship with the install.
    add_library(rapidcheck_gtest INTERFACE)
    target_link_libraries(rapidcheck_gtest INTERFACE rapidcheck)
  endif()
  cece_register_dep("RapidCheck" "system (${rapidcheck_DIR})")
elseif(CECE_FETCH_DEPS)
  FetchContent_Declare(
    rapidcheck
    GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
    GIT_TAG b2d9ed2dddefc4b84318d664b4f221eb792d89c7
  )
  set(RC_ENABLE_GTEST ON CACHE BOOL "Enable RapidCheck GTest integration" FORCE)
  FetchContent_MakeAvailable(rapidcheck)
  cece_register_dep("RapidCheck" "FetchContent (pinned master)")
else()
  message(
    FATAL_ERROR
    "RapidCheck not found via find_package() and CECE_FETCH_DEPS=OFF. Install RapidCheck or configure with -DCECE_BUILD_TESTING=OFF."
  )
endif()

include(CeceTesting)

# --- Shared test harness (support/) ------------------------------------------
# Shared main() + process-level MPI/HALO/Kokkos environment. Link
# `cece_test_main` (MAIN MPI) or `cece_test_main_kokkos` (MAIN KOKKOS) in
# place of GTest::gtest_main. A test file that needs its own global
# environment adds it with CECE_TEST_ADD_ENVIRONMENT; it is registered after
# (so torn down before) the MPI/Kokkos one. Initialization happens in
# Environment::SetUp, which gtest skips for --gtest_list_tests, so test
# discovery never initializes MPI or Kokkos and no per-file discovery guard is
# needed.
add_library(cece_test_support STATIC support/cece_test_environment.cpp)
target_include_directories(cece_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cece_test_support PUBLIC cece GTest::gtest)
add_library(cece_test_main STATIC support/cece_test_main.cpp)
target_link_libraries(cece_test_main PUBLIC cece_test_support)
# Kokkos-only variant: no MPI init, for tests that never touch MPI and
# therefore carry no MPI TEST_LAUNCHER.
add_library(cece_test_main_kokkos STATIC support/cece_test_main_kokkos.cpp)
target_link_libraries(cece_test_main_kokkos PUBLIC cece_test_support)

set(RAPIDCHECK rapidcheck rapidcheck_gtest)

# --- Driver / configuration --------------------------------------------------
cece_add_gtest(test_driver_configuration)
cece_add_gtest(test_driver_clock_management NO_CECE)
cece_add_gtest(test_clock_operations_unit NO_CECE)
cece_add_gtest(test_driver_integration_end_to_end NO_CECE)
cece_add_gtest(test_cece_ingestor)
cece_add_gtest(test_cece_utils MAIN MPI MPI_LAUNCHER PROPERTIES PROCESSORS 1)
cece_add_gtest(test_numerical_equivalence MPI_LAUNCHER)
cece_add_gtest(test_cece_compute)
cece_add_gtest(test_standalone_writer_attributes MAIN MPI MPI_LAUNCHER PROPERTIES PROCESSORS 1)
cece_add_gtest(test_physics)
cece_add_gtest(test_hemco_mimic)
cece_add_gtest(test_stacking_engine)
cece_add_gtest(test_vertical_distribution_single)
cece_add_gtest(test_field_resolution_priority)
cece_add_gtest(test_ingest_setfield_bypass)
cece_add_gtest(test_ingest_copyback_neutralized)
cece_add_gtest(test_hemco_parity)

# Kokkos Configuration Tests (Requirement 6.13-6.18, 6.21)
cece_add_gtest(test_kokkos_configuration MAIN KOKKOS)
# Comprehensive Property-Based Test Suite (Task 11.1)
cece_add_gtest(test_properties MAIN KOKKOS)
# Test Suite Idempotence (Requirement 8.16)
cece_add_gtest(test_suite_idempotence)
# Temporary Reproduction Test
cece_add_gtest(test_repro_failure SOURCES repro_failure.cpp)
# CCPP Readiness and Core Link Isolation Test
cece_add_gtest(test_ccpp_readiness NO_CECE LINK cece_core SOURCE_DIR_DEFINE)

# --- Physics schemes ---------------------------------------------------------
# FENGSHA Dust Scheme Property Tests
cece_add_gtest(test_fengsha_properties)
# Ginoux (GOCART2G) Dust Scheme Property Tests
cece_add_gtest(test_ginoux_properties)
# K14 Dust Scheme Property Tests
cece_add_gtest(test_k14_properties)
# Speciation Config Property Tests (MEGAN3)
cece_add_gtest(test_speciation MAIN KOKKOS LINK ${RAPIDCHECK} SOURCE_DIR_DEFINE)
# MEGAN3 Canopy Model Property Tests
cece_add_gtest(test_megan3 MAIN KOKKOS LINK ${RAPIDCHECK})
# BDSNP Soil NO Property Tests
cece_add_gtest(test_bdsnp MAIN KOKKOS LINK ${RAPIDCHECK})
# Canonical BDSNP production-kernel regression tests
cece_add_gtest(test_bdsnp_runtime MAIN KOKKOS)

# --- Clock / time axis -------------------------------------------------------
# CeceClock Property Tests
cece_add_gtest(test_cece_clock_properties LINK ${RAPIDCHECK})
# Data Stream Time Indexing Unit Tests. These tests only reference cece_driver,
# so --as-needed would drop libcece_core from DT_NEEDED; Kokkos statics are
# duplicated in both libraries and double-free at teardown when it does.
cece_add_gtest(test_cece_time_indexing NO_AS_NEEDED)
# CF Time-Units Decoding Unit Tests
cece_add_gtest(test_cece_calendar NO_AS_NEEDED)
# AMIO View Widening Unit Tests
cece_add_gtest(test_cece_amio_utils NO_AS_NEEDED)
# Time Axis Integration Tests (real NetCDF fixtures read through AMIO).
# Provides its own main() for MPI init.
cece_add_gtest(test_cece_time_axis_netcdf MAIN OWN NO_AS_NEEDED MPI_LAUNCHER PROPERTIES PROCESSORS 1)
# CeceClock Run Loop Unit Tests
cece_add_gtest(test_cece_clock_run_loop MAIN KOKKOS)
# Bracket-equality Property Tests (driver-io-regrid-perf, Property 5)
cece_add_gtest(test_bracket_equal_properties LINK ${RAPIDCHECK})

# --- Band decomposition ------------------------------------------------------
cece_add_gtest(
  test_band_decomposition_partition
  LINK ${RAPIDCHECK}
  PROPERTIES ENVIRONMENT "RC_PARAMS=max_success=100"
)
cece_add_gtest(test_band_single_rank_properties MAIN OWN LINK MPI::MPI_CXX ${RAPIDCHECK} MPI_LAUNCHER)
cece_add_gtest(
  test_band_regrid_equals_global_rows
  LINK ${RAPIDCHECK}
  MPI_LAUNCHER
  PROPERTIES ENVIRONMENT "RC_PARAMS=max_success=100"
)
# Band-boundary conservation on NON-UNIFORM grids (distributed-domain-decomposition,
# Property 10). Drives the REAL build_band_mesh_with_global_corners production
# helper + AXIS conservative regrid.
cece_add_gtest(
  test_band_boundary_conservation
  MAIN MPI
  LINK MPI::MPI_CXX Kokkos::kokkos HELM::AXIS ${RAPIDCHECK}
  MPI_LAUNCHER
  PROPERTIES ENVIRONMENT "RC_PARAMS=max_success=100"
)
# Band-boundary conservation on CURVILINEAR grids (distributed-domain-decomposition,
# Property 11). Drives the REAL build_band_mesh_curvilinear_with_global_corners
# production helper (AXIS synthesize_band_corners) + AXIS conservative regrid.
cece_add_gtest(
  test_band_boundary_curvilinear
  MAIN MPI
  LINK MPI::MPI_CXX Kokkos::kokkos HELM::AXIS ${RAPIDCHECK}
  MPI_LAUNCHER
  PROPERTIES ENVIRONMENT "RC_PARAMS=max_success=100"
)

# --- Regrid-per-stream / driver-io-regrid-perf -------------------------------
# StreamKey Property Tests (regrid-per-stream, Property 1)
cece_add_gtest(test_stream_key_properties LINK ${RAPIDCHECK})
# HandleKey Property Tests (regrid-per-stream, Amendment 1, Property 4)
cece_add_gtest(test_handle_key_properties LINK ${RAPIDCHECK})
# StreamKey-extends-HandleKey Property Tests (regrid-per-stream, Amendment 1, Property 5)
cece_add_gtest(test_stream_key_extends_handle_key LINK ${RAPIDCHECK})
# StreamConfig Resolution Property Tests (driver-io-regrid-perf, Property 4)
cece_add_gtest(test_stream_config_resolution LINK ${RAPIDCHECK} SOURCE_DIR_DEFINE)
# No-manifest / No-reparse Regression Guards (driver-io-regrid-perf, Task 13.2)
cece_add_gtest(test_no_manifest_no_reparse SOURCE_DIR_DEFINE)
# Slice-cache Equivalence Property Tests (driver-io-regrid-perf, Property 6)
cece_add_gtest(test_slice_cache_equivalence LINK ${RAPIDCHECK})
# Slice-cache Independence Property Tests (regrid-per-stream, Property 3)
cece_add_gtest(test_slice_cache_independence LINK ${RAPIDCHECK})
# Open-once / no-per-step-close Tests (driver-io-regrid-perf, Task 13.1, Req 9.1)
cece_add_gtest(test_open_once_no_per_step_close LINK ${RAPIDCHECK} SOURCE_DIR_DEFINE)
# Cache-hit Skips-work Property Tests (driver-io-regrid-perf, Task 13.3, Req 9.4)
cece_add_gtest(test_cache_hit_skips_work LINK ${RAPIDCHECK})
# Regrid Conservation Property Tests (driver-io-regrid-perf, Property 9)
cece_add_gtest(test_regrid_conservation_properties LINK ${RAPIDCHECK} MPI_LAUNCHER)
# Shared-plan Equivalence Property Tests (regrid-per-stream, Property 2)
cece_add_gtest(test_shared_plan_equivalence LINK ${RAPIDCHECK} MPI_LAUNCHER)

# --- Temporal endpoint regrid cache -----------------------------------------
# Endpoint-blend Equivalence Property Tests (Property 1). Uses
# apply_regrid_plan + AXIS weight generation (Kokkos), so the file provides
# its own Kokkos/MPI environment.
cece_add_gtest(test_endpoint_blend_equivalence LINK ${RAPIDCHECK} MPI_LAUNCHER)
# Endpoint Work-reduction Property Tests (Property 2). Pure decision ladder
# with a WorkSpy; no Kokkos/MPI needed.
cece_add_gtest(test_endpoint_work_reduction LINK ${RAPIDCHECK})
# Endpoint per-variable Shared-plan Correctness Property Tests (Property 5).
cece_add_gtest(test_endpoint_shared_plan LINK ${RAPIDCHECK} MPI_LAUNCHER)
# Endpoint Tier-precedence Property Tests (Property 3, Task 9.1).
cece_add_gtest(test_endpoint_tier_precedence LINK ${RAPIDCHECK})
# Endpoint Sizing and Tier-3 Population Unit/Example Tests (Task 12.1).
cece_add_gtest(test_endpoint_sizing LINK MPI::MPI_CXX Kokkos::kokkos MPI_LAUNCHER)
# Endpoint Invalidation and Failure unit/example tests (Task 12.2).
cece_add_gtest(test_endpoint_invalidation LINK MPI::MPI_CXX Kokkos::kokkos MPI_LAUNCHER)

# --- Ingest copy consolidation ----------------------------------------------
# No-stale-data Property Tests (Property 4)
cece_add_gtest(test_ingest_no_stale_data LINK ${RAPIDCHECK})
# Ingest Consolidation Equivalence Property Tests (Property 1). Uses Kokkos
# DualView3D + deep_copy, so the file provides its own Kokkos/MPI environment.
cece_add_gtest(test_ingest_consolidation_equivalence LINK ${RAPIDCHECK} MPI_LAUNCHER)
# MPI Collective Gate Order Property Tests (Property 3)
cece_add_gtest(test_ingest_collective_gate_order LINK ${RAPIDCHECK})
# Ingest single-write Property Tests (Property 2)
cece_add_gtest(test_ingest_single_write LINK ${RAPIDCHECK} MPI_LAUNCHER)

# --- Multi-rank MPI tests ----------------------------------------------------
# These provide their own main() with MPI init and register at several rank
# counts: np1 runs the binary directly so the single-rank paths are validated
# even without a launcher; np2/np4 launch through ${MPIEXEC_EXECUTABLE}
# (container preflags come from the CECE_MPIEXEC_CONTAINER_FLAGS option).

# MPI cross-rank reuse-decision integration test (driver-io-regrid-perf,
# Property 8, Task 12.3).
cece_add_gtest(test_mpi_reuse_decision MAIN OWN LINK MPI::MPI_CXX MPI_LAUNCHER NO_DISCOVER)
cece_add_mpi_tests(test_mpi_reuse_decision NP 1 2)

# Endpoint reuse-decision rank-invariance property test
# (temporal-endpoint-regrid-cache, Property 4, Task 10.1).
cece_add_gtest(
  test_endpoint_reuse_decision
  MAIN OWN
  LINK MPI::MPI_CXX ${RAPIDCHECK}
  MPI_LAUNCHER
  NO_DISCOVER
)
cece_add_mpi_tests(test_endpoint_reuse_decision NP 1 2)

# Fused front-half gate-equivalence property test (cece-halo-collective-adoption,
# Property 4, Task 10.3): the pure FusedGateDecision against the five legacy
# gates, plus an end-to-end packing case over halo::allreduce.
cece_add_gtest(
  test_fused_gate_equivalence
  MAIN OWN
  LINK MPI::MPI_CXX HELM::HALO ${RAPIDCHECK}
  MPI_LAUNCHER
  NO_DISCOVER
)
cece_add_mpi_tests(test_fused_gate_equivalence NP 1 2 4)

# Collective-count independence Property Test (cece-halo-collective-adoption,
# Property 5, Task 10.4): a PMPI-style spy proves the single-gather assembly
# issues exactly one Allgatherv and a level-independent collective count.
cece_add_gtest(
  test_collective_count
  MAIN OWN
  LINK HELM::HALO MPI::MPI_CXX Kokkos::kokkos ${RAPIDCHECK}
  MPI_LAUNCHER
  NO_DISCOVER
)
cece_add_mpi_tests(test_collective_count NP 1 2)

# HALO single-gather assembly equivalence Property Test
# (cece-halo-collective-adoption, Property 1, Task 10.1): naive per-level
# MPI_Allgatherv assembly vs Replicated_Gather_Plan + gather_replicated.
cece_add_gtest(
  test_halo_gather_equivalence
  MAIN OWN
  LINK HELM::HALO MPI::MPI_CXX Kokkos::kokkos ${RAPIDCHECK}
  MPI_LAUNCHER
  NO_DISCOVER
)
cece_add_mpi_tests(test_halo_gather_equivalence NP 1 2 4)

# Collective gate-equivalence property tests (cece-halo-collective-adoption,
# Properties 2 & 3, Task 10.2): collective_all_ready / collective_int_matches
# against a legacy reference, including the NULL comm / size-1 short circuits.
cece_add_gtest(
  test_collective_gate_equivalence
  MAIN OWN
  LINK MPI::MPI_CXX ${RAPIDCHECK}
  MPI_LAUNCHER
  NO_DISCOVER
)
cece_add_mpi_tests(test_collective_gate_equivalence NP 1 2)

# HALO-adoption unit / example / edge-case tests (cece-halo-collective-adoption,
# Task 10.6): Replicated_Gather_Plan accessors, send_buf index math, the
# halo_comm_ wrapping decision and the cache-clear invalidation invariant.
cece_add_gtest(test_halo_adoption_units MAIN OWN LINK MPI::MPI_CXX HELM::HALO MPI_LAUNCHER NO_DISCOVER)
cece_add_mpi_tests(test_halo_adoption_units NP 1 2 4)

# HALO-adoption multi-rank integration test (cece-halo-collective-adoption,
# Task 11.1): the real regrid+gather path primitives under failure injection,
# np2/np4 cross-rank bit-identity and np1-equivalence to the serial reference.
cece_add_gtest(
  test_halo_multirank_integration
  MAIN OWN
  LINK HELM::HALO MPI::MPI_CXX Kokkos::kokkos
  MPI_LAUNCHER
  NO_DISCOVER
)
cece_add_mpi_tests(test_halo_multirank_integration NP 1 2 4)

# --- Distributed domain decomposition (Task 10) ------------------------------
# 10.1 Output_Gather reconstruction (Property 4). Pure pack/place reassembly +
# a per-level MPI_Gatherv multi-rank variant, both RapidCheck. Registered at
# np1/np2/np4 so the surplus-rank (ny < size) case is exercised.
cece_add_gtest(
  test_output_gather_reconstruction
  MAIN OWN
  LINK MPI::MPI_CXX ${RAPIDCHECK}
  MPI_LAUNCHER
  NO_DISCOVER
)
cece_add_mpi_tests(test_output_gather_reconstruction NP 1 2 4 ENVIRONMENT "RC_PARAMS=max_success=100")

# 10.3 Column-local band invariance (Property 7). Drives the REAL Ginoux scheme
# over the full global range vs a band and asserts bit-for-bit equality of the
# band-owned cells. Single-process, Kokkos only.
cece_add_gtest(test_column_local_band_invariance MAIN KOKKOS LINK Kokkos::kokkos ${RAPIDCHECK} NO_DISCOVER)
add_test(
  NAME test_column_local_band_invariance
  COMMAND $<TARGET_FILE:test_column_local_band_invariance>
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)
set_tests_properties(
  test_column_local_band_invariance
  PROPERTIES ENVIRONMENT "RC_PARAMS=max_success=100"
)

# 10.4 Collective lock-step with a surplus rank (Property 8). Needs np >= 4 to
# produce a surplus rank (ny = 2 and ny = 3): registered at np1 (structural)
# and np4 (surplus path).
cece_add_gtest(test_surplus_rank_lockstep MAIN OWN LINK MPI::MPI_CXX MPI_LAUNCHER NO_DISCOVER)
cece_add_mpi_tests(test_surplus_rank_lockstep NP 1 4)

# 10.2 Distributed output equivalence (Property 5 & 6). End-to-end: runs the
# REAL cece_standalone_driver at -np 1/2/3 on the tiny CEDS OC config and
# asserts the written `oc` field is bit-for-bit identical across rank counts.
# The shell harness compiles nccmp_var.c against the container NetCDF-C and
# drives mpirun itself, so it is registered as a single ctest entry with a
# DEPENDS on the driver target. Requires the CEDS source data in data/.
if(EXISTS "${PROJECT_SOURCE_DIR}/data/OC-em-anthro_CMIP_CEDS_2023.nc")
  add_test(
    NAME test_distributed_output_equivalence
    COMMAND
      ${CMAKE_COMMAND} -E env "MPIEXEC_EXECUTABLE=${MPIEXEC_EXECUTABLE}"
      "MPIEXEC_NUMPROC_FLAG=${MPIEXEC_NUMPROC_FLAG}" bash
      ${CMAKE_CURRENT_SOURCE_DIR}/test_distributed_output_equivalence.sh --driver
      $<TARGET_FILE:cece_standalone_driver> --config
      ${PROJECT_SOURCE_DIR}/examples/cece_config_ceds_oc_small.yaml --comparator
      ${CMAKE_CURRENT_SOURCE_DIR}/nccmp_var.c --workdir ${CMAKE_BINARY_DIR}/_equiv_workdir
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  set_tests_properties(
    test_distributed_output_equivalence
    PROPERTIES
      DEPENDS cece_standalone_driver
      ENVIRONMENT
        "FI_PROVIDER=tcp;I_MPI_FABRICS=shm;OMPI_ALLOW_RUN_AS_ROOT=1;OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1"
      TIMEOUT 600
      # Self-drives -np 1/2/3 sequentially; label/budget for its 3-rank peak
      # (cece_apply_test_defaults skips tests that carry an explicit np label).
      LABELS np3
      PROCESSORS 3
  )
endif()

# --- HELM integration --------------------------------------------------------
# C++ HELM Unit Tests
cece_add_gtest(
  test_helm_cpp
  MAIN MPI
  LINK HELM::DAGR HELM::HALO HELM::TICK HELM::SPAN
  SOURCE_DIR_DEFINE
  MPI_LAUNCHER
  PROPERTIES ENVIRONMENT "FI_PROVIDER=tcp" PROCESSORS 1
)

# --- ESMF-gated tests --------------------------------------------------------
if(ESMF_FOUND)
  cece_add_gtest(
    test_driver_config_validation
    LINK iso8601_utils
    MPI_LAUNCHER
    PROPERTIES ENVIRONMENT "FI_PROVIDER=tcp" PROCESSORS 1
  )
  cece_add_gtest(test_cece_cap PROPERTIES ENVIRONMENT "FI_PROVIDER=tcp")
  cece_add_gtest(test_diagnostics MAIN KOKKOS PROPERTIES ENVIRONMENT "FI_PROVIDER=tcp")

  # Fortran helpers for the field creation / pointer passing tests.
  set(_esmf_test_helpers)
  if(CECE_HAS_FORTRAN)
    add_library(test_helpers STATIC test_helpers.F90)
    target_include_directories(test_helpers PUBLIC ${ESMF_INCLUDE_DIRS})
    target_link_libraries(test_helpers PUBLIC ESMF::ESMF)
    if(MPI_FOUND)
      target_link_libraries(test_helpers PUBLIC MPI::MPI_CXX)
    endif()
    set(_esmf_test_helpers test_helpers)
  endif()

  # Unit tests for field creation (Requirement R4)
  cece_add_gtest(
    test_field_creation
    MAIN MPI
    LINK ${_esmf_test_helpers}
    MPI_LAUNCHER
    PROPERTIES ENVIRONMENT "FI_PROVIDER=tcp" PROCESSORS 1
  )
  cece_add_gtest(
    test_field_creation_error_handling
    MAIN MPI
    LINK ${_esmf_test_helpers}
    MPI_LAUNCHER
    PROPERTIES PROCESSORS 1
  )
  # Unit tests for field pointer passing (Requirement R5)
  cece_add_gtest(
    test_field_pointer_passing
    MAIN MPI
    LINK ${_esmf_test_helpers}
    MPI_LAUNCHER
    PROPERTIES ENVIRONMENT "FI_PROVIDER=tcp" PROCESSORS 1
  )
endif()

# --- Drivers and benchmarks (not gtest) --------------------------------------
add_executable(hemco_comparison_driver hemco_comparison_driver.cpp)
target_link_libraries(hemco_comparison_driver PRIVATE cece)
add_test(
  NAME test_hemco_comparison
  COMMAND hemco_comparison_driver
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

add_executable(benchmark_cece_hemco benchmark_cece_hemco.cpp)
target_link_libraries(benchmark_cece_hemco PRIVATE cece)

# Before/after ingest/assembly micro-benchmark (ingest-copy-consolidation, Task 1.1)
add_executable(benchmark_ingest_copy_consolidation benchmark_ingest_copy_consolidation.cpp)
target_link_libraries(benchmark_ingest_copy_consolidation PRIVATE cece)

# Standalone ex5-shaped end-to-end per-step ingest/assembly benchmark (F360)
add_executable(benchmark_ex5_end_to_end benchmark_ex5_end_to_end.cpp)
target_link_libraries(benchmark_ex5_end_to_end PRIVATE cece)

# Standalone regrid APPLY hot-loop micro-benchmark (driver-io-regrid-perf)
add_executable(benchmark_regrid_apply benchmark_regrid_apply.cpp)
target_link_libraries(benchmark_regrid_apply PRIVATE cece)

# Standalone develop-vs-optimize per-step pipeline benchmark (regrid+assemble+ingest).
# Its DEVELOP path constructs a KokkosSparse::CrsMatrix directly; KokkosKernels is
# a hard AXIS dependency (found or fetched), so this always builds and the
# KokkosSparse headers are always on the include path via the cece->axis link.
add_executable(benchmark_pipeline_develop_vs_optimize benchmark_pipeline_develop_vs_optimize.cpp)
target_link_libraries(benchmark_pipeline_develop_vs_optimize PRIVATE cece)

# Standalone MPI develop-vs-optimize distributed gather/assembly timing harness
# (own main() with MPI + Kokkos init). Runnable single-process AND under
# mpirun -np {1,2,4}. Reports actual MPI wall-time per assembly for the
# per-level MPI_Allgatherv (develop) vs the single cached gather_replicated
# (optimize).
add_executable(
  benchmark_mpi_pipeline
  benchmark_mpi_pipeline.cpp
  ${PROJECT_SOURCE_DIR}/tools/pmpi_allgatherv_timer.cpp
)
target_link_libraries(benchmark_mpi_pipeline PRIVATE cece HELM::HALO MPI::MPI_CXX Kokkos::kokkos)

# --- Python script tests -----------------------------------------------------
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  # Use a venv inside the build directory so pip installs work even in
  # read-only container environments (e.g. Singularity).
  set(CECE_PYTHON_DIR "${CMAKE_BINARY_DIR}/python")
  set(CECE_PYTHON_VENV_DIR "${CECE_PYTHON_DIR}/venv")
  set(CECE_PYTHON_TMP_DIR "${CECE_PYTHON_DIR}/tmp")
  set(CECE_PYTHON_CACHE_DIR "${CECE_PYTHON_DIR}/cache")

  file(MAKE_DIRECTORY "${CECE_PYTHON_DIR}")
  file(MAKE_DIRECTORY "${CECE_PYTHON_TMP_DIR}")
  file(MAKE_DIRECTORY "${CECE_PYTHON_CACHE_DIR}")

  if(WIN32)
    set(VENV_PYTHON "${CECE_PYTHON_VENV_DIR}/Scripts/python.exe")
  else()
    set(VENV_PYTHON "${CECE_PYTHON_VENV_DIR}/bin/python")
  endif()

  # Keep pip and temp files inside the build tree for containerized CI runs.
  set(
    CECE_PIP_TEST_ENV
    "PIP_CACHE_DIR=${CECE_PYTHON_CACHE_DIR}"
    "PIP_DISABLE_PIP_VERSION_CHECK=1"
    "HOME=${CECE_PYTHON_DIR}"
    "TMPDIR=${CECE_PYTHON_TMP_DIR}"
    "PYTHONNOUSERSITE=1"
  )
  if(WIN32)
    list(
      APPEND CECE_PIP_TEST_ENV
      "TMP=${CECE_PYTHON_TMP_DIR}"
      "TEMP=${CECE_PYTHON_TMP_DIR}"
      "USERPROFILE=${CECE_PYTHON_DIR}"
    )
  endif()

  # Python packages the pytest-based tests need — single source of truth for
  # the configure-time prefetch and the install_python_deps fixture test.
  set(CECE_PYTHON_TEST_DEPS pytest PyYAML matplotlib numpy)

  # Prefetch the venv + packages at configure time: configure typically runs
  # where the network is available (e.g. HPC login nodes) while ctest may run
  # where it is not (compute nodes). pip install is an offline no-op once the
  # requirements are satisfied, so the fixture tests below then pass without
  # network. Best-effort by design — an offline configure warns and defers to
  # the fixture tests, never fails.
  option(
    CECE_PREFETCH_PYTHON_TEST_DEPS
    "Install python test deps into the build-tree venv at configure time"
    ON
  )
  if(CECE_PREFETCH_PYTHON_TEST_DEPS)
    execute_process(
      COMMAND
        ${CMAKE_COMMAND} -E env ${CECE_PIP_TEST_ENV} ${Python3_EXECUTABLE} -m venv
        ${CECE_PYTHON_VENV_DIR}
      RESULT_VARIABLE _cece_venv_rc
      OUTPUT_QUIET
      ERROR_QUIET
    )
    set(_cece_prefetch_rc 1)
    if(_cece_venv_rc EQUAL 0)
      execute_process(
        COMMAND
          ${CMAKE_COMMAND} -E env ${CECE_PIP_TEST_ENV} ${VENV_PYTHON} -I -m pip install
          ${CECE_PYTHON_TEST_DEPS}
        RESULT_VARIABLE _cece_prefetch_rc
        OUTPUT_QUIET
        ERROR_QUIET
        TIMEOUT 300
      )
    endif()
    if(_cece_prefetch_rc EQUAL 0)
      cece_register_dep("python-test-deps" "prefetched at configure (${CECE_PYTHON_VENV_DIR})")
    else()
      message(
        WARNING
        "Could not prefetch python test deps into ${CECE_PYTHON_VENV_DIR} (offline configure?). The install_python_deps test will retry at test time."
      )
      cece_register_dep("python-test-deps" "deferred to test time (prefetch failed)")
    endif()
  else()
    cece_register_dep("python-test-deps" "deferred to test time (prefetch disabled)")
  endif()

  add_test(
    NAME create_python_venv
    COMMAND ${Python3_EXECUTABLE} -m venv ${CECE_PYTHON_VENV_DIR}
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  add_test(
    NAME ensure_pip_in_venv
    COMMAND ${VENV_PYTHON} -I -m ensurepip --upgrade
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  add_test(
    NAME upgrade_pip_in_venv
    COMMAND ${VENV_PYTHON} -I -m pip install --upgrade pip
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  set_tests_properties(
    create_python_venv
    ensure_pip_in_venv
    upgrade_pip_in_venv
    PROPERTIES FIXTURES_SETUP "python_venv_ready" ENVIRONMENT "${CECE_PIP_TEST_ENV}"
  )
  set_tests_properties(ensure_pip_in_venv PROPERTIES DEPENDS create_python_venv)
  set_tests_properties(upgrade_pip_in_venv PROPERTIES DEPENDS ensure_pip_in_venv)

  add_test(
    NAME install_python_deps
    COMMAND ${VENV_PYTHON} -I -m pip install ${CECE_PYTHON_TEST_DEPS}
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  set_tests_properties(
    install_python_deps
    PROPERTIES
      FIXTURES_SETUP "python_deps_installed"
      FIXTURES_REQUIRED "python_venv_ready"
      ENVIRONMENT "${CECE_PIP_TEST_ENV}"
  )

  # pytest suites: <name> <script> <PYTHONPATH>
  function(cece_add_pytest name script pythonpath)
    add_test(
      NAME ${name}
      COMMAND ${VENV_PYTHON} -m pytest ${CMAKE_CURRENT_SOURCE_DIR}/${script} ${ARGN}
      WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    set_tests_properties(
      ${name}
      PROPERTIES ENVIRONMENT "PYTHONPATH=${pythonpath}" FIXTURES_REQUIRED "python_deps_installed"
    )
  endfunction()

  cece_add_pytest(test_hemco_to_cece_python test_converters.py "${PROJECT_SOURCE_DIR}/scripts")
  cece_add_pytest(test_scripts_python test_scripts.py "${PROJECT_SOURCE_DIR}/scripts")

  if(BUILD_PYTHON_BINDINGS)
    set(_bindings_pkg "${CMAKE_BINARY_DIR}/src/python/cece")
    cece_add_pytest(test_enum_config_bindings test_enum_config_bindings.py "${_bindings_pkg}" -v)
    cece_add_pytest(test_state_bindings test_state_bindings.py "${_bindings_pkg}" -v)
    cece_add_pytest(test_compute_bindings test_compute_bindings.py "${_bindings_pkg}" -v)
    cece_add_pytest(test_validator_logger_bindings test_validator_logger_bindings.py "${_bindings_pkg}" -v)
    cece_add_pytest(test_python_api_rewired test_python_api_rewired.py "${CMAKE_BINARY_DIR}/src/python" -v)
  endif()
endif()

# --- Test defaults (labels, PROCESSORS, OMP_PROC_BIND) ------------------------
cece_apply_test_defaults()
