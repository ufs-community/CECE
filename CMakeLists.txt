cmake_minimum_required(VERSION 3.20)
project(ACES LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Optional Fortran support
include(CheckLanguage)
check_language(Fortran)
if(CMAKE_Fortran_COMPILER)
    enable_language(Fortran)
    set(ACES_HAS_FORTRAN ON)

    # Global module directory prevents race conditions and lookup errors during parallel builds
    # set(CMAKE_Fortran_MODULE_DIRECTORY ${CMAKE_BINARY_DIR}/fortran_modules)

    # file(MAKE_DIRECTORY ${CMAKE_Fortran_MODULE_DIRECTORY})

    # TIDE and PIO sometimes expect a share directory in the build root

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

include(FetchContent)

# Kokkos - Configure execution spaces
# Default to Serial and OpenMP for CPU portability
# Users can override with -DKokkos_ENABLE_CUDA=ON or -DKokkos_ENABLE_HIP=ON for GPU support
if(NOT DEFINED Kokkos_ENABLE_SERIAL)
    set(Kokkos_ENABLE_SERIAL ON CACHE BOOL "Enable Kokkos Serial execution space" FORCE)
endif()

if(NOT DEFINED Kokkos_ENABLE_OPENMP)
    set(Kokkos_ENABLE_OPENMP ON CACHE BOOL "Enable Kokkos OpenMP execution space" FORCE)
endif()

# GPU support is optional - users must explicitly enable
option(Kokkos_ENABLE_CUDA "Enable Kokkos CUDA execution space for NVIDIA GPUs" OFF)
option(Kokkos_ENABLE_HIP "Enable Kokkos HIP execution space for AMD GPUs" OFF)

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

FetchContent_Declare(
    kokkos
    GIT_REPOSITORY https://github.com/kokkos/kokkos.git
    GIT_TAG 4.2.00)
FetchContent_MakeAvailable(kokkos)

# YAML-CPP
FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG 0.8.0)
FetchContent_MakeAvailable(yaml-cpp)

# GTest
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt
    ON
    CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# RapidCheck (Property-Based Testing)
FetchContent_Declare(
    rapidcheck
    GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
    GIT_TAG        master
)
set(RC_ENABLE_GTEST ON CACHE BOOL "Enable RapidCheck GTest integration" FORCE)
FetchContent_MakeAvailable(rapidcheck)

# ESMF and NUOPC are required dependencies.
# NOTE: Mocking ESMF/NUOPC is strictly forbidden to ensure real-world parity.
find_package(ESMF REQUIRED)
message(STATUS "Found ESMF: ${ESMF_VERSION}")
message(STATUS "ESMF include dirs: ${ESMF_INCLUDE_DIRS}")

# Discovery PIO and NetCDF from ESMF if possible
# TIDE needs these as dependencies for its Fortran modules

# Helper function to find a library root based on a file existence
function(find_root_by_file root_var filename)
    if(NOT ${root_var})
        foreach(lib ${ESMF_LIBRARIES})
            if(lib MATCHES "^-L(.*)")
                set(search_path "${CMAKE_MATCH_1}")
                if(EXISTS "${search_path}/../include/${filename}")
                    set(root "${search_path}/..")
                    get_filename_component(root "${root}" ABSOLUTE)
                    set(${root_var}
                        "${root}"
                        PARENT_SCOPE)
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

# --- ACES Library ---

set(ACES_SRCS
    src/aces_config_parser.cpp
    src/aces_compute.cpp
    src/aces_stacking_engine.cpp
    src/aces_physics_factory.cpp
    src/aces_provenance.cpp
    src/physics/aces_native_example.cpp
    src/physics/aces_sea_salt.cpp
    src/physics/aces_megan.cpp
    src/physics/aces_dms.cpp
    src/physics/aces_lightning.cpp
    src/physics/aces_soil_nox.cpp
    src/physics/aces_dust.cpp
    src/physics/aces_volcano.cpp
    src/io/aces_data_ingestor.cpp
    src/io/aces_diagnostic_manager.cpp
    src/io/aces_standalone_writer.cpp
    src/aces_core_writer_init.cpp
    src/aces_core_driver_config.cpp
    src/aces_core_advertise.cpp
    src/aces_core_realize.cpp
    src/aces_core_realize_wrapper.cpp
    src/aces_core_initialize_p1.cpp
    src/aces_core_initialize_p2.cpp
    src/aces_core_initialize_p2_wrapper.cpp
    src/aces_core_field_helpers.cpp
    src/aces_core_run.cpp
    src/aces_core_finalize.cpp
    src/aces_config_path.cpp)

if(ACES_HAS_FORTRAN)
    add_subdirectory(src/io/tide)
    # TODO: Seperate aces_cap from core physics build. Physics should be included in the core. TIDE and the cap belong in an interface library that includes the core library. 
    list(
        APPEND
        ACES_SRCS
        src/aces_cap.F90
        src/physics/aces_fortran_bridge.cpp
        src/physics/legacy_fortran.F90
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
        src/aces_ingestor_bridge.cpp)
endif()

add_library(aces SHARED ${ACES_SRCS})

if(ACES_HAS_FORTRAN)
    # Set a target-specific module directory for aces to allow consumers to find it
    set_target_properties(aces PROPERTIES Fortran_MODULE_DIRECTORY ${CMAKE_BINARY_DIR}/fortran_modules/aces)
    target_include_directories(aces PUBLIC $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/fortran_modules/aces>)

    target_compile_definitions(aces PRIVATE ACES_HAS_FORTRAN)

    # target_include_directories(aces PUBLIC $<BUILD_INTERFACE:${CMAKE_Fortran_MODULE_DIRECTORY}>)
endif()

target_include_directories(
    aces
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
           ${ESMF_INCLUDE_DIRS})

target_link_libraries(aces PUBLIC Kokkos::kokkos ESMF::ESMF yaml-cpp)

if(ACES_HAS_FORTRAN)
    target_link_libraries(aces PUBLIC tide)
endif()

# Link NetCDF for TIDE
if(NetCDF_ROOT)
    if(EXISTS "${NetCDF_ROOT}/lib/libnetcdf.so")
        target_link_libraries(aces PUBLIC "${NetCDF_ROOT}/lib/libnetcdf.so")
    endif()
endif()

if(MPI_FOUND)
    target_link_libraries(aces PUBLIC MPI::MPI_CXX)
endif()

# --- Tests ---

enable_testing()

add_executable(test_driver_configuration tests/test_driver_configuration.cpp)
target_link_libraries(test_driver_configuration PRIVATE aces GTest::gtest_main)

add_executable(test_driver_config_validation tests/test_driver_config_validation.cpp)
target_link_libraries(test_driver_config_validation PRIVATE aces iso8601_utils GTest::gtest_main)

add_executable(test_driver_clock_management tests/test_driver_clock_management.cpp)
target_link_libraries(test_driver_clock_management PRIVATE GTest::gtest_main)

# Unit tests for clock operations (Requirements 4.1, 5.1, 7.1)
add_executable(test_clock_operations_unit tests/test_clock_operations_unit.cpp)
target_link_libraries(test_clock_operations_unit PRIVATE GTest::gtest_main)

add_executable(test_driver_integration_end_to_end tests/test_driver_integration_end_to_end.cpp)
target_link_libraries(test_driver_integration_end_to_end PRIVATE GTest::gtest_main)

add_executable(test_aces_cap tests/test_aces_cap.cpp)
target_link_libraries(test_aces_cap PRIVATE aces GTest::gtest_main)

add_executable(test_diagnostics tests/test_diagnostics.cpp)
target_link_libraries(test_diagnostics PRIVATE aces GTest::gtest)

add_executable(test_aces_ingestor tests/test_aces_ingestor.cpp)
target_link_libraries(test_aces_ingestor PRIVATE aces GTest::gtest_main)

add_executable(test_aces_utils tests/test_aces_utils.cpp)
target_link_libraries(test_aces_utils PRIVATE aces GTest::gtest_main)

add_executable(test_aces_compute tests/test_aces_compute.cpp)
target_link_libraries(test_aces_compute PRIVATE aces GTest::gtest_main)

add_executable(test_physics tests/test_physics.cpp)
target_link_libraries(test_physics PRIVATE aces GTest::gtest_main)

add_executable(test_hemco_mimic tests/test_hemco_mimic.cpp)
target_link_libraries(test_hemco_mimic PRIVATE aces GTest::gtest_main)

add_executable(test_stacking_engine tests/test_stacking_engine.cpp)
target_link_libraries(test_stacking_engine PRIVATE aces GTest::gtest_main)

add_executable(test_vertical_distribution_single tests/test_vertical_distribution_single.cpp)
target_link_libraries(test_vertical_distribution_single PRIVATE aces GTest::gtest_main)

add_executable(test_field_resolution_priority tests/test_field_resolution_priority.cpp)
target_link_libraries(test_field_resolution_priority PRIVATE aces GTest::gtest_main)

if(ACES_HAS_FORTRAN)
    add_library(test_helpers STATIC tests/test_helpers.F90)
    target_include_directories(test_helpers PUBLIC ${ESMF_INCLUDE_DIRS})
    target_link_libraries(test_helpers PUBLIC ESMF::ESMF)
endif()

# add_executable(example_driver tests/example_driver.cpp)
# target_link_libraries(example_driver PRIVATE aces)

# add_executable(hemco_mimic_driver tests/hemco_mimic_driver.cpp)
# target_link_libraries(hemco_mimic_driver PRIVATE aces)

add_executable(hemco_comparison_driver tests/hemco_comparison_driver.cpp)
target_link_libraries(hemco_comparison_driver PRIVATE aces)
add_test(NAME test_hemco_comparison COMMAND hemco_comparison_driver)

add_executable(benchmark_aces_hemco tests/benchmark_aces_hemco.cpp)
target_link_libraries(benchmark_aces_hemco PRIVATE aces)

add_executable(test_hemco_parity tests/test_hemco_parity.cpp)
target_link_libraries(test_hemco_parity PRIVATE aces GTest::gtest_main)

# Kokkos Configuration Tests (Requirement 6.13-6.18, 6.21)
add_executable(test_kokkos_configuration tests/test_kokkos_configuration.cpp)
target_link_libraries(test_kokkos_configuration PRIVATE aces GTest::gtest_main)

# Property 13: Kokkos Device Consistency (Requirement 6.22)


# Unit tests for Kokkos parallel efficiency (Requirement 6.19, 6.20)


# Comprehensive Property-Based Test Suite (Task 11.1)
# Consolidates all 23 correctness properties
add_executable(test_properties tests/test_properties.cpp)
target_link_libraries(test_properties PRIVATE aces GTest::gtest_main)

# Property 21: Command-Line Configuration (Requirement 2.11, 2.12)


# Property 17: Test Suite Idempotence (Requirement 8.16)
# Tests that running the full test suite twice produces identical results
add_executable(test_suite_idempotence tests/test_suite_idempotence.cpp)
target_link_libraries(test_suite_idempotence PRIVATE aces GTest::gtest_main)

# Temporary Reproduction Test
add_executable(test_repro_failure tests/repro_failure.cpp)
target_link_libraries(test_repro_failure PRIVATE aces GTest::gtest_main)

#---------------------------------------------------------------------
# Kernel Profiling Tests (Requirements 6.4, 6.19, 6.20)
#---------------------------------------------------------------------

# Unit tests for field creation (Requirement R4)
add_executable(test_field_creation tests/test_field_creation.cpp)
if(ACES_HAS_FORTRAN AND TARGET test_helpers)
    target_link_libraries(test_field_creation PRIVATE aces test_helpers GTest::gtest_main)
else()
    target_link_libraries(test_field_creation PRIVATE aces GTest::gtest_main)
endif()

# Unit tests for field pointer passing (Requirement R5)
add_executable(test_field_pointer_passing tests/test_field_pointer_passing.cpp)
if(ACES_HAS_FORTRAN AND TARGET test_helpers)
    target_link_libraries(test_field_pointer_passing PRIVATE aces test_helpers GTest::gtest_main)
else()
    target_link_libraries(test_field_pointer_passing PRIVATE aces GTest::gtest_main)
endif()

# Standalone NUOPC driver
add_subdirectory(standalone_nuopc)

# Python script tests
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(
        NAME test_hemco_to_aces_python
        COMMAND ${Python3_EXECUTABLE} -m pytest ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_converters.py)
    set_tests_properties(test_hemco_to_aces_python PROPERTIES ENVIRONMENT
                                                             "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts")

    add_test(
        NAME test_scripts_python
        COMMAND ${Python3_EXECUTABLE} -m pytest ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_scripts.py)
    set_tests_properties(test_scripts_python PROPERTIES ENVIRONMENT
                                                        "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts")
endif()

include(GoogleTest)
gtest_discover_tests(test_driver_configuration)
gtest_discover_tests(test_driver_config_validation)
gtest_discover_tests(test_driver_clock_management)
gtest_discover_tests(test_clock_operations_unit)
gtest_discover_tests(test_driver_integration_end_to_end)
gtest_discover_tests(test_aces_cap)
gtest_discover_tests(test_stacking_engine)
gtest_discover_tests(test_vertical_distribution_single)
gtest_discover_tests(test_aces_utils)
gtest_discover_tests(test_aces_compute)
gtest_discover_tests(test_physics)
gtest_discover_tests(test_aces_ingestor)
gtest_discover_tests(test_diagnostics)
gtest_discover_tests(test_hemco_mimic)
gtest_discover_tests(test_field_resolution_priority)
gtest_discover_tests(test_hemco_parity)
gtest_discover_tests(test_kokkos_configuration)
gtest_discover_tests(test_properties)
gtest_discover_tests(test_suite_idempotence)
gtest_discover_tests(test_repro_failure)
gtest_discover_tests(test_field_creation)
gtest_discover_tests(test_field_pointer_passing)

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

    doxygen_add_docs(docs
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        COMMENT "Generating API documentation with Doxygen"
        ALL)
else()
    message(STATUS "Doxygen not found. API documentation generation disabled.")
    message(STATUS "Install Doxygen to enable: sudo apt-get install doxygen graphviz")
endif()


# --- Code Quality Checks ---

# clang-format target
find_program(CLANG_FORMAT_EXECUTABLE clang-format)
if(CLANG_FORMAT_EXECUTABLE)
    file(GLOB_RECURSE ACES_SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/include/aces/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/aces/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.cpp"
    )

    add_custom_target(clang-format
        COMMAND ${CLANG_FORMAT_EXECUTABLE} -i ${ACES_SOURCE_FILES}
        COMMENT "Running clang-format on ACES source files"
    )

    add_custom_target(clang-format-check
        COMMAND ${CLANG_FORMAT_EXECUTABLE} --dry-run --Werror ${ACES_SOURCE_FILES}
        COMMENT "Checking clang-format compliance on ACES source files"
    )
else()
    message(STATUS "clang-format not found. Code formatting targets disabled.")
endif()

# clang-tidy target
find_program(CLANG_TIDY_EXECUTABLE clang-tidy)
if(CLANG_TIDY_EXECUTABLE)
    file(GLOB_RECURSE ACES_SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.cpp"
    )

    add_custom_target(clang-tidy
        COMMAND ${CLANG_TIDY_EXECUTABLE} -p ${CMAKE_BINARY_DIR} ${ACES_SOURCE_FILES}
        COMMENT "Running clang-tidy on ACES source files"
    )
else()
    message(STATUS "clang-tidy not found. Code analysis targets disabled.")
endif()
