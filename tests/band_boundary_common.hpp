#pragma once
#ifndef BAND_BOUNDARY_COMMON_HPP
#define BAND_BOUNDARY_COMMON_HPP

#include <Kokkos_Core.hpp>
#include <axis/solver/weight_generator.hpp>
#include <cmath>

#include "cece/cece_regridder_utils.hpp"

namespace cece::io {
namespace {

int band_start(int ny, int size, int r) {
    const int band_base = ny / size;
    const int band_rem = ny % size;
    return r * band_base + std::min(r, band_rem);
}

RegridPlan MakePlanFromMeshes(int src_nx, int src_ny, axis::topology::UnstructuredMesh<Kokkos::HostSpace> src_mesh,
                              axis::topology::UnstructuredMesh<Kokkos::HostSpace> dst_mesh, int j0, int j1) {
    RegridPlan plan;
    plan.file_nx = src_nx;
    plan.file_ny = src_ny;
    plan.j0 = j0;
    plan.j1 = j1;
    plan.identity = false;
    axis::solver::RegridConfig cfg;
    cfg.method = axis::solver::InterpolationMethod::Conservative1stOrder;
    cfg.norm_type = axis::solver::NormType::DstArea;
    cfg.unmapped = axis::solver::UnmappedAction::Ignore;
    plan.matrix = axis::solver::WeightGenerator::generate<Kokkos::HostSpace>(src_mesh, dst_mesh, cfg);
    plan.matrix.to_csr();
    plan.built = true;
    return plan;
}

}  // namespace
}  // namespace cece::io
#endif
