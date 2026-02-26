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
    if(NOT TARGET ESMF::ESMF)
        add_library(ESMF::ESMF ALIAS esmf_mock)
    endif()
  else()
    message(FATAL_ERROR "ESMF not found and mock is disabled.")
  endif()
endif()

# --- ACES Library ---

add_library(aces SHARED
  src/aces_cap.cpp
)

target_include_directories(aces PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(aces PUBLIC
  Kokkos::kokkos
  ESMF::ESMF
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

include(GoogleTest)
gtest_discover_tests(test_aces_cap)
