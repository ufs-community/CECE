# Validate and generate with the real UFS-pinned framework, not a stub generator.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_program(CECE_XMLLINT xmllint REQUIRED)
set(_caps "${PROJECT_SOURCE_DIR}/src/driver/ccpp")
set(_schemes "${_caps}/cece_native_example.meta,${_caps}/cece_bdsnp_yl95.meta,${_caps}/cece_megan3.meta")
set(_generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CECE_CCPP_FRAMEWORK_DIR}/capgen/ccpp_validator.py"
  --scheme-files "${_schemes}"
  --source-files "${_caps}/cece_native_example.F90,${_caps}/cece_bdsnp_yl95.F90,${_caps}/cece_megan3.F90"
  COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CECE_CCPP_FRAMEWORK_DIR}/capgen/ccpp_capgen.py"
  --host-name cece_test --host-files "${CMAKE_CURRENT_SOURCE_DIR}/host.meta"
  --scheme-files "${_schemes}" --suites "${CMAKE_CURRENT_SOURCE_DIR}/suite_cece.xml"
  --output-root "${_generated}" COMMAND_ERROR_IS_FATAL ANY)
file(GLOB _generated_sources CONFIGURE_DEPENDS "${_generated}/*.F90")
add_executable(test_cece_ccpp_suite host.F90 test_generated_suite.F90 ${_generated_sources})
target_link_libraries(test_cece_ccpp_suite PRIVATE cece_ccpp)
set_target_properties(test_cece_ccpp_suite PROPERTIES Fortran_MODULE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules")
target_include_directories(test_cece_ccpp_suite PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/modules")
if(CMAKE_Fortran_COMPILER_ID STREQUAL "GNU")
  target_compile_options(test_cece_ccpp_suite PRIVATE -ffree-line-length-none -fcheck=all -fbacktrace)
endif()
add_test(NAME ccpp_generated_suite COMMAND test_cece_ccpp_suite "${PROJECT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/flux.txt")
