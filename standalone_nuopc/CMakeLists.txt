cmake_minimum_required(VERSION 3.20)
project(CECE_Standalone_NUOPC LANGUAGES Fortran CXX)

# Ensure executables are placed in the bin directory for consistency and caching
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# -----------------------------------------------------------------------
# ISO8601 utilities library for testing Contains ISO8601 parsing and formatting
# functions
# -----------------------------------------------------------------------
add_library(iso8601_utils STATIC iso8601_utils.F90)

target_include_directories(
  iso8601_utils
  PUBLIC ${ESMF_INCLUDE_DIRS} ${CMAKE_Fortran_MODULE_DIRECTORY}
)

target_link_libraries(iso8601_utils PUBLIC ESMF::ESMF ${ESMF_LIBRARIES})

if(TARGET MPI::MPI_Fortran)
  target_link_libraries(iso8601_utils PUBLIC MPI::MPI_Fortran)
endif()

# -----------------------------------------------------------------------
# NUOPC Application (MPI+OpenMP support) Full NUOPC_Driver architecture for
# production MPI+OpenMP hybrid execution Architecture: mainApp.F90 - Entry point
# (creates NUOPC_Driver) driver.F90 - NUOPC_Driver component (orchestrates CECE)
# cece_cap.F90 - CECE component cap (in src/, implements CECE physics)
# -----------------------------------------------------------------------
add_executable(cece_nuopc_app mainApp.F90 driver.F90)

target_include_directories(cece_nuopc_app PRIVATE ${ESMF_INCLUDE_DIRS})

target_link_libraries(cece_nuopc_app PRIVATE cece ESMF::ESMF yaml-cpp ${ESMF_LIBRARIES})

if(TARGET MPI::MPI_Fortran)
  target_link_libraries(cece_nuopc_app PRIVATE MPI::MPI_Fortran)
endif()

# Allow undefined symbols from shared library (will be resolved at runtime)
target_link_options(cece_nuopc_app PRIVATE "LINKER:--allow-shlib-undefined")

# Ensure Fortran modules from cece are available before building the app
if(TARGET cece)
  add_dependencies(cece_nuopc_app cece)
endif()

# -----------------------------------------------------------------------
# Installation
# -----------------------------------------------------------------------
install(TARGETS cece_nuopc_app DESTINATION bin)
