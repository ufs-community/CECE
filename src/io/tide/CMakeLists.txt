cmake_minimum_required(VERSION 3.20)
project(TIDE LANGUAGES CXX Fortran VERSION 0.1)

if(POLICY CMP0074)
    cmake_policy(SET CMP0074 NEW)
endif()

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Options
option(WERROR "add the -Werror flag to compiler" OFF)

# Find Dependencies
find_package(ESMF REQUIRED)
find_package(MPI REQUIRED)

# Use paths from ESMFMKFILE if available
if(ESMF_FOUND)
  link_directories(${ESMF_F90ESMFLINKRPATHS})
endif()

# Fallback for JCSDA docker image if link_directories fails to find libraries
link_directories(
  /opt/software/linux-ubuntu24.04-x86_64/gcc-13.3.0/esmf-8.8.0-ms7pardr7fgwhdnp6mqgtw22qgcfewny/lib
  /opt/software/linux-ubuntu24.04-x86_64/gcc-13.3.0/parallelio-2.6.2-7b45pabda4ysavdoch6eielvyiwxramj/lib
  /opt/views/view/lib
)

# PIO handling
if (DEFINED ENV{PIO_ROOT})
  find_library(PIO_C_LIBRARY NAMES pioc pio PATHS "$ENV{PIO_ROOT}/lib" NO_DEFAULT_PATH)
  set(PIO_C_INCLUDE_DIR "$ENV{PIO_ROOT}/include")
  find_library(PIO_Fortran_LIBRARY NAMES piof pio PATHS "$ENV{PIO_ROOT}/lib" NO_DEFAULT_PATH)
  set(PIO_Fortran_INCLUDE_DIR "$ENV{PIO_ROOT}/include")
elseif (ESMF_FOUND)
    # Try to extract from ESMF paths if they point to the JCSDA views
    foreach(path ${ESMF_F90COMPILEPATHS})
        if(path MATCHES "parallelio")
            get_filename_component(pio_root "${path}/.." ABSOLUTE)
            find_library(PIO_C_LIBRARY NAMES pioc pio PATHS "${pio_root}/lib" NO_DEFAULT_PATH)
            set(PIO_C_INCLUDE_DIR "${path}")
            find_library(PIO_Fortran_LIBRARY NAMES piof pio PATHS "${pio_root}/lib" NO_DEFAULT_PATH)
            set(PIO_Fortran_INCLUDE_DIR "${path}")
            if(PIO_C_LIBRARY AND PIO_Fortran_LIBRARY)
              break()
            endif()
        endif()
    endforeach()
endif()

message(STATUS "DEBUG: ENV{PIO_ROOT} = $ENV{PIO_ROOT}")
message(STATUS "DEBUG: PIO_C_LIBRARY = ${PIO_C_LIBRARY}")
message(STATUS "DEBUG: PIO_Fortran_LIBRARY = ${PIO_Fortran_LIBRARY}")

# Fallback for JCSDA docker image if not found above
if (NOT PIO_C_LIBRARY AND IS_DIRECTORY "/opt/software/linux-ubuntu24.04-x86_64/gcc-13.3.0/parallelio-2.6.2-7b45pabda4ysavdoch6eielvyiwxramj")
    set(PIO_C_LIBRARY "/opt/software/linux-ubuntu24.04-x86_64/gcc-13.3.0/parallelio-2.6.2-7b45pabda4ysavdoch6eielvyiwxramj/lib/libpioc.so")
    set(PIO_C_INCLUDE_DIR "/opt/software/linux-ubuntu24.04-x86_64/gcc-13.3.0/parallelio-2.6.2-7b45pabda4ysavdoch6eielvyiwxramj/include")
    set(PIO_Fortran_LIBRARY "/opt/software/linux-ubuntu24.04-x86_64/gcc-13.3.0/parallelio-2.6.2-7b45pabda4ysavdoch6eielvyiwxramj/lib/libpiof.so")
    set(PIO_Fortran_INCLUDE_DIR "/opt/software/linux-ubuntu24.04-x86_64/gcc-13.3.0/parallelio-2.6.2-7b45pabda4ysavdoch6eielvyiwxramj/include")
endif()

find_package(PIO REQUIRED COMPONENTS C Fortran)

# yaml-cpp (Assuming it's available or can be fetched)
include(FetchContent)
FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG yaml-cpp-0.7.0
)
FetchContent_MakeAvailable(yaml-cpp)

# TIDE Library
add_subdirectory(share)
add_subdirectory(streams)
add_subdirectory(dshr)

add_library(tide
  tide/src/tide_yaml_c.cpp
  tide/src/tide_yaml_mod.F90
  tide/src/tide_cf_detection_mod.F90
  tide/src/tide_mod.F90
)

target_include_directories(tide PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/share>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/share>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/streams>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/streams>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/dshr>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/dshr>
)

target_include_directories(tide PRIVATE
  ${ESMF_F90COMPILEPATHS}
  ${PIO_Fortran_INCLUDE_DIR}
)

# Extract link directories from ESMF paths if they aren't already set
foreach(path ${ESMF_F90COMPILEPATHS})
  if(IS_DIRECTORY ${path}/../lib)
    link_directories(${path}/../lib)
  endif()
endforeach()
link_directories(/opt/views/view/lib)

target_link_libraries(tide PUBLIC
  dshr
  streams
  cdeps_share
  yaml-cpp
  ${ESMF_F90ESMFLINKLIBS}
  ${PIO_Fortran_LIBRARY}
  ${PIO_C_LIBRARY}
  ${MPI_Fortran_LIBRARIES}
  m
)

# Testing
enable_testing()
add_executable(test_tide tide/tests/test_tide.F90)
target_include_directories(test_tide PRIVATE
  ${ESMF_F90COMPILEPATHS}
  ${PIO_Fortran_INCLUDE_DIR}
)
target_link_libraries(test_tide PRIVATE
  tide
  dshr
  streams
  cdeps_share
  ${ESMF_F90ESMFLINKLIBS}
  ${PIO_Fortran_LIBRARY}
  ${PIO_C_LIBRARY}
  ${MPI_Fortran_LIBRARIES}
  m
)
add_test(NAME tide_api_test COMMAND test_tide)

add_executable(test_tide_roundtrip tide/tests/test_tide_roundtrip.F90)
target_include_directories(test_tide_roundtrip PRIVATE
  ${ESMF_F90COMPILEPATHS}
  ${PIO_Fortran_INCLUDE_DIR}
  ${NETCDF_Fortran_INCLUDE_DIR}
)
target_link_libraries(test_tide_roundtrip PRIVATE
  tide
  dshr
  streams
  cdeps_share
  ${ESMF_F90ESMFLINKLIBS}
  ${PIO_Fortran_LIBRARY}
  ${PIO_C_LIBRARY}
  ${NETCDF_Fortran_LIBRARY}
  ${MPI_Fortran_LIBRARIES}
  m
)
add_test(NAME tide_api_roundtrip COMMAND test_tide_roundtrip)

# -----------------------------------------------------------------------
# NetCDF-Fortran (required by CF property tests)
# -----------------------------------------------------------------------
find_package(NetCDF QUIET COMPONENTS Fortran)
if (NOT NetCDF_FOUND)
  # Fallback: search common paths in the JCSDA docker image
  find_library(NETCDF_Fortran_LIBRARY NAMES netcdff
    PATHS /opt/views/view/lib /usr/lib /usr/local/lib NO_DEFAULT_PATH)
  find_path(NETCDF_Fortran_INCLUDE_DIR netcdf.mod
    PATHS /opt/views/view/include /usr/include /usr/local/include NO_DEFAULT_PATH)
  if (NETCDF_Fortran_LIBRARY AND NETCDF_Fortran_INCLUDE_DIR)
    set(NetCDF_Fortran_FOUND TRUE)
    message(STATUS "NetCDF-Fortran (fallback): ${NETCDF_Fortran_LIBRARY}")
  else()
    message(WARNING "NetCDF-Fortran not found; CF property tests will be skipped")
    set(NetCDF_Fortran_FOUND FALSE)
  endif()
else()
  set(NETCDF_Fortran_LIBRARY    ${NetCDF_Fortran_LIBRARIES})
  set(NETCDF_Fortran_INCLUDE_DIR ${NetCDF_Fortran_INCLUDE_DIRS})
  set(NetCDF_Fortran_FOUND TRUE)
endif()

# Helper macro to add a CF property test executable
macro(add_cf_test test_name src_file)
  if (NetCDF_Fortran_FOUND)
    add_executable(${test_name} ${src_file})
    target_include_directories(${test_name} PRIVATE
      ${ESMF_F90COMPILEPATHS}
      ${PIO_Fortran_INCLUDE_DIR}
      ${NETCDF_Fortran_INCLUDE_DIR}
    )
    target_link_libraries(${test_name} PRIVATE
      tide
      dshr
      streams
      cdeps_share
      ${ESMF_F90ESMFLINKLIBS}
      ${PIO_Fortran_LIBRARY}
      ${PIO_C_LIBRARY}
      ${NETCDF_Fortran_LIBRARY}
      ${MPI_Fortran_LIBRARIES}
      m
    )
    add_test(NAME ${test_name} COMMAND ${test_name})
  endif()
endmacro()

# CF property tests (tasks 2–13)
add_cf_test(test_cf_prop1_complete_metadata
  tide/tests/test_cf_property_1_complete_metadata.F90)
add_cf_test(test_cf_prop2_missing_attrs
  tide/tests/test_cf_property_2_missing_attrs.F90)
add_cf_test(test_cf_prop3_stdname_match
  tide/tests/test_cf_property_3_stdname_match.F90)
add_cf_test(test_cf_prop4_field_map
  tide/tests/test_cf_property_4_field_map.F90)
add_cf_test(test_cf_prop5_string_match
  tide/tests/test_cf_property_5_string_match.F90)
add_cf_test(test_cf_prop6_unit_comparison
  tide/tests/test_cf_property_6_unit_comparison.F90)
add_cf_test(test_cf_prop7_fallback
  tide/tests/test_cf_property_7_fallback.F90)
add_cf_test(test_cf_prop8_explicit_priority
  tide/tests/test_cf_property_8_explicit_priority.F90)
add_cf_test(test_cf_prop9_backward_compat
  tide/tests/test_cf_property_9_backward_compat.F90)
add_cf_test(test_cf_prop10_mode_control
  tide/tests/test_cf_property_10_mode_control.F90)
add_cf_test(test_cf_prop11_coord_detect
  tide/tests/test_cf_property_11_coord_detect.F90)
add_cf_test(test_cf_prop12_monotonicity
  tide/tests/test_cf_property_12_monotonicity.F90)
add_cf_test(test_cf_prop13_multidim
  tide/tests/test_cf_property_13_multidim.F90)
add_cf_test(test_cf_prop14_vertical_dim
  tide/tests/test_cf_property_14_vertical_dim.F90)
add_cf_test(test_cf_prop15_dim_reorder
  tide/tests/test_cf_property_15_dim_reorder.F90)
add_cf_test(test_cf_prop16_caching
  tide/tests/test_cf_property_16_caching.F90)
add_cf_test(test_cf_prop17_cache_invalidation
  tide/tests/test_cf_property_17_cache_invalidation.F90)
add_cf_test(test_cf_prop18_success_log
  tide/tests/test_cf_property_18_success_log.F90)
add_cf_test(test_cf_prop19_failure_log
  tide/tests/test_cf_property_19_failure_log.F90)
add_cf_test(test_cf_prop20_fieldmap_log
  tide/tests/test_cf_property_20_fieldmap_log.F90)
add_cf_test(test_cf_prop21_compliance
  tide/tests/test_cf_property_21_compliance.F90)
add_cf_test(test_cf_prop22_noncf_fallback
  tide/tests/test_cf_property_22_noncf_fallback.F90)

# Install rules
install(TARGETS tide cdeps_share streams dshr yaml-cpp EXPORT tide DESTINATION lib)
install(EXPORT tide DESTINATION lib/cmake)
