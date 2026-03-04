cmake_minimum_required(VERSION 3.20)
project(ACES LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Allow find_package to use <PackageName>_ROOT variables
if(POLICY CMP0074)
  cmake_policy(SET CMP0074 NEW)
endif()

# --- Dependencies ---

include(FetchContent)

# Kokkos
FetchContent_Declare(
  kokkos
  GIT_REPOSITORY https://github.com/kokkos/kokkos.git
  GIT_TAG        4.2.00
)
FetchContent_MakeAvailable(kokkos)

# YAML-CPP
FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG        0.8.0
)
FetchContent_MakeAvailable(yaml-cpp)

# GTest
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Optional Fortran support
include(CheckLanguage)
check_language(Fortran)
if(CMAKE_Fortran_COMPILER)
  enable_language(Fortran)
  set(ACES_HAS_FORTRAN ON)
else()
  message(WARNING "Fortran compiler not found. Fortran features will be disabled.")
endif()

# ESMF and NUOPC are required dependencies.
# NOTE: Mocking ESMF/NUOPC is strictly forbidden to ensure real-world parity.
find_package(ESMF REQUIRED)
message(STATUS "Found ESMF: ${ESMF_VERSION}")
message(STATUS "ESMF include dirs: ${ESMF_INCLUDE_DIRS}")

# Discovery PIO and NetCDF from ESMF if possible
# CDEPS needs these as dependencies for its Fortran modules

# Helper function to find a library root based on a file existence
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
message(STATUS "Found MPI: ${MPI_CXX_VERSION}")

# CDEPS is a required dependency for the hybrid ingestion layer.
find_package(CDEPS QUIET)

if(NOT CDEPS_FOUND AND ACES_HAS_FORTRAN)
  message(STATUS "CDEPS not found on system, attempting to build from source using FetchContent...")

  # Discovery PIO from ESMF path if not already found (needed for CDEPS)
  if(NOT PIO_FOUND)
      find_package(PIO QUIET)
  endif()

  if(PIO_FOUND)
    # Export PIO paths for CDEPS internal find_package(PIO)
    set(PIO_PATH "${PIO_ROOT}")
    set(ENV{PIO_PATH} "${PIO_ROOT}")
    set(ENV{PIO_ROOT} "${PIO_ROOT}")

    FetchContent_Declare(
      cdeps
      GIT_REPOSITORY https://github.com/bbakernoaa/CDEPS.git
      GIT_TAG        ae5d2c457321ebae3a0d9c9c949f871bb8f2b09c
      GIT_SUBMODULES_RECURSE ON
      PATCH_COMMAND  sed -i "s/add_subdirectory(fox)/#add_subdirectory(fox)/" CMakeLists.txt
    )
    # Explicitly set PIO and NetCDF paths for CDEPS
    set(PIO_Fortran_INCLUDE_DIR "${PIO_ROOT}/include" CACHE PATH "" FORCE)
    set(PIO_C_INCLUDE_DIR "${PIO_ROOT}/include" CACHE PATH "" FORCE)
    set(NetCDF_Fortran_INCLUDE_DIR "${NetCDFF_ROOT}/include" CACHE PATH "" FORCE)
    set(NetCDF_C_INCLUDE_DIR "${NetCDF_ROOT}/include" CACHE PATH "" FORCE)

    # Force CDEPS to find the ESMF we already discovered
    # In JCSDA container, ESMF_ROOT might not be set, but ESMF_LIBRARIES contains it
    if(NOT ESMF_ROOT)
        foreach(lib ${ESMF_LIBRARIES})
            if(lib MATCHES "^-L(.*)")
                set(search_path "${CMAKE_MATCH_1}")
                if(EXISTS "${search_path}/../include/esmf.mod")
                    set(ESMF_ROOT "${search_path}/..")
                    get_filename_component(ESMF_ROOT "${ESMF_ROOT}" ABSOLUTE)
                    break()
                endif()
            endif()
        endforeach()
    endif()
    set(ESMF_ROOT "${ESMF_ROOT}" CACHE PATH "Path to ESMF" FORCE)
    set(ENV{ESMF_ROOT} "${ESMF_ROOT}")

    # Propagate ESMF include dirs to CDEPS if it uses ESMF_F90COMPILEPATHS
    set(ESMF_F90COMPILEPATHS "${ESMF_INCLUDE_DIRS}" CACHE STRING "ESMF include paths for CDEPS" FORCE)

    # Force CDEPS to use a larger line length for Fortran and define CPRGNU
    set(CMAKE_Fortran_FLAGS "${CMAKE_Fortran_FLAGS} -ffree-line-length-none -DCPRGNU" CACHE STRING "Fortran flags" FORCE)

    # Disable FoX in CDEPS
    set(DISABLE_FoX ON CACHE BOOL "Disable FoX library" FORCE)
    set(COMPILER "gnu" CACHE STRING "Compiler type")

    # We use a custom guard to ensure CDEPS is correctly integrated
    FetchContent_MakeAvailable(cdeps)
    if(TARGET dshr)
      set(CDEPS_FOUND TRUE)
      message(STATUS "CDEPS built from source successfully.")
    endif()
  endif()
endif()

# Fallback: if not found and not built from source, it's an error.
if(NOT CDEPS_FOUND)
  message(FATAL_ERROR "CDEPS is required but was not found on the system or successfully built from source.")
endif()

# --- ACES Library ---

set(ACES_SRCS
  src/aces_cap.cpp
  src/aces_config_parser.cpp
  src/aces_compute.cpp
  src/aces_stacking_engine.cpp
  src/aces_physics_factory.cpp
  src/physics/aces_native_example.cpp
  src/physics/aces_sea_salt.cpp
  src/physics/aces_megan.cpp
  src/physics/aces_dms.cpp
  src/physics/aces_lightning.cpp
  src/physics/aces_soil_nox.cpp
  src/physics/aces_dust.cpp
  src/physics/aces_volcano.cpp
  src/io/aces_data_ingestor.cpp
  src/io/aces_diagnostic_writer.cpp
)

if(ACES_HAS_FORTRAN)
  list(APPEND ACES_SRCS
    src/physics/aces_fortran_bridge.cpp
    src/physics/legacy_fortran.F90
    src/io/aces_cdeps_bridge.F90
    src/physics/aces_sea_salt_fortran_bridge.cpp
    src/physics/sea_salt_kernel.F90
    src/physics/aces_megan_fortran_bridge.cpp
    src/physics/megan_kernel.F90
    src/physics/aces_dms_fortran_bridge.cpp
    src/physics/dms_kernel.F90
    src/physics/aces_lightning_fortran_bridge.cpp
    src/physics/lightning_kernel.F90
    src/physics/aces_soil_nox_fortran_bridge.cpp
    src/physics/soil_nox_kernel.F90
    src/physics/aces_dust_fortran_bridge.cpp
    src/physics/dust_kernel.F90
    src/physics/aces_volcano_fortran_bridge.cpp
    src/physics/volcano_kernel.F90
  )
endif()

add_library(aces SHARED ${ACES_SRCS})

if(ACES_HAS_FORTRAN)
  target_compile_definitions(aces PRIVATE ACES_HAS_FORTRAN)
endif()

target_include_directories(aces PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(aces PUBLIC
  Kokkos::kokkos
  ESMF::ESMF
  yaml-cpp
)

if(CDEPS_FOUND)
  target_link_libraries(aces PUBLIC dshr streams cdeps_share)
  # CDEPS dependencies (NetCDF and PIO) discovered from ESMF
  if(PIO_FOUND)
    if(EXISTS "${PIO_ROOT}/lib/libpiof.so")
      target_link_libraries(aces PUBLIC "${PIO_ROOT}/lib/libpiof.so")
    endif()
    if(EXISTS "${PIO_ROOT}/lib/libpioc.so")
      target_link_libraries(aces PUBLIC "${PIO_ROOT}/lib/libpioc.so")
    endif()
  endif()
  if(NetCDFF_FOUND)
    if(EXISTS "${NetCDFF_ROOT}/lib/libnetcdff.so")
      target_link_libraries(aces PUBLIC "${NetCDFF_ROOT}/lib/libnetcdff.so")
    endif()
  endif()
  if(NetCDF_ROOT)
    if(EXISTS "${NetCDF_ROOT}/lib/libnetcdf.so")
      target_link_libraries(aces PUBLIC "${NetCDF_ROOT}/lib/libnetcdf.so")
    endif()
  endif()
endif()

if(MPI_FOUND)
    target_link_libraries(aces PUBLIC MPI::MPI_CXX)
endif()

# --- Tests ---

enable_testing()

add_executable(test_aces_cap
  tests/test_aces_cap.cpp
)

target_link_libraries(test_aces_cap
  PRIVATE
  aces
  GTest::gtest_main
)

add_executable(test_diagnostics
  tests/test_diagnostics.cpp
)

target_link_libraries(test_diagnostics
  PRIVATE
  aces
  GTest::gtest
)

add_executable(test_aces_ingestor
  tests/test_aces_ingestor.cpp
)

target_link_libraries(test_aces_ingestor
  PRIVATE
  aces
  GTest::gtest_main
)

add_executable(test_aces_utils
  tests/test_aces_utils.cpp
)

target_link_libraries(test_aces_utils
  PRIVATE
  aces
  GTest::gtest_main
)

add_executable(test_aces_compute
  tests/test_aces_compute.cpp
)

target_link_libraries(test_aces_compute
  PRIVATE
  aces
  GTest::gtest_main
)

add_executable(test_physics
  tests/test_physics.cpp
)

target_link_libraries(test_physics
  PRIVATE
  aces
  GTest::gtest_main
)

add_executable(test_hemco_mimic
  tests/test_hemco_mimic.cpp
)

target_link_libraries(test_hemco_mimic
  PRIVATE
  aces
  GTest::gtest_main
)

add_executable(test_stacking_engine
  tests/test_stacking_engine.cpp
)

target_link_libraries(test_stacking_engine
  PRIVATE
  aces
  GTest::gtest_main
)

add_executable(example_driver
  tests/example_driver.cpp
)

target_link_libraries(example_driver
  PRIVATE
  aces
)

add_executable(hemco_mimic_driver
  tests/hemco_mimic_driver.cpp
)

target_link_libraries(hemco_mimic_driver
  PRIVATE
  aces
)

# Standalone NUOPC driver
add_subdirectory(standalone_nuopc)

# Python script tests
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME test_hemco_to_aces_python
           COMMAND ${Python3_EXECUTABLE} -m pytest ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_converters.py)
  set_tests_properties(test_hemco_to_aces_python PROPERTIES ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts")

  add_test(NAME test_scripts_python
           COMMAND ${Python3_EXECUTABLE} -m pytest ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_scripts.py)
  set_tests_properties(test_scripts_python PROPERTIES ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts")
endif()

include(GoogleTest)
gtest_discover_tests(test_aces_cap)
gtest_discover_tests(test_stacking_engine)
gtest_discover_tests(test_aces_utils)
gtest_discover_tests(test_aces_compute)
gtest_discover_tests(test_physics)
gtest_discover_tests(test_aces_ingestor)
gtest_discover_tests(test_diagnostics)
gtest_discover_tests(test_hemco_mimic)
