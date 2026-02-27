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

# ESMF
option(ACES_USE_MOCK_ESMF "Use internal mock ESMF if system ESMF not found" ON)

# Optional Fortran support
include(CheckLanguage)
check_language(Fortran)
if(CMAKE_Fortran_COMPILER)
  enable_language(Fortran)
  set(ACES_HAS_FORTRAN ON)
else()
  message(WARNING "Fortran compiler not found. Fortran features will be disabled.")
endif()

find_package(ESMF QUIET)

if(ESMF_FOUND)
  message(STATUS "Found ESMF: ${ESMF_VERSION}")

  # Real ESMF usually requires MPI because ESMC.h includes mpi.h.
  find_package(MPI REQUIRED)
  if(MPI_FOUND)
    message(STATUS "Found MPI: ${MPI_CXX_VERSION}")
  endif()

else()
  if(ACES_USE_MOCK_ESMF)
    message(STATUS "ESMF not found, using internal mock.")
    add_subdirectory(externals/esmf_mock)
    target_compile_definitions(esmf_mock PUBLIC ACES_USING_MOCK_ESMF)
    if(NOT TARGET ESMF::ESMF)
        add_library(ESMF::ESMF ALIAS esmf_mock)
    endif()
  else()
    message(FATAL_ERROR "ESMF not found and mock is disabled.")
  endif()
endif()

# --- ACES Library ---

set(ACES_SRCS
  src/aces_cap.cpp
  src/aces_config_parser.cpp
  src/aces_compute.cpp
  src/aces_physics_factory.cpp
  src/physics/aces_native_example.cpp
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

if(NOT ACES_USE_MOCK_ESMF)
  add_executable(example_driver
    tests/example_driver.cpp
  )

  target_link_libraries(example_driver
    PRIVATE
    aces
  )
endif()

include(GoogleTest)
gtest_discover_tests(test_aces_cap)
gtest_discover_tests(test_aces_utils)
gtest_discover_tests(test_aces_compute)
gtest_discover_tests(test_physics)
