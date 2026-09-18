// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Feature 001 — T015 [US1] + T020/T021 [US2]: end-to-end local-time temporal
// scaling through the StackingEngine.
//
// Independent test (spec US1): one layer with an identity diurnal profile
// (factor[i] = i), enabled local time, one step at 06:00 UTC across cells with
// offsets {-8, 0, +5.5, +12} hours. Each cell's applied scale must equal
// D[(6 + offset) mod 24] => {22, 6, 11, 18}. With the feature disabled (or in
// UTC-fallback mode) every cell must equal D[6] — bit-identical to the scalar
// UTC path (SC-001, FR-008).

#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cece/cece_config.hpp"
#include "cece/cece_local_time.hpp"
#include "cece/cece_stacking_engine.hpp"

namespace cece {
namespace {

constexpr std::int64_t kHour = 3600;
// 2026-01-15T06:00:00Z — a Thursday (dow=4), month=0 (Jan), doy=14.
constexpr std::int64_t kStart0600 = 1768456800;

/// FieldResolver over real Kokkos DualViews with per-cell host access.
class GridResolver : public FieldResolver {
    std::map<std::string, DualView3D> fields_;

   public:
    void AddField(const std::string& name, int nx, int ny, int nz, double fill = 0.0) {
        fields_.try_emplace(name, "test_" + name, nx, ny, nz);
        if (fill != 0.0) SetAll(name, fill);
    }
    void SetAll(const std::string& name, double val) {
        auto h = fields_[name].view_host();
        Kokkos::deep_copy(h, val);
        fields_[name].modify_host();
        fields_[name].sync_device();
    }
    double At(const std::string& name, int i, int j, int k) {
        fields_[name].sync_host();
        return fields_[name].view_host()(i, j, k);
    }

    UnmanagedHostView3D ResolveImport(const std::string& n, int, int, int) override {
        return fields_[n].view_host();
    }
    UnmanagedHostView3D ResolveExport(const std::string& n, int, int, int) override {
        return fields_[n].view_host();
    }
    Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> ResolveImportDevice(const std::string& n, int, int,
                                                                                                         int) override {
        return fields_[n].view_device();
    }
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> ResolveExportDevice(const std::string& n, int, int, int) override {
        return fields_[n].view_device();
    }
};

/// Longitude-indexed stub provider: offset chosen by matching the cell's
/// longitude against a small table (the US1 {-8, 0, +5.5, +12} probe set).
/// Implements the same IUtcOffsetProvider seam as the real grid provider, so
/// the engine cannot tell them apart (US4).
class TableOffsetProvider final : public IUtcOffsetProvider {
   public:
    TableOffsetProvider(std::vector<double> lons, std::vector<std::int32_t> offsets_secs)
        : lons_(std::move(lons)), offsets_(std::move(offsets_secs)) {}
    std::int32_t offsetSecondsAt(double lat, double lon, std::int64_t) const override {
        (void)lat;
        for (size_t i = 0; i < lons_.size(); ++i) {
            if (std::fabs(lons_[i] - lon) < 1e-6) return offsets_[i];
        }
        return 0;
    }

   private:
    std::vector<double> lons_;
    std::vector<std::int32_t> offsets_;
};

class LocalTimeStackingTest : public ::testing::Test {
   protected:
    void SetUp() override {
        if (!Kokkos::is_initialized()) Kokkos::initialize();
    }

    // Identity profiles: factor[h] = h (diurnal), factor[d] = d (weekly), ...
    static CeceConfig MakeConfig(const std::string& cycle_name, int n_factors) {
        CeceConfig config;
        TemporalCycle cyc;
        cyc.factors.resize(static_cast<size_t>(n_factors));
        for (int i = 0; i < n_factors; ++i) cyc.factors[i] = static_cast<double>(i);
        config.temporal_profiles[cycle_name] = cyc;

        EmissionLayer layer;
        layer.operation = "add";
        layer.field_name = "f1";
        layer.scale = 1.0;
        layer.use_local_time = true;
        if (n_factors == 24) layer.diurnal_cycle = cycle_name;
        if (n_factors == 7) layer.weekly_cycle = cycle_name;
        if (n_factors == 12) layer.seasonal_cycle = cycle_name;
        config.species_layers["SP"] = {layer};
        return config;
    }

    // 4x1x1 grid: longitudes NY / London / Delhi / date-line-ish.
    static std::unique_ptr<LocalTimeService> MakeService(std::int64_t start_epoch) {
        std::vector<double> lons = {-74.0, 0.0, 77.2, 139.7};
        std::vector<std::int32_t> offs = {-8 * kHour, 0, 5 * kHour + 1800, 12 * kHour};
        return LocalTimeService::Create(std::make_unique<TableOffsetProvider>(lons, offs), lons, {40.0}, 4, 0, 1, start_epoch);
    }

    static void SetupFields(GridResolver& r, int nx, double field_value) {
        r.AddField("f1", nx, 1, 1, field_value);
        r.AddField("SP", nx, 1, 1, 0.0);
    }
};

// ---------------------------------------------------------------------------
// US1 — diurnal at local time
// ---------------------------------------------------------------------------

TEST_F(LocalTimeStackingTest, DiurnalUsesLocalHourPerCell) {
    auto config = MakeConfig("hour_id", 24);
    GridResolver resolver;
    SetupFields(resolver, 4, 10.0);

    auto svc = MakeService(kStart0600);
    StackingEngine engine(config);
    // 06:00 UTC step (elapsed 0). Local hours: -8->22, 0->6, +5.5->11, +12->18.
    engine.Execute(resolver, 4, 1, 1, {}, 6, 4, 0, nullptr, svc.get(), 0);

    const double expect[4] = {220.0, 60.0, 110.0, 180.0};  // 10 * identity[local hour]
    for (int i = 0; i < 4; ++i) {
        EXPECT_NEAR(resolver.At("SP", i, 0, 0), expect[i], 1e-9) << "cell " << i;
    }
}

TEST_F(LocalTimeStackingTest, DisabledIsBitIdenticalToUtcScalar) {
    auto config = MakeConfig("hour_id", 24);

    GridResolver with_local;
    SetupFields(with_local, 4, 10.0);
    auto svc = MakeService(kStart0600);
    StackingEngine engine_local(config);
    engine_local.Execute(with_local, 4, 1, 1, {}, 6, 4, 0, nullptr, nullptr, 0);

    GridResolver baseline;
    SetupFields(baseline, 4, 10.0);
    // Layer flagged but no service => scalar UTC path (FR-008 default UTC).
    StackingEngine engine_base(config);
    engine_base.Execute(baseline, 4, 1, 1, {}, 6, 4, 0, nullptr, nullptr, 0);

    for (int i = 0; i < 4; ++i) {
        EXPECT_DOUBLE_EQ(with_local.At("SP", i, 0, 0), baseline.At("SP", i, 0, 0));
        EXPECT_NEAR(with_local.At("SP", i, 0, 0), 60.0, 1e-12);  // D[6] everywhere
    }
}

TEST_F(LocalTimeStackingTest, UtcFallbackServiceMatchesScalarPath) {
    auto config = MakeConfig("hour_id", 24);
    GridResolver r;
    SetupFields(r, 4, 10.0);
    auto fallback = LocalTimeService::CreateUtcFallback(4, 1);
    StackingEngine engine(config);
    engine.Execute(r, 4, 1, 1, {}, 6, 4, 0, nullptr, fallback.get(), 0);
    for (int i = 0; i < 4; ++i) {
        EXPECT_NEAR(r.At("SP", i, 0, 0), 60.0, 1e-12);  // UTC-fallback => D[6]
    }
}

TEST_F(LocalTimeStackingTest, ElapsedSecondsAdvanceLocalHour) {
    auto config = MakeConfig("hour_id", 24);
    GridResolver r;
    SetupFields(r, 4, 10.0);
    auto svc = MakeService(kStart0600);
    StackingEngine engine(config);
    // +1 h => 07:00Z: local hours {23, 7, 12, 19}.
    engine.Execute(r, 4, 1, 1, {}, 7, 4, 0, nullptr, svc.get(), 3600);
    const double expect[4] = {230.0, 70.0, 120.0, 190.0};
    for (int i = 0; i < 4; ++i) EXPECT_NEAR(r.At("SP", i, 0, 0), expect[i], 1e-9) << "cell " << i;
}

// ---------------------------------------------------------------------------
// US2 — weekly / seasonal at local time with date rollover
// ---------------------------------------------------------------------------

TEST_F(LocalTimeStackingTest, WeeklyRollsOverWithLocalDate) {
    auto config = MakeConfig("dow_id", 7);
    GridResolver r;
    SetupFields(r, 4, 10.0);
    auto svc = MakeService(kStart0600);
    StackingEngine engine(config);
    // Thu 18:00Z (elapsed 12h). dow: -8h->Thu 10:00 (4); 0->Thu 18:00 (4);
    // +5.5h->Thu 23:30 (4); +12h->Fri 06:00 (5).
    engine.Execute(r, 4, 1, 1, {}, 18, 4, 0, nullptr, svc.get(), 12 * kHour);
    const double expect[4] = {40.0, 40.0, 40.0, 50.0};
    for (int i = 0; i < 4; ++i) EXPECT_NEAR(r.At("SP", i, 0, 0), expect[i], 1e-9) << "cell " << i;
}

TEST_F(LocalTimeStackingTest, SeasonalUsesLocalMonthWithRollover) {
    auto config = MakeConfig("month_id", 12);
    GridResolver r;
    SetupFields(r, 4, 10.0);
    // Provider with a -4 h cell so 02:00Z on Mar 01 rolls back to Feb 22:00.
    std::vector<double> lons = {-74.0, 0.0, 77.2, 139.7};
    std::vector<std::int32_t> offs = {-4 * kHour, 0, 5 * kHour + 1800, 12 * kHour};
    const std::int64_t mar01_02z = 1772330400;  // 2026-03-01T02:00:00Z
    auto svc = LocalTimeService::Create(std::make_unique<TableOffsetProvider>(lons, offs), lons, {40.0}, 4, 0, 1, mar01_02z);
    StackingEngine engine(config);
    // UTC month = 2 (Mar). Local months: -4h->1 (Feb); 0->2; +5.5->2; +12->2.
    engine.Execute(r, 4, 1, 1, {}, 2, 0, 2, nullptr, svc.get(), 0);
    const double expect[4] = {10.0, 20.0, 20.0, 20.0};
    for (int i = 0; i < 4; ++i) EXPECT_NEAR(r.At("SP", i, 0, 0), expect[i], 1e-9) << "cell " << i;
}

TEST_F(LocalTimeStackingTest, CombinedCyclesMultiplyAtLocalTime) {
    // One layer naming diurnal + weekly: factor = local_hour * local_dow.
    CeceConfig config;
    TemporalCycle d, w;
    for (int i = 0; i < 24; ++i) d.factors.push_back(static_cast<double>(i));
    for (int i = 0; i < 7; ++i) w.factors.push_back(static_cast<double>(i) + 1.0);  // avoid a zero factor
    config.temporal_profiles["hour_id"] = d;
    config.temporal_profiles["dow_id"] = w;
    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "f1";
    layer.scale = 1.0;
    layer.diurnal_cycle = "hour_id";
    layer.weekly_cycle = "dow_id";
    layer.use_local_time = true;
    config.species_layers["SP"] = {layer};

    GridResolver r;
    SetupFields(r, 4, 10.0);
    auto svc = MakeService(kStart0600);
    StackingEngine engine(config);
    // 06:00Z Thu(4). Local hours {22,6,11,18}; local dow: the -8h cell rolls
    // back to Wed(3), the others stay Thu(4) (the +12h cell is 18:00 Thu).
    engine.Execute(r, 4, 1, 1, {}, 6, 4, 0, nullptr, svc.get(), 0);
    const double expect[4] = {10 * 22 * 4, 10 * 6 * 5, 10 * 11 * 5, 10 * 18 * 5};  // dow factor = dow+1
    for (int i = 0; i < 4; ++i) EXPECT_NEAR(r.At("SP", i, 0, 0), expect[i], 1e-9) << "cell " << i;
}

TEST_F(LocalTimeStackingTest, BaseScaleStillApplies) {
    auto config = MakeConfig("hour_id", 24);
    config.species_layers["SP"][0].scale = 2.5;
    GridResolver r;
    SetupFields(r, 4, 10.0);
    auto svc = MakeService(kStart0600);
    StackingEngine engine(config);
    engine.Execute(r, 4, 1, 1, {}, 6, 4, 0, nullptr, svc.get(), 0);
    const double expect[4] = {2.5 * 220.0, 2.5 * 60.0, 2.5 * 110.0, 2.5 * 180.0};
    for (int i = 0; i < 4; ++i) EXPECT_NEAR(r.At("SP", i, 0, 0), expect[i], 1e-9) << "cell " << i;
}

// ---------------------------------------------------------------------------
// FR-008 spirit — never silently drop the local-time factor (loud slot check)
// ---------------------------------------------------------------------------

TEST_F(LocalTimeStackingTest, ScaleSlotExhaustionThrowsNotSilentDrop) {
    auto config = MakeConfig("hour_id", 24);
    auto& layer = config.species_layers["SP"][0];
    for (int s = 0; s < 16; ++s) {  // DeviceLayer::MAX_SCALES
        layer.scale_fields.push_back("sf" + std::to_string(s));
    }
    GridResolver r;
    SetupFields(r, 4, 10.0);
    for (int s = 0; s < 16; ++s) r.AddField("sf" + std::to_string(s), 4, 1, 1, 1.0);
    auto svc = MakeService(kStart0600);
    StackingEngine engine(config);
    EXPECT_THROW(engine.Execute(r, 4, 1, 1, {}, 6, 4, 0, nullptr, svc.get(), 0), std::runtime_error);
}

}  // namespace
}  // namespace cece
