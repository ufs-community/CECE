#include <Kokkos_Core.hpp>
#include <cmath>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "aces/aces_config.hpp"
#include "aces/aces_physics_factory.hpp"
#include "aces/aces_stacking_engine.hpp"
#include "aces/aces_state.hpp"

/**
 * @file hemco_comparison_driver.cpp
 * @brief Standalone driver for verifying HEMCO-parity in ACES using C++ API.
 */

int main(int argc, char** argv) {
    Kokkos::initialize(argc, argv);
    int result = 0;
    {
        int nx = 4, ny = 4, nz = 1;
        std::cout << "[ComparisonDriver] Grid: " << nx << "x" << ny << "x" << nz << std::endl;

        aces::AcesImportState import_state;
        aces::AcesExportState export_state;

        auto create_dv = [&](std::string name, double val, int l_nz) {
            aces::DualView3D dv(name, nx, ny, l_nz);
            Kokkos::deep_copy(dv.view_host(), val);
            dv.modify<Kokkos::HostSpace>();
            dv.sync<Kokkos::DefaultExecutionSpace>();
            return dv;
        };

        // 1. Setup Stacking Engine Test (CO)
        std::cout << "[ComparisonDriver] Setting up CO Stacking Test..." << std::endl;
        import_state.fields["MACCITY_CO"] = create_dv("MACCITY_CO", 1.0, 1);
        import_state.fields["HOURLY_SCALFACT"] = create_dv("HOURLY_SCALFACT", 0.5, 1);
        export_state.fields["co"] = create_dv("co", 0.0, 1);

        aces::AcesConfig config;
        aces::EmissionLayer co_layer;
        co_layer.field_name = "MACCITY_CO";
        co_layer.operation = "add";
        co_layer.hierarchy = 1;
        co_layer.scale_fields = {"HOURLY_SCALFACT"};
        co_layer.scale = 1.0;  // Fixed from base_scale to scale
        config.species_layers["co"] = {co_layer};

        std::cout << "[ComparisonDriver] Initializing Stacking Engine..." << std::endl;
        aces::StackingEngine stack_engine(config);

        std::unordered_map<std::string, std::string> empty_map;
        aces::AcesStateResolver resolver(import_state, export_state, empty_map, empty_map,
                                         empty_map);

        std::cout << "[ComparisonDriver] Executing Stacking Engine..." << std::endl;
        stack_engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

        export_state.fields["co"].sync<Kokkos::HostSpace>();
        double co_val = export_state.fields["co"].view_host()(0, 0, 0);
        double co_expected = 0.5;

        if (std::abs(co_val - co_expected) / (co_expected + 1e-20) < 1e-6) {
            std::cout << "  CO Stacking: SUCCESS" << std::endl;
        } else {
            std::cerr << "  CO Stacking: FAILED (got " << co_val << ", expected " << co_expected
                      << ")" << std::endl;
            result = 1;
        }

        // 2. Setup Physics Scheme Test (DMS)
        std::cout << "[ComparisonDriver] Setting up DMS Physics Test..." << std::endl;
        import_state.fields["wind_speed"] = create_dv("wind_speed", 10.0, 1);
        import_state.fields["tskin"] = create_dv("tskin", 293.15, 1);  // 20C
        import_state.fields["seawater_conc"] = create_dv("seawater_conc", 1.0e-6, 1);
        export_state.fields["dms_emissions"] = create_dv("dms_emissions", 0.0, 1);

        aces::PhysicsSchemeConfig dms_cfg;
        dms_cfg.name = "dms";
        dms_cfg.options =
            YAML::Load("schmidt_coeff: [2674.0, -147.12, 3.726, -0.038]\nkw_coeff: [0.222, 0.333]");

        std::cout << "[ComparisonDriver] Creating DMS Physics Scheme..." << std::endl;
        auto scheme = aces::PhysicsFactory::CreateScheme(dms_cfg);
        if (scheme) {
            std::cout << "[ComparisonDriver] Initializing DMS Physics Scheme..." << std::endl;
            scheme->Initialize(dms_cfg.options, nullptr);
            std::cout << "[ComparisonDriver] Running DMS Physics Scheme..." << std::endl;
            scheme->Run(import_state, export_state);
        } else {
            std::cerr << "  DMS Physics: FAILED (could not create scheme)" << std::endl;
            result = 1;
        }

        export_state.fields["dms_emissions"].sync<Kokkos::HostSpace>();
        double dms_val = export_state.fields["dms_emissions"].view_host()(0, 0, 0);
        double dms_expected = 5.73e-11;

        // Allow some tolerance for numerical precision in transfer velocity calculations
        if (std::abs(dms_val - dms_expected) / dms_expected < 0.1) {
            std::cout << "  DMS Physics: SUCCESS (got " << dms_val << ")" << std::endl;
        } else {
            std::cerr << "  DMS Physics: FAILED (got " << dms_val << ", expected ~" << dms_expected
                      << ")" << std::endl;
            result = 1;
        }
    }
    Kokkos::finalize();
    std::cout << "[ComparisonDriver] Final Result: " << (result == 0 ? "PASSED" : "FAILED")
              << std::endl;
    return result;
}
