#!/bin/bash
# rebuild_and_test.sh - Rebuild ACES and test CDEPS integration

set -e

IMAGE="jcsda/docker-gnu-openmpi-dev:1.9"

echo "=========================================="
echo "Rebuilding ACES with Mesh Fallback"
echo "=========================================="

docker run --rm \
  -v "$(pwd):/workspace" \
  -w /workspace \
  "$IMAGE" \
  bash -c "
    set -e
    cd build
    echo 'Rebuilding aces_cap.F90...'
    make -j4 2>&1 | tail -20
    echo 'Build complete'
  "

echo ""
echo "=========================================="
echo "Testing CDEPS Integration"
echo "=========================================="

./test_cdeps_integration.sh
