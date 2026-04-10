# Kokkos Execution Space Configuration Guide

## Overview

CECE uses Kokkos for performance portability across CPUs and GPUs. This guide explains how to configure and use different Kokkos execution spaces for your deployment.

## Supported Execution Spaces

CECE supports the following Kokkos execution spaces:

| Execution Space | Hardware | Use Case | Default |
|---|---|---|---|
| **Serial** | CPU | Debugging, validation, single-threaded execution | ✓ |
| **OpenMP** | CPU | Multi-threaded CPU execution, production | ✓ |
| **CUDA** | NVIDIA GPU | High-performance GPU acceleration (optional) | ✗ |
| **HIP** | AMD GPU | High-performance GPU acceleration (optional) | ✗ |

## Build-Time Configuration

### Default CPU-Only Build

By default, CECE builds with Serial and OpenMP execution spaces enabled:

```bash
mkdir build && cd build
cmake ..
make -j4
```

This configuration is suitable for CPU-only deployments and provides both single-threaded (Serial) and multi-threaded (OpenMP) execution options.

### NVIDIA GPU Support (CUDA)

To enable CUDA support for NVIDIA GPUs:

```bash
mkdir build && cd build
cmake .. -DKOKKOS_ENABLE_CUDA=ON
make -j4
```

**Requirements:**
- NVIDIA CUDA Toolkit installed
- NVIDIA GPU with compute capability 3.0 or higher
- CUDA-capable compiler (nvcc)

### AMD GPU Support (HIP)

To enable HIP support for AMD GPUs:

```bash
mkdir build && cd build
cmake .. -DKOKKOS_ENABLE_HIP=ON
make -j4
```

**Requirements:**
- AMD ROCm toolkit installed
- AMD GPU with RDNA or CDNA architecture
- HIP-capable compiler

### Serial-Only Build (Debugging)

For debugging purposes, you can build with only Serial execution:

```bash
mkdir build && cd build
cmake .. -DKOKKOS_ENABLE_OPENMP=OFF
make -j4
```

### Validation: Check Build Configuration

After building, verify the Kokkos configuration by checking the CMake output:

```
-- Kokkos Execution Spaces Configuration:
--   Serial: ON
--   OpenMP: ON
--   CUDA: OFF
--   HIP: OFF
```

## Runtime Configuration

### Environment Variables

CECE respects the following environment variables for runtime configuration:

#### OMP_NUM_THREADS (OpenMP)

Controls the number of OpenMP threads used by CECE:

```bash
# Use 16 threads
export OMP_NUM_THREADS=16
./cece_nuopc_single_driver --config cece_config.yaml

# Use all available threads (default if not set)
unset OMP_NUM_THREADS
./cece_nuopc_single_driver --config cece_config.yaml
```

**Default:** All available CPU threads

**Example Output:**
```
INFO: Setting OpenMP threads to 16
INFO: Kokkos initialized successfully
INFO: Default execution space: OpenMP
```

#### CECE_DEVICE_ID (GPU)

Selects which GPU device to use (for CUDA or HIP builds):

```bash
# Use GPU device 0 (default)
export CECE_DEVICE_ID=0
./cece_nuopc_single_driver --config cece_config.yaml

# Use GPU device 1
export CECE_DEVICE_ID=1
./cece_nuopc_single_driver --config cece_config.yaml
```

**Default:** Device 0

**Example Output:**
```
INFO: Setting CUDA device ID to 1
INFO: Kokkos initialized successfully
INFO: Default execution space: CUDA
```

### Querying Runtime Configuration

CECE logs its Kokkos configuration during initialization:

```
INFO: Initializing Kokkos execution space
INFO: Active Kokkos execution spaces:
INFO:   - Serial
INFO:   - OpenMP
INFO: Setting OpenMP threads to 16
INFO: Kokkos initialized successfully
INFO: Default execution space: OpenMP
```

## Performance Considerations

### CPU Execution (OpenMP)

For optimal CPU performance:

1. **Set OMP_NUM_THREADS to match physical cores:**
   ```bash
   export OMP_NUM_THREADS=16  # For 16-core CPU
   ```

2. **Disable hyperthreading** if using more than physical cores:
   ```bash
   export OMP_NUM_THREADS=16  # Not 32 for 16-core with HT
   ```

3. **Monitor thread efficiency:**
   - CECE logs "OpenMP threads: N" during initialization
   - Verify with `nproc` or `lscpu`

### GPU Execution (CUDA/HIP)

For optimal GPU performance:

1. **Verify GPU availability:**
   ```bash
   # For CUDA
   nvidia-smi

   # For HIP
   rocm-smi
   ```

2. **Select correct device if multiple GPUs present:**
   ```bash
   export CECE_DEVICE_ID=0  # First GPU
   export CECE_DEVICE_ID=1  # Second GPU
   ```

3. **Monitor GPU utilization:**
   ```bash
   # For CUDA (in another terminal)
   watch -n 1 nvidia-smi

   # For HIP
   watch -n 1 rocm-smi
   ```

## Troubleshooting

### Issue: "Kokkos already initialized - using existing instance"

**Cause:** Kokkos was initialized by another component before CECE.

**Solution:** This is normal in coupled systems. CECE will use the existing Kokkos configuration.

### Issue: OpenMP threads not matching OMP_NUM_THREADS

**Cause:** Environment variable not set before CECE initialization.

**Solution:** Set OMP_NUM_THREADS before running CECE:
```bash
export OMP_NUM_THREADS=16
./cece_nuopc_single_driver --config cece_config.yaml
```

### Issue: CUDA device not found

**Cause:** CECE_DEVICE_ID points to non-existent GPU.

**Solution:** Check available devices and set valid ID:
```bash
nvidia-smi  # Lists available GPUs
export CECE_DEVICE_ID=0  # Use first GPU
```

### Issue: "Cannot enable both CUDA and HIP"

**Cause:** CMake configuration attempted to enable both GPU backends.

**Solution:** Choose one GPU backend:
```bash
# CUDA only
cmake .. -DKOKKOS_ENABLE_CUDA=ON -DKOKKOS_ENABLE_HIP=OFF

# HIP only
cmake .. -DKOKKOS_ENABLE_CUDA=OFF -DKOKKOS_ENABLE_HIP=ON
```

## Performance Portability

All CECE physics kernels use Kokkos parallel primitives for automatic dispatch:

```cpp
// Automatically dispatches to configured execution space
Kokkos::parallel_for("kernel_name",
    Kokkos::MDRangePolicy<Kokkos::Rank<3>>(
        {0, 0, 0}, {nx, ny, nz}),
    KOKKOS_LAMBDA(int i, int j, int k) {
        // Physics computation
    });
```

This ensures:
- **Single source code** for CPU and GPU
- **Automatic optimization** for target hardware
- **No hardware-specific code** in physics implementations
- **Consistent results** across execution spaces (within floating-point precision)

## Requirements Mapping

This configuration satisfies the following requirements:

- **Req 6.13:** Initialize Kokkos with appropriate execution space based on CMake configuration
- **Req 6.14:** Finalize Kokkos during Finalize Phase if CECE initialized it
- **Req 6.15:** Support Kokkos::Serial execution space for debugging
- **Req 6.16:** Support Kokkos::OpenMP execution space for CPU parallelism
- **Req 6.17:** Support Kokkos::Cuda execution space for NVIDIA GPU acceleration
- **Req 6.18:** Support Kokkos::HIP execution space for AMD GPU acceleration
- **Req 6.21:** Compile with Kokkos::Serial and Kokkos::OpenMP in JCSDA Docker

## Examples

### Example 1: CPU-Only Production Run

```bash
# Build
mkdir build && cd build
cmake ..
make -j4

# Run with 32 threads
export OMP_NUM_THREADS=32
./cece_nuopc_single_driver \
  --config cece_config.yaml \
  --streams cece_emissions.streams \
  --start-time "2020-01-01T00:00:00" \
  --end-time "2020-01-02T00:00:00" \
  --time-step 3600
```

### Example 2: GPU-Accelerated Run

```bash
# Build with CUDA
mkdir build && cd build
cmake .. -DKOKKOS_ENABLE_CUDA=ON
make -j4

# Run on GPU device 1
export CECE_DEVICE_ID=1
./cece_nuopc_single_driver \
  --config cece_config.yaml \
  --streams cece_emissions.streams \
  --start-time "2020-01-01T00:00:00" \
  --end-time "2020-01-02T00:00:00" \
  --time-step 3600
```

### Example 3: Debugging with Serial Execution

```bash
# Build with Serial only
mkdir build && cd build
cmake .. -DKOKKOS_ENABLE_OPENMP=OFF
make -j4

# Run with Serial execution (single-threaded)
./cece_nuopc_single_driver \
  --config cece_config.yaml \
  --streams cece_emissions.streams \
  --start-time "2020-01-01T00:00:00" \
  --end-time "2020-01-01T01:00:00" \
  --time-step 3600
```

## See Also

- [CECE Developer Guide](./AGENTS.md)
- [Physics Scheme Development](./physics_scheme_development.md)
- [Kokkos Documentation](https://kokkos.github.io/)
- [JCSDA Docker Setup](./users-guide.md)
