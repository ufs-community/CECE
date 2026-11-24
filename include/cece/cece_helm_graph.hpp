#ifndef CECE_HELM_GRAPH_HPP
#define CECE_HELM_GRAPH_HPP

#include <mpi.h>

#include <dagr/dagr.hpp>
#include <filesystem>
#include <memory>

#include "cece/cece_io.hpp"

/**
 * @brief Programmatically compiles a high-level CECE streams YAML configuration
 *        into low-level HELM SPAN buffers and DAGR pipeline tasks.
 * @param config_file Path to unified cece_control.yaml file.
 * @param[out] dagr Unique pointer to the allocated dagr::GraphOrchestrator.
 * @param cece_io Reference to initialized C++ CeceIO component.
 * @param comm_c Custom MPI communicator to run the HELM graph on.
 */
void CompileHelmGraph(const std::filesystem::path& config_file, std::unique_ptr<dagr::GraphOrchestrator>& dagr, cece::io::CeceIO& cece_io,
                      MPI_Comm comm_c = MPI_COMM_WORLD);

#endif  // CECE_HELM_GRAPH_HPP
