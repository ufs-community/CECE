# ACES: Accelerated Component for Emission System

ACES is a high-performance, performance-portable emissions compute component for Earth System Models. It is built using C++17 and the Kokkos programming model, designed to run efficiently on both multi-core CPUs and GPUs.

## Quick Start

### 1. Development Environment
The recommended way to develop ACES is using the provided Docker container:
```bash
./setup.sh
```

### 2. Build
Inside the container:
```bash
mkdir build && cd build
cmake ..
make -j4
```

### 3. Run Standalone NUOPC Driver
ACES provides a standalone NUOPC driver for testing and development in the `standalone_nuopc` directory. This driver demonstrates how to host the ACES component within a NUOPC-compliant environment.

To run the standalone driver:
```bash
cd build/standalone_nuopc
./aces_nuopc_driver
```
The driver will look for `aces_config.yaml` in the current working directory.

## Documentation
Full documentation is available in the `docs/` directory and can be viewed as Markdown or built using [Zensical](https://github.com/jcsda/zensical).

- [User's Guide](docs/users-guide.md)
- [Tutorial](docs/tutorial.md)
- [Configuration Reference](docs/configuration.md)
