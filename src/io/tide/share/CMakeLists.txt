project(TIDE_share Fortran)
include(ExternalProject)
set (GenF90_SRCS shr_infnan_mod.F90
                 shr_assert_mod.F90)

add_library(cdeps_share STATIC ${GenF90_SRCS}
                         shr_nl_mod.F90
                         shr_timer_mod.F90
                         shr_cal_mod.F90
                         shr_kind_mod.F90
                         shr_file_mod.F90
                         shr_sys_mod.F90
                         shr_abort_mod.F90
                         shr_const_mod.F90
                         shr_orb_mod.F90
                         shr_log_mod.F90
                         shr_strconvert_mod.F90
                         shr_string_mod.F90
                         nuopc_shr_methods.F90)

target_include_directories (cdeps_share PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)
target_include_directories (cdeps_share PRIVATE ${ESMF_F90COMPILEPATHS} ${PIO_Fortran_INCLUDE_DIR})

target_compile_definitions(cdeps_share PUBLIC CPRGNU)

install(TARGETS cdeps_share ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)

# genf90 support
if (NOT DEFINED GENF90_PATH)
  ExternalProject_Add (genf90
    PREFIX ${CMAKE_CURRENT_BINARY_DIR}/genf90
    GIT_REPOSITORY https://github.com/PARALLELIO/genf90
    GIT_TAG genf90_200608
    UPDATE_COMMAND "" CONFIGURE_COMMAND "" BUILD_COMMAND "" INSTALL_COMMAND "")
  ExternalProject_Get_Property (genf90 SOURCE_DIR)
  set (GENF90_PATH ${SOURCE_DIR})
endif ()

foreach (SRC_FILE IN LISTS GenF90_SRCS)
  add_custom_command (OUTPUT ${SRC_FILE}
    COMMAND ${GENF90_PATH}/genf90.pl ${CMAKE_CURRENT_SOURCE_DIR}/${SRC_FILE}.in > ${SRC_FILE}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SRC_FILE}.in genf90)
endforeach ()
