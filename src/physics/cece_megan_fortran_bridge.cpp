/**
 * @file cece_megan_fortran_bridge.cpp
 * @brief Fortran bridge for MEGAN biogenic emission calculations.
 *
 * This module provides the C++/Fortran interface for the MEGAN (Model of Emissions
 * of Gases and Aerosols from Nature) biogenic emission scheme. It handles data
 * marshalling between Kokkos device views and Fortran arrays, enabling the use
 * of legacy Fortran MEGAN implementations within the CECE framework.
 *
 * The bridge manages:
 * - Memory synchronization between host and device
 * - Data type conversions between C++ and Fortran
 * - Field validation and error handling
 * - Integration with the CECE physics factory system
 *
 * @author Barry Baker
 * @date 2024
 * @version 1.0
 */

#include <Kokkos_Core.hpp>
#include <iostream>

#include "cece/cece_physics_factory.hpp"
#include "cece/physics/cece_megan_fortran.hpp"

extern "C" {
/**
 * @brief External Fortran subroutine for MEGAN emission calculations.
 *
 * @param temp Temperature field [K]
 * @param lai Leaf area index field [m²/m²]
 * @param pardr Direct photosynthetically active radiation [W/m²]
 * @param pardf Diffuse photosynthetically active radiation [W/m²]
 * @param suncos Cosine of solar zenith angle
 * @param isop Output isoprene emissions [kg/m²/s]
 * @param nx Grid dimension in x-direction
 * @param ny Grid dimension in y-direction
 * @param nz Grid dimension in z-direction
 */
void run_megan_fortran(double* temp, double* lai, double* pardr, double* pardf, double* suncos,
                       double* isop, int nx, int ny, int nz);
}

namespace cece {

#ifdef CECE_HAS_FORTRAN
/// @brief Self-registration for the MEGAN Fortran bridge scheme.
static PhysicsRegistration<MeganFortranScheme> register_scheme("megan_fortran");
#endif

/**
 * @brief Initialize the MEGAN Fortran bridge scheme.
 *
 * Performs any necessary setup for the Fortran MEGAN implementation.
 * Currently minimal initialization is required.
 *
 * @param config YAML configuration node (unused for Fortran version)
 * @param diag_manager Diagnostic manager for output handling (unused)
 */
void MeganFortranScheme::Initialize(const YAML::Node& /*config*/,
                                    CeceDiagnosticManager* /*diag_manager*/) {
    std::cout << "MeganFortranScheme: Initialized." << "\n";
}

/**
 * @brief Execute MEGAN biogenic emission calculations using Fortran implementation.
 *
 * This method bridges C++ Kokkos data structures with Fortran arrays to compute
 * biogenic emissions. It handles memory synchronization, field validation, and
 * calls the legacy Fortran MEGAN subroutine.
 *
 * Required input fields:
 * - temperature: Air temperature [K]
 * - lai: Leaf area index [m²/m²]
 * - pardr: Direct PAR [W/m²]
 * - pardf: Diffuse PAR [W/m²]
 * - suncos: Solar zenith angle cosine
 *
 * Output fields:
 * - isoprene: Biogenic isoprene emissions [kg/m²/s]
 *
 * @param import_state Input field state containing meteorological data
 * @param export_state Output field state for emission results
 */
void MeganFortranScheme::Run(CeceImportState& import_state, CeceExportState& export_state) {
    auto it_temp = import_state.fields.find("temperature");
    auto it_isop = export_state.fields.find("isoprene");
    auto it_lai = import_state.fields.find("lai");
    auto it_pardr = import_state.fields.find("pardr");
    auto it_pardf = import_state.fields.find("pardf");
    auto it_suncos = import_state.fields.find("suncos");

    if (it_temp == import_state.fields.end() || it_isop == export_state.fields.end() ||
        it_lai == import_state.fields.end() || it_pardr == import_state.fields.end() ||
        it_pardf == import_state.fields.end() || it_suncos == import_state.fields.end())
        return;

    auto& dv_temp = it_temp->second;
    auto& dv_isop = it_isop->second;
    auto& dv_lai = it_lai->second;
    auto& dv_pardr = it_pardr->second;
    auto& dv_pardf = it_pardf->second;
    auto& dv_suncos = it_suncos->second;

    dv_temp.sync<Kokkos::HostSpace>();
    dv_isop.sync<Kokkos::HostSpace>();
    dv_lai.sync<Kokkos::HostSpace>();
    dv_pardr.sync<Kokkos::HostSpace>();
    dv_pardf.sync<Kokkos::HostSpace>();
    dv_suncos.sync<Kokkos::HostSpace>();

    int nx = static_cast<int>(dv_isop.extent(0));
    int ny = static_cast<int>(dv_isop.extent(1));
    int nz = static_cast<int>(dv_isop.extent(2));

    run_megan_fortran(dv_temp.view_host().data(), dv_lai.view_host().data(),
                      dv_pardr.view_host().data(), dv_pardf.view_host().data(),
                      dv_suncos.view_host().data(), dv_isop.view_host().data(), nx, ny, nz);

    dv_isop.modify<Kokkos::HostSpace>();
    dv_isop.sync<Kokkos::DefaultExecutionSpace>();
}

}  // namespace cece
