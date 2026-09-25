cmake_minimum_required(VERSION 3.20)
project(CECE LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optional Fortran support
include(CheckLanguage)
check_language(Fortran)
if(CMAKE_Fortran_COMPILER)
  enable_language(Fortran)
  set(CECE_HAS_FORTRAN ON)
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/share)
else()
  message(WARNING "Fortran compiler not found. Fortran features will be disabled.")
endif()

# Set build type before any FetchContent dependencies configure,
# since ArborX (via HELM AXIS) force-sets this cache variable itself if left unspecified.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Choose the type of build." FORCE)
  set_property(
    CACHE CMAKE_BUILD_TYPE
    PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel"
  )
endif()

list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(EXISTS "/opt/views/view")
  list(APPEND CMAKE_PREFIX_PATH "/opt/views/view")
endif()

# Allow find_package to use <PackageName>_ROOT variables
if(POLICY CMP0074)
  cmake_policy(SET CMP0074 NEW)
endif()

# Discover and append standard HPC/Spack-Stack NetCDF environment variables to CMAKE_PREFIX_PATH
foreach(
  env_var
  NetCDF_ROOT
  NETCDF_ROOT
  NETCDF_DIR
  NetCDF_DIR
  netCDF_ROOT
  netcdf_ROOT
  NetCDF_C_ROOT
  NETCDF_C_ROOT
)
  if(DEFINED ENV{${env_var}})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{${env_var}}")
  endif()
endforeach()

# Disable Intel oneAPI warnings about -g without -O0
if(CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Rno-debug-disables-optimization")
endif()
if(CMAKE_C_COMPILER_ID STREQUAL "IntelLLVM")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Rno-debug-disables-optimization")
endif()
if(CECE_HAS_FORTRAN AND CMAKE_Fortran_COMPILER_ID STREQUAL "IntelLLVM")
  set(CMAKE_Fortran_FLAGS "${CMAKE_Fortran_FLAGS} -diag-disable=10440")
endif()

# --- Dependencies ---
#
# Every external dependency is discovered with find_package() first; FetchContent
# is only a fallback for hosts that do not provide the package, and the fallbacks
# can be disabled entirely for network-isolated builds (e.g. HPC compute nodes).

include(FetchContent)

# Master switch for the FetchContent fallbacks. When OFF, every dependency must
# resolve via find_package() and configure never reaches for the network.
option(CECE_FETCH_DEPS "Allow FetchContent fallbacks when find_package() fails" ON)

# The test suite needs GTest and RapidCheck, which HPC stacks generally do not
# provide (e.g. spack-stack); turn testing off there.
option(CECE_BUILD_TESTING "Build the CECE test suite (requires GTest and RapidCheck)" ON)

# Record how each dependency was resolved; summarized at the end of configure.
set_property(GLOBAL PROPERTY CECE_DEP_SUMMARY "")
function(cece_register_dep name provider)
  set_property(GLOBAL APPEND PROPERTY CECE_DEP_SUMMARY "${name}: ${provider}")
  message(STATUS "CECE dependency ${name}: ${provider}")
endfunction()

# Check for OpenMP availability to set generic defaults
find_package(OpenMP QUIET)

# GPU support is optional - users must explicitly enable
option(Kokkos_ENABLE_CUDA "Enable Kokkos CUDA execution space for NVIDIA GPUs" OFF)
option(Kokkos_ENABLE_HIP "Enable Kokkos HIP execution space for AMD GPUs" OFF)

# Python bindings support
option(BUILD_PYTHON_BINDINGS "Build Python bindings for CECE" OFF)

# Validate that at most one GPU backend is enabled
if(Kokkos_ENABLE_CUDA AND Kokkos_ENABLE_HIP)
  message(FATAL_ERROR "Cannot enable both CUDA and HIP. Choose one GPU backend or neither.")
endif()

# Kokkos
find_package(Kokkos QUIET)

if(TARGET Kokkos::kokkos)
  cece_register_dep("Kokkos" "system ${Kokkos_VERSION} (${Kokkos_DIR})")
  message(STATUS "Kokkos execution spaces (pre-installed): ${Kokkos_DEVICES}")
  # A pre-installed Kokkos arrives already configured; verify the required
  # execution spaces are present instead of forcing cache options that the
  # installed binary cannot honor.
  if(COMMAND kokkos_check)
    kokkos_check(DEVICES SERIAL)
    if(Kokkos_ENABLE_CUDA)
      kokkos_check(DEVICES CUDA)
    endif()
    if(Kokkos_ENABLE_HIP)
      kokkos_check(DEVICES HIP)
    endif()
  endif()
elseif(CECE_FETCH_DEPS)
  # Configure execution spaces for the in-tree build: default to Serial and
  # OpenMP (when available) for CPU portability. Users can override with
  # -DKokkos_ENABLE_CUDA=ON or -DKokkos_ENABLE_HIP=ON for GPU support.
  if(NOT DEFINED Kokkos_ENABLE_SERIAL)
    set(Kokkos_ENABLE_SERIAL ON CACHE BOOL "Enable Kokkos Serial execution space" FORCE)
  endif()

  if(NOT DEFINED Kokkos_ENABLE_OPENMP)
    if(OpenMP_CXX_FOUND)
      set(Kokkos_ENABLE_OPENMP ON CACHE BOOL "Enable Kokkos OpenMP execution space" FORCE)
    else()
      set(Kokkos_ENABLE_OPENMP OFF CACHE BOOL "Enable Kokkos OpenMP execution space" FORCE)
    endif()
  endif()

  # Log Kokkos configuration
  message(STATUS "Kokkos Execution Spaces Configuration:")
  message(STATUS "  Serial: ${Kokkos_ENABLE_SERIAL}")
  message(STATUS "  OpenMP: ${Kokkos_ENABLE_OPENMP}")
  message(STATUS "  CUDA: ${Kokkos_ENABLE_CUDA}")
  message(STATUS "  HIP: ${Kokkos_ENABLE_HIP}")

  FetchContent_Declare(kokkos GIT_REPOSITORY https://github.com/kokkos/kokkos.git GIT_TAG 5.1.1)
  FetchContent_MakeAvailable(kokkos)
  cece_register_dep("Kokkos" "FetchContent (5.1.1)")

  # The HELM subdirectories call find_package(Kokkos) themselves; point them at
  # a stub package config that vouches for the in-tree FetchContent targets.
  # The stub lives in the build dir so it can never shadow a real installation.
  set(_cece_kokkos_stub_dir "${CMAKE_BINARY_DIR}/cece-deps/Kokkos")
  file(
    WRITE "${_cece_kokkos_stub_dir}/KokkosConfig.cmake"
    [=[
# Generated by CECE: bridges the FetchContent-built Kokkos to downstream
# find_package(Kokkos) consumers (e.g. the HELM libraries).
set(Kokkos_FOUND TRUE)
set(Kokkos_VERSION "5.1.1")
set(Kokkos_VERSION_MAJOR 5)
set(Kokkos_VERSION_MINOR 1)
set(Kokkos_VERSION_PATCH 1)
if(NOT TARGET Kokkos::kokkos)
  message(
    WARNING
    "Kokkos::kokkos target not defined yet. Ensure FetchContent_MakeAvailable(kokkos) runs before find_package."
  )
endif()
]=]
  )
  file(
    WRITE "${_cece_kokkos_stub_dir}/KokkosConfigVersion.cmake"
    [=[
set(PACKAGE_VERSION "5.1.1")
set(PACKAGE_VERSION_COMPATIBLE TRUE)
set(PACKAGE_VERSION_EXACT FALSE)
]=]
  )
  set(Kokkos_DIR "${_cece_kokkos_stub_dir}" CACHE PATH "Kokkos stub config (FetchContent)" FORCE)
else()
  message(
    FATAL_ERROR
    "Kokkos not found via find_package() and CECE_FETCH_DEPS=OFF. Install Kokkos (e.g. via spack) or re-enable the FetchContent fallback."
  )
endif()

# mdspan headers (<mdspan/mdspan.hpp>), used by the HELM libraries (SPAN, AXIS, DAGR)
if(kokkos_SOURCE_DIR)
  # FetchContent path: use Kokkos's bundled reference mdspan headers.
  include_directories(SYSTEM "${kokkos_SOURCE_DIR}/tpls/mdspan/include")
  cece_register_dep("mdspan" "bundled with fetched Kokkos")
else()
  find_package(mdspan CONFIG QUIET)
  if(TARGET std::mdspan)
    get_target_property(_cece_mdspan_incs std::mdspan INTERFACE_INCLUDE_DIRECTORIES)
    include_directories(SYSTEM ${_cece_mdspan_incs})
    cece_register_dep("mdspan" "system package (${mdspan_DIR})")
  else()
    # An installed Kokkos may ship the reference mdspan headers in its include dir.
    find_path(CECE_MDSPAN_INCLUDE_DIR NAMES mdspan/mdspan.hpp)
    if(CECE_MDSPAN_INCLUDE_DIR)
      include_directories(SYSTEM "${CECE_MDSPAN_INCLUDE_DIR}")
      cece_register_dep("mdspan" "system headers (${CECE_MDSPAN_INCLUDE_DIR})")
    elseif(CECE_FETCH_DEPS)
      FetchContent_Declare(
        mdspan
        GIT_REPOSITORY https://github.com/kokkos/mdspan.git
        GIT_TAG mdspan-0.6.0
      )
      FetchContent_MakeAvailable(mdspan)
      include_directories(SYSTEM "${mdspan_SOURCE_DIR}/include")
      cece_register_dep("mdspan" "FetchContent (mdspan-0.6.0)")
    else()
      message(
        FATAL_ERROR
        "mdspan headers (mdspan/mdspan.hpp) not found and CECE_FETCH_DEPS=OFF. Install the reference mdspan (or a Kokkos that ships it)."
      )
    endif()
  endif()
endif()

# YAML-CPP
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.30")
  set(CMAKE_POLICY_VERSION_MINIMUM "3.5")
endif()

find_package(yaml-cpp 0.8 CONFIG QUIET)

if(TARGET yaml-cpp::yaml-cpp)
  cece_register_dep("yaml-cpp" "system ${yaml-cpp_VERSION} (${yaml-cpp_DIR})")
  # Have HELM CONF reuse the same installation instead of its own FetchContent.
  set(CONF_USE_SYSTEM_YAMLCPP ON)
  # Installed yaml-cpp packages do not always provide the legacy un-namespaced
  # target; provide a global shim so consumers written against `yaml-cpp`
  # (e.g. HELM CONF) resolve it as a target in every directory scope instead of
  # degrading to a raw -lyaml-cpp on their link lines.
  if(NOT TARGET yaml-cpp)
    add_library(yaml-cpp INTERFACE IMPORTED GLOBAL)
    target_link_libraries(yaml-cpp INTERFACE yaml-cpp::yaml-cpp)
  endif()
else()
  # In-tree build: prefer the vendored submodule (no network), then the GitHub
  # fetch. Both feed through FetchContent so HELM CONF's own
  # FetchContent_MakeAvailable(yaml-cpp) remains a no-op (the name is already
  # populated here); a plain add_subdirectory would leave CONF trying its own
  # network clone on top of duplicate targets.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/yaml-cpp/CMakeLists.txt")
    FetchContent_Declare(yaml-cpp SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/yaml-cpp")
    set(_cece_yaml_cpp_provider "submodule (extern/yaml-cpp)")
  elseif(CECE_FETCH_DEPS)
    # Keep the tag in sync with the extern/yaml-cpp submodule pin (the submodule
    # is the source of truth if they drift).
    FetchContent_Declare(
      yaml-cpp
      GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
      GIT_TAG 0.8.0
    )
    set(_cece_yaml_cpp_provider "FetchContent (0.8.0)")
  else()
    message(
      FATAL_ERROR
      "yaml-cpp not found via find_package(), the extern/yaml-cpp submodule is not populated, and CECE_FETCH_DEPS=OFF. Run `git submodule update --init extern/yaml-cpp`, install yaml-cpp >= 0.8, or re-enable the FetchContent fallback."
    )
  endif()
  FetchContent_MakeAvailable(yaml-cpp)
  if(NOT TARGET yaml-cpp::yaml-cpp)
    add_library(yaml-cpp::yaml-cpp ALIAS yaml-cpp)
  endif()
  cece_register_dep("yaml-cpp" "${_cece_yaml_cpp_provider}")
endif()

# HELM Ingestion (subdirectories added individually because HELM has no root CMakeLists)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/helm/libs/tick/CMakeLists.txt")
  set(HELM_BUILD_TESTING_SAVE ${BUILD_TESTING})
  set(BUILD_TESTING OFF)

  add_subdirectory(extern/helm/libs/logs EXCLUDE_FROM_ALL)
  add_subdirectory(extern/helm/libs/tick EXCLUDE_FROM_ALL)
  add_subdirectory(extern/helm/libs/span EXCLUDE_FROM_ALL)
  add_subdirectory(extern/helm/libs/conf EXCLUDE_FROM_ALL)
  add_subdirectory(extern/helm/libs/halo EXCLUDE_FROM_ALL)
  add_subdirectory(extern/helm/libs/amio EXCLUDE_FROM_ALL)
  add_subdirectory(extern/helm/libs/axis EXCLUDE_FROM_ALL)
  add_subdirectory(extern/helm/libs/blend EXCLUDE_FROM_ALL)
  add_subdirectory(extern/helm/libs/dagr EXCLUDE_FROM_ALL)

  set(BUILD_TESTING ${HELM_BUILD_TESTING_SAVE})
else()
  message(
    FATAL_ERROR
    "HELM submodule not found at extern/helm. Please run 'git submodule update --init --recursive'"
  )
endif()

# ESMF and NUOPC support option (disabled by default)
option(ENABLE_ESMF "Enable ESMF and NUOPC support" OFF)

if(ENABLE_ESMF)
  find_package(ESMF)
  if(ESMF_FOUND)
    message(STATUS "Found ESMF: ${ESMF_VERSION}")
    message(STATUS "ESMF include dirs: ${ESMF_INCLUDE_DIRS}")
    cece_register_dep("ESMF" "system ${ESMF_VERSION}")
  endif()
else()
  set(ESMF_FOUND FALSE)
  message(STATUS "ESMF/NUOPC support is disabled (ENABLE_ESMF is OFF by default)")
endif()

# Find standard NetCDF: try CONFIG mode first, fallback to standard library discovery
find_package(netCDF CONFIG QUIET)

if(NOT netCDF_FOUND)
  message(STATUS "NetCDF Config file not found. Falling back to standard library discovery...")

  find_path(
    netCDF_INCLUDE_DIR
    NAMES netcdf.h
    PATHS
      ${NetCDF_ROOT}
      $ENV{NetCDF_ROOT}
      $ENV{NETCDF_ROOT}
      $ENV{NETCDF_DIR}
      $ENV{NetCDF_DIR}
      $ENV{NETCDF_C_ROOT}
      $ENV{NetCDF_C_ROOT}
    PATH_SUFFIXES include
  )
  find_library(
    netCDF_LIBRARY
    NAMES netcdf
    PATHS
      ${NetCDF_ROOT}
      $ENV{NetCDF_ROOT}
      $ENV{NETCDF_ROOT}
      $ENV{NETCDF_DIR}
      $ENV{NetCDF_DIR}
      $ENV{NETCDF_C_ROOT}
      $ENV{NetCDF_C_ROOT}
    PATH_SUFFIXES lib lib64
  )

  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(netCDF DEFAULT_MSG netCDF_LIBRARY netCDF_INCLUDE_DIR)

  if(netCDF_FOUND)
    set(netCDF_INCLUDE_DIRS "${netCDF_INCLUDE_DIR}")
    set(netCDF_LIBRARIES "${netCDF_LIBRARY}")

    if(NOT TARGET netCDF::netcdf)
      add_library(netCDF::netcdf UNKNOWN IMPORTED GLOBAL)
      set_target_properties(
        netCDF::netcdf
        PROPERTIES
          IMPORTED_LOCATION "${netCDF_LIBRARY}"
          INTERFACE_INCLUDE_DIRECTORIES "${netCDF_INCLUDE_DIR}"
      )
    endif()

    cece_register_dep("NetCDF" "system library (${netCDF_LIBRARY})")
  else()
    message(
      FATAL_ERROR
      "Could not find NetCDF library or headers. Please set NetCDF_ROOT or NETCDF_ROOT."
    )
  endif()
else()
  if(NOT netCDF_INCLUDE_DIRS)
    set(netCDF_INCLUDE_DIRS "${netCDF_INCLUDE_DIR}")
  endif()
  cece_register_dep("NetCDF" "system package (${netCDF_DIR})")
endif()

# Discovery PIO and NetCDF from ESMF if possible
function(find_root_by_file root_var filename)
  if(NOT ${root_var})
    foreach(lib ${ESMF_LIBRARIES})
      if(lib MATCHES "^-L(.*)")
        set(search_path "${CMAKE_MATCH_1}")
        if(EXISTS "${search_path}/../include/${filename}")
          set(root "${search_path}/..")
          get_filename_component(root "${root}" ABSOLUTE)
          set(${root_var} "${root}" PARENT_SCOPE)
          message(STATUS "Found ${root_var} from ESMF path: ${root}")
          return()
        endif()
      endif()
    endforeach()
  endif()
endfunction()

find_root_by_file(NetCDF_ROOT "netcdf.h")
find_root_by_file(NetCDFF_ROOT "netcdf.mod")
find_root_by_file(PIO_ROOT "pio.h")

if(NetCDF_ROOT)
  set(NetCDF_FOUND TRUE)
  set(ENV{NetCDF_ROOT} "${NetCDF_ROOT}")
endif()
if(NetCDFF_ROOT)
  set(NetCDFF_FOUND TRUE)
  set(ENV{NetCDFF_ROOT} "${NetCDFF_ROOT}")
endif()
if(PIO_ROOT)
  set(PIO_FOUND TRUE)
  set(ENV{PIO_ROOT} "${PIO_ROOT}")
endif()

find_package(MPI REQUIRED)

# Container preflags for mpiexec/mpirun test launches, composed exactly once
# so every TEST_LAUNCHER and multi-rank add_test sees the same list.
# --allow-run-as-root exists for the container's root user and
# --oversubscribe for oversubscribed local/container CPUs — use-case flags,
# never needed on HPC compute nodes, and Intel MPI's hydra rejects
# --allow-run-as-root outright. Container entry points pass this ON;
# HPC/default configures stay clean.
option(
  CECE_MPIEXEC_CONTAINER_FLAGS
  "Append --allow-run-as-root/--oversubscribe to MPIEXEC_PREFLAGS for container/root/oversubscribed local runs (never needed on HPC)"
  OFF
)
if(CECE_MPIEXEC_CONTAINER_FLAGS)
  if(NOT "${MPIEXEC_PREFLAGS}" MATCHES "allow-run-as-root")
    list(APPEND MPIEXEC_PREFLAGS "--allow-run-as-root")
  endif()
  if(NOT "${MPIEXEC_PREFLAGS}" MATCHES "oversubscribe")
    list(APPEND MPIEXEC_PREFLAGS "--oversubscribe")
  endif()
endif()
message(STATUS "Found MPI: ${MPI_CXX_VERSION}")
cece_register_dep("MPI" "system (${MPI_CXX_COMPILER})")

# --- CECE Core Library ---

set(
  CECE_CORE_SRCS
  src/core/cece_band_decomposition.cpp
  src/core/cece_clock.cpp
  src/core/cece_config_parser.cpp
  src/core/cece_compute.cpp
  src/core/cece_stacking_engine.cpp
  src/core/cece_physics_factory.cpp
  src/core/cece_provenance.cpp
  src/core/physics/cece_native_example.cpp
  src/core/physics/cece_sea_salt.cpp
  src/core/physics/cece_megan.cpp
  src/core/physics/cece_dms.cpp
  src/core/physics/cece_lightning.cpp
  src/core/physics/cece_soil_nox.cpp
  src/core/physics/cece_dust.cpp
  src/core/physics/cece_fengsha.cpp
  src/core/physics/cece_ginoux.cpp
  src/core/physics/cece_k14.cpp
  src/core/physics/cece_volcano.cpp
  src/core/physics/cece_speciation_config.cpp
  src/core/physics/cece_speciation_engine.cpp
  src/core/physics/cece_canopy_model.cpp
  src/core/physics/cece_emission_activity.cpp
  src/core/physics/cece_megan3.cpp
  src/core/physics/cece_bdsnp.cpp
  src/core/cece_data_ingestor.cpp
  src/core/cece_diagnostic_manager.cpp
  src/core/cece_io.cpp
  src/core/cece_core_driver_config.cpp
  src/core/cece_core_advertise.cpp
  src/core/cece_core_realize.cpp
  src/core/cece_core_realize_wrapper.cpp
  src/core/cece_core_initialize_p1.cpp
  src/core/cece_core_initialize_p2.cpp
  src/core/cece_core_field_helpers.cpp
  src/core/cece_core_run.cpp
  src/core/cece_core_finalize.cpp
  src/core/cece_config_path.cpp
  src/core/cece_c_api.cpp
  src/core/cece_config_validator.cpp
  src/core/cece_run_info.cpp
)

if(CECE_HAS_FORTRAN)
  list(
    APPEND CECE_CORE_SRCS
    src/core/physics/cece_fortran_bridge.cpp
    src/core/physics/legacy_fortran.F90
    src/core/physics/cece_sea_salt_fortran_bridge.cpp
    src/core/physics/sea_salt_kernel.F90
    src/core/physics/cece_megan_fortran_bridge.cpp
    src/core/physics/megan_kernel.F90
    src/core/physics/cece_dms_fortran_bridge.cpp
    src/core/physics/dms_kernel.F90
    src/core/physics/cece_lightning_fortran_bridge.cpp
    src/core/physics/lightning_kernel.F90
    src/core/physics/cece_soil_nox_fortran_bridge.cpp
    src/core/physics/soil_nox_kernel.F90
    src/core/physics/cece_dust_fortran_bridge.cpp
    src/core/physics/dust_kernel.F90
    src/core/physics/cece_fengsha_fortran_bridge.cpp
    src/core/physics/fengsha_kernel.F90
    src/core/physics/cece_ginoux_fortran_bridge.cpp
    src/core/physics/ginoux_kernel.F90
    src/core/physics/cece_k14_fortran_bridge.cpp
    src/core/physics/k14_kernel.F90
    src/core/physics/cece_volcano_fortran_bridge.cpp
    src/core/physics/volcano_kernel.F90
    src/core/physics/cece_megan3_fortran_bridge.cpp
    src/core/physics/megan3_kernel.F90
    src/core/physics/cece_bdsnp_fortran_bridge.cpp
    src/core/physics/bdsnp_kernel.F90
    src/core/cece_ingestor_bridge.cpp
  )
endif()

add_library(cece_core SHARED ${CECE_CORE_SRCS})

if(CECE_HAS_FORTRAN)
  set_target_properties(
    cece_core
    PROPERTIES Fortran_MODULE_DIRECTORY ${CMAKE_BINARY_DIR}/fortran_modules/cece
  )
  target_include_directories(
    cece_core
    PUBLIC $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/fortran_modules/cece>
  )
  target_compile_definitions(cece_core PRIVATE CECE_HAS_FORTRAN)
endif()

target_include_directories(
  cece_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${ESMF_INCLUDE_DIRS}
)

# Suppress conflicting C++ MPI bindings pulled in via ESMF's ESMC.h -> mpi.h -> mpicxx.h
target_compile_definitions(cece_core PUBLIC MPICH_SKIP_MPICXX OMPI_SKIP_MPICXX)

if(ESMF_FOUND)
  target_link_libraries(cece_core PUBLIC Kokkos::kokkos ESMF::ESMF conf yaml-cpp)
else()
  target_link_libraries(cece_core PUBLIC Kokkos::kokkos conf yaml-cpp)
endif()

target_link_libraries(cece_core PUBLIC HELM::LOGS)

if(netCDF_FOUND)
  target_link_libraries(cece_core PUBLIC netCDF::netcdf)
  target_include_directories(cece_core PUBLIC ${netCDF_INCLUDE_DIRS})
endif()

if(CECE_HAS_FORTRAN AND NetCDFF_ROOT)
  target_include_directories(cece_core PRIVATE "${NetCDFF_ROOT}/include")
endif()

if(MPI_FOUND)
  target_link_libraries(cece_core PUBLIC MPI::MPI_CXX)
endif()

# Suppress conflicting C++ MPI bindings pulled in via ESMF's ESMC.h -> mpi.h -> mpicxx.h
target_compile_definitions(cece_core PUBLIC MPICH_SKIP_MPICXX OMPI_SKIP_MPICXX)

# --- cece_driver Library ---

add_library(
  cece_driver
  SHARED
  src/driver/cece_helm_graph.cpp
  src/driver/cece_regridder_utils.cpp
  src/driver/cece_standalone_writer.cpp
  src/driver/cece_core_writer_init.cpp
  src/driver/cece_amio_utils.cpp
  src/driver/cece_calendar.cpp
  src/driver/cece_time_indexing.cpp
  src/driver/cece_driver_facade.cpp
)

target_include_directories(
  cece_driver
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
)

target_link_libraries(
  cece_driver
  PUBLIC cece_core amio_core HELM::SPAN blend axis HELM::DAGR HELM::HALO HELM::TICK conf
)

# --- Backward-Compatibility target aliases ---
add_library(cece INTERFACE)
target_link_libraries(cece INTERFACE cece_core cece_driver)

# --- Install Rules ---
install(
  TARGETS cece_core cece_driver
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
)
install(DIRECTORY include/cece DESTINATION include)

# --- Tests ---

enable_testing()

if(POLICY CMP0178)
  # New behavior ignores TEST_LAUNCHER documentation about
  # semicolon-separated lists
  cmake_policy(SET CMP0178 OLD)
endif()

if(CECE_BUILD_TESTING)
  add_subdirectory(tests)
endif()

# Standalone NUOPC driver
if(ESMF_FOUND)
  add_subdirectory(src/driver/nuopc)
endif()

# --- Python Bindings ---

if(BUILD_PYTHON_BINDINGS)
  message(STATUS "Building Python bindings for CECE")
  add_subdirectory(src/python)
else()
  message(STATUS "Python bindings disabled. Enable with -DBUILD_PYTHON_BINDINGS=ON")
endif()

# --- Doxygen Documentation ---

find_package(Doxygen OPTIONAL_COMPONENTS dot)
if(DOXYGEN_FOUND)
  set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/docs/api)
  set(DOXYGEN_GENERATE_HTML YES)
  set(DOXYGEN_GENERATE_LATEX NO)
  set(DOXYGEN_GENERATE_MAN NO)
  set(DOXYGEN_HAVE_DOT ${DOXYGEN_dot_FOUND})
  set(DOXYGEN_DOT_NUM_THREADS 4)
  set(DOXYGEN_EXTRACT_ALL NO)
  set(DOXYGEN_EXTRACT_PRIVATE NO)
  set(DOXYGEN_EXTRACT_STATIC YES)
  set(DOXYGEN_SOURCE_BROWSER YES)
  set(DOXYGEN_GENERATE_TREEVIEW YES)
  set(DOXYGEN_TREEVIEW_WIDTH 250)
  set(DOXYGEN_CALL_GRAPH YES)
  set(DOXYGEN_CALLER_GRAPH YES)
  set(DOXYGEN_INCLUDE_GRAPH YES)
  set(DOXYGEN_INCLUDED_BY_GRAPH YES)
  set(DOXYGEN_DIRECTORY_GRAPH YES)
  set(DOXYGEN_CLASS_GRAPH YES)
  set(DOXYGEN_COLLABORATION_GRAPH YES)
  set(DOXYGEN_GROUP_GRAPHS YES)
  set(DOXYGEN_PREDEFINED "KOKKOS_LAMBDA=[]" "KOKKOS_FUNCTION=" "KOKKOS_INLINE_FUNCTION=inline")

  doxygen_add_docs(
    cece_docs
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    COMMENT "Generating CECE API documentation with Doxygen"
    ALL
  )
else()
  message(STATUS "Doxygen not found. API documentation generation disabled.")
  message(STATUS "Install Doxygen to enable: sudo apt-get install doxygen graphviz")
endif()

# --- Code Quality Checks ---

find_program(CLANG_FORMAT_EXECUTABLE clang-format)
if(CLANG_FORMAT_EXECUTABLE)
  file(
    GLOB_RECURSE CECE_FORMAT_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/include/cece/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/cece/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
  )

  add_custom_target(
    clang-format
    COMMAND ${CLANG_FORMAT_EXECUTABLE} -i ${CECE_FORMAT_FILES}
    COMMENT "Running clang-format on CECE source files"
  )

  add_custom_target(
    clang-format-check
    COMMAND ${CLANG_FORMAT_EXECUTABLE} --dry-run --Werror ${CECE_FORMAT_FILES}
    COMMENT "Checking clang-format compliance on CECE source files"
  )
else()
  message(STATUS "clang-format not found. Code formatting targets disabled.")
endif()

find_program(CLANG_TIDY_EXECUTABLE clang-tidy)
if(CLANG_TIDY_EXECUTABLE)
  file(GLOB_RECURSE CECE_TIDY_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

  set(_tidy_extra_args "")

  # Python sources require Python.h; exclude them unless bindings are built,
  # and if they are built pass the Python include dir so clang-tidy can find Python.h.
  if(NOT BUILD_PYTHON_BINDINGS)
    list(FILTER CECE_TIDY_FILES EXCLUDE REGEX ".*/src/python/.*")
  else()
    find_package(Python3 COMPONENTS Development QUIET)
    foreach(_py_inc ${Python3_INCLUDE_DIRS})
      list(APPEND _tidy_extra_args "--extra-arg=-I${_py_inc}")
    endforeach()
  endif()

  # clang-tidy uses clang's frontend, which can't find the host compiler's omp.h
  # automatically. Try GCC's -print-file-name first, fall back to a path search.
  execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=include
    OUTPUT_VARIABLE _gcc_include_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET # suppress noise from non-GCC compilers that don't support this flag
    RESULT_VARIABLE _gcc_print_result
  )
  if(_gcc_print_result EQUAL 0 AND EXISTS "${_gcc_include_dir}/omp.h")
    list(APPEND _tidy_extra_args "--extra-arg=-I${_gcc_include_dir}")
  else()
    # Fallback for Intel, Clang, or other compilers: search standard locations
    find_path(_omp_h_dir NAMES omp.h)
    if(_omp_h_dir)
      list(APPEND _tidy_extra_args "--extra-arg=-I${_omp_h_dir}")
    endif()
  endif()

  add_custom_target(
    clang-tidy
    COMMAND
      ${CLANG_TIDY_EXECUTABLE} -p ${CMAKE_BINARY_DIR}
      "--header-filter=^${CMAKE_CURRENT_SOURCE_DIR}/(include|src)/.*\\.(hpp|cpp|h)$$"
      ${_tidy_extra_args} ${CECE_TIDY_FILES}
    COMMENT "Running clang-tidy on CECE source files"
    VERBATIM
  )
else()
  message(STATUS "clang-tidy not found. Code analysis targets disabled.")
endif()

# --- Standalone Driver Executable ---
add_executable(cece_standalone_driver src/main.cpp)
target_link_libraries(
  cece_standalone_driver
  PRIVATE
    cece
    HELM::DAGR
    HELM::SPAN
    HELM::TICK
    AMIO::amio_core
    HELM::AXIS
    HELM::BLEND
    Kokkos::kokkos
    conf
    yaml-cpp
    MPI::MPI_CXX
)

# --- Dependency Resolution Summary ---
get_property(_cece_dep_summary GLOBAL PROPERTY CECE_DEP_SUMMARY)
message(STATUS "CECE external dependency resolution:")
foreach(_cece_dep_entry IN LISTS _cece_dep_summary)
  message(STATUS "  ${_cece_dep_entry}")
endforeach()
