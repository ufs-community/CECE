#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "cece/cece_physics_factory.hpp"
#include "cece/physics/cece_briggs.hpp"
#include "cece/physics/cece_plume_rise.hpp"

namespace cece {
namespace {

constexpr std::array<const char*, 7> profile_names = {"ZF", "ZH", "PRES", "TA", "QV", "UW", "VW"};

class BriggsTest : public ::testing::Test {
   protected:
    YAML::Node config;
    BriggsScheme scheme;
    CeceImportState imports;
    CeceExportState exports;
    int nx, ny, nz;

    DualView3D field(const std::string& name, int levels, double value = 0.0) {
        DualView3D result(name, nx, ny, levels);
        Kokkos::deep_copy(result.view_host(), value);
        result.modify_host();
        result.sync_device();
        return result;
    }

    void SetUp() override {
        config = YAML::LoadFile(std::string(CECE_SOURCE_DIR) + "/examples/cece_config_briggs.yaml");
        const auto test = config["physics_schemes"][0]["options"]["column_test"];
        nx = test["nx"].as<int>();
        ny = test["ny"].as<int>();
        nz = test["nz"].as<int>();
        auto options = config["physics_schemes"][0]["options"];
        scheme.Initialize(conf::Value::from_raw(&options), nullptr);
        for (const auto* name : profile_names) imports.fields[name] = field(name, nz);
        std::ifstream input(std::string(CECE_SOURCE_DIR) + "/" + test["profile"].as<std::string>());
        if (!input) throw std::runtime_error("Cannot open Briggs reference profile");
        std::string line;
        std::getline(input, line);
        for (int k = 0; k < nz; ++k) {
            if (!std::getline(input, line)) throw std::runtime_error("Truncated Briggs reference profile");
            std::replace(line.begin(), line.end(), ',', ' ');
            std::istringstream row(line);
            int layer = -1;
            if (!(row >> layer) || layer != k) throw std::runtime_error("Unexpected Briggs reference layer");
            for (const auto* name : profile_names) {
                double value;
                if (!(row >> value)) throw std::runtime_error("Invalid Briggs reference profile");
                auto host = imports.fields.at(name).view_host();
                for (int j = 0; j < ny; ++j)
                    for (int i = 0; i < nx; ++i) host(i, j, k) = value;
            }
        }
        for (const auto* name : profile_names) {
            imports.fields.at(name).modify_host();
            imports.fields.at(name).sync_device();
        }
        for (const auto& entry : test["surface_inputs"]) {
            const auto name = entry.first.as<std::string>();
            imports.fields[name] = field(name, 1, entry.second.as<double>());
        }
        exports.fields["plume_height"] = field("plume_height", 1, -1.0);
        exports.fields["plume_fraction"] = field("plume_fraction", nz, -1.0);
    }

    void set(const std::string& name, double value) {
        auto& v = imports.fields.at(name);
        Kokkos::deep_copy(v.view_host(), value);
        v.modify_host();
        v.sync_device();
    }

    void synthetic() {
        for (int k = 0; k < nz; ++k) {
            for (int j = 0; j < ny; ++j) {
                for (int i = 0; i < nx; ++i) {
                    imports.fields.at("ZF").view_host()(i, j, k) = 100.0 * (k + 1);
                    imports.fields.at("ZH").view_host()(i, j, k) = 100.0 * k + 50.0;
                }
            }
        }
        for (const auto* name : {"ZF", "ZH"}) {
            imports.fields.at(name).modify_host();
            imports.fields.at(name).sync_device();
        }
        set("PRES", 1000.0);
        set("TA", 300.0);
        set("QV", 0.0);
        set("UW", 10.0);
        set("VW", 0.0);
    }

    void check(double expected_height, const std::vector<std::pair<int, double>>& nonzero, double tolerance = 1.0e-9) {
        exports.fields.at("plume_height").sync_host();
        exports.fields.at("plume_fraction").sync_host();
        auto heights = exports.fields.at("plume_height").view_host();
        auto fractions = exports.fields.at("plume_fraction").view_host();
        std::vector<double> expected(nz, 0.0);
        for (const auto& [k, value] : nonzero) expected.at(k) = value;
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                EXPECT_NEAR(heights(i, j, 0), expected_height, tolerance);
                double total = 0.0;
                for (int k = 0; k < nz; ++k) {
                    EXPECT_NEAR(fractions(i, j, k), expected[k], tolerance) << "Layer " << k;
                    EXPECT_GE(fractions(i, j, k), 0.0);
                    EXPECT_LE(fractions(i, j, k), 1.0);
                    total += fractions(i, j, k);
                }
                EXPECT_NEAR(total, 1.0, 1.0e-12);
            }
        }
    }

    void check_valid() {
        exports.fields.at("plume_height").sync_host();
        exports.fields.at("plume_fraction").sync_host();
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                const double height = exports.fields.at("plume_height").view_host()(i, j, 0);
                EXPECT_TRUE(std::isfinite(height));
                EXPECT_GE(height, imports.fields.at("stack_height").view_host()(i, j, 0));
                double sum = 0.0;
                for (int k = 0; k < nz; ++k) {
                    const double fraction = exports.fields.at("plume_fraction").view_host()(i, j, k);
                    EXPECT_TRUE(std::isfinite(fraction));
                    EXPECT_GE(fraction, 0.0);
                    EXPECT_LE(fraction, 1.0);
                    sum += fraction;
                }
                EXPECT_NEAR(sum, 1.0, 1.0e-12);
            }
        }
    }
};

TEST_F(BriggsTest, ExampleYamlAndEveryReferenceLayer) {
    PhysicsSchemeConfig factory_config;
    factory_config.name = config["physics_schemes"][0]["name"].as<std::string>();
    auto factory_scheme = PhysicsFactory::CreateScheme(factory_config);
    ASSERT_NE(factory_scheme, nullptr);
    auto options = config["physics_schemes"][0]["options"];
    factory_scheme->Initialize(conf::Value::from_raw(&options), nullptr);
    factory_scheme->Run(imports, exports);
    check(219.41083364358906, {{4, 0.25398545808009954}, {5, 0.74601454191990046}});
    // Default-real (32-bit) upstream output allows for its roundoff.
    check(219.410492, {{4, 0.253992230}, {5, 0.746007740}}, 5.0e-4);
}

TEST_F(BriggsTest, LegacyBriggsFactoryNameRemainsAvailable) {
    PhysicsSchemeConfig factory_config;
    factory_config.name = "briggs";
    auto factory_scheme = PhysicsFactory::CreateScheme(factory_config);
    ASSERT_NE(factory_scheme, nullptr);
    auto options = config["physics_schemes"][0]["options"];
    factory_scheme->Initialize(conf::Value::from_raw(&options), nullptr);
    factory_scheme->Run(imports, exports);
    check(219.41083364358906, {{4, 0.25398545808009954}, {5, 0.74601454191990046}});
}

TEST_F(BriggsTest, OmittedMethodDefaultsToBriggs) {
    auto options = config["physics_schemes"][0]["options"];
    options.remove("method");
    PlumeRiseScheme scheme;
    scheme.Initialize(conf::Value::from_raw(&options), nullptr);
    scheme.Run(imports, exports);
    check(219.41083364358906, {{4, 0.25398545808009954}, {5, 0.74601454191990046}});
}

TEST_F(BriggsTest, PlannedAndUnknownMethodsFailExplicitly) {
    auto options = config["physics_schemes"][0]["options"];
    for (const auto* method : {"sofiev", "freitas", "grell_freitas", "not_a_method"}) {
        options["method"] = method;
        PlumeRiseScheme scheme;
        try {
            scheme.Initialize(conf::Value::from_raw(&options), nullptr);
            FAIL() << "Expected unsupported method to be rejected: " << method;
        } catch (const std::invalid_argument& error) {
            EXPECT_NE(std::string(error.what()).find(method), std::string::npos);
            EXPECT_NE(std::string(error.what()).find("briggs"), std::string::npos);
        }
    }
    EXPECT_EQ(PlumeRiseMethodFactory::AvailableMethods(), std::vector<std::string>{"briggs"});
    EXPECT_THROW(PlumeRiseMethodFactory::RegisterMethod("briggs", [] { return std::make_unique<BriggsScheme>(); }), std::invalid_argument);
}

TEST_F(BriggsTest, StableAndZeroHeatFluxReference) {
    for (double heat : {-25.0, 0.0}) {
        set("sensible_heat_flux", heat);
        scheme.Run(imports, exports);
        check(200.43017601095414, {{4, 0.99052741383673237}, {5, 0.009472586163267627}});
    }
}

TEST_F(BriggsTest, AboveMixingHeightReference) {
    set("mixing_height", 100.0);
    scheme.Run(imports, exports);
    check(204.60262911853181, {{4, 0.72703321967046664}, {5, 0.27296678032953331}});
}

TEST_F(BriggsTest, ColdStackMomentumOnlyHasFractions) {
    set("stack_temperature", 280.0);
    scheme.Run(imports, exports);
    check(376.23359809337614, {{6, 0.35388356116997655}, {7, 0.25087300791626288}, {8, 0.39524343091376046}});
}

TEST_F(BriggsTest, CalmWindReference) {
    set("UW", 0.0);
    set("VW", 0.0);
    scheme.Run(imports, exports);
    check(228.74170673992165, {{4, 0.1066228456759927}, {5, 0.89337715432400722}});
}

TEST_F(BriggsTest, WeakPositiveBuoyancyUsesMomentumBound) {
    synthetic();
    set("stack_temperature", 300.01);
    scheme.Run(imports, exports);
    const double center = 181.0 + 3.0 * 7.0 * 27.0 / 10.0;
    check(center, {{2, 1.0}});
}

TEST_F(BriggsTest, MultilayerNeutralReference) {
    synthetic();
    set("sensible_heat_flux", 0.0);
    scheme.Run(imports, exports);
    check(477.32459292859915, {{3, 0.23905441946483702}, {4, 0.33746777144513102}, {5, 0.33746777144513102}, {6, 0.086010037644900977}});
}

TEST_F(BriggsTest, MultilayerUnstableReference) {
    synthetic();
    scheme.Run(imports, exports);
    check(474.44001884101391, {{3, 0.24631947225526329}, {4, 0.34078514714852198}, {5, 0.34078514714852198}, {6, 0.072110233447692809}});
}

TEST_F(BriggsTest, MultilayerStableReference) {
    synthetic();
    set("sensible_heat_flux", -25.0);
    scheme.Run(imports, exports);
    check(464.96859893176463, {{3, 0.27121203127330257}, {4, 0.35215161245356275}, {5, 0.35215161245356275}, {6, 0.024484743819571855}});
}

TEST_F(BriggsTest, RepeatedRunsOverwriteOutputs) {
    scheme.Run(imports, exports);
    scheme.Run(imports, exports);
    check(219.41083364358906, {{4, 0.25398545808009954}, {5, 0.74601454191990046}});
}

TEST_F(BriggsTest, IndependentColumnsAndFullDepthSurfaceFields) {
    imports.fields["sensible_heat_flux"] = field("heat", nz, 25.0);
    imports.fields.at("sensible_heat_flux").view_host()(1, 1, 0) = -25.0;
    imports.fields.at("sensible_heat_flux").modify_host();
    imports.fields.at("sensible_heat_flux").sync_device();
    exports.fields["plume_height"] = field("height", nz);
    scheme.ClearPhysicsCache();
    scheme.Run(imports, exports);
    exports.fields.at("plume_height").sync_host();
    for (int k = 0; k < nz; ++k) {
        EXPECT_NEAR(exports.fields.at("plume_height").view_host()(0, 0, k), 219.41083364358906, 1.0e-9);
        EXPECT_NEAR(exports.fields.at("plume_height").view_host()(1, 1, k), 200.43017601095414, 1.0e-9);
    }
    check_valid();
}

TEST_F(BriggsTest, ZeroFrictionVelocityAndNoExhaust) {
    set("friction_velocity", 0.0);
    scheme.Run(imports, exports);
    check_valid();
    set("stack_velocity", 0.0);
    scheme.Run(imports, exports);
    check(183.0, {{4, 1.0}});
}

TEST_F(BriggsTest, NearestCenterTieSelectsLowerLayer) {
    synthetic();
    set("stack_height", 200.0);
    set("stack_temperature", 280.0);
    auto wind = imports.fields.at("UW").view_host();
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) wind(i, j, 2) = 20.0;
    imports.fields.at("UW").modify_host();
    imports.fields.at("UW").sync_device();
    scheme.Run(imports, exports);
    const double rise = 56.7;
    check(200.0 + rise, {{2, 1.0}});
}

TEST_F(BriggsTest, LowHotStackRetainsStandaloneAdjustment) {
    synthetic();
    set("stack_height", 10.0);
    set("stack_diameter", 1.0);
    set("stack_velocity", 1.0);
    set("sensible_heat_flux", 0.0);
    scheme.Run(imports, exports);
    check(110.0, {{0, 0.4}, {1, 0.6}});
}

TEST_F(BriggsTest, CrossingDomainTopConservesOverflow) {
    synthetic();
    set("stack_height", 3290.0);
    set("stack_velocity", 100.0);
    set("stack_temperature", 280.0);
    scheme.Run(imports, exports);
    check(3500.0, {{33, 5.0 / 210.0}, {34, 205.0 / 210.0}});
}

TEST_F(BriggsTest, PlumeAboveDomainGoesToTopLayer) {
    set("stack_height", 20000.0);
    set("stack_temperature", 180.0);
    scheme.Run(imports, exports);
    check_valid();
    exports.fields.at("plume_fraction").sync_host();
    EXPECT_DOUBLE_EQ(exports.fields.at("plume_fraction").view_host()(0, 0, nz - 1), 1.0);
    set("stack_temperature", 348.0);
    set("sensible_heat_flux", -25.0);
    scheme.Run(imports, exports);
    check(20000.0, {{nz - 1, 1.0}});
    set("sensible_heat_flux", 25.0);
    scheme.Run(imports, exports);
    check_valid();
    EXPECT_DOUBLE_EQ(exports.fields.at("plume_fraction").view_host()(0, 0, nz - 1), 1.0);
}

TEST_F(BriggsTest, RiseReachesTopAndTwoLayerMinimum) {
    set("sensible_heat_flux", -25.0);
    set("stack_diameter", 10000.0);
    scheme.Run(imports, exports);
    check_valid();
    EXPECT_LE(exports.fields.at("plume_height").view_host()(0, 0, 0), imports.fields.at("ZF").view_host()(0, 0, nz - 1));
    for (const auto* name : profile_names) {
        auto replacement = field(name, 2);
        auto old = imports.fields.at(name).view_host();
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i)
                for (int k = 0; k < 2; ++k) replacement.view_host()(i, j, k) = old(i, j, k);
        replacement.modify_host();
        replacement.sync_device();
        imports.fields[name] = replacement;
    }
    nz = 2;
    exports.fields["plume_fraction"] = field("fractions", nz);
    scheme.ClearPhysicsCache();
    scheme.Run(imports, exports);
    check_valid();
}

TEST_F(BriggsTest, MissingFieldsAndShapeErrorsAreExplicit) {
    auto saved = imports.fields.at("TA");
    imports.fields.erase("TA");
    EXPECT_THROW(scheme.Run(imports, exports), std::runtime_error);
    imports.fields["TA"] = saved;
    exports.fields.erase("plume_height");
    scheme.ClearPhysicsCache();
    EXPECT_THROW(scheme.Run(imports, exports), std::runtime_error);
    exports.fields["plume_height"] = field("height", 1);
    imports.fields["TA"] = field("bad_temperature", 1);
    scheme.ClearPhysicsCache();
    EXPECT_THROW(scheme.Run(imports, exports), std::invalid_argument);
    imports.fields["TA"] = saved;
    exports.fields["plume_fraction"] = field("bad_fractions", 1);
    scheme.ClearPhysicsCache();
    EXPECT_THROW(scheme.Run(imports, exports), std::invalid_argument);
}

TEST_F(BriggsTest, AliasedOutputsAndOneLayerProfilesRejected) {
    auto options = config["physics_schemes"][0]["options"];
    options["output_mapping"]["plume_centerline_height"] = "plume_fraction";
    scheme.Initialize(conf::Value::from_raw(&options), nullptr);
    EXPECT_THROW(scheme.Run(imports, exports), std::invalid_argument);
    SetUp();
    exports.fields["plume_height"] = imports.fields.at("TA");
    EXPECT_THROW(scheme.Run(imports, exports), std::invalid_argument);
    SetUp();
    for (const auto* name : profile_names) imports.fields[name] = field(name, 1);
    scheme.ClearPhysicsCache();
    EXPECT_THROW(scheme.Run(imports, exports), std::invalid_argument);
}

TEST_F(BriggsTest, NonfiniteInputsRejectedBeforeOutputMutation) {
    for (auto& [name, value] : imports.fields) {
        SCOPED_TRACE(name);
        auto host = value.view_host();
        const double first = host(0, 0, 0);
        host(0, 0, 0) = std::numeric_limits<double>::quiet_NaN();
        value.modify_host();
        value.sync_device();
        EXPECT_THROW(scheme.Run(imports, exports), std::invalid_argument);
        host(0, 0, 0) = first;
        value.modify_host();
        value.sync_device();
    }
    exports.fields.at("plume_height").sync_host();
    EXPECT_DOUBLE_EQ(exports.fields.at("plume_height").view_host()(0, 0, 0), -1.0);
}

TEST_F(BriggsTest, InvalidPhysicalInputsRejected) {
    for (const auto* name : {"PRES", "TA", "QV", "mixing_height", "friction_velocity", "surface_temperature", "surface_pressure", "stack_diameter",
                             "stack_height", "stack_temperature", "stack_velocity"}) {
        SCOPED_TRACE(name);
        set(name, -1.0);
        EXPECT_THROW(scheme.Run(imports, exports), std::invalid_argument);
        // Restore the full fixture rather than only its first level.
        SetUp();
    }
    set("ZF", 1.0);
    EXPECT_THROW(scheme.Run(imports, exports), std::invalid_argument);
}

TEST_F(BriggsTest, OverflowingInputArithmeticIsRejected) {
    for (const auto* name : {"UW", "stack_diameter", "sensible_heat_flux"}) {
        SCOPED_TRACE(name);
        set(name, std::numeric_limits<double>::max());
        EXPECT_THROW(scheme.Run(imports, exports), std::invalid_argument);
        SetUp();
    }
}

}  // namespace
}  // namespace cece
