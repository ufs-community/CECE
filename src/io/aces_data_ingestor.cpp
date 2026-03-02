#include "aces/aces_data_ingestor.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>

#include "aces/aces_utils.hpp"

// Forward declarations for DEMS/DEMIS-inline API.
// In this task, we assume the existence of these symbols in the real
// DEMS environment.
extern "C" {
void dems_inline_init(const char* config_file) __attribute__((weak));
void dems_inline_read(double* buffer, const char* stream_name) __attribute__((weak));
void dems_inline_finalize() __attribute__((weak));
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
        aces_state.fields.try_emplace(
            name, CreateDualViewFromESMF(importState, name.c_str(), nx, ny, nz));

        // Sync host to device to ensure Kokkos kernels see updated ESMF data
        auto& dv = aces_state.fields[name];
        if (dv.view_host().data()) {
            dv.modify<Kokkos::HostSpace>();
            dv.sync<Kokkos::DefaultExecutionSpace::memory_space>();
        }
    }
}

void AcesDataIngestor::InitializeDEMS(const AcesDemsConfig& config) {
    if (config.streams.empty()) return;

    // 1. Programmatically write DEMS .streams file (ESMF Config format)
    // This file provides full control over all stream attributes.
    std::ofstream stream_file("aces_emissions.streams");
    stream_file << "file_id: \"stream\"" << std::endl;
    stream_file << "file_version: 2.0" << std::endl;
    stream_file << "stream_info:";
    for (size_t i = 0; i < config.streams.size(); ++i) {
        stream_file << " " << config.streams[i].name << (i + 1 < 10 ? "0" : "") << (i + 1);
    }
    stream_file << std::endl << std::endl;

    for (size_t i = 0; i < config.streams.size(); ++i) {
        const auto& s = config.streams[i];
        std::string id = (i + 1 < 10) ? ("0" + std::to_string(i + 1)) : std::to_string(i + 1);
        stream_file << "taxmode" << id << ": " << s.taxmode << std::endl;
        stream_file << "tInterpAlgo" << id << ": " << s.tintalgo << std::endl;
        stream_file << "readMode" << id << ": " << s.readmode << std::endl;
        stream_file << "mapalgo" << id << ": " << s.mapalgo << std::endl;
        stream_file << "dtlimit" << id << ": " << s.dtlimit << std::endl;
        if (s.yearFirst != 0) stream_file << "yearFirst" << id << ": " << s.yearFirst << std::endl;
        if (s.yearLast != 0) stream_file << "yearLast" << id << ": " << s.yearLast << std::endl;
        if (s.yearAlign != 0) stream_file << "yearAlign" << id << ": " << s.yearAlign << std::endl;
        if (!s.vectors.empty())
            stream_file << "stream_vectors" << id << ": " << s.vectors << std::endl;
        if (!s.meshfile.empty())
            stream_file << "stream_mesh_file" << id << ": " << s.meshfile << std::endl;
        if (!s.lev_dimname.empty())
            stream_file << "stream_lev_dimname" << id << ": " << s.lev_dimname << std::endl;
        stream_file << "stream_data_files" << id << ": " << s.file_path << std::endl;
        stream_file << "stream_data_variables" << id << ": " << s.name << " " << s.name
                    << std::endl;
        stream_file << "stream_offset" << id << ": " << s.offset << std::endl << std::endl;
    }
    stream_file.close();

    // 2. Programmatically write DEMS namelist file
    std::ofstream nml_file("dems_in.nml");
    nml_file << "&dems_nml" << std::endl;
    nml_file << "  stream_file = 'aces_emissions.streams'" << std::endl;
    nml_file << "/" << std::endl;
    nml_file.close();

    // 3. Initialize DEMS-inline
    if (dems_inline_init) {
        dems_inline_init("dems_in.nml");
    }
}

void AcesDataIngestor::FinalizeDEMS() {
    if (dems_inline_finalize) {
        dems_inline_finalize();
    }
}

void AcesDataIngestor::IngestEmissionsInline(const AcesDemsConfig& config,
                                             AcesImportState& aces_state, int nx, int ny, int nz) {
    if (config.streams.empty()) return;

    // Trigger read and map pointers for all configured streams.
    // This allows the ingestion layer to be fully dynamic.
    for (const auto& s : config.streams) {
        if (aces_state.fields.find(s.name) == aces_state.fields.end()) {
            Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> host_view(
                "host_" + s.name, nx, ny, nz);
            Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> device_view(
                "device_" + s.name, nx, ny, nz);
            aces_state.fields.try_emplace(s.name, device_view, host_view);
        }

        auto& dv = aces_state.fields[s.name];
        if (dems_inline_read) {
            dems_inline_read(dv.view_host().data(), s.name.c_str());
        }
        dv.modify<Kokkos::HostSpace>();
        dv.sync<Kokkos::DefaultExecutionSpace::memory_space>();
    }
}

}  // namespace aces
