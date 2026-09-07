/**
 * @file test_hemco_megan_global.cpp
 * @brief Synthetic global invariants for the HEMCO 3.12.1 stateless source transcription.
 *
 * The tests evaluate analytically generated drivers on the 72x46 GEOS 4x5
 * coordinate layout. They exercise source-conformant scalar arithmetic and
 * spatial invariants without external files. They do not execute HEMCO and do
 * not constitute HEMCO-versus-CECE runtime parity evidence.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include "cece/physics/hemco_megan_stateless.hpp"

namespace {

using cece::hemco_megan::v3_12_1::IsopreneEmissionFactor;
using cece::hemco_megan::v3_12_1::MeganInputs;

constexpr int kNX = 72;
constexpr int kNY = 46;

double CellLon(int i) {
    return -180.0 + 5.0 * i;
}

double CellLat(int j) {
    if (j == 0) return -89.0;
    if (j == kNY - 1) return 89.0;
    return -86.0 + 4.0 * (j - 1);
}

int Index(int i, int j) {
    return j * kNX + i;
}

double SmoothBox(double lon, double lat, double lon1, double lon2, double lat1, double lat2, double edge = 3.0) {
    const auto sigmoid = [edge](double value, double lower, double upper) {
        return 0.5 * (std::tanh((value - lower) / edge) - std::tanh((value - upper) / edge));
    };
    return std::clamp(sigmoid(lon, lon1, lon2) * sigmoid(lat, lat1, lat2), 0.0, 1.0);
}

double SmoothEllipse(double lon, double lat, double center_lon, double center_lat, double radius_lon, double radius_lat, double edge = 4.0) {
    const double radius = std::sqrt(std::pow((lon - center_lon) / radius_lon, 2) + std::pow((lat - center_lat) / radius_lat, 2));
    return std::clamp(0.5 - 0.5 * std::tanh((radius - 1.0) * edge), 0.0, 1.0);
}

double LandFraction(double lon, double lat) {
    double fraction = 0.0;
    fraction += SmoothBox(lon, lat, -125, -60, 10, 72);
    fraction += SmoothBox(lon, lat, -90, -77, 7, 12);
    fraction += SmoothEllipse(lon, lat, -42, 72, 22, 12);
    fraction += SmoothEllipse(lon, lat, -58, -15, 25, 38);
    fraction += SmoothBox(lon, lat, -10, 40, 36, 72);
    fraction += SmoothEllipse(lon, lat, 20, 0, 32, 40);
    fraction += SmoothBox(lon, lat, 30, 50, 5, 37);
    fraction += SmoothBox(lon, lat, 25, 145, 0, 72);
    fraction += SmoothBox(lon, lat, 95, 145, -10, 10);
    fraction += SmoothEllipse(lon, lat, 134, -28, 22, 18);
    return std::clamp(fraction, 0.0, 1.0);
}

double BaseLai(double abs_lat) {
    if (abs_lat < 15.0) return 5.0;
    if (abs_lat < 35.0) return 3.5;
    if (abs_lat < 55.0) return 2.5;
    if (abs_lat < 70.0) return 1.5;
    return 0.0;
}

double EffectiveAef(double abs_lat) {
    if (abs_lat < 15.0) return 3.0e-9;
    if (abs_lat < 25.0) return 2.0e-9;
    if (abs_lat < 45.0) return 1.5e-9;
    if (abs_lat < 60.0) return 0.8e-9;
    return 0.3e-9;
}

struct GlobalFields {
    std::vector<double> land;
    std::vector<double> aef;
    std::vector<double> temperature;
    std::vector<double> lai;
    std::vector<double> pardr;
    std::vector<double> pardf;
    std::vector<double> suncos;

    GlobalFields() : land(kNX * kNY), aef(kNX * kNY), temperature(kNX * kNY), lai(kNX * kNY), pardr(kNX * kNY), pardf(kNX * kNY), suncos(kNX * kNY) {
        std::uint32_t state = 42U;
        const auto noise = [&state]() {
            state = state * 1664525U + 1013904223U;
            return static_cast<double>(state) / 4294967296.0 - 0.5;
        };

        for (int j = 0; j < kNY; ++j) {
            const double lat = CellLat(j);
            const double abs_lat = std::abs(lat);
            const double solar = std::clamp(std::cos(std::numbers::pi / 180.0 * (lat - 23.0)) * 0.85, 0.0, 1.0);
            for (int i = 0; i < kNX; ++i) {
                const int k = Index(i, j);
                const double fraction = LandFraction(CellLon(i), lat);
                land[k] = fraction;
                lai[k] = std::clamp(BaseLai(abs_lat) * fraction, 0.0, 6.0);
                aef[k] = lai[k] > 0.0 ? EffectiveAef(abs_lat) * fraction : 0.0;
                temperature[k] = std::clamp(302.0 - 0.10 * abs_lat + (lat > 0.0 && fraction > 0.5 ? 2.0 : 0.0), 255.0, 312.0);
                suncos[k] = solar;
                pardr[k] = std::clamp(400.0 * solar + 20.0 * noise() * solar, 0.0, 700.0);
                pardf[k] = std::clamp(120.0 * solar + 10.0 * noise() * solar, 0.0, 350.0);
            }
        }
    }
};

double Emit(const GlobalFields& fields, int k, double co2 = 390.0, bool co2_inhibition = true) {
    MeganInputs input;
    input.T_K = fields.temperature[k];
    input.lai = fields.lai[k];
    input.lai_prev = fields.lai[k];
    input.pardr_Wm2 = fields.pardr[k];
    input.pardf_Wm2 = fields.pardf[k];
    input.suncos = fields.suncos[k];
    input.co2_ppm = co2;
    input.apply_co2_inhibition = co2_inhibition;
    return fields.aef[k] * IsopreneEmissionFactor(input);
}

class HemcoMeganGlobalTest : public ::testing::Test {
   protected:
    static const GlobalFields& Fields() {
        static const GlobalFields fields;
        return fields;
    }
};

TEST(HEMCO3121GlobalGrid, ExactFourByFiveCoordinateLayout) {
    EXPECT_DOUBLE_EQ(CellLon(0), -180.0);
    EXPECT_DOUBLE_EQ(CellLon(kNX - 1), 175.0);
    EXPECT_DOUBLE_EQ(CellLat(0), -89.0);
    EXPECT_DOUBLE_EQ(CellLat(1), -86.0);
    EXPECT_DOUBLE_EQ(CellLat(2), -82.0);
    EXPECT_DOUBLE_EQ(CellLat(kNY - 2), 86.0);
    EXPECT_DOUBLE_EQ(CellLat(kNY - 1), 89.0);
}

TEST_F(HemcoMeganGlobalTest, FiniteNonnegativeAndExactVegetationMask) {
    const auto& fields = Fields();
    int positive = 0;
    for (int k = 0; k < kNX * kNY; ++k) {
        const double emission = Emit(fields, k);
        ASSERT_TRUE(std::isfinite(emission)) << "cell=" << k;
        ASSERT_GE(emission, 0.0) << "cell=" << k;
        EXPECT_EQ(emission > 0.0, fields.lai[k] > 0.0 && fields.aef[k] > 0.0 && fields.suncos[k] > 0.0) << "cell=" << k;
        if (emission > 0.0) ++positive;
    }
    EXPECT_GT(positive, 100);
}

TEST_F(HemcoMeganGlobalTest, TropicalLandCellsEmit) {
    const auto& fields = Fields();
    int tested = 0;
    for (int j = 0; j < kNY; ++j) {
        if (std::abs(CellLat(j)) > 20.0) continue;
        for (int i = 0; i < kNX; ++i) {
            const int k = Index(i, j);
            if (fields.land[k] > 0.5 && fields.lai[k] > 0.5) {
                ++tested;
                EXPECT_GT(Emit(fields, k), 0.0) << "cell=" << k;
            }
        }
    }
    EXPECT_GT(tested, 50);
}

TEST_F(HemcoMeganGlobalTest, HigherCo2OnlyChangesExplicitlyEnabledCases) {
    const auto& fields = Fields();
    double enabled_390 = 0.0;
    double enabled_560 = 0.0;
    double disabled_390 = 0.0;
    double disabled_560 = 0.0;
    for (int k = 0; k < kNX * kNY; ++k) {
        enabled_390 += Emit(fields, k, 390.0, true);
        enabled_560 += Emit(fields, k, 560.0, true);
        disabled_390 += Emit(fields, k, 390.0, false);
        disabled_560 += Emit(fields, k, 560.0, false);
    }
    EXPECT_GT(enabled_390, enabled_560);
    EXPECT_DOUBLE_EQ(disabled_390, disabled_560);
}

TEST_F(HemcoMeganGlobalTest, HistoryAndDateInputsAreActive) {
    const auto& fields = Fields();
    const int k = Index(40, 25);
    ASSERT_GT(fields.lai[k], 0.0);

    MeganInputs cold_start;
    cold_start.T_K = fields.temperature[k];
    cold_start.lai = fields.lai[k];
    cold_start.lai_prev = fields.lai[k];
    cold_start.pardr_Wm2 = fields.pardr[k];
    cold_start.pardf_Wm2 = fields.pardf[k];
    cold_start.suncos = fields.suncos[k];

    MeganInputs changed = cold_start;
    changed.pardr_history_Wm2 = 200.0;
    changed.pardf_history_Wm2 = 200.0;
    EXPECT_NE(IsopreneEmissionFactor(cold_start), IsopreneEmissionFactor(changed));

    changed = cold_start;
    changed.temperature_history_K = 297.0;
    EXPECT_NE(IsopreneEmissionFactor(cold_start), IsopreneEmissionFactor(changed));

    changed = cold_start;
    changed.doy = 1;
    EXPECT_NE(IsopreneEmissionFactor(cold_start), IsopreneEmissionFactor(changed));
}

TEST_F(HemcoMeganGlobalTest, PreviousDayLaiAffectsGrowingAndSenescingCanopies) {
    const auto& fields = Fields();
    const int k = Index(40, 25);
    MeganInputs input;
    input.T_K = fields.temperature[k];
    input.lai = fields.lai[k];
    input.pardr_Wm2 = fields.pardr[k];
    input.pardf_Wm2 = fields.pardf[k];
    input.suncos = fields.suncos[k];

    input.lai_prev = input.lai;
    const double steady = IsopreneEmissionFactor(input);
    input.lai_prev = 0.5 * input.lai;
    const double growing = IsopreneEmissionFactor(input);
    input.lai_prev = 1.5 * input.lai;
    const double senescing = IsopreneEmissionFactor(input);

    EXPECT_NE(steady, growing);
    EXPECT_NE(steady, senescing);
}

}  // namespace
