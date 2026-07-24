#ifndef CECE_DRIVER_FACADE_HPP
#define CECE_DRIVER_FACADE_HPP

#include <mpi.h>

#include <dagr/dagr.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "cece/cece_io.hpp"
#include "cece/cece_regridder_utils.hpp"

namespace cece {

class CeceDriverOrchestrator {
   public:
    CeceDriverOrchestrator(const std::string& config_file, int nx, int ny, int nz, const double* lon_coords, int lon_len, const double* lat_coords,
                           int lat_len, MPI_Comm comm_c);
    ~CeceDriverOrchestrator();

    // Prevent copy/move construction and assignment (Rule of Five)
    CeceDriverOrchestrator(const CeceDriverOrchestrator&) = delete;
    CeceDriverOrchestrator& operator=(const CeceDriverOrchestrator&) = delete;
    CeceDriverOrchestrator(CeceDriverOrchestrator&&) = delete;
    CeceDriverOrchestrator& operator=(CeceDriverOrchestrator&&) = delete;

    bool AdvanceTime(const std::string& time_iso8601, void* cece_core_data_ptr);

   private:
    std::string config_file_;
    int nx_{0}, ny_{0}, nz_{0};
    std::vector<double> target_lons_;
    std::vector<double> target_lats_;
    int step_index_{0};
    MPI_Comm comm_c_{MPI_COMM_NULL};

    // Cached regridding plans keyed by model variable name. The expensive
    // interpolation weights are built once (per rank-local destination band)
    // and reused for every timestep.
    std::unordered_map<std::string, io::RegridPlan> regrid_plans_;
    std::unordered_map<std::string, int> file_nt_cache_;

    // HELM Orchestration and pipeline components
    std::unique_ptr<dagr::GraphOrchestrator> dagr_;
    std::unique_ptr<io::CeceIO> cece_io_;
};

}  // namespace cece

extern "C" {
void cece_driver_create(const char* yaml_path, int path_len, int nx, int ny, int nz, const double* lon_coords, int lon_len, const double* lat_coords,
                        int lat_len, int mpi_comm_f, void** driver_ptr_out, int* rc);

void cece_driver_advance_time(void* driver_ptr, const char* time_iso8601, int time_len, void* cece_core_data_ptr, int* rc);

void cece_driver_destroy(void* driver_ptr);
}

#endif  // CECE_DRIVER_FACADE_HPP
