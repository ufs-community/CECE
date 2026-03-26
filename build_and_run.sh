#!/bin/bash
# Quick build and run script for ACES NUOPC driver

set -e

echo "=== Building ACES in Docker ==="

docker run --platform linux/amd64 --rm \
  -v "$(pwd):/work" \
  jcsda/docker-gnu-openmpi-dev:1.9 \
  bash -c "
    cd /work
    source /opt/spack-environment/activate.sh
    rm -rf build
    mkdir -p build
    cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1
    make -j4 > build.log 2>&1 || (cat build.log && exit 1)
    echo '=== Build complete ==='
    echo ''
    echo '=== Build Artifacts ==='
    ls -lh libaces.so bin/aces_nuopc_single_driver
    echo ''
    echo '=== Phase 3 Implementation Verified ==='
    nm libaces.so | grep -E '(aces_core_bind_fields|aces_core_get_species|aces_core_initialize_p2)' | grep -v '.cold' | wc -l | xargs echo 'Symbols found:'
    echo ''
    echo '=== Running example driver ==='
    cd /work/build
    chmod +x bin/aces_nuopc_single_driver
    ./bin/aces_nuopc_single_driver --config ../examples/aces_config_ex2.yaml 2>&1 
    echo ''
    echo '=== DEBUG Output ==='
    ls -l debug_barry.txt 2>/dev/null || echo "debug_barry.txt not found"
    cat debug_barry.txt 2>/dev/null || true
    echo '==================='
  "
