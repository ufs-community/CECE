cmake_minimum_required(VERSION 3.20)

# Python bindings for ACES
# Supports multiple binding methods: pybind11, cffi, ctypes (fallback)

# Find Python
find_package(Python3 COMPONENTS Interpreter Development)

if(NOT Python3_FOUND)
    message(WARNING "Python3 development headers not found. Skipping Python bindings.")
    return()
endif()

message(STATUS "Found Python3: ${Python3_EXECUTABLE}")
message(STATUS "Python3 version: ${Python3_VERSION}")
message(STATUS "Python3 include dirs: ${Python3_INCLUDE_DIRS}")

# Determine binding method
set(BINDING_METHOD "ctypes" CACHE STRING "Python binding method (pybind11, cffi, ctypes)")

# Try pybind11 first
find_package(pybind11 CONFIG QUIET)
if(pybind11_FOUND)
    set(BINDING_METHOD "pybind11")
    message(STATUS "Found pybind11. Using pybind11 binding method.")
else()
    # Try cffi
    execute_process(
        COMMAND ${Python3_EXECUTABLE} -c "import cffi; print(cffi.__version__)"
        OUTPUT_VARIABLE CFFI_VERSION
        ERROR_QUIET
    )
    if(CFFI_VERSION)
        set(BINDING_METHOD "cffi")
        message(STATUS "Found cffi. Using cffi binding method.")
    else()
        set(BINDING_METHOD "ctypes")
        message(STATUS "Using ctypes binding method (pure Python, no compilation required).")
    endif()
endif()

# Compile C binding library
add_library(aces_c_bindings SHARED aces_c_bindings.cpp)

target_include_directories(aces_c_bindings
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(aces_c_bindings
    PRIVATE
    aces
    Kokkos::kokkos
)

# Set library output directory
set_target_properties(aces_c_bindings PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/aces"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/aces"
)

# Create Python package directory
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/aces")

# Copy Python source files to build directory
file(COPY
    __init__.py
    _bindings.py
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
import os

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

# Create a custom target for Python package
add_custom_target(python_package ALL
    DEPENDS aces_c_bindings
    COMMENT "Python ACES package ready in ${CMAKE_CURRENT_BINARY_DIR}/aces"
)

message(STATUS "Python bindings configuration:")
message(STATUS "  Binding method: ${BINDING_METHOD}")
message(STATUS "  C binding library: aces_c_bindings")
message(STATUS "  Package directory: ${CMAKE_CURRENT_BINARY_DIR}/aces")
message(STATUS "  Installation target: ${Python3_SITELIB}/aces")
