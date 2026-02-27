#include "aces/aces_data_ingestor.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>

#include "aces/aces_utils.hpp"

// CDEPS-inline API.
// Since CDEPS is a required dependency, we expect these symbols to be resolved at link time.
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

void AcesDataIngestor::IngestMeteorology(ESMC_State importState,
                                         const std::vector<std::string>& field_names,
                                         AcesImportState& aces_state, int nx, int ny, int nz) {
    for (const auto& name : field_names) {
        if (aces_state.fields.find(name) == aces_state.fields.end()) {
            aces_state.fields[name] = CreateDualViewFromESMF(importState, name.c_str(), nx, ny, nz);
        }

        // Sync host to device to ensure Kokkos kernels see updated ESMF data
        auto& dv = aces_state.fields[name];
        if (dv.view_host().data()) {
            dv.modify<Kokkos::HostSpace>();
            dv.sync<Kokkos::DefaultExecutionSpace::memory_space>();
        }
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

    // 4. Trigger read and map pointers for all configured streams.
    // This allows the ingestion layer to be fully dynamic.
    for (const auto& s : config.streams) {
        if (aces_state.fields.find(s.name) == aces_state.fields.end()) {
            Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> host_view(
                "host_" + s.name, nx, ny, nz);
            Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> device_view(
                "device_" + s.name, nx, ny, nz);
            aces_state.fields[s.name] = DualView3D(device_view, host_view);
        }

        auto& dv = aces_state.fields[s.name];
        cdeps_inline_read(dv.view_host().data(), s.name.c_str());
        dv.modify<Kokkos::HostSpace>();
        dv.sync<Kokkos::DefaultExecutionSpace::memory_space>();
    }

    cdeps_inline_finalize();
}

}  // namespace aces
