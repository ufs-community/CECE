#!/bin/bash
#
# ACES Robust Runner - Handles large grid race condition automatically
# Usage: ./aces_robust_run.sh --config <config.yaml> --nx <nx> --ny <ny> [other options]
#

set -e

# Default values
CONFIG=""
NX=""
NY=""
MAX_RETRIES=5
RETRY_DELAY=2
OTHER_ARGS=""
TIMEOUT=120

# Parse arguments
while [[ $# -gt 0 ]]; do
  case $1 in
    --config)
      CONFIG="$2"
      shift 2
      ;;
    --nx)
      NX="$2"
      shift 2
      ;;
    --ny)
      NY="$2"
      shift 2
      ;;
    --max-retries)
      MAX_RETRIES="$2"
      shift 2
      ;;
    --timeout)
      TIMEOUT="$2"
      shift 2
      ;;
    *)
      OTHER_ARGS="$OTHER_ARGS $1"
      shift
      ;;
  esac
done

# Validate required arguments
if [[ -z "$CONFIG" || -z "$NX" || -z "$NY" ]]; then
  echo "Error: Missing required arguments"
  echo "Usage: $0 --config <config.yaml> --nx <nx> --ny <ny> [--max-retries <n>] [--timeout <secs>] [other options]"
  exit 1
fi

# Calculate grid size
GRID_SIZE=$((NX * NY))
echo "INFO: [RobustRunner] Running ACES with ${NX}x${NY} grid (${GRID_SIZE} points)"

# Adjust retry strategy based on grid size
if [[ $GRID_SIZE -gt 200000 ]]; then
  echo "INFO: [RobustRunner] Very large grid detected - using aggressive retry strategy"
  MAX_RETRIES=7
  RETRY_DELAY=3
  TIMEOUT=150
elif [[ $GRID_SIZE -gt 50000 ]]; then
  echo "INFO: [RobustRunner] Large grid detected - using enhanced retry strategy"
  MAX_RETRIES=5
  RETRY_DELAY=2
  TIMEOUT=120
else
  echo "INFO: [RobustRunner] Standard grid size - minimal retry needed"
  MAX_RETRIES=2
  RETRY_DELAY=1
  TIMEOUT=90
fi

# Run ACES with retry logic
SUCCESS=false
for ((attempt=1; attempt<=MAX_RETRIES; attempt++)); do
  echo "INFO: [RobustRunner] Attempt $attempt/$MAX_RETRIES"

  if timeout $TIMEOUT ./bin/aces_nuopc_single_driver --config "$CONFIG" --nx "$NX" --ny "$NY" $OTHER_ARGS; then
    echo "INFO: [RobustRunner] SUCCESS on attempt $attempt"
    SUCCESS=true
    break
  else
    EXIT_CODE=$?
    if [[ $EXIT_CODE -eq 124 ]]; then
      echo "WARNING: [RobustRunner] Attempt $attempt timed out after ${TIMEOUT}s"
    elif [[ $EXIT_CODE -eq 139 ]]; then
      echo "WARNING: [RobustRunner] Attempt $attempt failed with segmentation fault (race condition)"
    else
      echo "WARNING: [RobustRunner] Attempt $attempt failed with exit code $EXIT_CODE"
    fi

    if [[ $attempt -lt $MAX_RETRIES ]]; then
      echo "INFO: [RobustRunner] Waiting ${RETRY_DELAY}s before retry..."
      sleep $RETRY_DELAY
    fi
  fi
done

if [[ "$SUCCESS" == "true" ]]; then
  echo "INFO: [RobustRunner] ACES completed successfully"
  exit 0
else
  echo "ERROR: [RobustRunner] ACES failed after $MAX_RETRIES attempts"
  echo "ERROR: [RobustRunner] This may indicate a persistent issue beyond the known race condition"
  exit 1
fi