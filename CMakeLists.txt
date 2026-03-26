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

    # CDEPS and PIO sometimes expect a share directory in the build root

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
    src/aces_core_advertise.cpp
    src/aces_core_realize.cpp
    src/aces_core_initialize_p1.cpp
    src/aces_core_initialize_p2.cpp
    src/aces_core_field_helpers.cpp
    src/aces_core_run.cpp
    src/aces_core_finalize.cpp
    src/aces_config_path.cpp)

if(ACES_HAS_FORTRAN)
    add_subdirectory(src/io/tide)

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

# Link NetCDF for CDEPS parser
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

add_executable(test_vertical_distribution_range tests/test_vertical_distribution_range.cpp)
target_link_libraries(test_vertical_distribution_range PRIVATE aces GTest::gtest_main)

add_executable(test_vertical_distribution_pressure tests/test_vertical_distribution_pressure.cpp)
target_link_libraries(test_vertical_distribution_pressure PRIVATE aces GTest::gtest_main)

add_executable(test_vertical_distribution_height tests/test_vertical_distribution_height.cpp)
target_link_libraries(test_vertical_distribution_height PRIVATE aces GTest::gtest_main)

add_executable(test_vertical_distribution_pbl tests/test_vertical_distribution_pbl.cpp)
target_link_libraries(test_vertical_distribution_pbl PRIVATE aces GTest::gtest_main)

add_executable(test_field_resolution_priority tests/test_field_resolution_priority.cpp)
target_link_libraries(test_field_resolution_priority PRIVATE aces GTest::gtest_main)

add_executable(test_realize_phase tests/test_realize_phase.cpp)
target_link_libraries(test_realize_phase PRIVATE aces GTest::gtest_main)

if(ACES_HAS_FORTRAN)
    add_library(test_helpers STATIC tests/test_helpers.F90)
    target_include_directories(test_helpers PUBLIC ${ESMF_INCLUDE_DIRS})
    target_link_libraries(test_helpers PUBLIC ESMF::ESMF)
endif()


add_executable(test_initialize_phases tests/test_initialize_phases.cpp)
if(ACES_HAS_FORTRAN AND TARGET test_helpers)
    target_link_libraries(test_initialize_phases PRIVATE aces test_helpers GTest::gtest_main)
else()
    target_link_libraries(test_initialize_phases PRIVATE aces GTest::gtest_main)
endif()

add_executable(test_run_phase tests/test_run_phase.cpp)
if(ACES_HAS_FORTRAN AND TARGET test_helpers)
    target_link_libraries(test_run_phase PRIVATE aces test_helpers GTest::gtest_main)
else()
    target_link_libraries(test_run_phase PRIVATE aces GTest::gtest_main)
endif()

add_executable(test_finalize_phase tests/test_finalize_phase.cpp)
if(ACES_HAS_FORTRAN AND TARGET test_helpers)
    target_link_libraries(test_finalize_phase PRIVATE aces test_helpers GTest::gtest_main)
else()
    target_link_libraries(test_finalize_phase PRIVATE aces GTest::gtest_main)
endif()

add_executable(test_nuopc_phase_sequence tests/test_nuopc_phase_sequence.cpp)
if(ACES_HAS_FORTRAN AND TARGET test_helpers)
    target_link_libraries(test_nuopc_phase_sequence PRIVATE aces test_helpers GTest::gtest_main)
else()
    target_link_libraries(test_nuopc_phase_sequence PRIVATE aces GTest::gtest_main)
endif()

add_executable(test_single_model_driver tests/test_single_model_driver.cpp)
if(ACES_HAS_FORTRAN AND TARGET test_helpers)
    target_link_libraries(test_single_model_driver PRIVATE aces test_helpers GTest::gtest_main)
else()
    target_link_libraries(test_single_model_driver PRIVATE aces GTest::gtest_main)
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

add_executable(test_hierarchy_prioritization tests/test_hierarchy_prioritization.cpp)
target_link_libraries(test_hierarchy_prioritization PRIVATE aces GTest::gtest_main)

add_executable(test_scale_factor_commutativity tests/test_scale_factor_commutativity.cpp)
target_link_libraries(test_scale_factor_commutativity PRIVATE aces GTest::gtest_main)


# Property 11: Generated Scheme Compilation (Requirement 5.24)
add_executable(test_generated_scheme_compilation tests/test_generated_scheme_compilation.cpp)
target_link_libraries(test_generated_scheme_compilation PRIVATE aces GTest::gtest_main)
target_compile_definitions(test_generated_scheme_compilation PRIVATE
    ACES_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# Property 23: Output Round-Trip (Requirement 11.14)
add_executable(test_output_roundtrip tests/test_output_roundtrip.cpp)
target_link_libraries(test_output_roundtrip PRIVATE aces GTest::gtest_main)
if(NetCDF_ROOT)
    target_include_directories(test_output_roundtrip PRIVATE "${NetCDF_ROOT}/include")
    if(EXISTS "${NetCDF_ROOT}/lib/libnetcdf.so")
        target_link_libraries(test_output_roundtrip PRIVATE "${NetCDF_ROOT}/lib/libnetcdf.so")
    endif()
endif()

# Kokkos Configuration Tests (Requirement 6.13-6.18, 6.21)
add_executable(test_kokkos_configuration tests/test_kokkos_configuration.cpp)
target_link_libraries(test_kokkos_configuration PRIVATE aces GTest::gtest_main)

# Property 13: Kokkos Device Consistency (Requirement 6.22)
add_executable(test_kokkos_device_consistency tests/test_kokkos_device_consistency.cpp)
target_link_libraries(test_kokkos_device_consistency PRIVATE aces GTest::gtest_main)

# Unit tests for Kokkos parallel efficiency (Requirement 6.19, 6.20)
add_executable(test_kokkos_parallel_efficiency tests/test_kokkos_parallel_efficiency.cpp)
target_link_libraries(test_kokkos_parallel_efficiency PRIVATE aces GTest::gtest_main)

# Comprehensive Property-Based Test Suite (Task 11.1)
# Consolidates all 23 correctness properties
add_executable(test_properties tests/test_properties.cpp)
target_link_libraries(test_properties PRIVATE aces GTest::gtest_main)

# Property 21: Command-Line Configuration (Requirement 2.11, 2.12)
add_executable(test_command_line_configuration_property tests/test_command_line_configuration_property.cpp)
target_link_libraries(test_command_line_configuration_property PRIVATE aces GTest::gtest_main)

# Property 17: Test Idempotence (Requirement 8.16)
add_executable(test_idempotence_property tests/test_idempotence_property.cpp)
target_link_libraries(test_idempotence_property PRIVATE aces GTest::gtest_main)

# Property 17: Test Suite Idempotence (Requirement 8.16)
# Tests that running the full test suite twice produces identical results
add_executable(test_suite_idempotence tests/test_suite_idempotence.cpp)
target_link_libraries(test_suite_idempotence PRIVATE aces GTest::gtest_main)

# Property 15: Vertical Distribution Round-Trip (Requirement 6.24)
add_executable(test_vertical_distribution_roundtrip_property
    tests/test_vertical_distribution_roundtrip_property.cpp)
target_link_libraries(test_vertical_distribution_roundtrip_property PRIVATE aces GTest::gtest_main)

# Temporary Reproduction Test
add_executable(test_repro_failure tests/repro_failure.cpp)
target_link_libraries(test_repro_failure PRIVATE aces GTest::gtest_main)

# Property 14: Mass Conservation Invariant (Requirement 6.23)
add_executable(test_mass_conservation_property tests/test_mass_conservation_property.cpp)
target_link_libraries(test_mass_conservation_property PRIVATE aces GTest::gtest_main)

# Property 19: Temporal Cycle Application (Requirement 3.7)
add_executable(test_temporal_cycle_application_property
    tests/test_temporal_cycle_application_property.cpp)
target_link_libraries(test_temporal_cycle_application_property PRIVATE aces GTest::gtest_main)

# Property 5: NUOPC Phase Execution Order (Requirements 2.5-2.10, 4.3-4.17)
add_executable(test_nuopc_phase_execution_order_property
    tests/test_nuopc_phase_execution_order_property.cpp)
if(ACES_HAS_FORTRAN AND TARGET test_helpers)
    target_link_libraries(test_nuopc_phase_execution_order_property PRIVATE aces test_helpers GTest::gtest_main)
else()
    target_link_libraries(test_nuopc_phase_execution_order_property PRIVATE aces GTest::gtest_main)
endif()


# Property 12: BasePhysicsScheme Field Caching (Requirement 5.16)
add_executable(test_base_physics_scheme_field_caching
    tests/test_base_physics_scheme_field_caching.cpp)
if(ACES_HAS_FORTRAN AND TARGET test_helpers)
    target_link_libraries(test_base_physics_scheme_field_caching PRIVATE aces test_helpers GTest::gtest_main)
else()
    target_link_libraries(test_base_physics_scheme_field_caching PRIVATE aces GTest::gtest_main)
endif()

# Property 10: Physics Scheme Auto-Registration (Requirements 5.18, 5.23)
add_executable(test_physics_scheme_autoregistration
    tests/test_physics_scheme_autoregistration.cpp)
target_link_libraries(test_physics_scheme_autoregistration PRIVATE aces GTest::gtest_main)

# Standalone Writer Unit Tests (Requirements 11.6, 11.7, 11.9, 11.11, 11.13)
add_executable(test_standalone_writer_unit tests/test_standalone_writer_unit.cpp)
target_link_libraries(test_standalone_writer_unit PRIVATE aces GTest::gtest_main)
if(NetCDF_ROOT)
    target_include_directories(test_standalone_writer_unit PRIVATE "${NetCDF_ROOT}/include")
    if(EXISTS "${NetCDF_ROOT}/lib/libnetcdf.so")
        target_link_libraries(test_standalone_writer_unit PRIVATE
            "${NetCDF_ROOT}/lib/libnetcdf.so")
    endif()
endif()

# Kernel Profiling Tests (Requirements 6.4, 6.19, 6.20)
add_executable(test_kernel_profiling tests/test_kernel_profiling.cpp)
target_link_libraries(test_kernel_profiling PRIVATE aces GTest::gtest_main)

# Property 9: Dynamic Species Registration (Requirements 3.14, 3.15)
add_executable(test_dynamic_species_registration_property
    tests/test_dynamic_species_registration_property.cpp)
target_link_libraries(test_dynamic_species_registration_property PRIVATE aces GTest::gtest_main)


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
gtest_discover_tests(test_aces_cap)
gtest_discover_tests(test_stacking_engine)
gtest_discover_tests(test_vertical_distribution_single)
gtest_discover_tests(test_vertical_distribution_range)
gtest_discover_tests(test_vertical_distribution_pressure)
gtest_discover_tests(test_vertical_distribution_height)
gtest_discover_tests(test_vertical_distribution_pbl)
gtest_discover_tests(test_aces_utils)
gtest_discover_tests(test_aces_compute)
gtest_discover_tests(test_physics)
gtest_discover_tests(test_aces_ingestor)
gtest_discover_tests(test_diagnostics)
gtest_discover_tests(test_hemco_mimic)
gtest_discover_tests(test_field_resolution_priority)

gtest_discover_tests(test_realize_phase)
gtest_discover_tests(test_initialize_phases)
gtest_discover_tests(test_run_phase)
gtest_discover_tests(test_finalize_phase)
gtest_discover_tests(test_nuopc_phase_sequence)
gtest_discover_tests(test_single_model_driver)
gtest_discover_tests(test_hemco_parity)
gtest_discover_tests(test_hierarchy_prioritization)
gtest_discover_tests(test_scale_factor_commutativity)
gtest_discover_tests(test_generated_scheme_compilation)
gtest_discover_tests(test_output_roundtrip)
gtest_discover_tests(test_kokkos_configuration)
gtest_discover_tests(test_kokkos_device_consistency)
gtest_discover_tests(test_kokkos_parallel_efficiency)
gtest_discover_tests(test_properties)
gtest_discover_tests(test_command_line_configuration_property)
gtest_discover_tests(test_idempotence_property)
gtest_discover_tests(test_suite_idempotence)
gtest_discover_tests(test_vertical_distribution_roundtrip_property)
gtest_discover_tests(test_repro_failure)
gtest_discover_tests(test_mass_conservation_property)
gtest_discover_tests(test_temporal_cycle_application_property)
gtest_discover_tests(test_nuopc_phase_execution_order_property)

gtest_discover_tests(test_base_physics_scheme_field_caching)
gtest_discover_tests(test_physics_scheme_autoregistration)
gtest_discover_tests(test_standalone_writer_unit)
gtest_discover_tests(test_kernel_profiling)
gtest_discover_tests(test_dynamic_species_registration_property)

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
