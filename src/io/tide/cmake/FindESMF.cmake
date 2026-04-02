
if (DEFINED ENV{ESMFMKFILE})
  set(ESMFMKFILE $ENV{ESMFMKFILE})
  message("ESMFMKFILE from ENV:   ${ESMFMKFILE}")
elseif (DEFINED ESMFMKFILE)
  message("ESMFMKFILE from CACHE: ${ESMFMKFILE}")
else()
  # Try to find it in common locations
  find_file(ESMFMKFILE esmf.mk PATHS /opt/views/view/lib /usr/lib /usr/local/lib)
  if (NOT ESMFMKFILE)
    message(FATAL_ERROR "ESMFMKFILE not defined and esmf.mk not found")
  endif()
  message("ESMFMKFILE found:      ${ESMFMKFILE}")
endif()

# convert esmf.mk makefile variables to cmake variables until ESMF
# provides proper cmake package
file(STRINGS ${ESMFMKFILE} esmf_mk_text)
foreach(line ${esmf_mk_text})
  string(REGEX REPLACE "^[ ]+" "" line ${line}) # strip leading spaces
  if (line MATCHES "^ESMF_*")                   # process only line starting with ESMF_
    string(REGEX MATCH "^ESMF_[^=]+" esmf_name ${line})
    string(REPLACE "${esmf_name}=" "" emsf_value ${line})
    set(${esmf_name} "${emsf_value}")
  endif()
endforeach()
string(REPLACE "-I" "" ESMF_F90COMPILEPATHS ${ESMF_F90COMPILEPATHS})
string(REPLACE " " ";" ESMF_F90COMPILEPATHS ${ESMF_F90COMPILEPATHS})

# We use only these 4 variables in our build system. Make sure they are all set
if(ESMF_VERSION_MAJOR AND
   ESMF_F90COMPILEPATHS AND
   ESMF_F90ESMFLINKRPATHS AND
   ESMF_F90ESMFLINKLIBS)
  message(" Found ESMF:")
  message("ESMF_VERSION_MAJOR:     ${ESMF_VERSION_MAJOR}")
  message("ESMF_F90COMPILEPATHS:   ${ESMF_F90COMPILEPATHS}")
  message("ESMF_F90ESMFLINKRPATHS: ${ESMF_F90ESMFLINKRPATHS}")
  message("ESMF_F90ESMFLINKLIBS:   ${ESMF_F90ESMFLINKLIBS}")
else()
  message("One of the ESMF_ variables is not defined")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ESMF
                                    FOUND_VAR
                                      ESMF_FOUND
                                    REQUIRED_VARS
                                      ESMF_F90COMPILEPATHS
                                      ESMF_F90ESMFLINKRPATHS
                                      ESMF_F90ESMFLINKLIBS
                                    VERSION_VAR
                                      ESMF_VERSION_STRING)
