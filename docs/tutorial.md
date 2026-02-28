# Tutorial: Adding New Physics Schemes

ACES is designed to be extensible. You can add new physics logic by implementing a "Physics Scheme." This tutorial shows how to add both native C++ schemes and Fortran-based schemes.

## 1. The `PhysicsScheme` Interface

All schemes must inherit from the `aces::PhysicsScheme` base class defined in `include/aces/physics_scheme.hpp`:

```cpp
class PhysicsScheme {
public:
    virtual void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) = 0;
    virtual void Run(AcesImportState& import_state, AcesExportState& export_state) = 0;
};
```

---

## 2. Adding a Native C++ (Kokkos) Scheme

Native schemes are preferred for performance, as they can run on both CPUs and GPUs.

### Step A: Create the Header
Create `include/aces/physics/my_scheme.hpp`:

```cpp
#include "aces/physics_scheme.hpp"

namespace aces {
class MyScheme : public PhysicsScheme {
public:
    void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) override;
    void Run(AcesImportState& import_state, AcesExportState& export_state) override;
};
}
```

### Step B: Implement the Logic
Create `src/physics/my_scheme.cpp`:

```cpp
#include "aces/physics/my_scheme.hpp"
#include <Kokkos_Core.hpp>

namespace aces {
void MyScheme::Initialize(const YAML::Node& config, AcesDiagnosticManager* diag) {
    // Register diagnostics if needed
    diag->RegisterDiagnostic("my_diagnostic", ...);
}

void MyScheme::Run(AcesImportState& import, AcesExportState& export_s) {
    auto base_nox = import.fields["base_nox"].view_device();
    auto total_nox = export_s.fields["total_nox_emissions"].view_device();

    int nx = total_nox.extent(0);
    int ny = total_nox.extent(1);
    int nz = total_nox.extent(2);

    Kokkos::parallel_for("MyKernel",
        Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0,0,0}, {nx,ny,nz}),
        KOKKOS_LAMBDA(int i, int j, int k) {
            total_nox(i,j,k) += base_nox(i,j,k) * 0.5;
        });
}
}
```

---

## 3. Adding a Fortran Scheme

If you have legacy Fortran code, you can wrap it using a C++ bridge.

### Step A: Your Fortran Code
In `src/physics/my_fortran_code.f90`:
```fortran
subroutine compute_something(nox, nx, ny, nz) bind(c, name="compute_something")
    use iso_c_binding
    integer(c_int), intent(in) :: nx, ny, nz
    real(c_double), intent(inout) :: nox(nx, ny, nz)
    ! ... logic ...
end subroutine
```

### Step B: The C++ Bridge Implementation
In `src/physics/my_fortran_bridge.cpp`:

```cpp
extern "C" {
    void compute_something(double* nox, int nx, int ny, int nz);
}

void MyBridge::Run(AcesImportState& import, AcesExportState& export_s) {
    auto& dv_nox = export_s.fields["total_nox_emissions"];

    // 1. Sync to Host (Fortran runs on CPU)
    dv_nox.sync<Kokkos::HostSpace>();

    // 2. Call Fortran
    compute_something(dv_nox.view_host().data(), nx, ny, nz);

    // 3. Mark Host modified and Sync to Device
    dv_nox.modify<Kokkos::HostSpace>();
    dv_nox.sync<Kokkos::DefaultExecutionSpace>();
}
```

---

## 4. Registering Your Scheme

Finally, you must tell the `PhysicsFactory` how to create your scheme.

Edit `src/aces_physics_factory.cpp`:

```cpp
#include "aces/physics/my_scheme.hpp"

std::unique_ptr<PhysicsScheme> PhysicsFactory::CreateScheme(const PhysicsSchemeConfig& config) {
    if (config.name == "my_new_scheme") {
        return std::make_unique<MyScheme>();
    }
    // ...
}
```

Now you can enable it in `aces_config.yaml`:
```yaml
physics_schemes:
  - name: "my_new_scheme"
    language: "cpp"
```
