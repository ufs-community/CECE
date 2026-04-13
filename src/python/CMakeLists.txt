cmake_minimum_required(VERSION 3.20)

# Python bindings for ACES via pybind11

# Find Python
find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)

message(STATUS "Found Python3: ${Python3_EXECUTABLE}")
message(STATUS "Python3 version: ${Python3_VERSION}")
message(STATUS "Python3 include dirs: ${Python3_INCLUDE_DIRS}")

# Fetch pybind11 v2.12.0
include(FetchContent)

# Disable LTO before fetching pybind11 to work around GCC 13 internal compiler error.
# pybind11 enables LTO by default via pybind11::lto target.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)

FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG v2.12.0
)
FetchContent_MakeAvailable(pybind11)

# Build the pybind11 extension module
pybind11_add_module(_aces_core aces_pybind.cpp)

target_link_libraries(_aces_core PRIVATE aces Kokkos::kokkos yaml-cpp)

# Place the compiled .so in the aces package directory
set_target_properties(_aces_core PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/aces"
)

# Create Python package directory
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/aces")

# Copy Python source files to build directory (excluding old binding files)
file(COPY
    __init__.py
    config.py
    state.py
    exceptions.py
    utils.py
    py.typed
    DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/aces"
)

# Create setup.py for installation
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/setup.py"
"from setuptools import setup, find_packages

setup(
    name='aces',
    version='0.1.0',
    description='Python interface to ACES (Atmospheric Chemistry Emissions System)',
    author='ACES Development Team',
    packages=find_packages(),
    package_data={
        'aces': ['*.so', '*.dylib', '*.dll', 'py.typed'],
    },
    python_requires='>=3.8',
    install_requires=[
        'numpy>=1.20',
        'pyyaml>=5.4',
    ],
    zip_safe=False,
)
")

# Installation targets
install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/aces"
    DESTINATION "${Python3_SITELIB}"
    COMPONENT python
)

# Custom target for Python package
add_custom_target(python_package ALL
    DEPENDS _aces_core
    COMMENT "Python ACES package ready in ${CMAKE_CURRENT_BINARY_DIR}/aces"
)

message(STATUS "Python bindings configuration:")
message(STATUS "  Binding method: pybind11 (v2.12.0 via FetchContent)")
message(STATUS "  Extension module: _aces_core")
message(STATUS "  Package directory: ${CMAKE_CURRENT_BINARY_DIR}/aces")
message(STATUS "  Installation target: ${Python3_SITELIB}/aces")
