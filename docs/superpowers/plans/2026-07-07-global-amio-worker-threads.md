# Global AMIO Worker Threads Configuration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enable dynamic scaling of multi-threaded parallel I/O by parsing and propagating a global `amio_worker_threads` configuration key from high-level CECE configurations down into all AMIO read and write manifests.

**Architecture:** We will extend both `DriverConfig` and `CeceOutputConfig` to store the worker thread count. We'll update the config parser to retrieve this key, propagate it into the ingestor facade (`cece_driver_facade.cpp`), and copy it into the output writer facade (`cece_core_writer_init.cpp`), writing the user-configured value to all dynamically written AMIO manifests.

**Tech Stack:** C++20, Kokkos, YAML-cpp, AMIO, Google Test

## Global Constraints
* Every modified file must be complete, formatted properly, and syntactically valid C++20.
* Default values must be preserved as `1` thread for backwards compatibility and single-threaded safety.
* No temporary diagnostics or instrumentation can remain in the production code.

---

### Task 1: Extend Configuration Data Structures

**Files:**
- Modify: `include/cece/cece_config.hpp`

**Interfaces:**
- Consumes: None (base configuration declarations)
- Produces: `DriverConfig::amio_worker_threads` (int, default 1) and `CeceOutputConfig::amio_worker_threads` (int, default 1)

- [ ] **Step 1: Declare `amio_worker_threads` in DriverConfig and CeceOutputConfig**

Add the declaration to `DriverConfig` and `CeceOutputConfig` in `include/cece/cece_config.hpp` with default values of `1`.

*In `DriverConfig` around lines 190-195:*
```cpp
    int stacking_refresh_interval_seconds = 0;  ///< Stacking engine refresh interval in seconds (0 means use base timestep).
    int amio_worker_threads = 1;                 ///< Number of AMIO background I/O worker threads (default: 1).
```

*In `CeceOutputConfig` around lines 150-155:*
```cpp
    bool enabled = false;                                                         ///< True when an output block is present in the YAML.
    int amio_worker_threads = 1;                                                 ///< Number of AMIO background I/O worker threads (default: 1).
```

- [ ] **Step 2: Run verification build**

Load modules and run the build to ensure the header files compile.
Run:
```bash
module use modulefiles && module load cece_gaeac6.intelllvm && cd build && cmake .. && make -j4
```
Expected: Compile succeeds with exit code 0.

- [ ] **Step 3: Commit**

```bash
git add include/cece/cece_config.hpp
git commit -m "feat(config): add amio_worker_threads to configuration structures"
```

---

### Task 2: Implement Config Parser and Parser Unit Test

**Files:**
- Modify: `src/core/cece_config_parser.cpp`
- Modify: `tests/test_driver_configuration.cpp`

**Interfaces:**
- Consumes: `DriverConfig::amio_worker_threads`
- Produces: Parse logic that extracts `amio_worker_threads` from global `driver` node.

- [ ] **Step 1: Add a failing test case in `tests/test_driver_configuration.cpp`**

Insert the following Google Test case into `tests/test_driver_configuration.cpp` around line 125 to check that a custom thread count parses correctly and a omitted key defaults to 1.

```cpp
TEST_F(DriverConfigurationTest, ParseAmioWorkerThreads) {
    std::string test_yaml =
        "driver:\n"
        "  start_time: \"2010-01-01T00:00:00\"\n"
        "  end_time: \"2010-01-01T23:00:00\"\n"
        "  timestep_seconds: 3600\n"
        "  amio_worker_threads: 4\n";

    std::string filename = "test_driver_config_amio_threads.yaml";
    std::ofstream out(filename);
    out << yaml_header_ << test_yaml;
    out.close();

    auto config = cece::ParseConfig(filename);
    EXPECT_EQ(config.driver_config.amio_worker_threads, 4);

    std::remove(filename.c_str());
}
```

- [ ] **Step 2: Run test suite to verify it fails compile/run**

Run:
```bash
module use modulefiles && module load cece_gaeac6.intelllvm && cd build && cmake .. && make -j4 && ./test_driver_configuration --gtest_filter=DriverConfigurationTest.ParseAmioWorkerThreads
```
Expected: Compile FAILS or test FAILS since the parser does not yet support `amio_worker_threads`.

- [ ] **Step 3: Update `src/core/cece_config_parser.cpp` to parse `amio_worker_threads`**

Add the parsing code to `src/core/cece_config_parser.cpp` inside `ParseConfig` under the `if (root["driver"])` block (around line 430).

```cpp
        if (driver_node["amio_worker_threads"]) {
            config.driver_config.amio_worker_threads = driver_node["amio_worker_threads"].as<int>();
        }
```

- [ ] **Step 4: Run test to verify it passes**

Run:
```bash
module use modulefiles && module load cece_gaeac6.intelllvm && cd build && make -j4 && ./test_driver_configuration --gtest_filter=DriverConfigurationTest.ParseAmioWorkerThreads
```
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/core/cece_config_parser.cpp tests/test_driver_configuration.cpp
git commit -m "feat(parser): parse global amio_worker_threads from driver config"
```

---

### Task 3: Propagate setting to AMIO Read Manifests

**Files:**
- Modify: `src/driver/cece_driver_facade.cpp`

**Interfaces:**
- Consumes: Configured thread count under YAML's `driver.amio_worker_threads` or defaults to 1.
- Produces: Dynamically written AMIO read manifests with `threads: <amio_worker_threads>`.

- [ ] **Step 1: Extract `amio_worker_threads` and format it into read manifests**

Modify `src/driver/cece_driver_facade.cpp` to query the parsed thread count from the YAML configuration, and format it dynamically.

*Around lines 315-325, extract the thread count:*
```cpp
        int amio_threads = 1;
        if (config["driver"] && config["driver"]["amio_worker_threads"]) {
            amio_threads = config["driver"]["amio_worker_threads"].as<int>();
        }
```

*Update the `worker_pool` block formatted in `m_file` write (around line 330) to use `amio_threads`:*
```cpp
                 m_file << "backend: netcdf4\n"
                        << "path: " << input_file_path << "\n"
                        << "data_model: " << candidate_model << "\n"
                        << "staging_pool:\n"
                        << "  buffer_count: 8\n"
                        << "  buffer_capacity_bytes: 268435456\n"
                        << "worker_pool:\n"
                        << "  threads: " << amio_threads << "\n"
                        << "prefetch:\n"
                        << "  depth: 2\n"
                        << "  read_timeout_s: 120\n"
                        << "staging_timeout_ms: 30000\n";
```

- [ ] **Step 2: Rebuild and execute example to verify no errors**

Run:
```bash
module use modulefiles && module load cece_gaeac6.intelllvm && cd build && make -j4 && cd .. && ./build/cece_standalone_driver examples/cece_config_ex7.yaml
```
Expected: Rebuild succeeds, the simulation runs successfully, and temporary read manifests (e.g. `amio_read_manifest_facade_CAMS_HOURLY.yaml`) are correctly written with `threads: 1` (default).

- [ ] **Step 3: Commit**

```bash
git add src/driver/cece_driver_facade.cpp
git commit -m "feat(driver): dynamically format amio_worker_threads into read manifests"
```

---

### Task 4: Propagate thread count and update Standalone Writer

**Files:**
- Modify: `src/driver/cece_core_writer_init.cpp`
- Modify: `src/driver/cece_standalone_writer.cpp`

**Interfaces:**
- Consumes: `driver_config.amio_worker_threads`
- Produces: Dynamically written AMIO write manifests with `threads: <amio_worker_threads>`.

- [ ] **Step 1: Propagate thread count to Standalone Writer configuration copy**

Update `src/driver/cece_core_writer_init.cpp` to set the output config's `amio_worker_threads` to match the driver's global thread count during initialization.

*Modify both instances of `g_standalone_writer` initialization (around lines 58 and 124):*
```cpp
        if (!g_standalone_writer) {
            auto output_config = internal_data->config.output_config;
            output_config.amio_worker_threads = internal_data->config.driver_config.amio_worker_threads;
            g_standalone_writer = std::make_unique<cece::CeceStandaloneWriter>(output_config);
            std::atexit([]() { g_standalone_writer.reset(); });
        }
```

- [ ] **Step 2: Inject thread count into Standalone Writer output manifest format**

Modify `src/driver/cece_standalone_writer.cpp` to format the worker thread parameter in the generated manifest.

*Update `worker_pool` block in `m_file` output around line 217:*
```cpp
            m_file << "backend: netcdf4\n"
                   << "path: " << filename << "\n"
                   << "data_model: enhanced\n"
                   << "staging_pool:\n"
                   << "  buffer_count: 16\n"
                   << "  buffer_capacity_bytes: 104857600\n"
                   << "worker_pool:\n"
                   << "  threads: " << config_.amio_worker_threads << "\n"
                   << "prefetch:\n"
                   << "  depth: 4\n"
                   << "  read_timeout_s: 60\n"
                   << "staging_timeout_ms: 10000\n"
```

- [ ] **Step 3: Build and run verification**

Run:
```bash
module use modulefiles && module load cece_gaeac6.intelllvm && cd build && make -j4 && cd .. && ./build/cece_standalone_driver examples/cece_config_ex7.yaml
```
Expected: Simulation runs and output files are generated successfully.

- [ ] **Step 4: Commit**

```bash
git add src/driver/cece_core_writer_init.cpp src/driver/cece_standalone_writer.cpp
git commit -m "feat(writer): propagate and format amio_worker_threads in write manifests"
```

---

### Task 5: End-to-End Verification with Multi-Threading Configured

**Files:**
- Modify: `examples/cece_config_ex7.yaml`

**Interfaces:** None

- [ ] **Step 1: Configure example 7 to use multiple AMIO I/O threads**

Modify `examples/cece_config_ex7.yaml` to include `amio_worker_threads: 2` under the `driver` section.

```yaml
driver:
  start_time: "2010-01-01T00:00:00"
  end_time: "2010-01-01T23:00:00"
  timestep_seconds: 3600
  log_file: "cece.log"
  amio_worker_threads: 2
  grid:
    grid_name: "F360"
```

- [ ] **Step 2: Run example 7 and check generated read/write manifests**

Run the simulation:
```bash
module use modulefiles && module load cece_gaeac6.intelllvm && ./build/cece_standalone_driver examples/cece_config_ex7.yaml
```

Verify that the generated read and write manifests (e.g. `amio_read_manifest_facade_CAMS_HOURLY.yaml` or standalone output manifests) correctly have `threads: 2`.
Run:
```bash
grep -Hn "threads:" amio_read_manifest_facade_*.yaml
```
Expected: Output showing `threads: 2` for each manifest file.

- [ ] **Step 3: Execute full test suite to guarantee regression-free state**

Run the automated test runner:
```bash
cd build && ctest --output-on-failure
```
Expected: 100% test success (280/280 passing).

- [ ] **Step 4: Commit**

```bash
git add examples/cece_config_ex7.yaml
git commit -m "test(integration): configure example 7 to use multi-threaded I/O"
```
