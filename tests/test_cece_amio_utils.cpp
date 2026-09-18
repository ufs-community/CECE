/**
 * @file test_cece_amio_utils.cpp
 * @brief Tests for widening AMIO view payloads to double.
 *
 * The view's declared element type -- not its byte size -- decides how the
 * payload is read, and CF packing attributes are applied on the way to double.
 * read_cf_packing() needs a live dataset handle, so it is covered by
 * test_cece_time_axis_netcdf.cpp instead.
 */

#include <amio/amio.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "cece/cece_amio_utils.hpp"

namespace cece {

TEST(CeceViewWidening, DtypeSizes) {
    using namespace cece::detail;

    EXPECT_EQ(amio_dtype_size(AMIO_DTYPE_I8), 1u);
    EXPECT_EQ(amio_dtype_size(AMIO_DTYPE_U8), 1u);
    EXPECT_EQ(amio_dtype_size(AMIO_DTYPE_I16), 2u);
    EXPECT_EQ(amio_dtype_size(AMIO_DTYPE_U16), 2u);
    EXPECT_EQ(amio_dtype_size(AMIO_DTYPE_I32), 4u);
    EXPECT_EQ(amio_dtype_size(AMIO_DTYPE_U32), 4u);
    EXPECT_EQ(amio_dtype_size(AMIO_DTYPE_F32), 4u);
    EXPECT_EQ(amio_dtype_size(AMIO_DTYPE_I64), 8u);
    EXPECT_EQ(amio_dtype_size(AMIO_DTYPE_U64), 8u);
    EXPECT_EQ(amio_dtype_size(AMIO_DTYPE_F64), 8u);

    // An unknown tag is reported as unhandled rather than guessed.
    EXPECT_EQ(amio_dtype_size(static_cast<amio_dtype_t>(999)), 0u);
}

TEST(CeceViewWidening, WidensEveryNumericType) {
    using namespace cece::detail;

    std::vector<double> out;

    const float f32[] = {0.5f, -1.5f};
    ASSERT_TRUE(widen_amio_elements(f32, AMIO_DTYPE_F32, 2, 1.0, 0.0, out));
    EXPECT_EQ(out, (std::vector<double>{0.5, -1.5}));

    const double f64[] = {0.25, -2.75};
    ASSERT_TRUE(widen_amio_elements(f64, AMIO_DTYPE_F64, 2, 1.0, 0.0, out));
    EXPECT_EQ(out, (std::vector<double>{0.25, -2.75}));

    const std::int8_t i8[] = {-128, 127};
    ASSERT_TRUE(widen_amio_elements(i8, AMIO_DTYPE_I8, 2, 1.0, 0.0, out));
    EXPECT_EQ(out, (std::vector<double>{-128.0, 127.0}));

    const std::int16_t i16[] = {-32768, 32767};
    ASSERT_TRUE(widen_amio_elements(i16, AMIO_DTYPE_I16, 2, 1.0, 0.0, out));
    EXPECT_EQ(out, (std::vector<double>{-32768.0, 32767.0}));

    const std::int32_t i32[] = {0, 6, 12, 18};
    ASSERT_TRUE(widen_amio_elements(i32, AMIO_DTYPE_I32, 4, 1.0, 0.0, out));
    EXPECT_EQ(out, (std::vector<double>{0.0, 6.0, 12.0, 18.0}));

    const std::int64_t i64[] = {1, std::int64_t{1} << 40};
    ASSERT_TRUE(widen_amio_elements(i64, AMIO_DTYPE_I64, 2, 1.0, 0.0, out));
    EXPECT_EQ(out, (std::vector<double>{1.0, 1099511627776.0}));

    const std::uint8_t u8[] = {0, 255};
    ASSERT_TRUE(widen_amio_elements(u8, AMIO_DTYPE_U8, 2, 1.0, 0.0, out));
    EXPECT_EQ(out, (std::vector<double>{0.0, 255.0}));

    const std::uint16_t u16[] = {0, 65535};
    ASSERT_TRUE(widen_amio_elements(u16, AMIO_DTYPE_U16, 2, 1.0, 0.0, out));
    EXPECT_EQ(out, (std::vector<double>{0.0, 65535.0}));

    const std::uint32_t u32[] = {0u, 4294967295u};
    ASSERT_TRUE(widen_amio_elements(u32, AMIO_DTYPE_U32, 2, 1.0, 0.0, out));
    EXPECT_EQ(out, (std::vector<double>{0.0, 4294967295.0}));

    const std::uint64_t u64[] = {0u, std::uint64_t{1} << 40};
    ASSERT_TRUE(widen_amio_elements(u64, AMIO_DTYPE_U64, 2, 1.0, 0.0, out));
    EXPECT_EQ(out, (std::vector<double>{0.0, 1099511627776.0}));

    // Unsupported tag and null payload are rejected, not guessed.
    EXPECT_FALSE(widen_amio_elements(i32, static_cast<amio_dtype_t>(999), 4, 1.0, 0.0, out));
    EXPECT_FALSE(widen_amio_elements(nullptr, AMIO_DTYPE_F64, 4, 1.0, 0.0, out));
}

TEST(CeceViewWidening, AppliesCfPacking) {
    using namespace cece::detail;

    // CF packing: unpacked = stored * scale_factor + add_offset. This is how
    // int16-packed emission inventories store their values.
    const std::int16_t packed[] = {0, 100, 1000, -500};
    std::vector<double> out;
    ASSERT_TRUE(widen_amio_elements(packed, AMIO_DTYPE_I16, 4, 0.001, 5.0, out));
    EXPECT_NEAR(out[0], 5.0, 1e-12);
    EXPECT_NEAR(out[1], 5.1, 1e-12);
    EXPECT_NEAR(out[2], 6.0, 1e-12);
    EXPECT_NEAR(out[3], 4.5, 1e-12);

    // The identity transform leaves float data untouched.
    const float raw[] = {1.5f, 2.5f};
    ASSERT_TRUE(widen_amio_elements(raw, AMIO_DTYPE_F32, 2, 1.0, 0.0, out));
    EXPECT_EQ(out, (std::vector<double>{1.5, 2.5}));
}

}  // namespace cece
