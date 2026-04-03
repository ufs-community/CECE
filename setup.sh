#!/bin/bash
# setup.sh
#
# This script sets up the ACES development environment using Docker.
# It pulls the official JCSDA image and drops you into a bash shell.
#
# Usage:
#   ./setup.sh              # Interactive shell
#   ./setup.sh -c "command" # Execute command and exit

set -e

# Define the container image
IMAGE="jcsda/docker-gnu-openmpi-dev:1.9"

# Ensure docker is installed
if ! command -v docker &> /dev/null; then
    echo "Error: Docker is not installed or not in PATH."
    exit 1
fi

# Check if the image already exists locally
if docker image inspect "$IMAGE" &> /dev/null; then
    echo "Docker image $IMAGE already exists locally."
    echo "Checking for updates..."
    # docker pull "$IMAGE"
else
    echo "Pulling Docker image: $IMAGE"
    # docker pull "$IMAGE"
fi

echo "Launching ACES Development Container..."

# Check if command mode or interactive mode
if [ "$1" = "-c" ] && [ -n "$2" ]; then
    # Command mode: execute command and exit
    docker run --rm \
        -v "$(pwd):/work" \
        -w /work \
        "$IMAGE" \
        /bin/bash -c "source /opt/spack-environment/activate.sh && $2"
else
    # Interactive mode: drop into bash shell
    docker run --rm \
        -v "$(pwd):/work" \
        -w /work \
        "$IMAGE" \
        /bin/bash -c "source /opt/spack-environment/activate.sh && exec bash"
fi
