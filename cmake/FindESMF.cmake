# FindESMF.cmake
#
# Finds the ESMF library.
#
# This module checks for ESMF in the following order: 1. Config mode
# (ESMFConfig.cmake) 2. Parsing esmf.mk (via ESMFMKFILE environment variable)
#
# Variables defined: ESMF_FOUND ESMF_INCLUDE_DIRS ESMF_LIBRARIES ESMF_VERSION
#
# Targets defined: ESMF::ESMF

if(TARGET ESMF::ESMF)
  set(ESMF_FOUND TRUE)
  return()
endif()

# 1. Try Config mode first
find_package(ESMF CONFIG QUIET)

if(ESMF_FOUND)
  if(NOT TARGET ESMF::ESMF)
    add_library(ESMF::ESMF INTERFACE IMPORTED)
    if(DEFINED ESMF_INCLUDE_DIRS)
      target_include_directories(ESMF::ESMF INTERFACE ${ESMF_INCLUDE_DIRS})
    endif()
    if(DEFINED ESMF_LIBRARIES)
      target_link_libraries(ESMF::ESMF INTERFACE ${ESMF_LIBRARIES})
    endif()
  endif()
  return()
endif()

# 1. Try parsing esmf.mk
if(NOT ESMF_FOUND)
  set(ESMFMKFILE "$ENV{ESMFMKFILE}")
  if(NOT ESMFMKFILE)
    set(ESMFMKFILE "$ENV{ESMF_MK_FILE}")
  endif()

  if(NOT ESMFMKFILE AND DEFINED ENV{ESMF_ROOT})
    if(EXISTS "$ENV{ESMF_ROOT}/lib/esmf.mk")
      set(ESMFMKFILE "$ENV{ESMF_ROOT}/lib/esmf.mk")
    endif()
  endif()

  if(ESMFMKFILE AND EXISTS "${ESMFMKFILE}")
    message(STATUS "Found esmf.mk: ${ESMFMKFILE}")

    # Also add the directory containing esmf.mk to search paths for modules
    get_filename_component(ESMF_MK_DIR "${ESMFMKFILE}" DIRECTORY)
    list(APPEND ESMF_INCLUDE_DIRS "${ESMF_MK_DIR}")

    # Create a temporary Makefile to extract variables We extract include paths
    # and link flags separately to avoid mixing them up
    set(MK_FILE "${CMAKE_BINARY_DIR}/esmf_probe.mk")
    file(
      WRITE "${MK_FILE}"
      "include ${ESMFMKFILE}\nprint_inc:\n\t@echo \"__ESMF_INC__\$(ESMF_F90COMPILEPATHS)\"\nprint_lib:\n\t@echo \"__ESMF_LIB__\$(ESMF_F90LINKPATHS) \$(ESMF_F90LINKRPATHS) \$(ESMF_F90ESMFLINKLIBS)\"\n"
    )

    # Get Includes - use grep to strictly capture our tagged line and avoid
    # 'make' noise
    execute_process(
      COMMAND make --no-print-directory -f "${MK_FILE}" print_inc
      COMMAND grep "__ESMF_INC__"
      OUTPUT_VARIABLE ESMF_INC_RAW
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(ESMF_INC_RAW MATCHES "__ESMF_INC__([^\n\r]*)")
      set(ESMF_INC_RAW "${CMAKE_MATCH_1}")
    endif()

    # Get Libs - use grep to strictly capture our tagged line and avoid 'make'
    # noise
    execute_process(
      COMMAND make --no-print-directory -f "${MK_FILE}" print_lib
      COMMAND grep "__ESMF_LIB__"
      OUTPUT_VARIABLE ESMF_LIB_RAW
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(ESMF_LIB_RAW MATCHES "__ESMF_LIB__([^\n\r]*)")
      set(ESMF_LIB_RAW "${CMAKE_MATCH_1}")
    endif()

    # Process Includes
    string(REPLACE " " ";" ESMF_INC_LIST "${ESMF_INC_RAW}")
    set(ESMF_INCLUDE_DIRS "")
    foreach(item ${ESMF_INC_LIST})
      if(item MATCHES "^-I(.*)")
        list(APPEND ESMF_INCLUDE_DIRS "${CMAKE_MATCH_1}")
      endif()
    endforeach()

    # Process Libs
    string(REPLACE " " ";" ESMF_LIB_LIST "${ESMF_LIB_RAW}")
    set(ESMF_LIBRARIES "")
    foreach(item ${ESMF_LIB_LIST})
      if(item)
        list(APPEND ESMF_LIBRARIES "${item}")
      endif()
    endforeach()

    if(ESMF_INCLUDE_DIRS OR ESMF_LIBRARIES)
      set(ESMF_FOUND TRUE)
    endif()

    # Many ESMF installations put Fortran module files in the lib directory or a
    # separate 'mod' directory.
    foreach(lib_path ${ESMF_LIBRARIES})
      set(lib_dir "")
      if(IS_DIRECTORY "${lib_path}")
        set(lib_dir "${lib_path}")
      elseif(lib_path MATCHES "^-L(.*)")
        set(lib_dir "${CMAKE_MATCH_1}")
      else()
        get_filename_component(lib_dir "${lib_path}" DIRECTORY)
      endif()

      if(IS_DIRECTORY "${lib_dir}")
        list(APPEND ESMF_INCLUDE_DIRS "${lib_dir}")
        # Also check for 'mod' and 'include' directories at the same level or
        # one level up
        if(IS_DIRECTORY "${lib_dir}/../mod")
          list(APPEND ESMF_INCLUDE_DIRS "${lib_dir}/../mod")
        endif()
        if(IS_DIRECTORY "${lib_dir}/../include")
          list(APPEND ESMF_INCLUDE_DIRS "${lib_dir}/../include")
        endif()
      endif()
    endforeach()
    if(ESMF_INCLUDE_DIRS)
      list(REMOVE_DUPLICATES ESMF_INCLUDE_DIRS)
    endif()

    # If we still haven't found esmf.mod, search for it more broadly
    set(esmf_mod_found FALSE)
    foreach(dir ${ESMF_INCLUDE_DIRS})
      if(EXISTS "${dir}/esmf.mod")
        set(esmf_mod_found TRUE)
        break()
      endif()
    endforeach()

    if(NOT esmf_mod_found)
      # Search relative to library paths and common JCSDA locations
      foreach(lib_path ${ESMF_LIBRARIES})
        if(lib_path MATCHES "^-L(.*)")
          set(base_dir "${CMAKE_MATCH_1}/..")
          find_path(
            EXTRA_ESMF_MOD_DIR
            NAMES esmf.mod
            PATHS "${base_dir}/include" "${base_dir}/mod" "${base_dir}/lib"
            NO_DEFAULT_PATH
          )
          if(EXTRA_ESMF_MOD_DIR)
            list(APPEND ESMF_INCLUDE_DIRS "${EXTRA_ESMF_MOD_DIR}")
          endif()
        endif()
      endforeach()
    endif()
  endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ESMF DEFAULT_MSG ESMF_LIBRARIES ESMF_INCLUDE_DIRS)

if(ESMF_FOUND)
  if(NOT TARGET ESMF::ESMF)
    add_library(ESMF::ESMF INTERFACE IMPORTED)
    target_include_directories(ESMF::ESMF INTERFACE ${ESMF_INCLUDE_DIRS})
    target_link_libraries(ESMF::ESMF INTERFACE ${ESMF_LIBRARIES})
  endif()
  # Provide lowercase alias for compatibility with projects like TIDE
  if(NOT TARGET esmf)
    add_library(esmf ALIAS ESMF::ESMF)
  endif()
endif()
