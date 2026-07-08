# Specification: Global AMIO Worker Threads Configuration in CECE

**Date**: 2026-07-07
**Author**: Community Emissions Computing Engine Contributors
**Status**: Approved

---

## 1. Overview
The Community Emissions Computing Engine (CECE) leverages the Asynchronous Multi-Step I/O (AMIO) library to read heavy netCDF files and perform background prefetching during simulation. Currently, the thread count for AMIO's background worker pools is hard-coded to `1` across all reads and writes.

This design introduces a top-level configuration key `amio_worker_threads` under the `driver` block of high-level CECE YAML configurations. This permits dynamic scaling of multi-threaded parallel I/O based on the available HPC resources (such as NOAA RDHPC Gaea C6 login vs. compute nodes) to optimize background caching and mitigate file system overhead.

---

## 2. Requirements & Interface Changes

### 2.1 YAML Configuration Interface
A new optional parameter `amio_worker_threads` is added to the `driver` YAML configuration block.

```yaml
driver:
  start_time: "2010-01-01T00:00:00"
  end_time: "2010-01-01T23:00:00"
  timestep_seconds: 3600
  # Sets the AMIO background thread count for reads and writes
  amio_worker_threads: 2
```

* **Type**: Positive Integer
* **Default**: `1` (ensures backward compatibility and matches the current default behavior)

---

## 3. Architecture & Data Flow

```
   High-Level YAML (cece_config_ex7.yaml)
         │
         ▼ (driver.amio_worker_threads = 2)
   ┌────────────────────────────────────────────────────────┐
   │ CECE Core Parser & Drivers                             │
   │  - Parse configured threads in YAML                    │
   │  - Set driver_config.amio_worker_threads               │
   └────────────────────────┬───────────────────────────────┘
                            │
         ┌──────────────────┴──────────────────┐
         ▼ (read streams)                      ▼ (write targets)
   ┌────────────────────────┐            ┌────────────────────────┐
   │ CECE Ingestor Facade   │            │ CECE Standalone Writer │
   │  - Write read manifests│            │  - Propagate threads   │
   │    with threads = 2    │            │  - Write output        │
   │  - Initialize AMIO     │            │    manifests with      │
   │                        │            │    threads = 2         │
   └────────────────────────┘            └────────────────────────┘
```

---

## 4. Component-by-Component Implementation

### 4.1 Interface Definitions (`include/cece/cece_config.hpp`)
Extend the global configuration structs `DriverConfig` and `CeceOutputConfig` to contain the `amio_worker_threads` parameter.

```cpp
struct DriverConfig {
    std::string start_time = "2020-01-01T00:00:00";
    std::string end_time = "2020-01-02T00:00:00";
    int timestep_seconds = 3600;
    std::string gridspec_file;
    DriverGridConfig grid;
    int stacking_refresh_interval_seconds = 0;
    int amio_worker_threads = 1;                     ///< Number of AMIO background I/O worker threads (default: 1).
};

struct CeceOutputConfig {
    std::string directory = ".";
    std::string filename_pattern = "cece_output_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc";
    int frequency_steps = 1;
    std::vector<std::string> fields;
    bool include_diagnostics = false;
    bool enabled = false;
    int amio_worker_threads = 1;                     ///< Number of AMIO background I/O worker threads (default: 1).
};
```

### 4.2 Configuration Parsing (`src/core/cece_config_parser.cpp`)
Update `ParseConfig` to parse `amio_worker_threads` if present under `driver`.

```cpp
        if (driver_node["amio_worker_threads"]) {
            config.driver_config.amio_worker_threads = driver_node["amio_worker_threads"].as<int>();
        }
```

### 4.3 Emissions Ingestion (`src/driver/cece_driver_facade.cpp`)
Retrieve the global setting from the parsed config node, and format it dynamically in the AMIO read manifest generation:

```cpp
        int amio_threads = 1;
        if (config["driver"] && config["driver"]["amio_worker_threads"]) {
            amio_threads = config["driver"]["amio_worker_threads"].as<int>();
        }

        // ... in m_file write block:
        m_file << "worker_pool:\n"
               << "  threads: " << amio_threads << "\n";
```

### 4.4 Standalone Writer Propagation (`src/driver/cece_core_writer_init.cpp`)
Pass the parsed global setting into the `output_config` copy before instantiating the writer:

```cpp
        if (!g_standalone_writer) {
            auto output_config = internal_data->config.output_config;
            output_config.amio_worker_threads = internal_data->config.driver_config.amio_worker_threads;
            g_standalone_writer = std::make_unique<cece::CeceStandaloneWriter>(output_config);
            std::atexit([]() { g_standalone_writer.reset(); });
        }
```

### 4.5 Standalone Writer Manifest (`src/driver/cece_standalone_writer.cpp`)
Inject the parameter into the written output manifest layout:

```cpp
            m_file << "backend: netcdf4\n"
                   << "path: " << filename << "\n"
                   << "data_model: enhanced\n"
                   << "staging_pool:\n"
                   << "  buffer_count: 16\n"
                   << "  buffer_capacity_bytes: 104857600\n"
                   << "worker_pool:\n"
                   << "  threads: " << config_.amio_worker_threads << "\n";
```

---

## 5. Verification & Testing

### 5.1 Dynamic Validation Test Case (`tests/test_driver_configuration.cpp`)
We will add a dedicated unit test in `test_driver_configuration.cpp` to verify that:
1. `amio_worker_threads` parses successfully with values greater than `1`.
2. Default values are correctly preserved when the key is omitted.

### 5.2 Functional System Verification
We will verify that running `cece_standalone_driver` with `examples/cece_config_ex7.yaml` containing `amio_worker_threads: 2` correctly writes the temporary read manifests with the configured pool count and successfully runs.
