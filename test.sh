#!/bin/bash
docker run --platform linux/amd64 -v "$(pwd):/work" jcsda/docker-gnu-openmpi-dev:1.9 bash -c "
cd /work/build
cmake .. -DCMAKE_BUILD_TYPE=Release
make aces_nuopc_single_driver
./bin/aces_nuopc_single_driver --config ../examples/aces_config_ex1.yaml
"