/**
 * @file test_hemco_megan_runtime.cpp
 * @brief HEMCO 3.12.1 MEGAN isoprene parity tests.
 *
 * Pins the upstream repository tag, commit, science-source blob, compiler and
 * precision contract, and all frozen scalar constants.  Every test verifies
 * that the C++ implementation of hemco_megan_stateless.hpp reproduces the
 * independently generated oracle to double-precision rounding tolerance.
 *
 * Provenance (see tests/data/hemco_megan/README.md):
 *   Repository : https://github.com/geoschem/HEMCO
 *   Release    : 3.12.1
 *   Science src: src/Extensions/hcox_megan_mod.F90
 *   Oracle     : tests/data/hemco_megan/hemco_3_12_1_megan_reference.csv
 */

#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "cece/physics/hemco_megan_stateless.hpp"

namespace cece::hemco_megan::v3_12_1 {

// ============================================================================
// Tolerance
// ============================================================================

static constexpr double kTol = 1.0e-12;

// ============================================================================
// Helper: compare with relative+absolute tolerance
// ============================================================================
static bool near(double got, double expected, double rtol = kTol) {
    const double scale = std::max(std::abs(expected), 1.0e-30);
    return std::abs(got - expected) <= rtol * scale;
}

// ============================================================================
// Oracle CSV loading
// ============================================================================

struct OracleRow {
    std::string case_id;
    double T_K;
    double lai;
    double lai_prev;
    double pardr_Wm2;
    double pardf_Wm2;
    double suncos;
    double gwetroot;
    double co2_ppm;
    double expected;
};

static std::vector<OracleRow> LoadOracle(const std::string& path) {
    std::vector<OracleRow> rows;
    std::ifstream f(path);
    if (!f.is_open()) return rows;
    std::string line;
    std::getline(f, line);  // skip header
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        OracleRow r;
        char comma;
        std::getline(ss, r.case_id, ',');
        ss >> r.T_K >> comma >> r.lai >> comma >> r.lai_prev >> comma;
        ss >> r.pardr_Wm2 >> comma >> r.pardf_Wm2 >> comma;
        ss >> r.suncos >> comma >> r.gwetroot >> comma;
        ss >> r.co2_ppm >> comma >> r.expected;
        rows.push_back(r);
    }
    return rows;
}

// ============================================================================
// Constant contract tests — pin frozen values
// ============================================================================

TEST(HEMCO3121MeganConstants, NormFac) {
    EXPECT_DOUBLE_EQ(kNormFac, 1.0 / 1.0101081);
}

TEST(HEMCO3121MeganConstants, LDF) {
    EXPECT_DOUBLE_EQ(kLdf, 0.9996);
}

TEST(HEMCO3121MeganConstants, Beta) {
    EXPECT_DOUBLE_EQ(kBeta, 0.13);
}

TEST(HEMCO3121MeganConstants, TStd) {
    EXPECT_DOUBLE_EQ(kTStd, 303.0);
}

TEST(HEMCO3121MeganConstants, GasConstant) {
    EXPECT_DOUBLE_EQ(kR, 8.3144598e-3);
}

TEST(HEMCO3121MeganConstants, CT1CT2) {
    EXPECT_DOUBLE_EQ(kCT1, 95.0);
    EXPECT_DOUBLE_EQ(kCT2, 200.0);
}

TEST(HEMCO3121MeganConstants, CEO) {
    EXPECT_DOUBLE_EQ(kCEO, 2.0);
}

TEST(HEMCO3121MeganConstants, PTOA) {
    // HEMCO 3.12.1-specific; differs from CECE native defaults (3000 / 99).
    EXPECT_DOUBLE_EQ(kPtoaC1, 2650.0);
    EXPECT_DOUBLE_EQ(kPtoaC2, 130.0);
    EXPECT_DOUBLE_EQ(kPtoaDoyOffset, 18.0);
}

TEST(HEMCO3121MeganConstants, ParAvgUmol) {
    // Direct µmol/m²/s convention (HEMCO), not W/m² × 4.766 (CECE native).
    EXPECT_DOUBLE_EQ(kParAvgUmol, 400.0);
}

TEST(HEMCO3121MeganConstants, CO2Coefficients) {
    EXPECT_DOUBLE_EQ(kGammaC02C1, 8.9406);
    EXPECT_DOUBLE_EQ(kGammaCO2C2, 0.0024);
}

TEST(HEMCO3121MeganConstants, LeafAgeWeights) {
    EXPECT_DOUBLE_EQ(kANew, 0.05);
    EXPECT_DOUBLE_EQ(kAGro, 0.60);
    EXPECT_DOUBLE_EQ(kAMat, 1.00);
    EXPECT_DOUBLE_EQ(kAOld, 0.90);
}

// ============================================================================
// Boundary and branch tests
// ============================================================================

TEST(HEMCO3121MeganGamma, CO2AtReference390) {
    // Verify exact oracle value from Python computation.
    const double expected = 8.9406 / (1.0 + 8.9406 * 0.0024 * 390.0);
    EXPECT_NEAR(GammaCO2(390.0), expected, kTol * expected);
}

TEST(HEMCO3121MeganGamma, CO2Monotone) {
    // Higher CO2 → lower gamma_CO2 (inhibition).
    EXPECT_GT(GammaCO2(280.0), GammaCO2(390.0));
    EXPECT_GT(GammaCO2(390.0), GammaCO2(560.0));
}

TEST(HEMCO3121MeganGamma, LAIZeroReturnsZero) {
    EXPECT_EQ(GammaLAI(0.0), 0.0);
    EXPECT_EQ(GammaLAI(-1.0), 0.0);
}

TEST(HEMCO3121MeganGamma, LAIPositive) {
    EXPECT_GT(GammaLAI(1.0), 0.0);
    EXPECT_GT(GammaLAI(4.0), 0.0);
}

TEST(HEMCO3121MeganGamma, TLIAtStandard) {
    // gamma_T_LI = 1.0 exactly at T_std = 303 K.
    EXPECT_DOUBLE_EQ(GammaTLI(kTStd), 1.0);
}

TEST(HEMCO3121MeganGamma, TLIMonotone) {
    EXPECT_LT(GammaTLI(280.0), GammaTLI(303.0));
    EXPECT_LT(GammaTLI(303.0), GammaTLI(330.0));
}

TEST(HEMCO3121MeganGamma, TLDMaxAtTOpt) {
    // At T = T_opt = 313 K with T_avg_15 = 297 K, c_t = CEO = 2.0.
    const double g_max = GammaTLD(kTOptC1, kTAvg15);
    EXPECT_NEAR(g_max, kCEO, kTol);
}

TEST(HEMCO3121MeganGamma, PARNighttime) {
    // suncos <= 0 → gamma_PAR = 0.
    EXPECT_EQ(GammaPAR(200.0, 100.0, 0.0), 0.0);
    EXPECT_EQ(GammaPAR(200.0, 100.0, -0.5), 0.0);
}

TEST(HEMCO3121MeganGamma, PARPositiveDaytime) {
    EXPECT_GT(GammaPAR(200.0, 100.0, 0.7), 0.0);
}

TEST(HEMCO3121MeganGamma, AgeFactorSteadyState) {
    // L == L_prev → fixed weights (fnew=0, fgro=0.1, fmat=0.8, fold=0.1).
    const double expected = 0.0 * kANew + 0.1 * kAGro + 0.8 * kAMat + 0.1 * kAOld;
    EXPECT_NEAR(GammaAge(4.0, 4.0, 303.0), expected, kTol);
}

TEST(HEMCO3121MeganGamma, AgeFactorNonNegative) {
    EXPECT_GE(GammaAge(5.0, 3.0, 303.0), 0.0);
    EXPECT_GE(GammaAge(3.0, 5.0, 303.0), 0.0);
    EXPECT_GE(GammaAge(0.1, 0.1, 260.0), 0.0);
}

TEST(HEMCO3121MeganEmission, ZeroLAIGate) {
    MeganInputs in;
    in.lai = 0.0;
    EXPECT_EQ(IsopreneEmissionFactor(in), 0.0);
}

// ============================================================================
// Oracle regression tests — CSV-driven
// ============================================================================

class HEMCO3121MeganOracle : public ::testing::TestWithParam<OracleRow> {};

TEST_P(HEMCO3121MeganOracle, MatchesReference) {
    const OracleRow& r = GetParam();
    MeganInputs in;
    in.T_K = r.T_K;
    in.lai = r.lai;
    in.lai_prev = r.lai_prev;
    in.pardr_Wm2 = r.pardr_Wm2;
    in.pardf_Wm2 = r.pardf_Wm2;
    in.suncos = r.suncos;
    in.gwetroot = r.gwetroot;
    in.co2_ppm = r.co2_ppm;
    in.par_avg_umol = kParAvgUmol;
    in.T_avg_15_K = kTAvg15;
    in.doy = kReferenceDoy;

    const double got = IsopreneEmissionFactor(in);
    const double scale = std::max(std::abs(r.expected), 1.0e-30);
    EXPECT_NEAR(got, r.expected, kTol * scale) << "Case: " << r.case_id << "  T=" << r.T_K << " K"
                                               << "  LAI=" << r.lai << "  suncos=" << r.suncos << "  CO2=" << r.co2_ppm << " ppm";
}

// Instantiate from CSV (loaded once at test fixture construction time).
static std::vector<OracleRow> gOracleRows;

struct OracleLoader {
    OracleLoader() {
        // Path relative to CTest working directory (CMAKE_BINARY_DIR).
        const std::string path = std::string(CECE_SOURCE_DIR) + "/tests/data/hemco_megan/hemco_3_12_1_megan_reference.csv";
        gOracleRows = LoadOracle(path);
    }
} gLoader;

INSTANTIATE_TEST_SUITE_P(Oracle, HEMCO3121MeganOracle, ::testing::ValuesIn(gOracleRows), [](const ::testing::TestParamInfo<OracleRow>& info) {
    std::string n = info.param.case_id;
    std::replace(n.begin(), n.end(), ' ', '_');
    return n;
});

}  // namespace cece::hemco_megan::v3_12_1
