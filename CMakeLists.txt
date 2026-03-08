cmake_minimum_required(VERSION 3.20)
project(ACES LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# macro to disable installation for subprojects
macro(aces_noop)
endmacro()

# Optional Fortran support
include(CheckLanguage)
check_language(Fortran)
if(CMAKE_Fortran_COMPILER)
    enable_language(Fortran)
    set(ACES_HAS_FORTRAN ON)
else()
    message(WARNING "Fortran compiler not found. Fortran features will be disabled.")
endif()

list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# --- Dependencies ---
include(FetchContent)

# Kokkos
FetchContent_Declare(kokkos
    GIT_REPOSITORY https://github.com/kokkos/kokkos.git
    GIT_TAG 4.2.00)
FetchContent_MakeAvailable(kokkos)

# YAML-CPP
FetchContent_Declare(yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG 0.8.0)
FetchContent_MakeAvailable(yaml-cpp)

# GTest
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ESMF
find_package(ESMF REQUIRED)

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
                    return()
                endif()
            endif()
        endforeach()
    endif()
endfunction()

find_root_by_file(NetCDF_ROOT "netcdf.h")
find_root_by_file(NetCDFF_ROOT "netcdf.mod")
find_root_by_file(PIO_ROOT "pio.h")

find_package(MPI REQUIRED)

# CDEPS
find_package(CDEPS QUIET)
if(NOT CDEPS_FOUND AND ACES_HAS_FORTRAN)
    # Even if PIO not found via find_package, we proceed if we have a path from ESMF
    if(PIO_ROOT OR PIO_FOUND)
        set(PIO_PATH "${PIO_ROOT}")
        set(ENV{PIO_ROOT} "${PIO_ROOT}")

        FetchContent_Declare(cdeps
            GIT_REPOSITORY https://github.com/bbakernoaa/CDEPS.git
            GIT_TAG ae5d2c457321ebae3a0d9c9c949f871bb8f2b09c
            GIT_SUBMODULES_RECURSE ON
            PATCH_COMMAND bash -c "sed -i 's/add_subdirectory(fox)/#add_subdirectory(fox)/' CMakeLists.txt && \
                                   sed -i 's/install(/aces_noop(/g' CMakeLists.txt streams/CMakeLists.txt dshr/CMakeLists.txt share/CMakeLists.txt datm/CMakeLists.txt dice/CMakeLists.txt dglc/CMakeLists.txt dlnd/CMakeLists.txt docn/CMakeLists.txt drof/CMakeLists.txt dwav/CMakeLists.txt && \
                                   sed -i 's|\\\${CMAKE_BINARY_DIR}/share|\\\${CMAKE_Fortran_MODULE_DIRECTORY}|g' streams/CMakeLists.txt dshr/CMakeLists.txt && \
                                   sed -i 's|\\\${CMAKE_CURRENT_BINARY_DIR}/share|\\\${CMAKE_Fortran_MODULE_DIRECTORY}|g' CMakeLists.txt && \
                                   sed -i 's|\\\${CMAKE_CURRENT_BINARY_DIR}/../share|\\\${CMAKE_Fortran_MODULE_DIRECTORY}|g' streams/CMakeLists.txt dshr/CMakeLists.txt"
        )

        set(PIO_Fortran_INCLUDE_DIR "${PIO_ROOT}/include" CACHE PATH "" FORCE)
        set(PIO_C_INCLUDE_DIR "${PIO_ROOT}/include" CACHE PATH "" FORCE)
        set(NetCDF_Fortran_INCLUDE_DIR "${NetCDFF_ROOT}/include" CACHE PATH "" FORCE)
        set(NetCDF_C_INCLUDE_DIR "${NetCDF_ROOT}/include" CACHE PATH "" FORCE)

        if(NOT ESMF_ROOT)
            find_root_by_file(ESMF_ROOT_DISC "esmf.mod")
            set(ESMF_ROOT "${ESMF_ROOT_DISC}")
        endif()
        set(ESMF_ROOT "${ESMF_ROOT}" CACHE PATH "Path to ESMF" FORCE)
        set(ENV{ESMF_ROOT} "${ESMF_ROOT}")
        set(ESMF_F90COMPILEPATHS "${ESMF_INCLUDE_DIRS}" CACHE STRING "" FORCE)

        set(CMAKE_Fortran_FLAGS "${CMAKE_Fortran_FLAGS} -ffree-line-length-none -DCPRGNU" CACHE STRING "" FORCE)
        set(DISABLE_FoX ON CACHE BOOL "" FORCE)
        set(COMPILER "gnu" CACHE STRING "")

        # Scoped module directory for CDEPS
        set(OLD_FORTRAN_MOD_DIR ${CMAKE_Fortran_MODULE_DIRECTORY})
        set(CMAKE_Fortran_MODULE_DIRECTORY ${CMAKE_BINARY_DIR}/cdeps_modules)
        FetchContent_MakeAvailable(cdeps)
        set(CMAKE_Fortran_MODULE_DIRECTORY ${OLD_FORTRAN_MOD_DIR})

        if(TARGET dshr)
            set(CDEPS_FOUND TRUE)
        endif()
    endif()
endif()

if(NOT CDEPS_FOUND)
    message(FATAL_ERROR "CDEPS is required but was not found or built from source.")
endif()

# --- ACES Library ---
set(ACES_SRCS
    src/aces_cap.cpp src/aces_config_parser.cpp src/aces_compute.cpp src/aces_stacking_engine.cpp
    src/aces_physics_factory.cpp src/physics/aces_native_example.cpp src/physics/aces_sea_salt.cpp
    src/physics/aces_megan.cpp src/physics/aces_dms.cpp src/physics/aces_lightning.cpp
    src/physics/aces_soil_nox.cpp src/physics/aces_dust.cpp src/physics/aces_volcano.cpp
    src/io/aces_data_ingestor.cpp src/io/aces_diagnostic_writer.cpp)

if(ACES_HAS_FORTRAN)
    list(APPEND ACES_SRCS
        src/physics/aces_fortran_bridge.cpp src/physics/legacy_fortran.F90 src/io/aces_cdeps_bridge.F90
        src/physics/aces_sea_salt_fortran_bridge.cpp src/physics/sea_salt_kernel.F90
        src/physics/aces_megan_fortran_bridge.cpp src/physics/megan_kernel.F90
        src/physics/aces_dms_fortran_bridge.cpp src/physics/dms_kernel.F90
        src/physics/aces_lightning_fortran_bridge.cpp src/physics/lightning_kernel.F90
        src/physics/aces_soil_nox_fortran_bridge.cpp src/physics/soil_nox_kernel.F90
        src/physics/aces_dust_fortran_bridge.cpp src/physics/dust_kernel.F90
        src/physics/aces_volcano_fortran_bridge.cpp src/physics/volcano_kernel.F90)
endif()

add_library(aces SHARED ${ACES_SRCS})

if(ACES_HAS_FORTRAN)
    target_compile_definitions(aces PRIVATE ACES_HAS_FORTRAN)
    # Target-specific module directory for ACES
    set_target_properties(aces PROPERTIES Fortran_MODULE_DIRECTORY ${CMAKE_BINARY_DIR}/fortran_modules)
    target_include_directories(aces PUBLIC $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/fortran_modules>)
    target_include_directories(aces PRIVATE ${CMAKE_BINARY_DIR}/cdeps_modules)
endif()

target_include_directories(aces PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include> ${ESMF_INCLUDE_DIRS})
target_link_libraries(aces PUBLIC Kokkos::kokkos ESMF::ESMF yaml-cpp dshr streams cdeps_share)

if(PIO_FOUND OR PIO_ROOT)
    foreach(libf "${PIO_ROOT}/lib/libpiof.so" "${PIO_ROOT}/lib/libpioc.so")
        if(EXISTS "${libf}")
            target_link_libraries(aces PUBLIC "${libf}")
        endif()
    endforeach()
endif()
if(NetCDFF_FOUND AND EXISTS "${NetCDFF_ROOT}/lib/libnetcdff.so")
    target_link_libraries(aces PUBLIC "${NetCDFF_ROOT}/lib/libnetcdff.so")
endif()
if(NetCDF_ROOT AND EXISTS "${NetCDF_ROOT}/lib/libnetcdf.so")
    target_link_libraries(aces PUBLIC "${NetCDF_ROOT}/lib/libnetcdf.so")
endif()

if(MPI_FOUND)
    target_link_libraries(aces PUBLIC MPI::MPI_CXX)
endif()

# --- Tests ---
enable_testing()
set(ACES_TESTS test_aces_cap test_diagnostics test_aces_ingestor test_aces_utils test_aces_compute test_physics test_hemco_mimic test_stacking_engine)
foreach(t ${ACES_TESTS})
    add_executable(${t} tests/${t}.cpp)
    if(t STREQUAL "test_diagnostics")
        target_link_libraries(${t} PRIVATE aces GTest::gtest)
    else()
        target_link_libraries(${t} PRIVATE aces GTest::gtest_main)
    endif()
endforeach()

add_executable(example_driver tests/example_driver.cpp)
target_link_libraries(example_driver PRIVATE aces)
add_executable(hemco_mimic_driver tests/hemco_mimic_driver.cpp)
target_link_libraries(hemco_mimic_driver PRIVATE aces)

add_subdirectory(standalone_nuopc)

# Python tests (skipped if pytest not found)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    execute_process(COMMAND ${Python3_EXECUTABLE} -c "import pytest" RESULT_VARIABLE PYTEST_RESULT OUTPUT_QUIET ERROR_QUIET)
    if(PYTEST_RESULT EQUAL 0)
        add_test(NAME test_hemco_to_aces_python COMMAND ${Python3_EXECUTABLE} -m pytest ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_converters.py)
        set_tests_properties(test_hemco_to_aces_python PROPERTIES ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts")
        add_test(NAME test_scripts_python COMMAND ${Python3_EXECUTABLE} -m pytest ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_scripts.py)
        set_tests_properties(test_scripts_python PROPERTIES ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/scripts")
    endif()
endif()

include(GoogleTest)
foreach(t ${ACES_TESTS})
    gtest_discover_tests(${t})
endforeach()
