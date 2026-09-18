/**
 * @file test_seasalt_geos12.cpp
 * @brief Registration and configuration-contract checks for the GEOS-12 sea
 *        salt Fortran bridge scheme.
 */

#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <conf/config.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cece/cece_physics_factory.hpp"
#include "cece/cece_state.hpp"
#include "cece/physics/cece_seasalt_geos12_fortran.hpp"

namespace cece {
namespace {

// Writes a temporary MICM mechanism file with 5 sea salt aerosol bins.
// effective_radius is omitted when with_radius is false (midpoint default).
std::string WriteMechanismFile(const std::string& tag, bool with_radius = true) {
    const std::string path = (std::filesystem::temp_directory_path() / ("cece_seasalt_geos12_spc_" + tag + ".yaml")).string();
    std::ofstream f(path);
    f << "name: TEST_GOCART\nspecies:\n";
    const char* names[5] = {"SS001", "SS002", "SS003", "SS004", "SS005"};
    const double r_low[5] = {0.03, 0.1, 0.5, 1.5, 5.0};
    const double r_up[5] = {0.1, 0.5, 1.5, 5.0, 10.0};
    const double r_eff[5] = {0.079, 0.316, 0.898, 2.83, 7.5};
    for (int n = 0; n < 5; ++n) {
        f << "  - name: " << names[n] << "\n";
        f << "    molecular weight [kg mol-1]: 0.05844\n";
        f << "    is_aerosol: true\n";
        f << "    density [kg m-3]: 2200.0\n";
        f << "    lower_radius [um]: " << r_low[n] << "\n";
        f << "    upper_radius [um]: " << r_up[n] << "\n";
        if (with_radius) f << "    effective_radius [um]: " << r_eff[n] << "\n";
    }
    return path;
}

// Writes the identity aerosol map (nested SS00n -> {SS00n: 1.0}) for dataset SEASALT.
std::string WriteMapFile(const std::string& tag) {
    const std::string path = (std::filesystem::temp_directory_path() / ("cece_seasalt_geos12_map_" + tag + ".yaml")).string();
    std::ofstream f(path);
    f << "mechanism: TEST_GOCART\ndatasets:\n  SEASALT:\n";
    for (int n = 1; n <= 5; ++n) f << "    SS00" << n << ":\n      SS00" << n << ": 1.0\n";
    return path;
}

// Builds an in-memory conf config referencing the given mechanism/map files.
conf::Config MakeConfig(const std::string& mechanism_file, const std::string& speciation_file, const std::string& dataset = "SEASALT") {
    std::ostringstream y;
    y << "weibull_flag: false\n"
      << "scale_factor: 1.0\n"
      << "mechanism_file: \"" << mechanism_file << "\"\n"
      << "speciation_file: \"" << speciation_file << "\"\n"
      << "speciation_dataset: \"" << dataset << "\"\n";
    return conf::Config::from_string(y.str());
}

DualView3D MakeField(const std::string& name, int nx, int ny, int levels, double value) {
    DualView3D field(name, nx, ny, levels);
    Kokkos::deep_copy(field.view_host(), value);
    field.modify_host();
    field.sync_device();
    return field;
}

void AddOceanImports(CeceImportState& import_state, int nx, int ny) {
    import_state.fields["frocean"] = MakeField("frocean", nx, ny, 1, 1.0);
    import_state.fields["frseaice"] = MakeField("frseaice", nx, ny, 1, 0.0);
    import_state.fields["lat"] = MakeField("lat", nx, ny, 1, 0.0);
    import_state.fields["lon"] = MakeField("lon", nx, ny, 1, 0.0);
    import_state.fields["sst"] = MakeField("sst", nx, ny, 1, 293.15);
    import_state.fields["u10m"] = MakeField("u10m", nx, ny, 1, 5.0);
    import_state.fields["v10m"] = MakeField("v10m", nx, ny, 1, 5.0);
    import_state.fields["ustar"] = MakeField("ustar", nx, ny, 1, 0.35);
}

void AddOutputs(CeceExportState& export_state, int nx, int ny, int ns) {
    for (int n = 0; n < ns; ++n) {
        char bin[8];
        std::snprintf(bin, sizeof(bin), "SS%03d", n + 1);
        export_state.fields[std::string("seasalt_mass_") + bin] = MakeField(std::string("seasalt_mass_") + bin, nx, ny, 1, -1.0);
        export_state.fields[std::string("seasalt_number_") + bin] = MakeField(std::string("seasalt_number_") + bin, nx, ny, 1, -1.0);
    }
    export_state.fields["seasalt_mass_total"] = MakeField("seasalt_mass_total", nx, ny, 1, -1.0);
    export_state.fields["seasalt_number_total"] = MakeField("seasalt_number_total", nx, ny, 1, -1.0);
}

#ifdef CECE_HAS_FORTRAN
TEST(SeaSaltGeos12SchemeTest, FactoryCreatesScheme) {
    PhysicsSchemeConfig config;
    config.name = "sea_salt_geos12_fortran";
    auto scheme = PhysicsFactory::CreateScheme(config);
    EXPECT_NE(scheme, nullptr);
}
#endif

TEST(SeaSaltGeos12SchemeTest, ValidConfigurationInitializes) {
    conf::Config cfg = MakeConfig(WriteMechanismFile("valid"), WriteMapFile("valid"));
    SeaSaltGeos12FortranScheme scheme;
    EXPECT_NO_THROW(scheme.Initialize(cfg.root(), nullptr));
}

TEST(SeaSaltGeos12SchemeTest, MissingMechanismFileFails) {
    conf::Config cfg = MakeConfig("/nonexistent/path/spc.yaml", WriteMapFile("valid"));
    SeaSaltGeos12FortranScheme scheme;
    EXPECT_THROW(scheme.Initialize(cfg.root(), nullptr), std::runtime_error);
}

TEST(SeaSaltGeos12SchemeTest, MalformedMechanismFileFails) {
    const std::string path = (std::filesystem::temp_directory_path() / "cece_seasalt_geos12_bad_spc.yaml").string();
    std::ofstream(path) << "name: BAD\nspecies:\n  - name: SS001\n    molecular weight [kg mol-1]: 0.05844\n    is_aerosol: true\n"
                        << "    density [kg m-3]: 2200.0\n    lower_radius [um]: 0.5\n    upper_radius [um]: 0.1\n";  // lower >= upper
    conf::Config cfg = MakeConfig(path, WriteMapFile("valid"));
    SeaSaltGeos12FortranScheme scheme;
    EXPECT_THROW(scheme.Initialize(cfg.root(), nullptr), std::invalid_argument);
}

TEST(SeaSaltGeos12SchemeTest, OptionalEffectiveRadiusInitializes) {
    conf::Config cfg = MakeConfig(WriteMechanismFile("no_radius", /*with_radius=*/false), WriteMapFile("valid"));
    SeaSaltGeos12FortranScheme scheme;
    EXPECT_NO_THROW(scheme.Initialize(cfg.root(), nullptr));
}

#ifdef CECE_HAS_FORTRAN
TEST(SeaSaltGeos12SchemeTest, MissingImportFieldsLeaveOutputsUnchanged) {
    CeceImportState import_state;
    CeceExportState export_state;
    // Provide outputs but omit all imports so Run returns early.
    AddOutputs(export_state, 1, 1, 5);

    SeaSaltGeos12FortranScheme scheme;
    conf::Config cfg = MakeConfig(WriteMechanismFile("valid"), WriteMapFile("valid"));
    scheme.Initialize(cfg.root(), nullptr);
    EXPECT_NO_THROW(scheme.Run(import_state, export_state));

    auto& mass = export_state.fields.at("seasalt_mass_SS001");
    mass.sync_host();
    EXPECT_DOUBLE_EQ(mass.view_host()(0, 0, 0), -1.0);
}

TEST(SeaSaltGeos12SchemeTest, ProducesNonNegativeEmissionsOverOcean) {
    CeceImportState import_state;
    CeceExportState export_state;
    AddOceanImports(import_state, 1, 1);
    AddOutputs(export_state, 1, 1, 5);

    SeaSaltGeos12FortranScheme scheme;
    conf::Config cfg = MakeConfig(WriteMechanismFile("valid"), WriteMapFile("valid"));
    scheme.Initialize(cfg.root(), nullptr);
    scheme.Run(import_state, export_state);

    // The -1.0 sentinel is overwritten with a physical (non-negative) flux.
    auto& mass = export_state.fields.at("seasalt_mass_SS002");
    auto& total = export_state.fields.at("seasalt_mass_total");
    mass.sync_host();
    total.sync_host();
    EXPECT_GE(mass.view_host()(0, 0, 0), 0.0);
    EXPECT_GE(total.view_host()(0, 0, 0), mass.view_host()(0, 0, 0));
}
#endif

}  // namespace
}  // namespace cece

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (!Kokkos::is_initialized()) {
        Kokkos::initialize(argc, argv);
    }
    const int result = RUN_ALL_TESTS();
    if (Kokkos::is_initialized()) {
        Kokkos::finalize();
    }
    return result;
}
