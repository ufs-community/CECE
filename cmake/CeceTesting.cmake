# SPDX-License-Identifier: Apache-2.0
# CECE test-registration helpers. Included from tests/CMakeLists.txt.
#
#   cece_add_gtest(<target> [SOURCES ...] [LINK ...] [MAIN GTEST|MPI|KOKKOS|OWN]
#                  [MPI_LAUNCHER] [SOURCE_DIR_DEFINE] [NO_AS_NEEDED] [NO_CECE]
#                  [NO_DISCOVER] [PROPERTIES ...])
#     Defines a GoogleTest executable and (unless NO_DISCOVER) registers its
#     cases with gtest_discover_tests. MAIN selects the main():
#       GTEST  (default) GTest::gtest_main — plain tests, no MPI/Kokkos
#       MPI    cece_test_main        — MPI + HALO + Kokkos environment (tests/support)
#       KOKKOS cece_test_main_kokkos — Kokkos only, no MPI
#       OWN    GTest::gtest          — the source file provides main()
#     MPI_LAUNCHER runs the binary through `${MPIEXEC_EXECUTABLE} -n 1` under
#     ctest (TEST_LAUNCHER). SOURCE_DIR_DEFINE adds CECE_SOURCE_DIR (the repo
#     root) as a compile definition. NO_AS_NEEDED keeps libcece_core in
#     DT_NEEDED on Linux (Kokkos statics are duplicated across the cece
#     libraries and double-free at teardown otherwise). NO_CECE omits the
#     default `cece` link. PROPERTIES are forwarded to gtest_discover_tests
#     (single-valued only — discovery cannot carry semicolon lists).
#
#   cece_add_mpi_tests(<target> NP <n>... [ENVIRONMENT ...])
#     Registers <target>_np<n> for each rank count: np1 runs the binary
#     directly, np>1 launches it through ${MPIEXEC_*}. ENVIRONMENT entries
#     are prepended to the MPI fabric defaults; multi-rank registrations also
#     get the OpenMPI run-as-root allowances for container runs.
#
#   cece_apply_test_defaults()
#     Call once after all registrations: gives every directly-registered test
#     its rank-count label/PROCESSORS (from the `_np<N>` name suffix, np1
#     otherwise) and the OMP_PROC_BIND default.
include_guard(GLOBAL)
include(GoogleTest)

# Defer GTest test enumeration from build time to test (ctest) run time. With
# the default POST_BUILD mode, CMake runs each test binary during `make` to
# list its cases; on HPC/MPI systems that aborts for MPI-initializing binaries
# when built on a login/build node with no Slurm PMI context (e.g.
# "PMI2_Job_GetId returned 14"), breaking the build. PRE_TEST enumerates at
# `ctest` time instead — inside the job allocation where MPI can initialize.
set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)

set(CECE_TEST_MPI_LAUNCHER "${MPIEXEC_EXECUTABLE};${MPIEXEC_NUMPROC_FLAG};1;${MPIEXEC_PREFLAGS}")
# Loopback-only fabrics so single-node test launches never probe HPC fabrics.
set(CECE_TEST_MPI_ENVIRONMENT "FI_PROVIDER=tcp" "I_MPI_FABRICS=shm")
# Multi-rank launches in the dev container run as root.
set(CECE_TEST_MPI_ROOT_ENVIRONMENT "OMPI_ALLOW_RUN_AS_ROOT=1" "OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1")

# Every discovered gtest suite runs single-rank (its TEST_LAUNCHER, where
# present, is `mpiexec -n 1`), so tag all discovered tests LABELS np1 —
# rank-count selection (`ctest -L np1|np2|np4`) then covers the whole suite.
# Shadowing the module function keeps call sites untouched and future calls
# covered; the module's implementation stays reachable under the underscore
# name, and repeated PROPERTIES keywords accumulate, so call-site PROPERTIES
# merge with the injected pairs. The injection is PREPENDED: discovery
# serializes PROPERTIES as a flat list and re-pairs it name/value, so a
# semicolon-list value at a call site silently corrupts every pair after it.
#
# ENVIRONMENT_MODIFICATION OMP_PROC_BIND=set:false is Kokkos's own
# unit-testing recommendation (silences the OpenMP-backend warning). It is a
# separate property from ENVIRONMENT, so call-site ENVIRONMENT values are
# untouched. Discovery itself needs no such default: gtest lists tests before
# any Environment::SetUp runs, so no test binary initializes Kokkos while
# being enumerated.
#
# Tests run from the build root (not this subdirectory's binary dir), where
# the test binaries also land (CMAKE_RUNTIME_OUTPUT_DIRECTORY in tests/).
function(gtest_discover_tests target)
  _gtest_discover_tests(
    ${target}
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    PROPERTIES LABELS np1 ENVIRONMENT_MODIFICATION "OMP_PROC_BIND=set:false" ${ARGN}
  )
endfunction()

function(cece_add_gtest target)
  cmake_parse_arguments(
    PARSE_ARGV 1
    ARG
    "MPI_LAUNCHER;SOURCE_DIR_DEFINE;NO_AS_NEEDED;NO_CECE;NO_DISCOVER"
    "MAIN"
    "SOURCES;LINK;PROPERTIES"
  )
  if(ARG_UNPARSED_ARGUMENTS)
    message(
      FATAL_ERROR
      "cece_add_gtest(${target}): unexpected arguments: ${ARG_UNPARSED_ARGUMENTS}"
    )
  endif()
  if(NOT ARG_SOURCES)
    set(ARG_SOURCES "${target}.cpp")
  endif()
  if(NOT ARG_MAIN)
    set(ARG_MAIN GTEST)
  endif()
  if(ARG_MAIN STREQUAL "GTEST")
    set(_main GTest::gtest_main)
  elseif(ARG_MAIN STREQUAL "MPI")
    set(_main cece_test_main)
  elseif(ARG_MAIN STREQUAL "KOKKOS")
    set(_main cece_test_main_kokkos)
  elseif(ARG_MAIN STREQUAL "OWN")
    set(_main GTest::gtest)
  else()
    message(
      FATAL_ERROR
      "cece_add_gtest(${target}): MAIN must be GTEST, MPI, KOKKOS or OWN (got '${ARG_MAIN}')"
    )
  endif()

  add_executable(${target} ${ARG_SOURCES})
  set(_libs)
  if(NOT ARG_NO_CECE)
    list(APPEND _libs cece)
  endif()
  list(APPEND _libs ${ARG_LINK} ${_main})
  target_link_libraries(${target} PRIVATE ${_libs})
  if(ARG_MPI_LAUNCHER)
    set_target_properties(${target} PROPERTIES TEST_LAUNCHER "${CECE_TEST_MPI_LAUNCHER}")
  endif()
  if(ARG_SOURCE_DIR_DEFINE)
    target_compile_definitions(${target} PRIVATE CECE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  endif()
  if(ARG_NO_AS_NEEDED AND UNIX AND NOT APPLE)
    target_link_options(${target} PRIVATE "LINKER:--no-as-needed")
  endif()
  if(NOT ARG_NO_DISCOVER)
    if(ARG_PROPERTIES)
      gtest_discover_tests(${target} PROPERTIES ${ARG_PROPERTIES})
    else()
      gtest_discover_tests(${target})
    endif()
  endif()
endfunction()

function(cece_add_mpi_tests target)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "NP;ENVIRONMENT")
  if(ARG_UNPARSED_ARGUMENTS OR NOT ARG_NP)
    message(FATAL_ERROR "cece_add_mpi_tests(${target}): expected NP <n>... [ENVIRONMENT ...]")
  endif()
  foreach(np IN LISTS ARG_NP)
    set(_name ${target}_np${np})
    set(_env ${ARG_ENVIRONMENT} ${CECE_TEST_MPI_ENVIRONMENT})
    if(np EQUAL 1)
      # Single-rank registration. COMMAND is the target NAME, not
      # $<TARGET_FILE:...>: TEST_LAUNCHER is applied only to target-name
      # commands, and on HPC (srun launcher) a raw MPI_Init outside a job step
      # aborts (PMI2_Job_GetId). Where the launcher is absent or ignored
      # (CMake < 3.29) the binary runs directly, validating the single-rank
      # paths without a launcher.
      add_test(NAME ${_name} COMMAND ${target} WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
    else()
      add_test(
        NAME ${_name}
        COMMAND
          ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} ${np} ${MPIEXEC_PREFLAGS}
          $<TARGET_FILE:${target}> ${MPIEXEC_POSTFLAGS}
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
      )
      list(APPEND _env ${CECE_TEST_MPI_ROOT_ENVIRONMENT})
    endif()
    set_tests_properties(${_name} PROPERTIES ENVIRONMENT "${_env}")
  endforeach()
endfunction()

# Rank-count labels/processors and the OMP_PROC_BIND default for the
# directly-registered (non-discovered) tests — the counterpart of the
# gtest_discover_tests wrapper — keyed on the `_np<N>` name-suffix convention
# rather than a hand-kept list. LABELS enable rank-count selection
# (`ctest -L np2`); PROCESSORS makes `ctest -j` budget ranks correctly (e.g.
# `-j 4` packs 4x np1, 2x np2, or 1x np4 without oversubscribing a 4-task
# allocation). Tests without the suffix default to np1 unless their
# registration already set an explicit np label (e.g. the self-driving
# test_distributed_output_equivalence). On CMake >= 3.28 the sweep walks the
# whole project tree — the vendored HELM libs register tests in their own
# subdirectories — via the DIRECTORY-scoped test-property API; older CMakes
# lack that API, so only the calling directory's tests are swept there.
function(cece_apply_test_defaults)
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    set(_sweep_subdirs ON)
    set(_queue "${PROJECT_SOURCE_DIR}")
  else()
    set(_sweep_subdirs OFF)
    set(_queue "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
  while(_queue)
    list(POP_FRONT _queue _dir)
    if(_sweep_subdirs)
      get_property(_subdirs DIRECTORY "${_dir}" PROPERTY SUBDIRECTORIES)
      list(APPEND _queue ${_subdirs})
      set(_dir_arg DIRECTORY "${_dir}")
    else()
      set(_dir_arg)
    endif()
    get_property(_tests DIRECTORY "${_dir}" PROPERTY TESTS)
    foreach(_test IN LISTS _tests)
      set_property(
        TEST ${_test} ${_dir_arg}
        APPEND
        PROPERTY ENVIRONMENT_MODIFICATION "OMP_PROC_BIND=set:false"
      )
      if(_test MATCHES "_np([0-9]+)$")
        set_property(TEST ${_test} ${_dir_arg} APPEND PROPERTY LABELS "np${CMAKE_MATCH_1}")
        set_property(TEST ${_test} ${_dir_arg} PROPERTY PROCESSORS "${CMAKE_MATCH_1}")
      else()
        get_property(_labels TEST ${_test} ${_dir_arg} PROPERTY LABELS)
        if(NOT _labels MATCHES "np[0-9]")
          set_property(TEST ${_test} ${_dir_arg} APPEND PROPERTY LABELS np1)
        endif()
      endif()
    endforeach()
  endwhile()
endfunction()
