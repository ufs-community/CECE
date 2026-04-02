#include <gtest/gtest.h>

#include <algorithm>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <vector>

// ESMF C API
extern "C" {
#include "ESMC.h"
}

// Forward declarations for Fortran functions
extern "C" {
void init_iso8601_utils_c_wrapper();
void parse_iso8601_to_esmf_time_c_wrapper(const char* iso_str, int* yy, int* mm, int* dd,
                                           int* hh, int* mn, int* ss, int* rc);
void format_esmf_time_to_iso8601_c_wrapper(int yy, int mm, int dd, int hh, int mn, int ss,
                                            char* iso_str, int* rc);
}

namespace aces {

/**
 * @brief Property-based test suite for ISO8601 parsing round trip.
 *
 * **Validates: Requirements 1.1, 2.1**
 *
 * Property 1: ISO8601 Parsing Round Trip
 *
 * FOR ALL valid ISO8601 datetime strings in format YYYY-MM-DDTHH:MM:SS,
 * parsing the string and then reconstructing it SHALL produce an equivalent
 * datetime representation.
 *
 * This property ensures that:
 * - The parser correctly extracts all datetime components
 * - The formatter correctly reconstructs the ISO8601 string
 * - No information is lost in the parse/format cycle
 * - The implementation is consistent and deterministic
 */
class ISO8601RoundTripPropertyTest : public ::testing::Test {
   protected:
    std::mt19937 rng{42};  // Deterministic seed for reproducibility

    void SetUp() override {
        // Initialize ESMF
        int rc = ESMC_Initialize(nullptr, ESMC_ArgLast);

        if (rc != ESMF_SUCCESS) {
            FAIL() << "Failed to initialize ESMF, rc=" << rc;
        }

        // Initialize ISO8601 utilities (creates the calendar)
        init_iso8601_utils_c_wrapper();
    }

    void TearDown() override {
        // Note: We don't finalize ESMF here because other tests may need it
        // ESMF_Finalize is typically called once at program exit
    }

    /**
     * @brief Generate a random valid ISO8601 datetime string.
     * @return ISO8601 string in format YYYY-MM-DDTHH:MM:SS
     */
    std::string GenerateRandomISO8601() {
        // Generate random date components within valid ranges
        std::uniform_int_distribution<int> year_dist(1900, 2100);
        std::uniform_int_distribution<int> month_dist(1, 12);
        std::uniform_int_distribution<int> hour_dist(0, 23);
        std::uniform_int_distribution<int> minute_dist(0, 59);
        std::uniform_int_distribution<int> second_dist(0, 59);

        int year = year_dist(rng);
        int month = month_dist(rng);
        int hour = hour_dist(rng);
        int minute = minute_dist(rng);
        int second = second_dist(rng);

        // Generate valid day based on month
        int max_day = GetMaxDayForMonth(year, month);
        std::uniform_int_distribution<int> day_dist(1, max_day);
        int day = day_dist(rng);

        // Format as ISO8601
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(4) << year << "-" << std::setw(2) << month << "-"
            << std::setw(2) << day << "T" << std::setw(2) << hour << ":" << std::setw(2)
            << minute << ":" << std::setw(2) << second;

        return oss.str();
    }

    /**
     * @brief Get maximum day for a given month and year (accounting for leap years).
     * @param year Year
     * @param month Month (1-12)
     * @return Maximum day for the month
     */
    int GetMaxDayForMonth(int year, int month) {
        static const int days_per_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

        if (month == 2 && IsLeapYear(year)) {
            return 29;
        }
        return days_per_month[month - 1];
    }

    /**
     * @brief Check if a year is a leap year.
     * @param year Year
     * @return True if leap year, false otherwise
     */
    bool IsLeapYear(int year) {
        return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    }

    /**
     * @brief Parse an ISO8601 string and format it back.
     * @param iso_str Input ISO8601 string
     * @param reconstructed Output reconstructed ISO8601 string
     * @return True if successful, false otherwise
     */
    bool ParseAndFormat(const std::string& iso_str, std::string& reconstructed) {
        int yy, mm, dd, hh, mn, ss, rc;

        // Parse ISO8601 string
        parse_iso8601_to_esmf_time_c_wrapper(iso_str.c_str(), &yy, &mm, &dd, &hh, &mn, &ss, &rc);

        if (rc != 0) {  // ESMF_SUCCESS is typically 0
            return false;
        }

        // Format back to ISO8601
        char formatted[256];
        format_esmf_time_to_iso8601_c_wrapper(yy, mm, dd, hh, mn, ss, formatted, &rc);

        if (rc != 0) {
            return false;
        }

        reconstructed = std::string(formatted);
        return true;
    }
};

/**
 * @brief Test that random valid ISO8601 strings round-trip correctly.
 *
 * This test generates random valid ISO8601 datetime strings, parses them,
 * and verifies that formatting them back produces the original string.
 */
TEST_F(ISO8601RoundTripPropertyTest, RandomDatesRoundTrip) {
    const int num_tests = 100;

    for (int i = 0; i < num_tests; ++i) {
        std::string original = GenerateRandomISO8601();
        std::string reconstructed;

        bool success = ParseAndFormat(original, reconstructed);

        EXPECT_TRUE(success) << "Failed to parse/format: " << original;

        if (success) {
            EXPECT_EQ(original, reconstructed)
                << "Round-trip mismatch:\n"
                << "  Original:      " << original << "\n"
                << "  Reconstructed: " << reconstructed;
        }
    }
}

/**
 * @brief Test that boundary dates round-trip correctly.
 *
 * This test verifies round-trip for important boundary dates.
 */
TEST_F(ISO8601RoundTripPropertyTest, BoundaryDatesRoundTrip) {
    std::vector<std::string> boundary_dates = {
        "1900-01-01T00:00:00",  // Start of 20th century
        "2000-01-01T00:00:00",  // Y2K
        "2000-02-29T12:00:00",  // Leap year Feb 29
        "2100-12-31T23:59:59",  // End of 21st century
        "2020-01-01T00:00:00",  // Default start time
        "2020-01-02T00:00:00",  // Default end time
        "1999-12-31T23:59:59",  // Y2K - 1 second
        "2004-02-29T00:00:00",  // Another leap year
    };

    for (const auto& original : boundary_dates) {
        std::string reconstructed;
        bool success = ParseAndFormat(original, reconstructed);

        EXPECT_TRUE(success) << "Failed to parse/format: " << original;

        if (success) {
            EXPECT_EQ(original, reconstructed)
                << "Round-trip mismatch for boundary date:\n"
                << "  Original:      " << original << "\n"
                << "  Reconstructed: " << reconstructed;
        }
    }
}

/**
 * @brief Test that midnight times round-trip correctly.
 *
 * This test verifies round-trip for midnight (00:00:00) times.
 */
TEST_F(ISO8601RoundTripPropertyTest, MidnightTimesRoundTrip) {
    const int num_tests = 20;

    for (int i = 0; i < num_tests; ++i) {
        std::uniform_int_distribution<int> year_dist(1900, 2100);
        std::uniform_int_distribution<int> month_dist(1, 12);

        int year = year_dist(rng);
        int month = month_dist(rng);
        int max_day = GetMaxDayForMonth(year, month);
        std::uniform_int_distribution<int> day_dist(1, max_day);
        int day = day_dist(rng);

        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(4) << year << "-" << std::setw(2) << month << "-"
            << std::setw(2) << day << "T00:00:00";

        std::string original = oss.str();
        std::string reconstructed;
        bool success = ParseAndFormat(original, reconstructed);

        EXPECT_TRUE(success) << "Failed to parse/format: " << original;

        if (success) {
            EXPECT_EQ(original, reconstructed)
                << "Round-trip mismatch for midnight time:\n"
                << "  Original:      " << original << "\n"
                << "  Reconstructed: " << reconstructed;
        }
    }
}

/**
 * @brief Test that end-of-day times round-trip correctly.
 *
 * This test verifies round-trip for end-of-day (23:59:59) times.
 */
TEST_F(ISO8601RoundTripPropertyTest, EndOfDayTimesRoundTrip) {
    const int num_tests = 20;

    for (int i = 0; i < num_tests; ++i) {
        std::uniform_int_distribution<int> year_dist(1900, 2100);
        std::uniform_int_distribution<int> month_dist(1, 12);

        int year = year_dist(rng);
        int month = month_dist(rng);
        int max_day = GetMaxDayForMonth(year, month);
        std::uniform_int_distribution<int> day_dist(1, max_day);
        int day = day_dist(rng);

        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(4) << year << "-" << std::setw(2) << month << "-"
            << std::setw(2) << day << "T23:59:59";

        std::string original = oss.str();
        std::string reconstructed;
        bool success = ParseAndFormat(original, reconstructed);

        EXPECT_TRUE(success) << "Failed to parse/format: " << original;

        if (success) {
            EXPECT_EQ(original, reconstructed)
                << "Round-trip mismatch for end-of-day time:\n"
                << "  Original:      " << original << "\n"
                << "  Reconstructed: " << reconstructed;
        }
    }
}

/**
 * @brief Test that leap year dates round-trip correctly.
 *
 * This test specifically verifies round-trip for February 29 in leap years.
 */
TEST_F(ISO8601RoundTripPropertyTest, LeapYearDatesRoundTrip) {
    std::vector<int> leap_years = {1904, 1908, 2000, 2004, 2008, 2012, 2016, 2020, 2024};

    for (int year : leap_years) {
        std::uniform_int_distribution<int> hour_dist(0, 23);
        std::uniform_int_distribution<int> minute_dist(0, 59);
        std::uniform_int_distribution<int> second_dist(0, 59);

        int hour = hour_dist(rng);
        int minute = minute_dist(rng);
        int second = second_dist(rng);

        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(4) << year << "-02-29T" << std::setw(2) << hour
            << ":" << std::setw(2) << minute << ":" << std::setw(2) << second;

        std::string original = oss.str();
        std::string reconstructed;
        bool success = ParseAndFormat(original, reconstructed);

        EXPECT_TRUE(success) << "Failed to parse/format leap year date: " << original;

        if (success) {
            EXPECT_EQ(original, reconstructed)
                << "Round-trip mismatch for leap year date:\n"
                << "  Original:      " << original << "\n"
                << "  Reconstructed: " << reconstructed;
        }
    }
}

/**
 * @brief Test that month boundary dates round-trip correctly.
 *
 * This test verifies round-trip for the first and last day of each month.
 */
TEST_F(ISO8601RoundTripPropertyTest, MonthBoundaryDatesRoundTrip) {
    int year = 2020;  // Use a leap year to test February 29

    for (int month = 1; month <= 12; ++month) {
        int max_day = GetMaxDayForMonth(year, month);

        // Test first day of month
        {
            std::ostringstream oss;
            oss << std::setfill('0') << std::setw(4) << year << "-" << std::setw(2) << month
                << "-01T12:00:00";

            std::string original = oss.str();
            std::string reconstructed;
            bool success = ParseAndFormat(original, reconstructed);

            EXPECT_TRUE(success) << "Failed to parse/format: " << original;
            if (success) {
                EXPECT_EQ(original, reconstructed);
            }
        }

        // Test last day of month
        {
            std::ostringstream oss;
            oss << std::setfill('0') << std::setw(4) << year << "-" << std::setw(2) << month
                << "-" << std::setw(2) << max_day << "T12:00:00";

            std::string original = oss.str();
            std::string reconstructed;
            bool success = ParseAndFormat(original, reconstructed);

            EXPECT_TRUE(success) << "Failed to parse/format: " << original;
            if (success) {
                EXPECT_EQ(original, reconstructed);
            }
        }
    }
}

/**
 * @brief Test that noon times round-trip correctly.
 *
 * This test verifies round-trip for noon (12:00:00) times.
 */
TEST_F(ISO8601RoundTripPropertyTest, NoonTimesRoundTrip) {
    const int num_tests = 20;

    for (int i = 0; i < num_tests; ++i) {
        std::uniform_int_distribution<int> year_dist(1900, 2100);
        std::uniform_int_distribution<int> month_dist(1, 12);

        int year = year_dist(rng);
        int month = month_dist(rng);
        int max_day = GetMaxDayForMonth(year, month);
        std::uniform_int_distribution<int> day_dist(1, max_day);
        int day = day_dist(rng);

        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(4) << year << "-" << std::setw(2) << month << "-"
            << std::setw(2) << day << "T12:00:00";

        std::string original = oss.str();
        std::string reconstructed;
        bool success = ParseAndFormat(original, reconstructed);

        EXPECT_TRUE(success) << "Failed to parse/format: " << original;

        if (success) {
            EXPECT_EQ(original, reconstructed)
                << "Round-trip mismatch for noon time:\n"
                << "  Original:      " << original << "\n"
                << "  Reconstructed: " << reconstructed;
        }
    }
}

}  // namespace aces
