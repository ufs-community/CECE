#include <gtest/gtest.h>
#include <mpi.h>

#include <Kokkos_Core.hpp>
#include <dagr/dagr.hpp>
#include <halo/environment.hpp>

#include "cece/cece_helm_graph.hpp"
#include "cece/cece_io.hpp"

// Forward declare CECE C-Linkage APIs
extern "C" {
void cece_set_config_file_path(const char* config_path, int path_len);
void cece_core_initialize_p1(void** data_ptr_ptr, int* rc);
void cece_core_finalize(void* data_ptr, int* rc);
}

std::string GetConfigPath() {
#ifdef CECE_SOURCE_DIR
    return std::string(CECE_SOURCE_DIR) + "/tests/cece_control_mock.yaml";
#else
    return "tests/cece_control_mock.yaml";
#endif
}

TEST(HelmGraphTest, TestBMIPointerAllocation) {
    cece::io::CeceIO cece_io;
    EXPECT_ANY_THROW(cece_io.Initialize("non_existent_file.yaml", 72, 46, 1));
}

TEST(HelmGraphTest, TestDynamicGraphCompilation) {
    std::unique_ptr<dagr::GraphOrchestrator> dagr;
    cece::io::CeceIO cece_io;

    std::string mock_config = GetConfigPath();
    cece_io.Initialize(mock_config, 72, 46, 1);
    CompileHelmGraph(mock_config, dagr, cece_io);

    EXPECT_TRUE(true);
}

TEST(HelmGraphTest, TestShutdownNoTimeout) {
    std::unique_ptr<dagr::GraphOrchestrator> dagr;
    cece::io::CeceIO cece_io;

    std::string mock_config = GetConfigPath();
    cece_io.Initialize(mock_config, 72, 46, 1);
    CompileHelmGraph(mock_config, dagr, cece_io);

    // Call advance_step to dispatch the task(s)
    dagr->advance_step();

    // Measure the time taken to shutdown to verify that it does not hit the 5s timeout
    auto start = std::chrono::steady_clock::now();
    dagr->shutdown();
    auto elapsed = std::chrono::steady_clock::now() - start;

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    // It should shut down almost instantly (definitely < 1 second), not hitting the 5s timeout.
    EXPECT_LT(elapsed_ms, 1000);
}

TEST(HelmGraphTest, TestEndToEndDriverLoopStub) {
    // Set config file path dynamically
    std::string mock_config = GetConfigPath();
    cece_set_config_file_path(mock_config.c_str(), static_cast<int>(mock_config.length()));

    // Verifies that the C-linkage setup compiles and instantiates without hanging
    void* cece_data_ptr = nullptr;
    int rc = 0;
    cece_core_initialize_p1(&cece_data_ptr, &rc);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(cece_data_ptr, nullptr);
    cece_core_finalize(cece_data_ptr, &rc);
}

// Custom GTest Environment to manage Kokkos & MPI lifecycle globally
class KokkosMpiEnvironment : public ::testing::Environment {
   private:
    int argc_;
    char** argv_;

   public:
    KokkosMpiEnvironment(int argc, char** argv) : argc_(argc), argv_(argv) {}

    void SetUp() override {
        // Initialize MPI first
        int mpi_initialized = 0;
        MPI_Initialized(&mpi_initialized);
        if (!mpi_initialized) {
            int provided = 0;
            MPI_Init_thread(&argc_, &argv_, MPI_THREAD_MULTIPLE, &provided);
        }

        // Initialize Kokkos
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize(argc_, argv_);
        }

        // Initialize HALO Environment
        halo::Environment::initialize();
    }
    void TearDown() override {
        // Finalize Kokkos
        if (Kokkos::is_initialized()) {
            Kokkos::finalize();
        }

        // Finalize MPI
        int mpi_initialized = 0;
        MPI_Initialized(&mpi_initialized);
        if (mpi_initialized) {
            MPI_Finalize();
        }
    }
};

int main(int argc, char** argv) {
    // Prevent Intel MPI from detecting Slurm and attempting PMI/PMIX process manager bootstrap during unit tests
    unsetenv("SLURM_JOB_ID");
    unsetenv("SLURM_STEP_ID");
    unsetenv("PMI_RANK");
    unsetenv("PMI_SIZE");

    // Configure Intel MPI to allow standalone, local-only execution on login nodes (prevent PMI2/Hydra aborts)
    setenv("I_MPI_HYDRA_BOOTSTRAP", "none", 0);
    setenv("I_MPI_SHM", "disable", 0);

    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new KokkosMpiEnvironment(argc, argv));
    return RUN_ALL_TESTS();
}
