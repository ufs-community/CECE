/**
 * @file test_seasalt_geos12_runtime.cpp
 * @brief Runtime acceptance tests for the GEOS-12 sea salt Fortran bridge.
 *
 * Exercises SeaSaltGeos12FortranScheme end-to-end: physical invariants
 * (land/ice masking, wind and density dependence, non-negativity), diagnostic
 * self-consistency (totals equal the per-bin sum, diagnostics mirror exports),
 * and a frozen golden regression generated from the production kernel.
 */

#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <cmath>
#include <conf/config.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "cece/cece_diagnostics.hpp"
#include "cece/cece_state.hpp"
#include "cece/physics/cece_seasalt_geos12_fortran.hpp"

#ifdef CECE_HAS_FORTRAN

namespace cece {
namespace {

struct Met {
    double frocean = 1.0;
    double frseaice = 0.0;
    double lat = 0.0;
    double lon = 0.0;
    double sst = 293.15;
    double u10m = 6.0;
    double v10m = 0.0;
    double ustar = 0.4;
};

class SeaSaltGeos12RuntimeTest : public ::testing::Test {
   protected:
    static constexpr int kNs = 5;

    static const char* BinName(int n) {
        static const char* kNames[kNs] = {"SS001", "SS002", "SS003", "SS004", "SS005"};
        return kNames[n];
    }
    static std::string MassField(int n) {
        return std::string("seasalt_mass_") + BinName(n);
    }
    static std::string NumberField(int n) {
        return std::string("seasalt_number_") + BinName(n);
    }

    std::vector<double> density_{2200.0, 2200.0, 2200.0, 2200.0, 2200.0};
    std::vector<double> r_low_{0.03, 0.1, 0.5, 1.5, 5.0};
    std::vector<double> r_up_{0.1, 0.5, 1.5, 5.0, 10.0};
    std::vector<double> r_eff_{0.079, 0.316, 0.898, 2.83, 7.5};

    // Serializes the current member properties into a temp MICM mechanism file
    // so that per-test mutations (e.g. density) take effect on reload.
    std::string WriteMechanismFile() const {
        const std::string path = (std::filesystem::temp_directory_path() / "cece_seasalt_geos12_runtime_spc.yaml").string();
        std::ofstream f(path);
        f << "name: TEST_GOCART\nspecies:\n";
        for (int n = 0; n < kNs; ++n) {
            f << "  - name: " << BinName(n) << "\n";
            f << "    molecular weight [kg mol-1]: 0.05844\n";
            f << "    is_aerosol: true\n";
            f << "    density [kg m-3]: " << density_[n] << "\n";
            f << "    lower_radius [um]: " << r_low_[n] << "\n";
            f << "    upper_radius [um]: " << r_up_[n] << "\n";
            f << "    effective_radius [um]: " << r_eff_[n] << "\n";
        }
        return path;
    }

    // Writes the identity aerosol map (nested bin -> {bin: 1.0}) for dataset SEASALT.
    std::string WriteMapFile() const {
        const std::string path = (std::filesystem::temp_directory_path() / "cece_seasalt_geos12_runtime_map.yaml").string();
        std::ofstream f(path);
        f << "mechanism: TEST_GOCART\ndatasets:\n  SEASALT:\n";
        for (int n = 0; n < kNs; ++n) f << "    " << BinName(n) << ":\n      " << BinName(n) << ": 1.0\n";
        return path;
    }

    conf::Config MakeConfig(bool weibull = false, double scale = 1.0) {
        std::ostringstream y;
        y << "weibull_flag: " << (weibull ? "true" : "false") << "\n"
          << "scale_factor: " << scale << "\n"
          << "mechanism_file: \"" << WriteMechanismFile() << "\"\n"
          << "speciation_file: \"" << WriteMapFile() << "\"\n"
          << "speciation_dataset: \"SEASALT\"\n";
        return conf::Config::from_string(y.str());
    }

    static DualView3D MakeField(const std::string& name, int nx, int ny, int levels, double value) {
        DualView3D field(name, nx, ny, levels);
        Kokkos::deep_copy(field.view_host(), value);
        field.modify_host();
        field.sync_device();
        return field;
    }

    void Run(conf::Config config, const Met& met, CeceExportState& export_state, int nx = 1, int ny = 1, CeceDiagnosticManager* diag = nullptr) {
        CeceImportState import_state;
        import_state.fields["frocean"] = MakeField("frocean", nx, ny, 1, met.frocean);
        import_state.fields["frseaice"] = MakeField("frseaice", nx, ny, 1, met.frseaice);
        import_state.fields["lat"] = MakeField("lat", nx, ny, 1, met.lat);
        import_state.fields["lon"] = MakeField("lon", nx, ny, 1, met.lon);
        import_state.fields["sst"] = MakeField("sst", nx, ny, 1, met.sst);
        import_state.fields["u10m"] = MakeField("u10m", nx, ny, 1, met.u10m);
        import_state.fields["v10m"] = MakeField("v10m", nx, ny, 1, met.v10m);
        import_state.fields["ustar"] = MakeField("ustar", nx, ny, 1, met.ustar);

        export_state.fields["seasalt_mass_total"] = MakeField("seasalt_mass_total", nx, ny, 1, 0.0);
        export_state.fields["seasalt_number_total"] = MakeField("seasalt_number_total", nx, ny, 1, 0.0);
        for (int n = 0; n < kNs; ++n) {
            export_state.fields[MassField(n)] = MakeField(MassField(n), nx, ny, 1, 0.0);
            export_state.fields[NumberField(n)] = MakeField(NumberField(n), nx, ny, 1, 0.0);
        }

        SeaSaltGeos12FortranScheme scheme;
        scheme.Initialize(config.root(), diag);
        scheme.Run(import_state, export_state);
    }

    static double MassAt(CeceExportState& s, int i, int j, int n) {
        auto& f = s.fields.at(MassField(n));
        f.sync_host();
        return f.view_host()(i, j, 0);
    }
    static double NumberAt(CeceExportState& s, int i, int j, int n) {
        auto& f = s.fields.at(NumberField(n));
        f.sync_host();
        return f.view_host()(i, j, 0);
    }
    static double TotalMass(CeceExportState& s, int i, int j) {
        double sum = 0.0;
        for (int n = 0; n < kNs; ++n) sum += MassAt(s, i, j, n);
        return sum;
    }
};

TEST_F(SeaSaltGeos12RuntimeTest, LandCellsProduceZeroEmissions) {
    CeceExportState out;
    Met land;
    land.frocean = 0.0;  // no open ocean
    Run(MakeConfig(), land, out);
    for (int n = 0; n < kNs; ++n) {
        EXPECT_DOUBLE_EQ(MassAt(out, 0, 0, n), 0.0);
        EXPECT_DOUBLE_EQ(NumberAt(out, 0, 0, n), 0.0);
    }
}

TEST_F(SeaSaltGeos12RuntimeTest, FullyIceCoveredOceanProducesZero) {
    CeceExportState out;
    Met ice;
    ice.frocean = 1.0;
    ice.frseaice = 1.0;  // frocean - frseaice = 0
    Run(MakeConfig(), ice, out);
    EXPECT_DOUBLE_EQ(TotalMass(out, 0, 0), 0.0);
}

TEST_F(SeaSaltGeos12RuntimeTest, ZeroFrictionVelocityProducesZero) {
    CeceExportState out;
    Met calm;
    calm.ustar = 0.0;  // emission wind function is ustar^wpow
    Run(MakeConfig(), calm, out);
    EXPECT_DOUBLE_EQ(TotalMass(out, 0, 0), 0.0);
}

TEST_F(SeaSaltGeos12RuntimeTest, EmissionsAreNonNegativeAndFinite) {
    CeceExportState out;
    Run(MakeConfig(), Met{}, out);
    for (int n = 0; n < kNs; ++n) {
        EXPECT_TRUE(std::isfinite(MassAt(out, 0, 0, n)));
        EXPECT_GE(MassAt(out, 0, 0, n), 0.0);
        EXPECT_GE(NumberAt(out, 0, 0, n), 0.0);
    }
}

TEST_F(SeaSaltGeos12RuntimeTest, EmissionsIncreaseWithFrictionVelocity) {
    CeceExportState low_out;
    CeceExportState high_out;
    Met low_wind;
    low_wind.ustar = 0.2;
    Met high_wind;
    high_wind.ustar = 0.6;
    Run(MakeConfig(), low_wind, low_out);
    Run(MakeConfig(), high_wind, high_out);
    EXPECT_GT(TotalMass(high_out, 0, 0), TotalMass(low_out, 0, 0));
}

TEST_F(SeaSaltGeos12RuntimeTest, MassScalesWithDensityButNumberDoesNot) {
    CeceExportState base_out;
    CeceExportState dense_out;
    Run(MakeConfig(), Met{}, base_out);

    density_ = std::vector<double>(kNs, 4400.0);  // double the density
    Run(MakeConfig(), Met{}, dense_out);

    // Mass roughly doubles (density is a linear prefactor per bin); number unchanged.
    EXPECT_NEAR(MassAt(dense_out, 0, 0, 2), 2.0 * MassAt(base_out, 0, 0, 2), 1.0e-6 * MassAt(base_out, 0, 0, 2));
    EXPECT_DOUBLE_EQ(NumberAt(dense_out, 0, 0, 2), NumberAt(base_out, 0, 0, 2));
}

TEST_F(SeaSaltGeos12RuntimeTest, ScaleFactorScalesEmissionsLinearly) {
    CeceExportState base_out;
    CeceExportState scaled_out;
    Run(MakeConfig(false, 1.0), Met{}, base_out);
    Run(MakeConfig(false, 2.5), Met{}, scaled_out);
    EXPECT_NEAR(TotalMass(scaled_out, 0, 0), 2.5 * TotalMass(base_out, 0, 0), 1.0e-6 * TotalMass(base_out, 0, 0));
}

TEST_F(SeaSaltGeos12RuntimeTest, DeepLakesMaskZeroesGreatLakesAndCaspian) {
    CeceExportState great_lakes;
    Met gl;
    gl.lat = 45.0;
    gl.lon = -83.0;  // 277 E, inside Great Lakes box
    Run(MakeConfig(), gl, great_lakes);
    EXPECT_DOUBLE_EQ(TotalMass(great_lakes, 0, 0), 0.0);

    CeceExportState caspian;
    Met cs;
    cs.lat = 42.0;
    cs.lon = 50.0;  // inside Caspian box
    Run(MakeConfig(), cs, caspian);
    EXPECT_DOUBLE_EQ(TotalMass(caspian, 0, 0), 0.0);
}

TEST_F(SeaSaltGeos12RuntimeTest, ExportTotalsEqualPerBinSum) {
    CeceExportState out;
    Run(MakeConfig(), Met{}, out);

    auto& mass_total = out.fields.at("seasalt_mass_total");
    auto& number_total = out.fields.at("seasalt_number_total");
    mass_total.sync_host();
    number_total.sync_host();

    double mass_sum = 0.0;
    double number_sum = 0.0;
    for (int n = 0; n < kNs; ++n) {
        mass_sum += MassAt(out, 0, 0, n);
        number_sum += NumberAt(out, 0, 0, n);
    }
    EXPECT_NEAR(mass_total.view_host()(0, 0, 0), mass_sum, 1.0e-30);
    EXPECT_NEAR(number_total.view_host()(0, 0, 0), number_sum, 1.0e-6 * number_sum);
}

TEST_F(SeaSaltGeos12RuntimeTest, EffectiveRadiusDiagnosticMatchesConfig) {
    CeceDiagnosticManager diag;
    CeceExportState out;
    Run(MakeConfig(), Met{}, out, 1, 1, &diag);

    auto radius_diag = diag.RegisterDiagnostic("seasalt_effective_radius", 1, 1, kNs);
    radius_diag.sync_host();
    for (int n = 0; n < kNs; ++n) {
        EXPECT_DOUBLE_EQ(radius_diag.view_host()(0, 0, n), r_eff_[static_cast<std::size_t>(n)]);
    }
}

TEST_F(SeaSaltGeos12RuntimeTest, GoldenRegressionForFixedInputs) {
    // Golden values generated from the production kernel (frozen).
    // Inputs: frocean=1, frseaice=0, lat=0, lon=0, sst=293.15 K,
    //         u10m=6, v10m=0, ustar=0.4, weibull_flag=false, scale_factor=1,
    //         density=2200, r_low={0.03,0.1,0.5,1.5,5.0}, r_up={0.1,0.5,1.5,5.0,10.0}.
    constexpr double expected_mass[kNs] = {
        4.31242974767013038e-13, 1.41183235907150790e-11, 1.50261183103161854e-10, 6.39504266703238592e-10, 4.80169457726905072e-10,
    };
    constexpr double expected_number[kNs] = {
        1.15161676310736540e+05, 1.44104528244776244e+05, 1.71000764156424193e+04, 5.49025825839366462e+03, 1.48800106978016743e+02,
    };

    CeceExportState out;
    Run(MakeConfig(), Met{}, out);

    for (int n = 0; n < kNs; ++n) {
        SCOPED_TRACE(::testing::Message() << "bin=" << n);
        EXPECT_NEAR(MassAt(out, 0, 0, n), expected_mass[n], 1.0e-9 * std::abs(expected_mass[n]));
        EXPECT_NEAR(NumberAt(out, 0, 0, n), expected_number[n], 1.0e-9 * std::abs(expected_number[n]));
    }
}

}  // namespace
}  // namespace cece

#endif  // CECE_HAS_FORTRAN

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
