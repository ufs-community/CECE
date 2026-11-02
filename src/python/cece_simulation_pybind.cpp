// SPDX-License-Identifier: Apache-2.0
// Python lifecycle adapter for the shared standalone/NUOPC simulation facade.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <mpi.h>
#include <Kokkos_Core.hpp>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include "cece/cece_sim_c_abi.h"
#include "cece/cece_simulation.hpp"

namespace py = pybind11;
namespace {
bool active = false;
bool owns_mpi = false;
void finish_mpi() {
    int finalized = 0;
    MPI_Finalized(&finalized);
    if (owns_mpi && !finalized) MPI_Finalize();
}
class PythonSimulation {
 public:
    explicit PythonSimulation(const std::string& path) {
        if (active) throw std::runtime_error("Only one full CECE simulation may be active in a process");
        int initialized = 0, finalized = 0;
        MPI_Initialized(&initialized);
        MPI_Finalized(&finalized);
        if (finalized) throw std::runtime_error("MPI has already been finalized");
        if (!initialized) {
            int provided = 0;
            if (MPI_Init_thread(nullptr, nullptr, MPI_THREAD_SERIALIZED, &provided) != MPI_SUCCESS)
                throw std::runtime_error("Could not initialize MPI");
            owns_mpi = true;
            std::atexit(finish_mpi);
        }
        int size = 0;
        MPI_Comm_size(MPI_COMM_WORLD, &size);
        if (size != 1) throw std::runtime_error("Python simulation currently supports one MPI rank");
        int rc = 0;
        cece_sim_create_from_yaml(path.c_str(), static_cast<int>(path.size()), MPI_Comm_c2f(MPI_COMM_WORLD), &sim_, &rc);
        if (rc != 0 || !sim_) throw std::runtime_error("Could not initialize CECE simulation; see native log");
        active = true;
    }
    ~PythonSimulation() { (void)close(); }
    PythonSimulation(const PythonSimulation&) = delete;
    int close() {
        if (sim_) {
            int rc = 0;
            cece_sim_finalize(sim_, &rc);
            sim_ = nullptr;
            active = false;
            return rc;
        }
        return 0;
    }
    cece::CeceSimulation& impl() const {
        if (!sim_) throw std::runtime_error("CECE simulation is closed");
        return *reinterpret_cast<cece::CeceSimulation*>(sim_);
    }
    bool step(const std::string& start, const std::string& end, int index) {
        if (!sim_) throw std::runtime_error("CECE simulation is closed");
        int complete = 0, rc = 0;
        cece_sim_step(sim_, start.c_str(), static_cast<int>(start.size()), end.c_str(), static_cast<int>(end.size()), index, &complete, &rc);
        if (rc != 0) throw std::runtime_error("CECE simulation step failed; see native log");
        return complete != 0;
    }
    void set_input(const std::string& name, const std::vector<double>& values, int nx, int ny_local, int nz) {
        int rc = 0;
        if (!impl().stage_import_field(name, values, nx, ny_local, nz, &rc) || rc != 0)
            throw std::invalid_argument("Could not stage external import '" + name + "' (shape or lifecycle mismatch)");
    }
    py::dict geometry() const {
        auto& sim = impl();
        auto& grid = sim.grid();
        py::dict result;
        result["nx"] = grid.nx; result["ny"] = grid.ny; result["nz"] = grid.nz;
        result["longitude"] = grid.lon_coords; result["latitude"] = grid.lat_coords;
        result["topology"] = static_cast<int>(grid.topology);
        result["j0"] = sim.band().j0; result["j1"] = sim.band().j1;
        return result;
    }
 private:
    ::CeceSimulation* sim_ = nullptr;
};
}
PYBIND11_MODULE(_cece_simulation, module) {
    module.def("kokkos_initialized", [] { return Kokkos::is_initialized(); });
    py::class_<PythonSimulation>(module, "Simulation")
        .def(py::init<const std::string&>())
        .def("step", &PythonSimulation::step)
        .def("set_input", &PythonSimulation::set_input)
        .def("geometry", &PythonSimulation::geometry)
        .def("output_names", [](const PythonSimulation& self) { return self.impl().output_names(); })
        .def("snapshot", [](const PythonSimulation& self, const std::string& name) { return self.impl().snapshot_export(name); })
        .def("close", &PythonSimulation::close);
}
