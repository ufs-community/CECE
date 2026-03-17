#!/bin/bash
# Script to build and test ACES in Docker container

set -e

echo "=== Building ACES in Docker ==="

# Source the spack environment
source /opt/spack-environment/activate.sh

# Create build directory
mkdir -p build
cd build

# Configure with CMake
echo "=== Running CMake ==="
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Build
echo "=== Building ==="
make -j4

# Run the specific test for realize phase
echo "=== Running realize phase test ==="
./test_realize_phase

echo "=== Build and test complete ==="
