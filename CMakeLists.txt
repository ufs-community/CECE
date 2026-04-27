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

list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Allow find_package to use <PackageName>_ROOT variables
if(POLICY CMP0074)
  cmake_policy(SET CMP0074 NEW)
endif()

# --- Dependencies ---

# NetCDF Discovery
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(NETCDF QUIET netcdf)
endif()

if(NOT NETCDF_FOUND)
  find_path(NetCDF_INCLUDE_DIR netcdf.h)
  find_library(NetCDF_LIBRARY netcdf)
  if(NetCDF_INCLUDE_DIR AND NetCDF_LIBRARY)
    set(NETCDF_FOUND TRUE)
    set(NETCDF_LIBRARIES ${NetCDF_LIBRARY})
    set(NETCDF_INCLUDE_DIRS ${NetCDF_INCLUDE_DIR})
  endif()
endif()

if(NETCDF_FOUND)
  message(STATUS "Found NetCDF: ${NETCDF_LIBRARIES}")
endif()

include(FetchContent)

# Kokkos - Configure execution spaces Default to Serial and OpenMP for CPU
# portability Users can override with -DKokkos_ENABLE_CUDA=ON or
# -DKokkos_ENABLE_HIP=ON for GPU support
if(NOT DEFINED Kokkos_ENABLE_SERIAL)
  set(Kokkos_ENABLE_SERIAL ON CACHE BOOL "Enable Kokkos Serial execution space" FORCE)
endif()

if(NOT DEFINED Kokkos_ENABLE_OPENMP)
  set(Kokkos_ENABLE_OPENMP ON CACHE BOOL "Enable Kokkos OpenMP execution space" FORCE)
endif()

# GPU support is optional - users must explicitly enable
option(Kokkos_ENABLE_CUDA "Enable Kokkos CUDA execution space for NVIDIA GPUs" OFF)
option(Kokkos_ENABLE_HIP "Enable Kokkos HIP execution space for AMD GPUs" OFF)

# Python bindings support
option(BUILD_PYTHON_BINDINGS "Build Python bindings for CECE" OFF)

# CCPP and NUOPC interface options
option(CECE_ENABLE_CCPP "Build CCPP interface for CECE" OFF)
option(CECE_ENABLE_NUOPC "Build NUOPC/ESMF interface for CECE" ON)

# Validate that at most one GPU backend is enabled
if(Kokkos_ENABLE_CUDA AND Kokkos_ENABLE_HIP)
  message(FATAL_ERROR "Cannot enable both CUDA and HIP. Choose one GPU backend or neither.")
endif()

# Log Kokkos configuration
message(STATUS "Kokkos Execution Spaces Configuration:")
message(STATUS "  Serial: ${Kokkos_ENABLE_SERIAL}")
message(STATUS "  OpenMP: ${Kokkos_ENABLE_OPENMP}")
message(STATUS "  CUDA: ${Kokkos_ENABLE_CUDA}")
message(STATUS "  HIP: ${Kokkos_ENABLE_HIP}")

FetchContent_Declare(kokkos GIT_REPOSITORY https://github.com/kokkos/kokkos.git GIT_TAG 4.2.00)
FetchContent_MakeAvailable(kokkos)

# YAML-CPP
FetchContent_Declare(yaml-cpp GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git GIT_TAG 0.8.0)
FetchContent_MakeAvailable(yaml-cpp)

# GTest
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# RapidCheck (Property-Based Testing)
FetchContent_Declare(
  rapidcheck
  GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
  GIT_TAG master
)
set(RC_ENABLE_GTEST ON CACHE BOOL "Enable RapidCheck GTest integration" FORCE)
FetchContent_MakeAvailable(rapidcheck)

# ESMF and NUOPC are required dependencies.
find_package(ESMF REQUIRED)
message(STATUS "Found ESMF: ${ESMF_VERSION}")
message(STATUS "ESMF include dirs: ${ESMF_INCLUDE_DIRS}")

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
      endforeach()
    endif()
  endfunction()

  find_root_by_file(NetCDF_ROOT "netcdf.h")
  find_root_by_file(NetCDFF_ROOT "netcdf.mod")
  find_root_by_file(PIO_ROOT "pio.h")

  if(NetCDF_ROOT)
    set(ENV{NetCDF_ROOT} "${NetCDF_ROOT}")
  endif()
  if(NetCDFF_ROOT)
    set(ENV{NetCDFF_ROOT} "${NetCDFF_ROOT}")
  endif()
  if(PIO_ROOT)
    set(ENV{PIO_ROOT} "${PIO_ROOT}")
  endif()
else()
  message(STATUS "NUOPC/ESMF interface disabled (CECE_ENABLE_NUOPC=OFF)")
endif()

find_package(MPI REQUIRED)
message(STATUS "Found MPI: ${MPI_CXX_VERSION}")

# --- CECE Library ---

set(
  CECE_SRCS
  src/cece_config_parser.cpp
  src/cece_compute.cpp
  src/cece_stacking_engine.cpp
  src/cece_physics_factory.cpp
  src/cece_provenance.cpp
  src/physics/cece_native_example.cpp
  src/physics/cece_sea_salt.cpp
  src/physics/cece_megan.cpp
  src/physics/cece_dms.cpp
  src/physics/cece_lightning.cpp
  src/physics/cece_soil_nox.cpp
  src/physics/cece_dust.cpp
  src/physics/cece_fengsha.cpp
  src/physics/cece_ginoux.cpp
  src/physics/cece_k14.cpp
  src/physics/cece_volcano.cpp
  src/io/cece_data_ingestor.cpp
  src/io/cece_diagnostic_manager.cpp
  src/io/cece_standalone_writer.cpp
  src/cece_core_writer_init.cpp
  src/cece_core_driver_config.cpp
  src/cece_core_advertise.cpp
  src/cece_core_realize.cpp
  src/cece_core_realize_wrapper.cpp
  src/cece_core_initialize_p1.cpp
  src/cece_core_initialize_p2.cpp
  src/cece_core_field_helpers.cpp
  src/cece_core_run.cpp
  src/cece_core_finalize.cpp
  src/cece_config_path.cpp
  src/cece_c_api.cpp
  src/cece_config_validator.cpp
)

if(CECE_HAS_FORTRAN)
  # tide library requires ESMF — only build when NUOPC is enabled
  if(CECE_ENABLE_NUOPC)
    add_subdirectory(src/io/tide)
  endif()

  list(
    APPEND CECE_SRCS
    src/cece_cap.F90
    src/physics/cece_fortran_bridge.cpp
    src/physics/legacy_fortran.F90
    src/physics/cece_sea_salt_fortran_bridge.cpp
    src/physics/sea_salt_kernel.F90
    src/physics/cece_megan_fortran_bridge.cpp
    src/physics/megan_kernel.F90
    src/physics/cece_dms_fortran_bridge.cpp
    src/physics/dms_kernel.F90
    src/physics/cece_lightning_fortran_bridge.cpp
    src/physics/lightning_kernel.F90
    src/physics/cece_soil_nox_fortran_bridge.cpp
    src/physics/soil_nox_kernel.F90
    src/physics/cece_dust_fortran_bridge.cpp
    src/physics/dust_kernel.F90
    src/physics/cece_fengsha_fortran_bridge.cpp
    src/physics/fengsha_kernel.F90
    src/physics/cece_ginoux_fortran_bridge.cpp
    src/physics/ginoux_kernel.F90
    src/physics/cece_k14_fortran_bridge.cpp
    src/physics/k14_kernel.F90
    src/physics/cece_volcano_fortran_bridge.cpp
    src/physics/volcano_kernel.F90)

  # ESMF-dependent sources — only when NUOPC interface is enabled
  if(CECE_ENABLE_NUOPC)
    list(APPEND CECE_SRCS src/cece_cap.F90 src/cece_ingestor_bridge.cpp)
  endif()
endif()

# CCPP interface sources — when CCPP is enabled (Req 10.2)
if(CECE_ENABLE_CCPP)
  message(STATUS "CCPP interface enabled")
  # C++ API can be compiled regardless of Fortran availability
  list(APPEND CECE_SRCS src/ccpp/cece_ccpp_api.cpp)

  # Fortran CCPP driver schemes require Fortran compiler
  if(CECE_HAS_FORTRAN)
    list(
      APPEND
      CECE_SRCS
      src/ccpp/cece_ccpp_state.F90
      src/ccpp/cece_ccpp_sea_salt.F90
      src/ccpp/cece_ccpp_megan.F90
      src/ccpp/cece_ccpp_dust.F90
      src/ccpp/cece_ccpp_dms.F90
      src/ccpp/cece_ccpp_lightning.F90
      src/ccpp/cece_ccpp_soil_nox.F90
      src/ccpp/cece_ccpp_volcano.F90
      src/ccpp/cece_ccpp_example.F90
      src/ccpp/cece_ccpp_stacking.F90)
  else()
    message(STATUS "CCPP Fortran driver schemes disabled (no Fortran compiler)")
  endif()
endif()

add_library(cece SHARED ${CECE_SRCS})

if(CECE_HAS_FORTRAN)
  set_target_properties(
    cece
    PROPERTIES Fortran_MODULE_DIRECTORY ${CMAKE_BINARY_DIR}/fortran_modules/cece
  )
  target_include_directories(
    cece
    PUBLIC $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/fortran_modules/cece>
  )
  target_compile_definitions(cece PRIVATE CECE_HAS_FORTRAN)
endif()

if(CECE_ENABLE_CCPP)
  target_compile_definitions(cece PRIVATE CECE_ENABLE_CCPP)
endif()
target_include_directories(
  cece
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${ESMF_INCLUDE_DIRS}
)

if(CECE_ENABLE_NUOPC)
  target_include_directories(
    cece PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                $<INSTALL_INTERFACE:include> ${ESMF_INCLUDE_DIRS})
  target_link_libraries(cece PUBLIC Kokkos::kokkos ESMF::ESMF yaml-cpp)
else()
  target_include_directories(
    cece PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                $<INSTALL_INTERFACE:include>)
  target_link_libraries(cece PUBLIC Kokkos::kokkos yaml-cpp)
endif()

if(CECE_HAS_FORTRAN AND CECE_ENABLE_NUOPC)
  target_link_libraries(cece PUBLIC tide)
endif()

if(NETCDF_FOUND)
  target_link_libraries(cece PUBLIC ${NETCDF_LIBRARIES})
  target_include_directories(cece PUBLIC ${NETCDF_INCLUDE_DIRS})
  target_compile_definitions(cece PRIVATE CECE_HAS_NETCDF)
endif()

if(MPI_FOUND)
  target_link_libraries(cece PUBLIC MPI::MPI_CXX)
endif()

# --- Tests ---

enable_testing()

if(CECE_ENABLE_NUOPC)
  add_executable(test_driver_configuration tests/test_driver_configuration.cpp)
  target_link_libraries(test_driver_configuration PRIVATE cece
                                                          GTest::gtest_main)

add_executable(test_driver_config_validation tests/test_driver_config_validation.cpp)
target_link_libraries(test_driver_config_validation PRIVATE cece iso8601_utils GTest::gtest_main)

add_executable(test_driver_clock_management tests/test_driver_clock_management.cpp)
target_link_libraries(test_driver_clock_management PRIVATE GTest::gtest_main)

add_executable(test_clock_operations_unit tests/test_clock_operations_unit.cpp)
target_link_libraries(test_clock_operations_unit PRIVATE GTest::gtest_main)

add_executable(test_driver_integration_end_to_end tests/test_driver_integration_end_to_end.cpp)
target_link_libraries(test_driver_integration_end_to_end PRIVATE GTest::gtest_main)

if(CECE_ENABLE_NUOPC)
  add_executable(test_cece_cap tests/test_cece_cap.cpp)
  target_link_libraries(test_cece_cap PRIVATE cece GTest::gtest_main)

  add_executable(test_diagnostics tests/test_diagnostics.cpp)
  target_link_libraries(test_diagnostics PRIVATE cece GTest::gtest)
endif()

add_executable(test_cece_ingestor tests/test_cece_ingestor.cpp)
target_link_libraries(test_cece_ingestor PRIVATE cece GTest::gtest_main)

add_executable(test_cece_utils tests/test_cece_utils.cpp)
target_link_libraries(test_cece_utils PRIVATE cece GTest::gtest_main)

add_executable(test_cece_compute tests/test_cece_compute.cpp)
target_link_libraries(test_cece_compute PRIVATE cece GTest::gtest_main)

add_executable(test_physics tests/test_physics.cpp)
target_link_libraries(test_physics PRIVATE cece GTest::gtest_main)

add_executable(test_hemco_mimic tests/test_hemco_mimic.cpp)
target_link_libraries(test_hemco_mimic PRIVATE cece GTest::gtest_main)

add_executable(test_stacking_engine tests/test_stacking_engine.cpp)
target_link_libraries(test_stacking_engine PRIVATE cece GTest::gtest_main)

add_executable(test_vertical_distribution_single tests/test_vertical_distribution_single.cpp)
target_link_libraries(test_vertical_distribution_single PRIVATE cece GTest::gtest_main)

add_executable(test_field_resolution_priority tests/test_field_resolution_priority.cpp)
target_link_libraries(test_field_resolution_priority PRIVATE cece GTest::gtest_main)

if(CECE_HAS_FORTRAN AND CECE_ENABLE_NUOPC)
  add_library(test_helpers STATIC tests/test_helpers.F90)
  target_include_directories(test_helpers PUBLIC ${ESMF_INCLUDE_DIRS})
  target_link_libraries(test_helpers PUBLIC ESMF::ESMF)
endif()

add_executable(hemco_comparison_driver tests/hemco_comparison_driver.cpp)
target_link_libraries(hemco_comparison_driver PRIVATE cece)
add_test(NAME test_hemco_comparison COMMAND hemco_comparison_driver)

add_executable(benchmark_cece_hemco tests/benchmark_cece_hemco.cpp)
target_link_libraries(benchmark_cece_hemco PRIVATE cece)

add_executable(test_hemco_parity tests/test_hemco_parity.cpp)
target_link_libraries(test_hemco_parity PRIVATE cece GTest::gtest_main)

# Kokkos Configuration Tests (Requirement 6.13-6.18, 6.21)
add_executable(test_kokkos_configuration tests/test_kokkos_configuration.cpp)
target_link_libraries(test_kokkos_configuration PRIVATE cece GTest::gtest_main)

# Comprehensive Property-Based Test Suite (Task 11.1)
add_executable(test_properties tests/test_properties.cpp)
target_link_libraries(test_properties PRIVATE cece GTest::gtest_main)

# CCPP Property-Based Tests (RapidCheck + GTest) — only when CCPP is enabled
if(CECE_ENABLE_CCPP)
  add_executable(test_ccpp_pbt tests/test_ccpp_pbt.cpp)
  target_link_libraries(
    test_ccpp_pbt PRIVATE cece GTest::gtest GTest::gtest_main rapidcheck
                          rapidcheck_gtest)
endif()

# Test Suite Idempotence (Requirement 8.16)
add_executable(test_suite_idempotence tests/test_suite_idempotence.cpp)
target_link_libraries(test_suite_idempotence PRIVATE cece GTest::gtest_main)

# Temporary Reproduction Test
add_executable(test_repro_failure tests/repro_failure.cpp)
target_link_libraries(test_repro_failure PRIVATE cece GTest::gtest_main)

# Unit tests for field creation (Requirement R4) FENGSHA Dust Scheme Property
# Tests
add_executable(test_fengsha_properties tests/test_fengsha_properties.cpp)
target_link_libraries(test_fengsha_properties PRIVATE cece GTest::gtest_main)

# Ginoux (GOCART2G) Dust Scheme Property Tests
add_executable(test_ginoux_properties tests/test_ginoux_properties.cpp)
target_link_libraries(test_ginoux_properties PRIVATE cece GTest::gtest_main)

# K14 Dust Scheme Property Tests
add_executable(test_k14_properties tests/test_k14_properties.cpp)
target_link_libraries(test_k14_properties PRIVATE cece GTest::gtest_main)

if(CECE_ENABLE_NUOPC)
  add_executable(test_field_creation tests/test_field_creation.cpp)
  if(CECE_HAS_FORTRAN AND TARGET test_helpers)
    target_link_libraries(test_field_creation PRIVATE cece test_helpers
                                                      GTest::gtest_main)
  else()
    target_link_libraries(test_field_creation PRIVATE cece GTest::gtest_main)
  endif()

  # Unit tests for field pointer passing (Requirement R5)
  add_executable(test_field_pointer_passing
                 tests/test_field_pointer_passing.cpp)
  if(CECE_HAS_FORTRAN AND TARGET test_helpers)
    target_link_libraries(test_field_pointer_passing PRIVATE cece test_helpers
                                                             GTest::gtest_main)
  else()
    target_link_libraries(test_field_pointer_passing PRIVATE cece
                                                             GTest::gtest_main)
  endif()
endif()

# Standalone NUOPC driver — only when NUOPC interface is enabled
if(CECE_ENABLE_NUOPC)
  add_subdirectory(standalone_nuopc)
endif()

# Python script tests
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(
    NAME install_pytest
    COMMAND ${Python3_EXECUTABLE} -m pip install pytest --quiet
  )
  set_tests_properties(install_pytest PROPERTIES FIXTURES_SETUP "pytest_installed")

  add_test(
    NAME test_hemco_to_cece_python
    COMMAND ${Python3_EXECUTABLE} -m pytest ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_converters.py
  )
  set_tests_properties(
    test_hemco_to_cece_python
    PROPERTIES
      ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts"
      FIXTURES_REQUIRED "pytest_installed"
  )

  add_test(
    NAME test_scripts_python
    COMMAND ${Python3_EXECUTABLE} -m pytest ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_scripts.py
  )
  set_tests_properties(
    test_scripts_python
    PROPERTIES
      ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts"
      FIXTURES_REQUIRED "pytest_installed"
  )

  if(BUILD_PYTHON_BINDINGS)
    add_test(
      NAME test_enum_config_bindings
      COMMAND
        ${Python3_EXECUTABLE} -m pytest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_enum_config_bindings.py -v
    )
    set_tests_properties(
      test_enum_config_bindings
      PROPERTIES
        ENVIRONMENT "PYTHONPATH=${CMAKE_BINARY_DIR}/src/python/cece"
        FIXTURES_REQUIRED "pytest_installed"
    )

    add_test(
      NAME test_state_bindings
      COMMAND
        ${Python3_EXECUTABLE} -m pytest ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_state_bindings.py -v
    )
    set_tests_properties(
      test_state_bindings
      PROPERTIES
        ENVIRONMENT "PYTHONPATH=${CMAKE_BINARY_DIR}/src/python/cece"
        FIXTURES_REQUIRED "pytest_installed"
    )

    add_test(
      NAME test_compute_bindings
      COMMAND
        ${Python3_EXECUTABLE} -m pytest ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_compute_bindings.py
        -v
    )
    set_tests_properties(
      test_compute_bindings
      PROPERTIES
        ENVIRONMENT "PYTHONPATH=${CMAKE_BINARY_DIR}/src/python/cece"
        FIXTURES_REQUIRED "pytest_installed"
    )

    add_test(
      NAME test_validator_logger_bindings
      COMMAND
        ${Python3_EXECUTABLE} -m pytest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_validator_logger_bindings.py -v
    )
    set_tests_properties(
      test_validator_logger_bindings
      PROPERTIES
        ENVIRONMENT "PYTHONPATH=${CMAKE_BINARY_DIR}/src/python/cece"
        FIXTURES_REQUIRED "pytest_installed"
    )

    add_test(
      NAME test_python_api_rewired
      COMMAND
        ${Python3_EXECUTABLE} -m pytest ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_python_api_rewired.py
        -v
    )
    set_tests_properties(
      test_python_api_rewired
      PROPERTIES
        ENVIRONMENT "PYTHONPATH=${CMAKE_BINARY_DIR}/src/python"
        FIXTURES_REQUIRED "pytest_installed"
    )
  endif()
endif()

include(GoogleTest)
if(CECE_ENABLE_NUOPC)
  gtest_discover_tests(test_driver_configuration)
  gtest_discover_tests(test_driver_config_validation)
endif()
gtest_discover_tests(test_driver_clock_management)
gtest_discover_tests(test_clock_operations_unit)
gtest_discover_tests(test_driver_integration_end_to_end)
if(CECE_ENABLE_NUOPC)
  gtest_discover_tests(test_cece_cap)
endif()
gtest_discover_tests(test_stacking_engine)
gtest_discover_tests(test_vertical_distribution_single)
gtest_discover_tests(test_cece_utils)
gtest_discover_tests(test_cece_compute)
gtest_discover_tests(test_physics)
gtest_discover_tests(test_cece_ingestor)
if(CECE_ENABLE_NUOPC)
  gtest_discover_tests(test_diagnostics)
endif()
gtest_discover_tests(test_hemco_mimic)
gtest_discover_tests(test_field_resolution_priority)
gtest_discover_tests(test_hemco_parity)
gtest_discover_tests(test_kokkos_configuration)
gtest_discover_tests(test_properties)
if(CECE_ENABLE_CCPP)
  gtest_discover_tests(test_ccpp_pbt)
endif()
gtest_discover_tests(test_suite_idempotence)
gtest_discover_tests(test_repro_failure)
gtest_discover_tests(test_fengsha_properties)
gtest_discover_tests(test_ginoux_properties)
gtest_discover_tests(test_k14_properties)
if(CECE_ENABLE_NUOPC)
  gtest_discover_tests(test_field_creation)
  gtest_discover_tests(test_field_pointer_passing)
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
    docs
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    COMMENT "Generating API documentation with Doxygen"
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
