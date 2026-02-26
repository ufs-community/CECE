#!/bin/bash
set -e

echo "Fixing Docker overlayfs whiteout issue..."

# Stop Docker and clear corrupted snapshots
sudo systemctl stop docker containerd || true
sudo rm -rf /var/lib/containerd/io.containerd.snapshotter.v1.overlayfs/snapshots/*
sudo systemctl start containerd

# Install fuse-overlayfs for compatibility
sudo apt-get update
sudo apt-get install -y fuse-overlayfs
echo '{ "storage-driver": "fuse-overlayfs" }' | sudo tee /etc/docker/daemon.json

# Restart Docker
sudo systemctl restart docker

# Install Docker CE (official repo)
sudo apt-get install -y ca-certificates curl gnupg
sudo install -m 0755 -d /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo tee /etc/apt/keyrings/docker.asc > /dev/null
echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] https://download.docker.com/linux/ubuntu $(. /etc/os-release && echo $VERSION_CODENAME) stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null
sudo apt-get update
sudo apt-get install -y docker-ce docker-ce-cli

# Pull and run JCSDA container
echo "Starting jcsda/docker-gnu-openmpi-dev:1.9..."
docker pull jcsda/docker-gnu-openmpi-dev:1.9
CONTAINER_ID=$(docker run -d --rm -u nonroot -v /app:/app jcsda/docker-gnu-openmpi-dev:1.9 sleep infinity)

# Activate Spack environment
echo "Activating Spack environment..."
docker exec $CONTAINER_ID /bin/bash -c "source /opt/spack-environment/activate.sh && echo 'Spack environment activated.'"

echo "✅ Setup complete. Use 'docker exec $CONTAINER_ID <command>' to run tasks."
echo "Container ID is: $CONTAINER_ID"
