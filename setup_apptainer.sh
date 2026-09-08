#!/bin/bash
# setup.sh
#
# This script sets up the CECE development environment.
# It automatically detects Docker, Singularity, or Apptainer to pull 
# the official JCSDA image and drop you into a bash shell.
#
# Usage:
#   ./setup.sh                                    # Interactive shell (uses default SIF)
#   ./setup.sh -s /path/to/custom.sif             # Interactive shell (uses custom SIF)
#   ./setup.sh -c "command"                       # Execute command and exit
#   ./setup.sh -s /path/to/custom.sif -c "make"   # Custom SIF + command

set -e

# Define the container image and default Singularity equivalents
IMAGE="jcsda/docker-gnu-openmpi-dev:1.9"
SINGULARITY_URI="docker://${IMAGE}"
SIF_FILE="jcsda-docker-gnu-openmpi-dev-1.9.sif" # Default fallback
CMD=""

# Parse command line arguments
while getopts "s:c:h" opt; do
    case ${opt} in
        s )
            SIF_FILE=$OPTARG
            ;;
        c )
            CMD=$OPTARG
            ;;
        h )
            echo "Usage: $0 [-s /path/to/image.sif] [-c 'command']"
            exit 0
            ;;
        \? )
            echo "Invalid option. Usage: $0 [-s /path/to/image.sif] [-c 'command']"
            exit 1
            ;;
    esac
done

# Detect available container engine
ENGINE=""
if command -v singularity &> /dev/null; then
    ENGINE="singularity"
elif command -v apptainer &> /dev/null; then
    ENGINE="apptainer"
elif command -v docker &> /dev/null; then
    ENGINE="docker"
else
    echo "Error: No supported container engine (Docker, Singularity, or Apptainer) found in PATH."
    exit 1
fi

echo "Using container engine: $ENGINE"

# ==========================================
# DOCKER EXECUTION PATH
# ==========================================
if [ "$ENGINE" = "docker" ]; then
    # Check if the image already exists locally
    if docker image inspect "$IMAGE" &> /dev/null; then
        echo "Docker image $IMAGE already exists locally."
        echo "Checking for updates..."
        docker pull "$IMAGE"
    else
        echo "Pulling Docker image: $IMAGE"
        docker pull "$IMAGE"
    fi

    echo "Launching CECE Development Container..."

    if [ -n "$CMD" ]; then
        docker run --rm \
            -v "$(pwd):/work" \
            -w /work \
            "$IMAGE" \
            /bin/bash -c "source /opt/spack-environment/activate.sh && $CMD"
    else
        docker run -it --rm \
            -v "$(pwd):/work" \
            -w /work \
            "$IMAGE" \
            /bin/bash -c "source /opt/spack-environment/activate.sh && exec bash"
    fi

# ==========================================
# SINGULARITY / APPTAINER EXECUTION PATH
# ==========================================
else
    # Check if the SIF (Singularity Image Format) file exists locally
    if [ -f "$SIF_FILE" ]; then
        echo "Image $SIF_FILE already exists locally."
    else
        echo "Image $SIF_FILE not found."
        echo "Pulling image from Docker Hub into Singularity format at $SIF_FILE..."
        $ENGINE pull "$SIF_FILE" "$SINGULARITY_URI"
    fi

    echo "Launching CECE Development Container..."

    if [ -n "$CMD" ]; then
        $ENGINE exec \
            --bind "$(pwd):/work" \
            --pwd /work \
            "$SIF_FILE" \
            /bin/bash -c "source /opt/spack-environment/activate.sh && $CMD"
    else
        $ENGINE exec \
            --bind "$(pwd):/work" \
            --pwd /work \
            "$SIF_FILE" \
            /bin/bash -c "source /opt/spack-environment/activate.sh && exec bash"
    fi
fi