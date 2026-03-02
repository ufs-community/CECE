#include "aces/aces_data_ingestor.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>

#include "aces/aces_utils.hpp"

// Forward declarations for CDEPS-inline API (as it would appear in a production
// bridge). In this task, we assume the existence of these symbols in a real
// CDEPS environment.
extern "C" {
void cdeps_inline_init(const char* config_file) __attribute__((weak));
void cdeps_inline_read(double* buffer, const char* stream_name) __attribute__((weak));
void cdeps_inline_finalize() __attribute__((weak));
}

namespace aces {

static DualView3D CreateDualViewFromESMF(ESMC_State state, const char* name, int nx, int ny,
                                         int nz) {
    ESMC_Field field;
    int rc = ESMC_StateGetField(state, name, &field);
    if (rc != ESMF_SUCCESS) {
        return {};
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
        // Try to get field info to handle 2D or special 3D (like ak/bk)
        ESMC_Field field;
        int local_nx = nx, local_ny = ny, local_nz = nz;
        if (ESMC_StateGetField(importState, name.c_str(), &field) == ESMF_SUCCESS) {
            int lbound[3] = {1, 1, 1}, ubound[3] = {1, 1, 1}, localDe = 0;
            // Robustly discover rank and dimensions by trying different dimCounts
            if (ESMC_FieldGetBounds(field, &localDe, lbound, ubound, 3) == ESMF_SUCCESS) {
                local_nx = ubound[0] - lbound[0] + 1;
                local_ny = ubound[1] - lbound[1] + 1;
                local_nz = ubound[2] - lbound[2] + 1;
            } else if (ESMC_FieldGetBounds(field, &localDe, lbound, ubound, 2) == ESMF_SUCCESS) {
                local_nx = ubound[0] - lbound[0] + 1;
                local_ny = ubound[1] - lbound[1] + 1;
                local_nz = 1;
            } else if (ESMC_FieldGetBounds(field, &localDe, lbound, ubound, 1) == ESMF_SUCCESS) {
                local_nx = ubound[0] - lbound[0] + 1;
                local_ny = 1;
                local_nz = 1;
            }
        }

        aces_state.fields.try_emplace(
            name, CreateDualViewFromESMF(importState, name.c_str(), local_nx, local_ny, local_nz));

        // Sync host to device to ensure Kokkos kernels see updated ESMF data
        auto& dv = aces_state.fields[name];
        if (dv.view_host().data()) {
            dv.modify<Kokkos::HostSpace>();
            dv.sync<Kokkos::DefaultExecutionSpace::memory_space>();
        }
    }
}

void AcesDataIngestor::InitializeCDEPS(const AcesCdepsConfig& config) {
    if (config.streams.empty()) return;

    // 1. Programmatically write CDEPS .streams file (ESMF Config format)
    std::ofstream stream_file("aces_emissions.streams");
    stream_file << "file_id: \"stream\"" << "\n";
    stream_file << "file_version: 2.0" << "\n";
    stream_file << "stream_info:" << "\n";

    for (size_t i = 0; i < config.streams.size(); ++i) {
        const auto& s = config.streams[i];
        std::string id = (i + 1 < 10) ? ("0" + std::to_string(i + 1)) : std::to_string(i + 1);
        stream_file << "taxmode" << id << ": cycle" << "\n";
        stream_file << "tInterpAlgo" << id << ": " << s.interpolation_method << "\n";
        stream_file << "stream_data_files" << id << ": " << s.file_path << "\n";
        stream_file << "stream_data_variables" << id << ": " << s.name << " " << s.name << "\n";
    }
    stream_file.close();

    // 2. Programmatically write CDEPS namelist file
    std::ofstream nml_file("cdeps_in.nml");
    nml_file << "&cdeps_nml" << "\n";
    nml_file << "  stream_file = 'aces_emissions.streams'" << "\n";
    nml_file << "/" << "\n";
    nml_file.close();

    // 3. Initialize CDEPS-inline
    if (cdeps_inline_init) {
        cdeps_inline_init("cdeps_in.nml");
    }
}

void AcesDataIngestor::FinalizeCDEPS() {
    if (cdeps_inline_finalize) {
        cdeps_inline_finalize();
    }
}

void AcesDataIngestor::IngestEmissionsInline(const AcesCdepsConfig& config,
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
        if (cdeps_inline_read) {
            cdeps_inline_read(dv.view_host().data(), s.name.c_str());
        }
        dv.modify<Kokkos::HostSpace>();
        dv.sync<Kokkos::DefaultExecutionSpace::memory_space>();
    }
}

}  // namespace aces
