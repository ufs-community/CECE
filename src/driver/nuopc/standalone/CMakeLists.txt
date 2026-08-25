cmake_minimum_required(VERSION 3.20)
project(CECE_Standalone_NUOPC LANGUAGES Fortran CXX)

# Set the shared Fortran module directory
set(CMAKE_Fortran_MODULE_DIRECTORY ${CMAKE_BINARY_DIR}/modules)

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

# NUOPC Application (MPI+OpenMP support) Full NUOPC_Driver architecture for
# production MPI+OpenMP hybrid execution Architecture: mainApp.F90 - Entry point
# (creates NUOPC_Driver) driver.F90 - NUOPC_Driver component (orchestrates CECE)
# -----------------------------------------------------------------------
add_executable(cece_nuopc_app mainApp.F90 driver.F90)

set_target_properties(cece_nuopc_app PROPERTIES LINKER_LANGUAGE Fortran)

# Include ESMF directories and our unified module search path
target_include_directories(
  cece_nuopc_app
  PRIVATE ${ESMF_INCLUDE_DIRS} ${CMAKE_Fortran_MODULE_DIRECTORY}
)

# Link against our unified cece_nuopc_cap library target, cece, ESMF, etc.
target_link_libraries(cece_nuopc_app PRIVATE cece_nuopc_cap cece ESMF::ESMF ${ESMF_LIBRARIES})

if(TARGET MPI::MPI_Fortran)
  target_link_libraries(cece_nuopc_app PRIVATE MPI::MPI_Fortran)
endif()

# Allow undefined symbols from shared library (will be resolved at runtime)
if(APPLE)
  target_link_options(cece_nuopc_app PRIVATE "LINKER:-undefined,dynamic_lookup")
else()
  target_link_options(cece_nuopc_app PRIVATE "LINKER:--allow-shlib-undefined")
endif()

# Ensure Fortran modules from cap are available before building the app
add_dependencies(cece_nuopc_app cece_nuopc_cap)

# -----------------------------------------------------------------------
# Installation
# -----------------------------------------------------------------------
install(TARGETS cece_nuopc_app DESTINATION bin)
