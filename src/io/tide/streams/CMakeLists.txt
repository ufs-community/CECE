set(SRCFILES dshr_methods_mod.F90 dshr_strdata_mod.F90 dshr_stream_mod.F90
             dshr_tinterp_mod.F90)

add_library(streams ${SRCFILES})
target_compile_definitions(streams PUBLIC DISABLE_FoX)
# Disable warnings about line truncation for legacy code in streams
target_compile_options(streams PRIVATE -Wno-line-truncation)

target_include_directories(
  streams PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)

target_include_directories(
  streams PRIVATE ${ESMF_F90COMPILEPATHS} ${PIO_Fortran_INCLUDE_DIR}
                  $<TARGET_PROPERTY:cdeps_share,INTERFACE_INCLUDE_DIRECTORIES>)

add_dependencies(streams cdeps_share)

install(
  TARGETS streams
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib)
