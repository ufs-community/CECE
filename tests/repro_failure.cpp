#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <iostream>
#include <random>
#include <vector>

#include "cece/cece_compute.hpp"
#include "cece/cece_config.hpp"
#include "cece/cece_stacking_engine.hpp"

namespace cece {

class VerticalDistributionReproResolver : public FieldResolver {
    std::map<std::string, DualView3D> fields;

   public:
    void AddField(const std::string& name, int nx, int ny, int nz) {
        fields[name] = DualView3D("test_" + name, nx, ny, nz);
    }

    void SetValue(const std::string& name, double val) {
        auto host = fields[name].view_host();
        Kokkos::deep_copy(host, val);
        fields[name].modify<Kokkos::HostSpace>();
        fields[name].sync<Kokkos::DefaultExecutionSpace::memory_space>();
    }

    void SetValue(const std::string& name, int i, int j, int k, double val) {
        auto host = fields[name].view_host();
        host(i, j, k) = val;
        fields[name].modify<Kokkos::HostSpace>();
        fields[name].sync<Kokkos::DefaultExecutionSpace::memory_space>();
    }

    double GetValue(const std::string& name, int i, int j, int k) {
        fields[name].sync<Kokkos::HostSpace>();
        return fields[name].view_host()(i, j, k);
    }

    UnmanagedHostView3D ResolveImport(const std::string& name, int, int, int) override {
        return fields[name].view_host();
    }
    UnmanagedHostView3D ResolveExport(const std::string& name, int, int, int) override {
        return fields[name].view_host();
    }
    Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>
    ResolveImportDevice(const std::string& name, int, int, int) override {
        if (fields.find(name) == fields.end()) {
            std::cerr << "CRITICAL ERROR: Field " << name << " not found!" << std::endl;
            // Return dummy to avoid crash, but issue is flagged
            return Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>(
                "MISSING", 0, 0, 0);
        }
        return fields[name].view_device();
    }
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> ResolveExportDevice(
        const std::string& name, int, int, int) override {
        return fields[name].view_device();
    }
};

class ReproTest : public ::testing::Test {
   protected:
    std::mt19937 rng{42};

    std::tuple<int, int, int> GenerateRandomGridDimensions() {
        std::uniform_int_distribution<int> nx_dist(2, 20);
        std::uniform_int_distribution<int> ny_dist(2, 20);
        std::uniform_int_distribution<int> nz_dist(5, 50);
        return {nx_dist(rng), ny_dist(rng), nz_dist(rng)};
    }

    void SetUp() override {
        if (!Kokkos::is_initialized()) Kokkos::initialize();
    }
};

TEST_F(ReproTest, ReplicateFailurePrecise) {
    // Run exactly as SmallEmissionsRoundTrip does
    std::vector<VerticalDistributionMethod> methods = {
        VerticalDistributionMethod::SINGLE, VerticalDistributionMethod::RANGE,
        VerticalDistributionMethod::PRESSURE, VerticalDistributionMethod::HEIGHT,
        VerticalDistributionMethod::PBL};

    // We want Iteration 7.
    // Loop 0 to 7.
    for (int iteration = 0; iteration <= 7; ++iteration) {
        auto [nx, ny, nz] = GenerateRandomGridDimensions();

        // Select random method
        std::uniform_int_distribution<int> method_dist(0, methods.size() - 1);
        auto method = methods[method_dist(rng)];

        if (iteration != 7) continue;

        std::cout << "Iteration 7 reached. Method: " << static_cast<int>(method) << " Grid: " << nx
                  << "," << ny << "," << nz << std::endl;

        CeceConfig config;
        EmissionLayer layer_config;
        layer_config.operation = "add";
        layer_config.field_name = "emissions_2d";
        layer_config.hierarchy = 0;
        layer_config.scale = 1.0;
        layer_config.vdist_method = method;

        // Configure method-specific parameters
        // Need to replicate the exact RNG calls from original test!
        // Original:
        /*
        if (method == VerticalDistributionMethod::SINGLE) {
            layer_config.vdist_layer_start = GenerateRandomLayer(nz);
            ...
        } else if ...
        */
        // I need to implement helper functions to match RNG consumption.

        // ... Wait, this is getting complicated to replicate exact RNG state
        // because I need all helper functions.
        // But the user GAVE me the parameters!
        // Grid (11,8,29). Method 2 (PRESSURE).

        // I'll just use those.
        nx = 11;
        ny = 8;
        nz = 29;
        method = VerticalDistributionMethod::PRESSURE;

        layer_config.vdist_method = method;  // Correction: Update layer config!

        // Now I need to generate P start/end using the same logic as the specific method block
        // "PRESSURE" block in SmallEmissionsRoundTrip use:
        std::uniform_real_distribution<double> pressure_dist(10000.0, 100000.0);
        double p_start = pressure_dist(rng);  // Calling this advances RNG
        double p_end = pressure_dist(rng);
        if (p_start > p_end) std::swap(p_start, p_end);

        layer_config.vdist_p_start = p_start;
        layer_config.vdist_p_end = p_end;

        std::cout << "P Start: " << p_start << " P End: " << p_end << std::endl;

        config.species_layers["TestSpecies"] = {layer_config};

        VerticalDistributionReproResolver resolver;
        resolver.AddField("emissions_2d", nx, ny, 1);
        resolver.AddField("TestSpecies", nx, ny, nz);

        // SetupMPASCoords
        {
            // Copy of SetupMPASCoords logic
            config.vertical_config.type = VerticalCoordType::MPAS;
            config.vertical_config.z_field = "height";
            config.vertical_config.pbl_field = "pbl_height";
            config.vertical_config.p_surf_field = "ps";

            resolver.AddField("height", nx, ny, nz + 1);
            double z_top = 20000.0;
            double dz = z_top / nz;
            for (int i = 0; i < nx; ++i) {
                for (int j = 0; j < ny; ++j) {
                    for (int k = 0; k <= nz; ++k) {
                        resolver.SetValue("height", i, j, k, z_top - k * dz);
                    }
                }
            }

            resolver.AddField("ps", nx, ny, 1);
            for (int i = 0; i < nx; ++i)
                for (int j = 0; j < ny; ++j) resolver.SetValue("ps", i, j, 0, 101325.0);

            resolver.AddField("pbl_height", nx, ny, 1);
            std::uniform_real_distribution<double> pbl_dist(500.0, 2000.0);
            for (int i = 0; i < nx; ++i) {
                for (int j = 0; j < ny; ++j) {
                    resolver.SetValue("pbl_height", i, j, 0, pbl_dist(rng));
                }
            }
        }

        // Set very small emissions
        std::uniform_real_distribution<double> small_emis_dist(1e-10, 1e-5);
        std::vector<double> emissions_2d(nx * ny);
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                double val = small_emis_dist(rng);
                emissions_2d[i * ny + j] = val;
                resolver.SetValue("emissions_2d", i, j, 0, val);
            }
        }

        // Sanity check
        std::cout << "Sample emission: " << emissions_2d[0] << std::endl;

        StackingEngine engine(config);
        engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

        // Sum vertically
        std::vector<double> recovered_2d(nx * ny, 0.0);
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                for (int k = 0; k < nz; ++k) {
                    recovered_2d[i * ny + j] += resolver.GetValue("TestSpecies", i, j, k);
                }
            }
        }

        double max_rel_error = 0.0;
        for (size_t i = 0; i < emissions_2d.size(); ++i) {
            if (emissions_2d[i] != 0.0) {
                double rel_error =
                    std::abs(recovered_2d[i] - emissions_2d[i]) / std::abs(emissions_2d[i]);
                max_rel_error = std::max(max_rel_error, rel_error);
            }
        }

        std::cout << "Max Rel Error: " << max_rel_error << std::endl;
        EXPECT_LT(max_rel_error, 1e-10);
    }
}

}  // namespace cece
