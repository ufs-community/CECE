#include "aces/aces_data_ingestor.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>

#include "aces/aces_utils.hpp"

// Forward declarations for ACES-CDEPS bridge (defined in aces_cdeps_bridge.F90)
extern "C" {
void aces_cdeps_init(void* gcomp, void* clock, void* mesh, const char* stream_file, int* rc)
    __attribute__((weak));
void aces_cdeps_advance(void* clock, int* rc) __attribute__((weak));
void aces_cdeps_get_ptr(int stream_idx, const char* fldname, void** data_ptr, int* rc)
    __attribute__((weak));
void aces_cdeps_finalize() __attribute__((weak));
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
            if (ESMC_FieldGetBounds(field, &localDe, lbound, ubound, 3) { == ESMF_SUCCESS) {
                    local_nx = ubound[0] - lbound[0] + 1;
                }
                local_ny = ubound[1] - lbound[1] + 1;
                local_nz = ubound[2] - lbound[2] + 1;
            } else if (ESMC_FieldGetBounds(field, &localDe, lbound, ubound, 2) { == ESMF_SUCCESS) {
                    local_nx = ubound[0] - lbound[0] + 1;
                }
                local_ny = ubound[1] - lbound[1] + 1;
                local_nz = 1;
            } else if (ESMC_FieldGetBounds(field, &localDe, lbound, ubound, 1) { == ESMF_SUCCESS) {
                    local_nx = ubound[0] - lbound[0] + 1;
                }
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

void AcesDataIngestor::InitializeCDEPS(ESMC_GridComp gcomp, ESMC_Clock clock, ESMC_Mesh mesh,
                                       const AcesCdepsConfig& config) {
    if (config.streams.empty()) {
        return;
    }

    // 1. Programmatically write CDEPS .streams file (ESMF Config format)
    std::ofstream stream_file("aces_emissions.streams");
    stream_file << "file_id: \"stream\"" << "\n";
    stream_file << "file_version: 2.0" << "\n";
    stream_file << "stream_info:" << "\n";

    for (size_t i = 0; i < config.streams.size(); ++i) {
        const auto& s = config.streams[i];
        std::string id = (i + 1 < 10) ? ("0" + std::to_string(i + 1)) : std::to_string(i + 1);
        stream_file << "taxmode" << id << ": " << s.taxmode << "\n";
        stream_file << "tInterpAlgo" << id << ": " << s.tintalgo << "\n";
        stream_file << "mapAlgo" << id << ": " << s.mapalgo << "\n";
        stream_file << "dtlimit" << id << ": " << s.dtlimit << "\n";
        stream_file << "yearFirst" << id << ": " << s.yearFirst << "\n";
        stream_file << "yearLast" << id << ": " << s.yearLast << "\n";
        stream_file << "yearAlign" << id << ": " << s.yearAlign << "\n";
        stream_file << "offset" << id << ": " << s.offset << "\n";
        if (!s.meshfile.empty()) {
            stream_file << "meshfile" << id << ": " << s.meshfile << "\n";
        }
        stream_file << "lev_dimname" << id << ": " << s.lev_dimname << "\n";

        stream_file << "stream_data_files" << id << ": " << "\n";
        for (const auto& f : s.file_paths) {
            stream_file << "  " << f << "\n";
        }

        stream_file << "stream_data_variables" << id << ": " << "\n";
        for (const auto& v : s.variables) {
            stream_file << "  " << v.name_in_file << " " << v.name_in_model << "\n";
        }
    }
    stream_file.close();

    // 2. Initialize CDEPS-inline
    if (aces_cdeps_init) {
        int rc = 0;
        std::string stream_file_path = "aces_emissions.streams";
        aces_cdeps_init(gcomp.ptr, clock.ptr, mesh.ptr, stream_file_path.c_str(), &rc);
        if (rc != 0) {
            std::cerr << "ACES: Error initializing CDEPS. RC=" << rc << "\n";
        }
    }
}

void AcesDataIngestor::AdvanceCDEPS(ESMC_Clock clock) {
    if (aces_cdeps_advance) {
        int rc = 0;
        aces_cdeps_advance(clock.ptr, &rc);
    }
}

void AcesDataIngestor::FinalizeCDEPS() {
    if (aces_cdeps_finalize) {
        aces_cdeps_finalize();
    }
}

void AcesDataIngestor::IngestEmissionsInline(const AcesCdepsConfig& config,
                                             AcesImportState& aces_state, int nx, int ny, int nz) {
    if (config.streams.empty()) {
        return;
    }

    // Trigger read and map pointers for all configured streams.
    // This allows the ingestion layer to be fully dynamic.
    for (size_t i = 0; i < config.streams.size(); ++i) {
        const auto& s = config.streams[i];
        int stream_idx = i + 1;

        for (const auto& v : s.variables) {
            if (aces_state.fields.find(v.name_in_model) { == aces_state.fields.end()) {
                    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> host_view(
                        "host_" + v.name_in_model, nx, ny, nz);
                }
                Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>
                    device_view("device_" + v.name_in_model, nx, ny, nz);
                aces_state.fields.try_emplace(v.name_in_model, device_view, host_view);
            }

            auto& dv = aces_state.fields[v.name_in_model];
            if (aces_cdeps_get_ptr) {
                void* data_ptr = nullptr;
                int rc = 0;
                aces_cdeps_get_ptr(stream_idx, v.name_in_model.c_str(), &data_ptr, &rc);
                if (rc == 0 && data_ptr != nullptr) {
                    // Copy data from CDEPS internal pointer to our host view.
                    // Assuming dimensions match. CDEPS data is often 1D (flattened).
                    double* dptr = static_cast<double*>(data_ptr);
                    std::copy(dptr, dptr + nx * ny * nz, dv.view_host().data());
                } else if (rc != 0) {
                    std::cerr << "ACES: Error getting field pointer from CDEPS for "
                              << v.name_in_model << ". RC=" << rc << "\n";
                }
            }
            dv.modify<Kokkos::HostSpace>();
            dv.sync<Kokkos::DefaultExecutionSpace::memory_space>();
        }
    }
}

}  // namespace aces
