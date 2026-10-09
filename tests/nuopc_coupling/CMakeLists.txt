# -----------------------------------------------------------------------
# NUOPC field-coupling integration harness (spec 003).
# -----------------------------------------------------------------------
# Two-component driver app: the production CECE cap plus a minimal sink peer
# connected through a NUOPC Connector. Built only when ESMF and Fortran are
# available (the parent gate in src/driver/nuopc/CMakeLists.txt).

set(CMAKE_Fortran_MODULE_DIRECTORY ${CMAKE_BINARY_DIR}/modules)

add_executable(
  cece_nuopc_coupling_test
  cece_sink_mod.F90
  cece_met_source_mod.F90
  test_coupling_app.F90
)

set_target_properties(cece_nuopc_coupling_test PROPERTIES LINKER_LANGUAGE Fortran)

target_include_directories(
  cece_nuopc_coupling_test
  PRIVATE ${ESMF_INCLUDE_DIRS} ${CMAKE_Fortran_MODULE_DIRECTORY}
)

# cece_nuopc_cap carries the production cap and links the cece libraries
# (which export the path-based nuopc config queries the app uses for sink
# setup) and ESMF transitively. The explicit cece/ESMF entries mirror the
# standalone cap app's link line: the Fortran linker needs MPI on the command
# line in the same order to resolve libmpi symbols.
target_link_libraries(
  cece_nuopc_coupling_test
  PRIVATE cece_nuopc_cap cece ESMF::ESMF ${ESMF_LIBRARIES}
)

if(TARGET MPI::MPI_Fortran)
  target_link_libraries(cece_nuopc_coupling_test PRIVATE MPI::MPI_Fortran)
endif()

# -----------------------------------------------------------------------
# Integration test: sink-received exports == standalone output (np1/np2),
# unconnected exports pruned at realization. Gated on the CEDS fixture like
# the driver parity tests.
# -----------------------------------------------------------------------
if(EXISTS "${PROJECT_SOURCE_DIR}/data/OC-em-anthro_CMIP_CEDS_2023.nc")
  add_test(
    NAME test_nuopc_field_coupling
    COMMAND
      ${CMAKE_COMMAND} -E env "MPIEXEC_EXECUTABLE=${MPIEXEC_EXECUTABLE}"
      "MPIEXEC_NUMPROC_FLAG=${MPIEXEC_NUMPROC_FLAG}" bash
      ${CMAKE_CURRENT_SOURCE_DIR}/test_nuopc_field_coupling.sh --app
      $<TARGET_FILE:cece_nuopc_coupling_test> --nuopc-app $<TARGET_FILE:cece_nuopc_app> --cxx-driver
      $<TARGET_FILE:cece_standalone_driver> --config
      ${PROJECT_SOURCE_DIR}/examples/cece_config_ceds_oc_small_coupled.yaml --dictionary
      ${CMAKE_CURRENT_SOURCE_DIR}/test_field_dictionary.yaml --comparator
      ${PROJECT_SOURCE_DIR}/tests/nccmp_var.c --var oc --workdir
      ${CMAKE_BINARY_DIR}/_coupling_workdir
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  set_tests_properties(
    test_nuopc_field_coupling
    PROPERTIES
      DEPENDS "cece_nuopc_coupling_test;cece_nuopc_app;cece_standalone_driver"
      ENVIRONMENT
        "FI_PROVIDER=tcp;I_MPI_FABRICS=shm;OMPI_ALLOW_RUN_AS_ROOT=1;OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1"
      TIMEOUT 900
      LABELS np2
      PROCESSORS 2
  )
endif()
