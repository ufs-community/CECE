cmake_minimum_required(VERSION 3.20)
project(ACES_Standalone_NUOPC LANGUAGES Fortran CXX)

# Ensure executables are placed in the bin directory for consistency and caching
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# -----------------------------------------------------------------------
# Legacy standalone driver (non-NUOPC, direct phase calls)
# -----------------------------------------------------------------------
add_executable(aces_nuopc_driver main.F90)

if(NOT TARGET yaml-cpp)
  find_package(yaml-cpp REQUIRED)
endif()

target_link_libraries(aces_nuopc_driver
  PRIVATE
  aces
  ESMF::ESMF
  yaml-cpp
)

if(TARGET MPI::MPI_Fortran)
  target_link_libraries(aces_nuopc_driver PRIVATE MPI::MPI_Fortran)
endif()

# -----------------------------------------------------------------------
# NUOPC Single-Model Driver (Requirements 2.1-2.15)
# Proper NUOPC_Driver-based standalone driver with full phase handling:
#   Advertise -> Realize -> Initialize (P1, P2) -> Run loop -> Finalize
# -----------------------------------------------------------------------
add_executable(aces_nuopc_single_driver single_driver.F90)

target_include_directories(aces_nuopc_single_driver
  PRIVATE
  ${ESMF_INCLUDE_DIRS}
  ${CMAKE_Fortran_MODULE_DIRECTORY}
)

target_link_libraries(aces_nuopc_single_driver
  PRIVATE
  aces
  ESMF::ESMF
  yaml-cpp
)

if(TARGET MPI::MPI_Fortran)
  target_link_libraries(aces_nuopc_single_driver PRIVATE MPI::MPI_Fortran)
endif()

# Ensure Fortran modules from aces are available before building the driver
if(TARGET aces)
  add_dependencies(aces_nuopc_single_driver aces)
endif()

# -----------------------------------------------------------------------
# Installation
# -----------------------------------------------------------------------
install(TARGETS aces_nuopc_driver aces_nuopc_single_driver DESTINATION bin)
