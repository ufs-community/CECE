cmake_minimum_required(VERSION 3.20)
project(ACES LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

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

find_package(ESMF REQUIRED)
message(STATUS "Found ESMF: ${ESMF_VERSION}")

# Discovery PIO from ESMF if possible
if(NOT PIO_FOUND)
  set(PIO_ROOT "$ENV{PIO_ROOT}")
  if(NOT PIO_ROOT)
    # ESMF often links against NetCDF/PIO, we might find them in the same view
      # We check common library lists in ESMF_LIBRARIES
      foreach(lib ${ESMF_LIBRARIES})
          if(lib MATCHES "^-L(.*)")
              set(search_path "${CMAKE_MATCH_1}/../include")
              find_path(PIO_INCLUDE_DIR NAMES pio.h PATHS "${search_path}" NO_DEFAULT_PATH)
              if(PIO_INCLUDE_DIR)
                  set(PIO_FOUND TRUE)
                  message(STATUS "Found PIO include from ESMF path: ${PIO_INCLUDE_DIR}")
                  break()
              endif()
          endif()
      endforeach()
  endif()
endif()

find_package(MPI REQUIRED)
message(STATUS "Found MPI: ${MPI_CXX_VERSION}")

# CDEPS is a required dependency for the hybrid ingestion layer.
find_package(CDEPS QUIET)

# Also look for cdeps in the current source dir if it was fetched
if(NOT CDEPS_FOUND AND EXISTS "${CMAKE_BINARY_DIR}/_deps/cdeps-src")
    set(CDEPS_FOUND TRUE)
endif()

if(NOT CDEPS_FOUND AND ACES_HAS_FORTRAN)
  message(STATUS "CDEPS not found on system, attempting to build from source using FetchContent...")

  # CDEPS requires PIO
  find_package(PIO QUIET)

  # For FetchContent to work well with CDEPS, we may need to set some variables
  if(PIO_INCLUDE_DIR)
    set(PIO_ROOT ${PIO_INCLUDE_DIR}/..)
  endif()

  FetchContent_Declare(
    cdeps
    GIT_REPOSITORY https://github.com/ESCOMP/CDEPS.git
    GIT_TAG        main
  )
  FetchContent_MakeAvailable(cdeps)
  if(TARGET streams)
      set(CDEPS_FOUND TRUE)
      if(NOT TARGET CDEPS::CDEPS)
          add_library(CDEPS::CDEPS ALIAS streams)
      endif()
      message(STATUS "CDEPS built from source successfully.")
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
  src/io/aces_data_ingestor.cpp
)

if(ACES_HAS_FORTRAN)
  list(APPEND ACES_SRCS
    src/physics/aces_fortran_bridge.cpp
    src/physics/legacy_fortran.F90
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

target_link_libraries(aces PUBLIC CDEPS::CDEPS)

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

add_executable(example_driver
  tests/example_driver.cpp
)

target_link_libraries(example_driver
  PRIVATE
  aces
)

include(GoogleTest)
gtest_discover_tests(test_aces_cap)
gtest_discover_tests(test_aces_utils)
gtest_discover_tests(test_aces_compute)
gtest_discover_tests(test_physics)
gtest_discover_tests(test_aces_ingestor)
