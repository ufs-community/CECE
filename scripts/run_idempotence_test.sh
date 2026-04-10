#!/bin/bash

##############################################################################
# Script: run_idempotence_test.sh
#
# Purpose: Run the CECE test suite twice in JCSDA Docker and verify that
#          results are identical (test idempotence property).
#
# Usage: ./scripts/run_idempotence_test.sh [build_dir]
#
# Arguments:
#   build_dir - Optional path to build directory (default: ./build)
#
# Requirements:
#   - Must be run inside JCSDA Docker container (jcsda/docker-gnu-openmpi-dev:1.9)
#   - CECE must be built with: mkdir build && cd build && cmake .. && make -j4
#
# Output:
#   - Logs test results to stdout
#   - Creates idempotence_run1.log and idempotence_run2.log
#   - Exits with 0 if idempotence verified, 1 otherwise
#
##############################################################################

set -e

# Configuration
BUILD_DIR="${1:-.build}"
LOG_DIR="./idempotence_logs"
RUN1_LOG="${LOG_DIR}/idempotence_run1.log"
RUN2_LOG="${LOG_DIR}/idempotence_run2.log"
COMPARISON_LOG="${LOG_DIR}/idempotence_comparison.log"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Create log directory
mkdir -p "${LOG_DIR}"

echo -e "${YELLOW}========================================${NC}"
echo -e "${YELLOW}CECE Test Suite Idempotence Verification${NC}"
echo -e "${YELLOW}========================================${NC}"
echo ""

# Verify build directory exists
if [ ! -d "${BUILD_DIR}" ]; then
    echo -e "${RED}ERROR: Build directory '${BUILD_DIR}' not found${NC}"
    echo "Please build CECE first:"
    echo "  mkdir build && cd build && cmake .. && make -j4"
    exit 1
fi

# Verify test executables exist
if [ ! -f "${BUILD_DIR}/test_suite_idempotence" ]; then
    echo -e "${RED}ERROR: test_suite_idempotence executable not found${NC}"
    echo "Please rebuild CECE:"
    echo "  cd ${BUILD_DIR} && make -j4"
    exit 1
fi

echo -e "${YELLOW}Running test suite - First execution...${NC}"
echo ""

# Run test suite first time
if "${BUILD_DIR}/test_suite_idempotence" --gtest_output="xml:${LOG_DIR}/run1_results.xml" \
    > "${RUN1_LOG}" 2>&1; then
    echo -e "${GREEN}✓ First test suite execution completed successfully${NC}"
else
    echo -e "${RED}✗ First test suite execution failed${NC}"
    echo "See ${RUN1_LOG} for details"
    exit 1
fi

echo ""
echo -e "${YELLOW}Running test suite - Second execution...${NC}"
echo ""

# Run test suite second time
if "${BUILD_DIR}/test_suite_idempotence" --gtest_output="xml:${LOG_DIR}/run2_results.xml" \
    > "${RUN2_LOG}" 2>&1; then
    echo -e "${GREEN}✓ Second test suite execution completed successfully${NC}"
else
    echo -e "${RED}✗ Second test suite execution failed${NC}"
    echo "See ${RUN2_LOG} for details"
    exit 1
fi

echo ""
echo -e "${YELLOW}Comparing test results...${NC}"
echo ""

# Compare results
{
    echo "=========================================="
    echo "CECE Test Suite Idempotence Comparison"
    echo "=========================================="
    echo ""
    echo "First Run Log:"
    echo "---"
    cat "${RUN1_LOG}"
    echo ""
    echo "Second Run Log:"
    echo "---"
    cat "${RUN2_LOG}"
    echo ""
    echo "=========================================="
} > "${COMPARISON_LOG}"

# Extract test counts from logs
RUN1_PASSED=$(grep -o "passed" "${RUN1_LOG}" | wc -l || echo "0")
RUN2_PASSED=$(grep -o "passed" "${RUN2_LOG}" | wc -l || echo "0")

RUN1_FAILED=$(grep -o "FAILED" "${RUN1_LOG}" | wc -l || echo "0")
RUN2_FAILED=$(grep -o "FAILED" "${RUN2_LOG}" | wc -l || echo "0")

echo "First run:  Passed=${RUN1_PASSED}, Failed=${RUN1_FAILED}"
echo "Second run: Passed=${RUN2_PASSED}, Failed=${RUN2_FAILED}"
echo ""

# Verify idempotence
if [ "${RUN1_PASSED}" -eq "${RUN2_PASSED}" ] && [ "${RUN1_FAILED}" -eq "${RUN2_FAILED}" ]; then
    echo -e "${GREEN}✓ Test suite idempotence verified!${NC}"
    echo "  - Pass counts match: ${RUN1_PASSED}"
    echo "  - Fail counts match: ${RUN1_FAILED}"
    echo ""
    echo "Logs saved to:"
    echo "  - ${RUN1_LOG}"
    echo "  - ${RUN2_LOG}"
    echo "  - ${COMPARISON_LOG}"
    exit 0
else
    echo -e "${RED}✗ Test suite idempotence FAILED!${NC}"
    echo "  - Run 1 passed: ${RUN1_PASSED}, Run 2 passed: ${RUN2_PASSED}"
    echo "  - Run 1 failed: ${RUN1_FAILED}, Run 2 failed: ${RUN2_FAILED}"
    echo ""
    echo "Logs saved to:"
    echo "  - ${RUN1_LOG}"
    echo "  - ${RUN2_LOG}"
    echo "  - ${COMPARISON_LOG}"
    exit 1
fi
