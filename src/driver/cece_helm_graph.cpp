#include "cece/cece_helm_graph.hpp"

#include <mpi.h>
#include <yaml-cpp/yaml.h>

#include <blend/helm_math_blend.hpp>
#include <dagr/pipeline_config.hpp>
#include <fstream>
#include <halo/communicator.hpp>
#include <stdexcept>
#include <tick/duration.hpp>

void CompileHelmGraph(const std::string& config_file, std::unique_ptr<dagr::GraphOrchestrator>& dagr, cece::io::CeceIO& cece_io, MPI_Comm comm_c) {
    std::ifstream f(config_file);
    if (!f.good()) {
        throw std::runtime_error("File not found: " + config_file);
    }

    YAML::Node config = YAML::LoadFile(config_file);

    // Build the Pipeline_Config dynamically from standard YAML
    dagr::Pipeline_Config pc;
    pc.max_concurrency = 4;
    // CECE currently performs its emission computation synchronously in the run
    // loop (cece_core_run / AdvanceTime), not through DAGR task callbacks, so the
    // placeholder pipeline task is dispatched but never completed. That leaves a
    // phantom in-flight task at teardown. A short shutdown drain lets
    // GraphOrchestrator::shutdown() force-cancel it quickly and exit cleanly
    // instead of stalling. The deadlock timer is kept long so the phantom task
    // does not trip a spurious "potential deadlock" warning during normal runs.
    pc.deadlock_timeout_s = 3600;
    pc.shutdown_timeout_s = 5;

    // Load active variables from CeceIO and dynamically compile them into HELM Stream Descriptors
    for (const auto& var_name : cece_io.GetOutputVarNames()) {
        dagr::Stream_Descriptor stream;
        stream.name = var_name;
        stream.temporal_profile = dagr::Temporal_Profile::linear;
        stream.oob_policy = dagr::OutOfBounds_Policy::cycle;
        stream.dataset_path = "data/emissions/" + var_name + ".nc";
        stream.snapshot_interval = tick::seconds(3600);  // 1 hour intervals
        pc.streams.push_back(std::move(stream));
    }

    // Instantiating GraphOrchestrator requires world communicator.
    // Wrap custom MPI communicator in halo::Communicator safely (duplicating to prevent RAII destruction of ESMF's handle)
    MPI_Comm comm_to_wrap = comm_c;
    if (comm_c != MPI_COMM_NULL && comm_c != MPI_COMM_WORLD && comm_c != MPI_COMM_SELF) {
        MPI_Comm_dup(comm_c, &comm_to_wrap);
    }
    halo::Communicator world(comm_to_wrap);

    // Allocate the GraphOrchestrator
    dagr = std::make_unique<dagr::GraphOrchestrator>(std::move(pc), std::move(world));
}

namespace blend {

void dispatch_blend(const double* left_ptr, const double* right_ptr, double* out_ptr, std::size_t count, double alpha, int profile_tag) {
    // Create unmanaged non-owning flat Kokkos views matching BLEND expectation
    span::FieldView left(const_cast<double*>(left_ptr), count);
    span::FieldView right(const_cast<double*>(right_ptr), count);
    span::FieldView target(out_ptr, count);

    BlendProfile profile = (profile_tag == 0) ? BlendProfile::Linear : BlendProfile::Step;

    execute_blend(left, right, target, alpha, profile);
}

}  // namespace blend
