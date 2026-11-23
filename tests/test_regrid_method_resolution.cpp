#include <gtest/gtest.h>

#include "cece/cece_regridder_utils.hpp"

TEST(RegridMethodResolutionTest, ResolvesAliasesToCanonicalMethods) {
    using cece::io::RegridMethod;
    EXPECT_EQ(cece::io::resolve_regrid_method("nearest").method, RegridMethod::Nearest);
    EXPECT_EQ(cece::io::resolve_regrid_method("near").method, RegridMethod::Nearest);
    EXPECT_EQ(cece::io::resolve_regrid_method("NN").method, RegridMethod::Nearest);
    EXPECT_EQ(cece::io::resolve_regrid_method("bilinear").method, RegridMethod::Bilinear);
    EXPECT_EQ(cece::io::resolve_regrid_method("bi").method, RegridMethod::Bilinear);
    EXPECT_EQ(cece::io::resolve_regrid_method("bilin").method, RegridMethod::Bilinear);
    EXPECT_EQ(cece::io::resolve_regrid_method("cubic").method, RegridMethod::Bicubic);
    EXPECT_EQ(cece::io::resolve_regrid_method("bicubic").method, RegridMethod::Bicubic);
    EXPECT_EQ(cece::io::resolve_regrid_method("cu").method, RegridMethod::Bicubic);
    EXPECT_EQ(cece::io::resolve_regrid_method("consd").method, RegridMethod::Conservative1);
    EXPECT_EQ(cece::io::resolve_regrid_method("conservative").method, RegridMethod::Conservative1);
    EXPECT_EQ(cece::io::resolve_regrid_method("cons").method, RegridMethod::Conservative1);
    EXPECT_EQ(cece::io::resolve_regrid_method("conservative1st").method, RegridMethod::Conservative1);
    EXPECT_EQ(cece::io::resolve_regrid_method("cons2nd").method, RegridMethod::Conservative2);
    EXPECT_EQ(cece::io::resolve_regrid_method("conservative2nd").method, RegridMethod::Conservative2);
    EXPECT_EQ(cece::io::resolve_regrid_method("passthrough").method, RegridMethod::Passthrough);
}

TEST(RegridMethodResolutionTest, RejectsUnimplementedConservativeAliases) {
    try {
        (void)cece::io::resolve_regrid_method("consf");
        FAIL() << "expected consf to be rejected";
    } catch (const cece::io::RegridError& error) {
        EXPECT_EQ(error.stage(), cece::io::RegridStage::PrerequisiteValidation);
        EXPECT_EQ(error.role(), cece::io::GridRole::None);
        EXPECT_NE(std::string(error.what()).find("consf is not implemented"), std::string::npos);
        EXPECT_NE(std::string(error.what()).find("Use consd"), std::string::npos);
    }
    try {
        (void)cece::io::resolve_regrid_method("redist");
        FAIL() << "expected redist to be rejected";
    } catch (const cece::io::RegridError& error) {
        EXPECT_EQ(error.stage(), cece::io::RegridStage::PrerequisiteValidation);
        EXPECT_NE(std::string(error.what()).find("redist is not supported"), std::string::npos);
    }
}

TEST(RegridMethodResolutionTest, RejectsUnknownMethodsWithStageContext) {
    try {
        (void)cece::io::resolve_regrid_method("not-a-method");
        FAIL() << "expected unknown method to fail";
    } catch (const cece::io::RegridError& error) {
        EXPECT_EQ(error.stage(), cece::io::RegridStage::PrerequisiteValidation);
        EXPECT_EQ(error.method(), "not-a-method");
        EXPECT_NE(std::string(error.what()).find("Use nearest"), std::string::npos);
    }
}

// Prerequisite validation, plan-key composition, and geometry preparation
// behaviour are covered by test_regrid_prerequisites.cpp,
// test_geometry_plan_lifecycle.cpp, and test_grid_geometry.cpp respectively;
// this file tests only alias resolution and unknown-method diagnostics.
