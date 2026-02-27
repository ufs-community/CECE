#include "aces/aces_data_ingestor.hpp"

#include <fstream>
#include <iostream>
#include <vector>

#include "aces/aces_utils.hpp"

// In a real environment, we would include <cdeps_inline.h>
// Since we don't have the actual library headers in this environment,
// we'll assume the existence of these C-linkage functions.
extern "C" {
void cdeps_inline_init(const char* config_file);
void cdeps_inline_read(double* buffer, const char* stream_name);
void cdeps_inline_finalize();
}

namespace aces {

static DualView3D CreateDualViewFromESMF(ESMC_State state, const char* name, int nx, int ny,
                                         int nz) {
    ESMC_Field field;
    int rc = ESMC_StateGetField(state, name, &field);
    if (rc != ESMF_SUCCESS) {
        return DualView3D();
    }
    UnmanagedHostView3D host_view = WrapESMCField(field, nx, ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> device_view(
        std::string("device_") + name, nx, ny, nz);
    return DualView3D(device_view, host_view);
}

void AcesDataIngestor::IngestMeteorology(ESMC_State importState, AcesImportState& aces_state,
                                         int nx, int ny, int nz) {
    if (aces_state.temperature.view_host().data() == nullptr) {
        aces_state.temperature = CreateDualViewFromESMF(importState, "temperature", nx, ny, nz);
        aces_state.wind_speed_10m =
            CreateDualViewFromESMF(importState, "wind_speed_10m", nx, ny, nz);
    }

    // Sync host to device to ensure Kokkos kernels see updated ESMF data
    if (aces_state.temperature.view_host().data()) {
        aces_state.temperature.modify<Kokkos::HostSpace>();
        aces_state.temperature.sync<Kokkos::DefaultExecutionSpace>();
    }
    if (aces_state.wind_speed_10m.view_host().data()) {
        aces_state.wind_speed_10m.modify<Kokkos::HostSpace>();
        aces_state.wind_speed_10m.sync<Kokkos::DefaultExecutionSpace>();
    }
}

void AcesDataIngestor::IngestEmissionsInline(const AcesCdepsConfig& config,
                                             AcesImportState& aces_state, int nx, int ny, int nz) {
    if (config.streams.empty()) return;

    // 1. Programmatically write CDEPS .streams file (ESMF Config format)
    std::ofstream stream_file("aces_emissions.streams");
    stream_file << "file_id: \"stream\"" << std::endl;
    stream_file << "file_version: 2.0" << std::endl;
    stream_file << "stream_info:" << std::endl;

    for (size_t i = 0; i < config.streams.size(); ++i) {
        const auto& s = config.streams[i];
        char id[3];
        std::sprintf(id, "%02zu", i + 1);
        stream_file << "taxmode" << id << ": cycle" << std::endl;
        stream_file << "tInterpAlgo" << id << ": " << s.interpolation_method << std::endl;
        stream_file << "stream_data_files" << id << ": " << s.file_path << std::endl;
        stream_file << "stream_data_variables" << id << ": " << s.name << " " << s.name
                    << std::endl;
    }
    stream_file.close();

    // 2. Programmatically write CDEPS namelist file
    std::ofstream nml_file("cdeps_in.nml");
    nml_file << "&cdeps_nml" << std::endl;
    nml_file << "  stream_file = 'aces_emissions.streams'" << std::endl;
    nml_file << "/" << std::endl;
    nml_file.close();

    // 3. Initialize CDEPS-inline
    cdeps_inline_init("cdeps_in.nml");

    // 4. Trigger read and map pointers
    for (const auto& s : config.streams) {
        if (s.name == "base_anthropogenic_nox") {
            if (aces_state.base_anthropogenic_nox.view_host().data() == nullptr) {
                Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> host_view(
                    "host_base_anthro_nox", nx, ny, nz);
                Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>
                    device_view("device_base_anthro_nox", nx, ny, nz);
                aces_state.base_anthropogenic_nox = DualView3D(device_view, host_view);
            }

            cdeps_inline_read(aces_state.base_anthropogenic_nox.view_host().data(), s.name.c_str());
            aces_state.base_anthropogenic_nox.modify<Kokkos::HostSpace>();
            aces_state.base_anthropogenic_nox.sync<Kokkos::DefaultExecutionSpace>();
        }
    }

    cdeps_inline_finalize();
}

}  // namespace aces
