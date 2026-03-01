cmake_minimum_required(VERSION 3.20)
project(ACES_Standalone_NUOPC LANGUAGES CXX)

# Ensure the executable is placed in the bin directory for consistency and caching
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# The standalone driver is built as an executable
add_executable(aces_nuopc_driver main.cpp)

# Link against the ACES library and its dependencies
# yaml-cpp is provided by the parent project via FetchContent or found via find_package
if(NOT TARGET yaml-cpp)
  find_package(yaml-cpp REQUIRED)
endif()

target_link_libraries(aces_nuopc_driver
  PRIVATE
  aces
  ESMF::ESMF
  MPI::MPI_CXX
  yaml-cpp
)

# Installation
install(TARGETS aces_nuopc_driver DESTINATION bin)
