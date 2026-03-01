cmake_minimum_required(VERSION 3.20)
project(ACES LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Ensure Fortran modules are placed in a central directory expected by CDEPS
set(CMAKE_Fortran_MODULE_DIRECTORY ${CMAKE_BINARY_DIR}/share)
file(MAKE_DIRECTORY ${CMAKE_Fortran_MODULE_DIRECTORY})

list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Allow find_package to use <PackageName>_ROOT variables
if(POLICY CMP0074)
  cmake_policy(SET CMP0074 NEW)
endif()

# --- Dependencies ---

include(FetchContent)
include(GNUInstallDirs)

# Kokkos
find_package(Kokkos 4.2.00 QUIET)
if(NOT Kokkos_FOUND)
  FetchContent_Declare(
    kokkos
    GIT_REPOSITORY https://github.com/kokkos/kokkos.git
    GIT_TAG        4.2.00
  )
  FetchContent_MakeAvailable(kokkos)
endif()

# YAML-CPP
find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
  FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        0.8.0
  )
  FetchContent_MakeAvailable(yaml-cpp)
endif()

# GTest
if(BUILD_TESTING)
  find_package(GTest QUIET)
  if(NOT GTest_FOUND)
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG        v1.14.0
    )
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()
endif()

# Optional Fortran support
include(CheckLanguage)
check_language(Fortran)
if(CMAKE_Fortran_COMPILER)
  enable_language(Fortran)
  set(ACES_HAS_FORTRAN ON)
else()
  message(WARNING "Fortran compiler not found. Fortran features will be disabled.")
endif()

option(ACES_USE_MOCK_ESMF "Use Mock ESMF" OFF)

if(ACES_USE_MOCK_ESMF)
  add_library(ESMF::ESMF INTERFACE IMPORTED)
  target_compile_definitions(ESMF::ESMF INTERFACE ACES_USE_MOCK_ESMF)
  set(ESMF_FOUND TRUE)
  if(NOT TARGET esmf)
    add_library(esmf ALIAS ESMF::ESMF)
  endif()
else()
  find_package(ESMF REQUIRED)
  message(STATUS "Found ESMF: ${ESMF_VERSION}")
endif()
message(STATUS "ESMF include dirs: ${ESMF_INCLUDE_DIRS}")

# Discovery PIO and NetCDF from ESMF if possible
# CDEPS needs these as dependencies for its Fortran modules
if(NOT NetCDF_FOUND AND DEFINED ESMF_LIBRARIES)
  foreach(lib ${ESMF_LIBRARIES})
    if(lib MATCHES "^-L(.*)")
      set(search_path "${CMAKE_MATCH_1}")
      if(EXISTS "${search_path}/../include/netcdf.mod")
        set(NetCDF_ROOT "${search_path}/..")
        get_filename_component(NetCDF_ROOT "${NetCDF_ROOT}" ABSOLUTE)
        set(NetCDF_FOUND TRUE)
        set(ENV{NetCDF_ROOT} "${NetCDF_ROOT}")
        message(STATUS "Found NetCDF root from ESMF path: ${NetCDF_ROOT}")
        break()
      endif()
    endif()
  endforeach()
endif()

if(NOT PIO_FOUND)
  set(PIO_ROOT "$ENV{PIO_ROOT}")
  if(NOT PIO_ROOT)
    # ESMF often links against NetCDF/PIO, we might find them in the same view
      foreach(lib ${ESMF_LIBRARIES})
          if(lib MATCHES "^-L(.*)")
              set(search_path "${CMAKE_MATCH_1}")
              if(EXISTS "${search_path}/../include/pio.h")
                  set(PIO_ROOT "${search_path}/..")
                  get_filename_component(PIO_ROOT "${PIO_ROOT}" ABSOLUTE)
                  set(PIO_FOUND TRUE)
                  set(ENV{PIO_ROOT} "${PIO_ROOT}")
                  message(STATUS "Found PIO root from ESMF path: ${PIO_ROOT}")
                  break()
              endif()
          endif()
      endforeach()
  endif()
endif()

if(ACES_USE_MOCK_ESMF)
  set(MPI_FOUND TRUE)
  add_library(MPI::MPI_CXX INTERFACE IMPORTED)
else()
  find_package(MPI REQUIRED)
  message(STATUS "Found MPI: ${MPI_CXX_VERSION}")
endif()

# Also discover NetCDF-Fortran for CDEPS
if(NOT NetCDFF_FOUND AND DEFINED ESMF_LIBRARIES)
  foreach(lib ${ESMF_LIBRARIES})
    if(lib MATCHES "^-L(.*)")
      set(search_path "${CMAKE_MATCH_1}")
      if(EXISTS "${search_path}/../include/netcdf.mod")
        set(NetCDFF_ROOT "${search_path}/..")
        get_filename_component(NetCDFF_ROOT "${NetCDFF_ROOT}" ABSOLUTE)
        set(NetCDFF_FOUND TRUE)
        set(ENV{NetCDFF_ROOT} "${NetCDFF_ROOT}")
        message(STATUS "Found NetCDF-Fortran root from ESMF path: ${NetCDFF_ROOT}")
        break()
      endif()
    endif()
  endforeach()
endif()

# CDEPS is a required dependency for the hybrid ingestion layer.
if(ACES_USE_MOCK_ESMF)
  add_library(CDEPS::CDEPS INTERFACE IMPORTED)
  set(CDEPS_FOUND TRUE)
else()
  find_package(CDEPS QUIET)
endif()

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
      GIT_REPOSITORY https://github.com/ESCOMP/CDEPS.git
      GIT_TAG        main
    )
    # Force CDEPS to find the ESMF we already discovered
    # In JCSDA container, ESMF_ROOT might not be set, but ESMF_LIBRARIES contains it
    if(NOT ESMF_ROOT AND DEFINED ESMF_LIBRARIES)
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

    # Force CDEPS to use a larger line length for Fortran
    set(CMAKE_Fortran_FLAGS "${CMAKE_Fortran_FLAGS} -ffree-line-length-none" CACHE STRING "Fortran flags" FORCE)

    # CDEPS-standalone build has a bug where it looks for modules in ${CMAKE_BINARY_DIR}/share
    # but they are actually in ${CMAKE_CURRENT_BINARY_DIR} of the share directory.
    # We can override CMAKE_BINARY_DIR for the CDEPS sub-project or just link/copy the directory.
    # Actually, if we set CMAKE_BINARY_DIR it might affect everything.
    # Let's try to set the expected variable for CDEPS.
    set(CDEPS_SHARE_MOD_DIR "${CMAKE_BINARY_DIR}/_deps/cdeps-build/share")

    # We use a custom guard to ensure CDEPS is correctly integrated
    FetchContent_MakeAvailable(cdeps)
    if(TARGET streams)
      set(CDEPS_FOUND TRUE)
      if(NOT TARGET CDEPS::CDEPS)
          add_library(CDEPS::CDEPS ALIAS streams)
      endif()
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
  $<IF:$<TARGET_EXISTS:yaml-cpp>,yaml-cpp,yaml-cpp::yaml-cpp>
)

target_link_libraries(aces PUBLIC CDEPS::CDEPS)

if(MPI_FOUND)
    target_link_libraries(aces PUBLIC MPI::MPI_CXX)
endif()

# --- Tests ---

if(BUILD_TESTING)
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
endif()

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

if(BUILD_TESTING)
  include(GoogleTest)
  gtest_discover_tests(test_aces_cap)
  gtest_discover_tests(test_aces_utils)
  gtest_discover_tests(test_aces_compute)
  gtest_discover_tests(test_physics)
  gtest_discover_tests(test_aces_ingestor)
  gtest_discover_tests(test_diagnostics)
  gtest_discover_tests(test_hemco_mimic)
endif()

# --- Installation ---

install(TARGETS aces
  EXPORT ACESTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/aces
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT ACESTargets
  FILE ACESTargets.cmake
  NAMESPACE ACES::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ACES
)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/ACESConfigVersion.cmake"
  VERSION 0.1.0
  COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
  "cmake/ACESConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/ACESConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ACES
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/ACESConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/ACESConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ACES
)
