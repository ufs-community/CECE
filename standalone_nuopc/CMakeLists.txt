cmake_minimum_required(VERSION 3.20)
project(ACES_Standalone_NUOPC LANGUAGES Fortran CXX)

# Ensure executables are placed in the bin directory for consistency and caching
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# -----------------------------------------------------------------------
# ISO8601 utilities library for testing
# Contains ISO8601 parsing and formatting functions
# -----------------------------------------------------------------------
add_library(iso8601_utils STATIC iso8601_utils.F90)

target_include_directories(iso8601_utils
  PUBLIC
  ${ESMF_INCLUDE_DIRS}
  ${CMAKE_Fortran_MODULE_DIRECTORY}
)

target_link_libraries(iso8601_utils
  PUBLIC
  ESMF::ESMF
  ${ESMF_LIBRARIES}
)

if(TARGET MPI::MPI_Fortran)
  target_link_libraries(iso8601_utils PUBLIC MPI::MPI_Fortran)
endif()

# -----------------------------------------------------------------------
# NUOPC Application (MPI+OpenMP support)
# Full NUOPC_Driver architecture for production MPI+OpenMP hybrid execution
# Architecture:
#   mainApp.F90 - Entry point (creates NUOPC_Driver)
#   driver.F90 - NUOPC_Driver component (orchestrates ACES)
#   aces_cap.F90 - ACES component cap (in src/, implements ACES physics)
# -----------------------------------------------------------------------
add_executable(aces_nuopc_app mainApp.F90 driver.F90)

target_include_directories(aces_nuopc_app
  PRIVATE
  ${ESMF_INCLUDE_DIRS}
)

target_link_libraries(aces_nuopc_app
  PRIVATE
  aces
  ESMF::ESMF
  yaml-cpp
  ${ESMF_LIBRARIES}
)

if(TARGET MPI::MPI_Fortran)
  target_link_libraries(aces_nuopc_app PRIVATE MPI::MPI_Fortran)
endif()

# Allow undefined symbols from shared library (will be resolved at runtime)
target_link_options(aces_nuopc_app PRIVATE "LINKER:--allow-shlib-undefined")

# Ensure Fortran modules from aces are available before building the app
if(TARGET aces)
  add_dependencies(aces_nuopc_app aces)
endif()

# -----------------------------------------------------------------------
# Example 5 integration test (Fortran ESMF, no ESMC)
# Runs aces_config_ex5.yaml through the full NUOPC phase sequence and
# verifies that co, no, and so2 export fields are nonzero and distinct.
# -----------------------------------------------------------------------
add_executable(test_example5_driver ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_example5_driver.F90 driver.F90)

target_include_directories(test_example5_driver PRIVATE ${ESMF_INCLUDE_DIRS})

target_link_libraries(test_example5_driver
  PRIVATE
  aces
  iso8601_utils
  ESMF::ESMF
  yaml-cpp
  ${ESMF_LIBRARIES}
)

if(TARGET MPI::MPI_Fortran)
  target_link_libraries(test_example5_driver PRIVATE MPI::MPI_Fortran)
endif()

target_link_options(test_example5_driver PRIVATE "LINKER:--allow-shlib-undefined")

if(TARGET aces)
  add_dependencies(test_example5_driver aces)
endif()

# Run from the workspace root so examples/aces_config_ex5.yaml and
# data/ files are reachable via relative paths.
add_test(
  NAME test_example5_ex5_run
  COMMAND test_example5_driver
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# -----------------------------------------------------------------------
# Installation
# -----------------------------------------------------------------------
install(TARGETS aces_nuopc_app DESTINATION bin)
