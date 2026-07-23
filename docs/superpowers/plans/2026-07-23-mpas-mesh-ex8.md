# MPAS Mesh Configuration for cece_config_ex8.yaml Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Configure the C++ standalone driver (`cece_standalone_driver`) to run with an unstructured MPAS mesh using a pre-generated grid description in `examples/cece_config_ex8.yaml`.

**Architecture:** We will set `gridspec_file` under the `driver:` section in `cece_config_ex8.yaml` to point to the standard global 2562-cell MPAS mesh NetCDF file (`data/x1.2562.grid.nc`). We will update the `grid` dimensions in the configuration to match (`nx: 2562`, `ny: 1`). To ensure the C++ standalone driver correctly extracts target coordinates from this unstructured MPAS grid description rather than generating structured latitude/longitude lists, we will enhance the coordinate discovery logic in `src/main.cpp` to check for MPAS `lonCell`/`latCell` coordinate variable names and handle their radian-to-degree conversion.

**Tech Stack:** C++, Kokkos, AMIO, YAML-CPP

## Global Constraints
- Target grid definition must use `gridspec_file` pointing to `data/x1.2562.grid.nc`.
- Target mesh dimension must be `nx: 2562`, `ny: 1`.
- Standard C++ standalone driver must be used (`cece_standalone_driver`).

---

### Task 1: Update Grid and Coordinate Discovery Logic in src/main.cpp

We will modify `src/main.cpp` to support retrieving coordinate arrays from the `gridspec_file` configuration when defined. We will support candidate MPAS coordinate variable names (`lonCell`/`latCell`) and convert units to degrees if they are in radians.

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `driver.gridspec_file` from parsed YAML config.
- Produces: Correct coordinate arrays `file_lons` and `file_lats` for the standalone writer and driver facade.

- [ ] **Step 1: Edit coordinate loading logic in src/main.cpp**

We will update the fallback data-stream coordinate check so that if `gridspec_file` is specified, the standalone driver reads cell locations directly from it. We also add checks to read `"lonCell"` and `"latCell"` if `"lon"`/`"lat"` are absent, and convert radians to degrees if needed.

```cpp
// Replace coordinate reading fallback logic around line 182 with a robust gridspec_file/stream coordinate loader:
<<<<
        } else {
            bool loaded_from_file = false;
            std::string input_file_path = "../scripts/data/MACCity_4x5.nc";  // default fallback
            if (config["cece_data"] && config["cece_data"]["streams"]) {
                auto stream = config["cece_data"]["streams"][0];
                if (stream["file"]) {
                    input_file_path = stream["file"].as<std::string>();
                }
            }
====
        } else {
            bool loaded_from_file = false;
            std::string input_file_path = "";
            if (config["driver"] && config["driver"]["gridspec_file"]) {
                std::string gf = config["driver"]["gridspec_file"].as<std::string>();
                if (!gf.empty() && gf != "none" && gf != "NONE") {
                    input_file_path = gf;
                }
            }
            if (input_file_path.empty() && config["cece_data"] && config["cece_data"]["streams"]) {
                auto stream = config["cece_data"]["streams"][0];
                if (stream["file"]) {
                    input_file_path = stream["file"].as<std::string>();
                }
            }
            if (input_file_path.empty()) {
                input_file_path = "../scripts/data/MACCity_4x5.nc"; // default fallback
            }
>>>>
```

We also update the AMIO variable reading section to handle `"lonCell"`/`"latCell"` with radian-to-degree conversion:
```cpp
<<<<
                    if (amio_read(coord_dataset, "lon", 0, nullptr, &lon_view) == AMIO_OK) {
                        const void* view_data = nullptr;
                        size_t view_size = 0;
                        if (amio_view_data(lon_view, &view_data, &view_size) == AMIO_OK) {
                            amio_shape_t lon_shape{};
                            if (amio_view_shape(lon_view, &lon_shape) == AMIO_OK) {
                                file_nx = static_cast<int>(lon_shape.extents[0]);
                                bool is_float = (view_size == static_cast<size_t>(file_nx) * 4);
                                const float* float_data = static_cast<const float*>(view_data);
                                const double* double_data = static_cast<const double*>(view_data);
                                file_lon_coords.resize(file_nx);
                                for (int i = 0; i < file_nx; ++i) {
                                    file_lon_coords[i] = is_float ? static_cast<double>(float_data[i]) : double_data[i];
                                }
                            }
                        }
                        amio_release_view(lon_view);
                    }

                    if (amio_read(coord_dataset, "lat", 0, nullptr, &lat_view) == AMIO_OK) {
                        const void* view_data = nullptr;
                        size_t view_size = 0;
                        if (amio_view_data(lat_view, &view_data, &view_size) == AMIO_OK) {
                            amio_shape_t lat_shape{};
                            if (amio_view_shape(lat_view, &lat_shape) == AMIO_OK) {
                                file_ny = static_cast<int>(lat_shape.extents[0]);
                                bool is_float = (view_size == static_cast<size_t>(file_ny) * 4);
                                const float* float_data = static_cast<const float*>(view_data);
                                const double* double_data = static_cast<const double*>(view_data);
                                file_lat_coords.resize(file_ny);
                                for (int j = 0; j < file_ny; ++j) {
                                    file_lat_coords[j] = is_float ? static_cast<double>(float_data[j]) : double_data[j];
                                }
                            }
                        }
                        amio_release_view(lat_view);
                    }
====
                    bool is_radian = false;
                    amio_status_t lon_status = amio_read(coord_dataset, "lon", 0, nullptr, &lon_view);
                    if (lon_status != AMIO_OK) {
                        lon_status = amio_read(coord_dataset, "lonCell", 0, nullptr, &lon_view);
                        if (lon_status == AMIO_OK) {
                            is_radian = true;
                        }
                    }

                    if (lon_status == AMIO_OK) {
                        const void* view_data = nullptr;
                        size_t view_size = 0;
                        if (amio_view_data(lon_view, &view_data, &view_size) == AMIO_OK) {
                            amio_shape_t lon_shape{};
                            if (amio_view_shape(lon_view, &lon_shape) == AMIO_OK) {
                                file_nx = static_cast<int>(lon_shape.extents[0]);
                                bool is_float = (view_size == static_cast<size_t>(file_nx) * 4);
                                const float* float_data = static_cast<const float*>(view_data);
                                const double* double_data = static_cast<const double*>(view_data);
                                file_lon_coords.resize(file_nx);
                                for (int i = 0; i < file_nx; ++i) {
                                    double val = is_float ? static_cast<double>(float_data[i]) : double_data[i];
                                    if (is_radian) {
                                        val *= 180.0 / M_PI;
                                    }
                                    file_lon_coords[i] = val;
                                }
                            }
                        }
                        amio_release_view(lon_view);
                    }

                    amio_status_t lat_status = amio_read(coord_dataset, "lat", 0, nullptr, &lat_view);
                    if (lat_status != AMIO_OK) {
                        lat_status = amio_read(coord_dataset, "latCell", 0, nullptr, &lat_view);
                    }

                    if (lat_status == AMIO_OK) {
                        const void* view_data = nullptr;
                        size_t view_size = 0;
                        if (amio_view_data(lat_view, &view_data, &view_size) == AMIO_OK) {
                            amio_shape_t lat_shape{};
                            if (amio_view_shape(lat_view, &lat_shape) == AMIO_OK) {
                                file_ny = static_cast<int>(lat_shape.extents[0]);
                                bool is_float = (view_size == static_cast<size_t>(file_ny) * 4);
                                const float* float_data = static_cast<const float*>(view_data);
                                const double* double_data = static_cast<const double*>(view_data);
                                file_lat_coords.resize(file_ny);
                                for (int j = 0; j < file_ny; ++j) {
                                    double val = is_float ? static_cast<double>(float_data[j]) : double_data[j];
                                    if (is_radian) {
                                        val *= 180.0 / M_PI;
                                    }
                                    file_lat_coords[j] = val;
                                }
                            }
                        }
                        amio_release_view(lat_view);
                    }
>>>>
```

- [ ] **Step 2: Run compiler to verify success**

Run: `./setup.sh -c "cd build && make -j4 cece_standalone_driver"`
Expected: Build succeeds with 100% target progress.

- [ ] **Step 3: Commit C++ changes**

Run:
```bash
git add src/main.cpp
git commit -m "feat: support gridspec_file and radian coordinate reading in standalone driver"
```

---

### Task 2: Configure and Run cece_config_ex8.yaml with the MPAS Mesh

We will update the YAML configuration to point to the 2562-cell unstructured MPAS mesh file and execute.

**Files:**
- Modify: `examples/cece_config_ex8.yaml`

**Interfaces:**
- Consumes: None
- Produces: Correct configuration parameters for MPAS execution.

- [ ] **Step 1: Replace grid specifications in examples/cece_config_ex8.yaml**

We replace the Gaussian grid description with the unstructured MPAS mesh:
```yaml
# In examples/cece_config_ex8.yaml, replace the grid section with the mesh file description:
<<<<
  grid:
    nx: 270
    ny: 135
    lon_min: -135.0
    lon_max: 135.0
    lat_min: -67.5
    lat_max: 67.5
====
  gridspec_file: "data/x1.2562.grid.nc"
  grid:
    nx: 2562
    ny: 1
>>>>
```

- [ ] **Step 2: Run verification execution of ex8 config**

Run: `./setup.sh -c "./build/cece_standalone_driver examples/cece_config_ex8.yaml"`
Expected: Runs successfully, showing configuration values and cell loading from `data/x1.2562.grid.nc`.

- [ ] **Step 3: Commit YAML changes**

Run:
```bash
git add examples/cece_config_ex8.yaml
git commit -m "config: configure ex8 to use 2562-cell MPAS mesh"
```
