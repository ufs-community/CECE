# Set a unified, shared Fortran module output directory
set(CMAKE_Fortran_MODULE_DIRECTORY ${CMAKE_BINARY_DIR}/modules)

# -----------------------------------------------------------------------
# NUOPC Model Cap target for CECE
# -----------------------------------------------------------------------
# Compile the official production-grade NUOPC Cap as a static library
add_library(cece_nuopc_cap STATIC cece_cap.F90 cece_cap_grid.F90)

target_include_directories(
  cece_nuopc_cap
  PUBLIC ${ESMF_INCLUDE_DIRS} ${CMAKE_Fortran_MODULE_DIRECTORY}
)

target_link_libraries(cece_nuopc_cap PUBLIC cece ESMF::ESMF ${ESMF_LIBRARIES})

if(TARGET MPI::MPI_Fortran)
  target_link_libraries(cece_nuopc_cap PUBLIC MPI::MPI_Fortran)
endif()

# Include the standalone mock testing harness
add_subdirectory(standalone)
