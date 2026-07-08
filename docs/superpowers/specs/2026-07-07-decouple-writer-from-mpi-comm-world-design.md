# Specification: Decouple Standalone Writer from MPI_COMM_WORLD

**Date**: 2026-07-07
**Author**: Community Emissions Computing Engine Contributors
**Status**: Approved

---

## 1. Overview
The diagnostic/standalone netCDF output writer (`CeceStandaloneWriter`) currently checks active process ranks using `MPI_COMM_WORLD`. In coupled Earth System Model simulations (such as FV3/UFS runs under NUOPC), `MPI_COMM_WORLD` is shared among all participating model components (Atmosphere, Ocean, Ice, land, etc.).

If diagnostic outputs are activated within the CECE coupled component, utilizing `MPI_COMM_WORLD` will lead to severe rank desynchronization, file overwrite races, and potential segmentation faults since CECE only runs on a subset of the global ranks.

This specification details a robust design to pass the actual CECE component sub-communicator from the driver/cap down into the `CeceStandaloneWriter` during initialization, replacing all usage of `MPI_COMM_WORLD`.

---

## 2. Requirements & API Modifications

### 2.1 Writer Header Updates (`include/cece/cece_standalone_writer.hpp`)
Introduce `MPI_Comm` as an optional parameter in the constructor, defaulting to `MPI_COMM_SELF` to maintain perfect backward compatibility with unit tests that construct the writer directly.

```cpp
#include <mpi.h>

class CeceStandaloneWriter {
   public:
    // ...
    explicit CeceStandaloneWriter(const CeceOutputConfig& config, MPI_Comm comm = MPI_COMM_SELF);
    // ...
   private:
    CeceOutputConfig config_;
    MPI_Comm comm_ = MPI_COMM_SELF;
    // ...
};
```

### 2.2 Bridge Signature Modifications (`src/driver/cece_core_writer_init.cpp`)
Extend both C-linkage entry points to accept the Fortran MPI communicator handle (`int mpi_comm_f`). This handle is translated to a C-linkage `MPI_Comm` via `MPI_Comm_f2c`.

```cpp
extern "C" {
void cece_core_writer_initialize_with_coords(void* data_ptr, int nx, int ny, int nz,
                                             const double* lon_coords, const double* lat_coords,
                                             const char* start_time_iso8601, int start_time_len,
                                             int mpi_comm_f, int* rc);

void cece_core_writer_initialize(void* data_ptr, int nx, int ny, int nz,
                                 const char* start_time_iso8601, int start_time_len,
                                 int mpi_comm_f, int* rc);
}
```

---

## 3. Data Flow and Execution

```
   [Coupled Model (NUOPC Cap)]          [Standalone Driver (main.cpp)]
               │                                      │
               ▼ (Pass 'g_comm')                      ▼ (Pass 'MPI_COMM_WORLD')
   ┌────────────────────────────────────────────────────────────────────────┐
   │ Bridge Layer (cece_core_writer_init)                                   │
   │  - Retrieve mpi_comm_f Fortran handle                                  │
   │  - Translate: comm = MPI_Comm_f2c(mpi_comm_f)                           │
   └───────────────────────────────────┬────────────────────────────────────┘
                                       │
                                       ▼ (Pass to constructor)
   ┌────────────────────────────────────────────────────────────────────────┐
   │ CeceStandaloneWriter                                                   │
   │  - Bind comm_ to internal member variable                              │
   │  - Retrieve rank & size using comm_ instead of MPI_COMM_WORLD          │
   └────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Detailed Component Modifications

### 4.1 Driver Standalone Main (`src/main.cpp`)
Update declarations and pass `MPI_Comm_c2f(MPI_COMM_WORLD)` to the writer initializers in standalone mode (where `MPI_COMM_WORLD` represents the component workspace):

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

### 4.2 Fortran ESMF Cap (`src/driver/nuopc/cece_cap.F90`)
Update the binding subroutine signatures to accept `g_comm` (the Fortran handle of the component sub-communicator):

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

And in `InitializeRealize` pass the component communicator `g_comm`:
```fortran
    call cece_core_writer_initialize_with_coords(g_cece_data_ptr, &
                                                 int(g_nx, c_int), int(g_ny, c_int), int(g_nz, c_int), &
                                                 lon_coords, lat_coords, &
                                                 trim(start_time_str)//c_null_char, &
                                                 int(len_trim(start_time_str), c_int), &
                                                 g_comm, c_rc)
```

---

## 5. Verification & Testing
1. **Compilation Validation**: Rebuild using Intel LLVM on Gaea C6 to verify that Fortran bindings, C/C++ bridge interfaces, and main execution loops compile without errors.
2. **Regression Testing**: Execute the entire automated test suite to verify that existing serial/isolated `CeceStandaloneWriter` tests pass unmodified (relying on the default constructor `MPI_COMM_SELF` fallback).
3. **Execution Safety Verification**: Verify that running the standalone driver executes to completion and produces valid outputs.
