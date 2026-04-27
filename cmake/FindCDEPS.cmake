# FindCDEPS.cmake
#
# Finds the CDEPS library.
#
# Variables defined: CDEPS_FOUND CDEPS_INCLUDE_DIRS CDEPS_LIBRARIES
#
# Targets defined: CDEPS::CDEPS

if(TARGET CDEPS::CDEPS)
  set(CDEPS_FOUND TRUE)
  return()
endif()

find_package(CDEPS CONFIG QUIET)

if(CDEPS_FOUND)
  if(NOT TARGET CDEPS::CDEPS)
    add_library(CDEPS::CDEPS INTERFACE IMPORTED)
    if(DEFINED CDEPS_INCLUDE_DIRS)
      target_include_directories(CDEPS::CDEPS INTERFACE ${CDEPS_INCLUDE_DIRS})
    endif()
    if(DEFINED CDEPS_LIBRARIES)
      target_link_libraries(CDEPS::CDEPS INTERFACE ${CDEPS_LIBRARIES})
    endif()
  endif()
else()
  # Basic fallback for common locations
  find_path(
    CDEPS_INCLUDE_DIR
    NAMES cdeps_inline.h
    PATHS /opt/software/cdeps/include /usr/local/include
  )
  find_library(CDEPS_LIBRARY NAMES cdeps PATHS /opt/software/cdeps/lib /usr/local/lib)

  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(CDEPS DEFAULT_MSG CDEPS_LIBRARY CDEPS_INCLUDE_DIR)

  if(CDEPS_FOUND)
    set(CDEPS_INCLUDE_DIRS ${CDEPS_INCLUDE_DIR})
    set(CDEPS_LIBRARIES ${CDEPS_LIBRARY})
    if(NOT TARGET CDEPS::CDEPS)
      add_library(CDEPS::CDEPS INTERFACE IMPORTED)
      target_include_directories(CDEPS::CDEPS INTERFACE ${CDEPS_INCLUDE_DIRS})
      target_link_libraries(CDEPS::CDEPS INTERFACE ${CDEPS_LIBRARIES})
    endif()
  endif()
endif()
