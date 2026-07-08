# Decouple Standalone Writer from MPI_COMM_WORLD Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Completely eliminate the use of `MPI_COMM_WORLD` inside the diagnostic standalone netCDF writer. Instead, retrieve and propagate the actual component sub-communicator from the driver/cap during initialization to achieve complete multi-component coupled safety.

**Architecture:** We will extend the constructor of `CeceStandaloneWriter` to accept an optional `MPI_Comm` parameter. We will update the C/C++ bridge signature in `cece_core_writer_init.cpp` and the Fortran bindings in `cece_cap.F90` to accept the Fortran MPI communicator handle (`int mpi_comm_f`). We'll translate this to C's `MPI_Comm` and construct the writer with it, while updating standalone model initialization in `main.cpp` to propagate the global communicator.

**Tech Stack:** C++20, Fortran 2018, MPI, NetCDF

## Global Constraints
* All C++ signatures and modified files must be complete, correctly typed, and compile cleanly with Zero warnings.
* perfect backward compatibility with directly-instantiated unit tests (such as `test_cece_utils.cpp`) must be preserved using a default constructor value (`MPI_COMM_SELF`).
* Coupling-isolation standards must be strictly adhered to: never use `MPI_COMM_WORLD` in reusable model component scopes.

---

### Task 1: Update CeceStandaloneWriter Class Interface

**Files:**
- Modify: `include/cece/cece_standalone_writer.hpp`
- Modify: `src/driver/cece_standalone_writer.cpp`

**Interfaces:**
- Consumes: None (base class modification)
- Produces: `CeceStandaloneWriter` constructor accepting `MPI_Comm comm = MPI_COMM_SELF`.

- [ ] **Step 1: Update class definition in `include/cece/cece_standalone_writer.hpp`**

Read `include/cece/cece_standalone_writer.hpp` first to see its constructor signature.
Then replace the constructor with the updated signature taking an optional `MPI_Comm comm = MPI_COMM_SELF` parameter, and declare the internal `comm_` member.

```cpp
    explicit CeceStandaloneWriter(const CeceOutputConfig& config, MPI_Comm comm = MPI_COMM_SELF);
```

And in the private section of the class:
```cpp
    MPI_Comm comm_ = MPI_COMM_SELF;
```

- [ ] **Step 2: Update constructor and `WriteTimeStep` in `src/driver/cece_standalone_writer.cpp`**

In the constructor implementation (around line 89), bind `comm` to the member variable:
```cpp
CeceStandaloneWriter::CeceStandaloneWriter(const CeceOutputConfig& config, MPI_Comm comm)
    : config_(config), initialized_(false), use_custom_coords_(false), nx_(0), ny_(0), nz_(0), comm_(comm) {}
```

In `WriteTimeStep` (around line 180), replace `MPI_COMM_WORLD` with `comm_` when retrieving the rank and size:
```cpp
    int rank = 0;
    int mpi_initialized = 0;
    MPI_Initialized(&mpi_initialized);
    if (mpi_initialized) {
        MPI_Comm_rank(comm_, &rank);
    }
```

- [ ] **Step 3: Run regression tests to verify compilation and backward compatibility**

Load modules and run the build and tests to verify unit tests compile and pass.
Run:
```bash
module use modulefiles && module load cece_gaeac6.intelllvm && cd build && cmake .. && make -j4 && ./test_cece_utils
```
Expected: Compile succeeds with exit code 0 and all tests in `test_cece_utils` succeed.

- [ ] **Step 4: Commit**

```bash
git add include/cece/cece_standalone_writer.hpp src/driver/cece_standalone_writer.cpp
git commit -m "feat(writer): support component-specific MPI communicator in StandaloneWriter"
```

---

### Task 2: Modify Writer C-Bridge Signature

**Files:**
- Modify: `src/driver/cece_core_writer_init.cpp`

**Interfaces:**
- Consumes: `CeceStandaloneWriter(const CeceOutputConfig& config, MPI_Comm comm)`
- Produces: Updated entry points:
  - `cece_core_writer_initialize_with_coords(..., int mpi_comm_f, int* rc)`
  - `cece_core_writer_initialize(..., int mpi_comm_f, int* rc)`

- [ ] **Step 1: Update entry points in `src/driver/cece_core_writer_init.cpp`**

Modify both C-linkage entry points in `src/driver/cece_core_writer_init.cpp` to accept `int mpi_comm_f` (the Fortran handle of the sub-communicator). Convert this handle to a C-linkage `MPI_Comm` using `MPI_Comm_f2c(static_cast<MPI_Fint>(mpi_comm_f))` and pass it to the constructor.

*Update `cece_core_writer_initialize_with_coords`:*
```cpp
void cece_core_writer_initialize_with_coords(void* data_ptr, int nx, int ny, int nz, const double* lon_coords, const double* lat_coords,
                                            const char* start_time_iso8601, int start_time_len, int mpi_comm_f, int* rc) {
    // ...
    try {
        auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);

        if (!g_standalone_writer) {
            auto output_config = internal_data->config.output_config;
            if (output_config.amio_worker_threads == -1) {
                output_config.amio_worker_threads = internal_data->config.driver_config.amio_worker_threads;
            }
            MPI_Comm comm = MPI_COMM_SELF;
            int mpi_initialized = 0;
            MPI_Initialized(&mpi_initialized);
            if (mpi_initialized && mpi_comm_f != 0) {
                comm = MPI_Comm_f2c(static_cast<MPI_Fint>(mpi_comm_f));
            }
            g_standalone_writer = std::make_unique<cece::CeceStandaloneWriter>(output_config, comm);
            std::atexit([]() { g_standalone_writer.reset(); });
        }
```

*Update `cece_core_writer_initialize` in the exact same manner:*
```cpp
void cece_core_writer_initialize(void* data_ptr, int nx, int ny, int nz, const char* start_time_iso8601, int start_time_len, int mpi_comm_f, int* rc) {
    // ...
    try {
        auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);

        if (!g_standalone_writer) {
            auto output_config = internal_data->config.output_config;
            if (output_config.amio_worker_threads == -1) {
                output_config.amio_worker_threads = internal_data->config.driver_config.amio_worker_threads;
            }
            MPI_Comm comm = MPI_COMM_SELF;
            int mpi_initialized = 0;
            MPI_Initialized(&mpi_initialized);
            if (mpi_initialized && mpi_comm_f != 0) {
                comm = MPI_Comm_f2c(static_cast<MPI_Fint>(mpi_comm_f));
            }
            g_standalone_writer = std::make_unique<cece::CeceStandaloneWriter>(output_config, comm);
            std::atexit([]() { g_standalone_writer.reset(); });
        }
```

- [ ] **Step 2: Verify C++ Compilation**

Run the build to ensure the updated bridge compiles cleanly.
Run:
```bash
module use modulefiles && module load cece_gaeac6.intelllvm && cd build && cmake .. && make -j4
```
Expected: C++ build succeeds with exit code 0.

- [ ] **Step 3: Commit**

```bash
git add src/driver/cece_core_writer_init.cpp
git commit -m "feat(writer): update C-bridge signatures to accept Fortran communicator"
```

---

### Task 3: Update Driver Standalone Main and Fortran Cap

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/driver/nuopc/cece_cap.F90`

**Interfaces:**
- Consumes: Updated C-bridge signatures.
- Produces: Compilation consistency across standalone driver and Fortran NUOPC cap.

- [ ] **Step 1: Update declarations and calls in `src/main.cpp`**

In `src/main.cpp`:
1. Update declarations for `cece_core_writer_initialize_with_coords` and `cece_core_writer_initialize` at the top of the file (around lines 30-35) to accept `int mpi_comm_f`.
2. Retrieve the Fortran communicator `writer_comm_f = MPI_Comm_c2f(MPI_COMM_WORLD)` (representing the component workspace in standalone mode), and pass it into both initializer calls (around line 305).

*Declarations (around line 30):*
```cpp
void cece_core_writer_initialize(void* data_ptr, int nx, int ny, int nz, const char* start_time_iso8601, int start_time_len, int mpi_comm_f, int* rc);
void cece_core_writer_initialize_with_coords(void* data_ptr, int nx, int ny, int nz, const double* lon_coords, const double* lat_coords,
                                             const char* start_time_iso8601, int start_time_len, int mpi_comm_f, int* rc);
```

*Calls (around line 305):*
```cpp
        // Standalone Writer: Initialize output writing if configured
        int writer_comm_f = MPI_Comm_c2f(MPI_COMM_WORLD);
        if (has_file_coords) {
            cece_core_writer_initialize_with_coords(cece_data_ptr, nx, ny, nz, file_lons.data(), file_lats.data(), start_time_str.c_str(),
                                                    start_time_str.length(), writer_comm_f, &rc);
        } else {
            cece_core_writer_initialize(cece_data_ptr, nx, ny, nz, start_time_str.c_str(), start_time_str.length(), writer_comm_f, &rc);
        }
```

- [ ] **Step 2: Update Fortran interface declarations and subroutines in `src/driver/nuopc/cece_cap.F90`**

Open `src/driver/nuopc/cece_cap.F90`.
1. In the `interface` declarations section (around lines 140-165), update the binding signatures of `cece_core_writer_initialize_with_coords` and `cece_core_writer_initialize` to accept `integer(c_int), value :: mpi_comm_f`.
2. In the `InitializeRealize` subroutine (around line 370), update the call to `cece_core_writer_initialize_with_coords` to pass the Fortran handle `g_comm` (the component-level sub-communicator initialized by ESMF).

*Inside the interface block (around line 142):*
```fortran
    subroutine cece_core_writer_initialize_with_coords(data_ptr, nx, ny, nz, &
                                                       lon_coords, lat_coords, &
                                                       start_time, start_time_len, &
                                                       mpi_comm_f, rc) &
                                                       bind(C, name="cece_core_writer_initialize_with_coords")
      import :: c_ptr, c_char, c_int, c_double
      type(c_ptr), value :: data_ptr
      integer(c_int), value :: nx, ny, nz
      real(c_double), intent(in) :: lon_coords(*), lat_coords(*)
      character(kind=c_char), intent(in) :: start_time(*)
      integer(c_int), value :: start_time_len
      integer(c_int), value :: mpi_comm_f
      integer(c_int), intent(out) :: rc
    end subroutine
```

*Update the other initialization signature inside interface as well:*
```fortran
    subroutine cece_core_writer_initialize(data_ptr, nx, ny, nz, &
                                           start_time, start_time_len, &
                                           mpi_comm_f, rc) &
                                           bind(C, name="cece_core_writer_initialize")
      import :: c_ptr, c_char, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), value :: nx, ny, nz
      character(kind=c_char), intent(in) :: start_time(*)
      integer(c_int), value :: start_time_len
      integer(c_int), value :: mpi_comm_f
      integer(c_int), intent(out) :: rc
    end subroutine
```

*Inside InitializeRealize call (around line 371):*
```fortran
    call cece_core_writer_initialize_with_coords(g_cece_data_ptr, &
                                                 int(g_nx, c_int), int(g_ny, c_int), int(g_nz, c_int), &
                                                 lon_coords, lat_coords, &
                                                 trim(start_time_str)//c_null_char, &
                                                 int(len_trim(start_time_str), c_int), &
                                                 g_comm, c_rc)
```

- [ ] **Step 3: Rebuild and Verify full project (C++ and Fortran)**

Compile the full project with Fortran capability enabled to verify bindings and calls compile correctly.
Run:
```bash
module use modulefiles && module load cece_gaeac6.intelllvm && cd build && cmake .. && make -j4
```
Expected: The full project compiles perfectly with exit code 0.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp src/driver/nuopc/cece_cap.F90
git commit -m "feat(driver): pass component sub-communicator from standalone main and Fortran NUOPC cap"
```

---

### Task 4: Complete System-wide Verification

**Files:** None

**Interfaces:** None

- [ ] **Step 1: Execute simulation in standalone driver**

Run the example 7 simulation to verify that the serial output files compile and run safely.
Run:
```bash
module use modulefiles && module load cece_gaeac6.intelllvm && ./build/cece_standalone_driver examples/cece_config_ex7.yaml
```
Expected: Simulation runs to completion and successfully writes NetCDF output files in `./cece_output`.

- [ ] **Step 2: Run full automated test suite to ensure regression-free state**

Execute `ctest` to verify all unit, property, and integration tests pass cleanly.
Run:
```bash
cd build && ctest --output-on-failure
```
Expected: 100% tests passed (281/281 succeeding).
