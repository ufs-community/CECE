set(SRCFILES  dshr_dfield_mod.F90
              dshr_fldlist_mod.F90
              dshr_mod.F90)

add_library(dshr ${SRCFILES})
target_compile_options(dshr PRIVATE -Wno-line-truncation)

target_include_directories(dshr PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)

target_include_directories(dshr PRIVATE
  ${ESMF_F90COMPILEPATHS}
  ${PIO_Fortran_INCLUDE_DIR}
)

target_link_libraries(dshr PUBLIC cdeps_share streams)

install(TARGETS dshr ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
