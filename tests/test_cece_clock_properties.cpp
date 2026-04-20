/**
 * @file test_cece_clock_properties.cpp
 * @brief Property-based tests for the CeceClock class.
 *
 * Validates correctness properties for the CeceClock time-management class
 * using RapidCheck with GTest integration.
 *
 * Properties tested:
 * 1. Advance preserves elapsed time (Requirements 1.2, 1.3, 7.5)
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "cece/cece_clock.hpp"
#include "cece/cece_config.hpp"

namespace cece {

// ============================================================================
// Property 1: Advance preserves elapsed time
// Feature: clock-refresh-intervals, Property 1: Advance preserves elapsed time
// **Validates: Requirements 1.2, 1.3, 7.5**
//
// For any valid base timestep B and any step count N >= 0, after calling
// Advance() exactly N times, ElapsedSeconds() SHALL equal N * B.
// ============================================================================

RC_GTEST_PROP(CeceClockProperty, Property1_AdvancePreservesElapsedTime, ()) {
    // Generate a random valid base timestep B in [1, 3600] seconds
    const int base_timestep = 1 + *rc::gen::inRange(0, 3600);

    // Generate a random step count N in [0, 100]
    const int step_count = *rc::gen::inRange(0, 101);

    // Fixed start time
    const std::string start_time = "2020-01-01T00:00:00";

    // Compute end_time far enough to accommodate N steps plus margin.
    // We need at least N * base_timestep seconds after start.
    // Use (step_count + 1) * base_timestep to ensure end > start even when N=0.
    const int64_t required_seconds =
        static_cast<int64_t>(step_count + 1) * static_cast<int64_t>(base_timestep);

    // Build end_time ISO8601 string by adding required_seconds to a known epoch.
    // 2020-01-01T00:00:00 UTC = 1577836800 epoch seconds.
    const int64_t start_epoch = 1577836800;
    const int64_t end_epoch = start_epoch + required_seconds;

    // Convert end_epoch back to ISO8601
    std::time_t end_t = static_cast<std::time_t>(end_epoch);
    std::tm* gm = std::gmtime(&end_t);
    RC_ASSERT(gm != nullptr);

    char end_buf[20];
    std::snprintf(end_buf, sizeof(end_buf), "%04d-%02d-%02dT%02d:%02d:%02d",
                  gm->tm_year + 1900, gm->tm_mon + 1, gm->tm_mday,
                  gm->tm_hour, gm->tm_min, gm->tm_sec);
    const std::string end_time(end_buf);

    // Create one component with refresh_interval = base_timestep (simplest valid config)
    std::vector<ClockComponent> components = {
        {ComponentType::kPhysicsScheme, "test_scheme", base_timestep}};

    // Construct the clock
    CeceClock clock(start_time, end_time, base_timestep, components);

    // Verify initial elapsed time is 0
    RC_ASSERT(clock.ElapsedSeconds() == 0);

    // Advance exactly N times
    for (int i = 0; i < step_count; ++i) {
        clock.Advance();
    }

    // Assert: ElapsedSeconds() == N * B
    const int64_t expected = static_cast<int64_t>(step_count) * static_cast<int64_t>(base_timestep);
    RC_ASSERT(clock.ElapsedSeconds() == expected);
}

// ============================================================================
// Property 2: Calendar decomposition correctness
// Feature: clock-refresh-intervals, Property 2: Calendar decomposition correctness
// **Validates: Requirements 1.4, 7.2, 7.3, 7.4**
//
// For any valid ISO8601 start time and any elapsed seconds value, the
// hour-of-day, day-of-week, and month returned by the clock SHALL match
// the values produced by std::gmtime applied to (start_epoch + elapsed_seconds).
// ============================================================================

RC_GTEST_PROP(CeceClockProperty, Property2_CalendarDecompositionCorrectness, ()) {
    // Random epoch between 2000-01-01 (946684800) and 2030-01-01 (1893456000)
    constexpr int64_t epoch_2000 = 946684800;
    constexpr int64_t epoch_2030 = 1893456000;
    const int64_t start_epoch =
        epoch_2000 + *rc::gen::inRange<int64_t>(0, epoch_2030 - epoch_2000);

    // Random base timestep B in [1, 3600]
    const int base_timestep = 1 + *rc::gen::inRange(0, 3600);

    // Random step count N in [1, 50]
    const int step_count = 1 + *rc::gen::inRange(0, 50);

    // Convert start_epoch to ISO8601 string
    std::time_t start_t = static_cast<std::time_t>(start_epoch);
    std::tm* start_gm = std::gmtime(&start_t);
    RC_ASSERT(start_gm != nullptr);

    char start_buf[20];
    std::snprintf(start_buf, sizeof(start_buf), "%04d-%02d-%02dT%02d:%02d:%02d",
                  start_gm->tm_year + 1900, start_gm->tm_mon + 1, start_gm->tm_mday,
                  start_gm->tm_hour, start_gm->tm_min, start_gm->tm_sec);
    const std::string start_time(start_buf);

    // Compute end_time far enough to accommodate N steps plus margin
    const int64_t required_seconds =
        static_cast<int64_t>(step_count + 1) * static_cast<int64_t>(base_timestep);
    const int64_t end_epoch = start_epoch + required_seconds;

    std::time_t end_t = static_cast<std::time_t>(end_epoch);
    std::tm* end_gm = std::gmtime(&end_t);
    RC_ASSERT(end_gm != nullptr);

    char end_buf[20];
    std::snprintf(end_buf, sizeof(end_buf), "%04d-%02d-%02dT%02d:%02d:%02d",
                  end_gm->tm_year + 1900, end_gm->tm_mon + 1, end_gm->tm_mday,
                  end_gm->tm_hour, end_gm->tm_min, end_gm->tm_sec);
    const std::string end_time(end_buf);

    // One component with refresh_interval = base_timestep
    std::vector<ClockComponent> components = {
        {ComponentType::kPhysicsScheme, "test_scheme", base_timestep}};

    // Construct the clock
    CeceClock clock(start_time, end_time, base_timestep, components);

    // Advance N times, keep the last StepResult
    StepResult last_result;
    for (int i = 0; i < step_count; ++i) {
        last_result = clock.Advance();
    }

    // Compute expected calendar values using std::gmtime on (start_epoch + N*B)
    const int64_t absolute_epoch =
        start_epoch + static_cast<int64_t>(step_count) * static_cast<int64_t>(base_timestep);
    std::time_t abs_t = static_cast<std::time_t>(absolute_epoch);
    std::tm* expected_gm = std::gmtime(&abs_t);
    RC_ASSERT(expected_gm != nullptr);

    const int expected_hour = expected_gm->tm_hour;   // 0-23
    const int expected_dow  = expected_gm->tm_wday;   // 0-6, Sunday=0
    const int expected_month = expected_gm->tm_mon;   // 0-11

    // Assert calendar fields match
    RC_ASSERT(last_result.hour_of_day == expected_hour);
    RC_ASSERT(last_result.day_of_week == expected_dow);
    RC_ASSERT(last_result.month == expected_month);
}

// ============================================================================
// Property 3: Invalid time range produces error
// Feature: clock-refresh-intervals, Property 3: Invalid time range produces error
// **Validates: Requirements 1.5**
//
// For any pair of ISO8601 times where start_time >= end_time, constructing
// a CeceClock SHALL throw std::invalid_argument.
// ============================================================================

RC_GTEST_PROP(CeceClockProperty, Property3_InvalidTimeRangeProducesError, ()) {
    // Random epoch for end_time between 2000-01-01 and 2030-01-01
    constexpr int64_t epoch_2000 = 946684800;
    constexpr int64_t epoch_2030 = 1893456000;
    const int64_t end_epoch =
        epoch_2000 + *rc::gen::inRange<int64_t>(0, epoch_2030 - epoch_2000);

    // Random offset >= 0 so that start_epoch >= end_epoch (start >= end)
    const int64_t offset = *rc::gen::inRange<int64_t>(0, 365 * 24 * 3600);
    const int64_t start_epoch = end_epoch + offset;

    // Convert start_epoch to ISO8601
    std::time_t start_t = static_cast<std::time_t>(start_epoch);
    std::tm* start_gm = std::gmtime(&start_t);
    RC_ASSERT(start_gm != nullptr);

    char start_buf[20];
    std::snprintf(start_buf, sizeof(start_buf), "%04d-%02d-%02dT%02d:%02d:%02d",
                  start_gm->tm_year + 1900, start_gm->tm_mon + 1, start_gm->tm_mday,
                  start_gm->tm_hour, start_gm->tm_min, start_gm->tm_sec);
    const std::string start_time(start_buf);

    // Convert end_epoch to ISO8601
    std::time_t end_t = static_cast<std::time_t>(end_epoch);
    std::tm* end_gm = std::gmtime(&end_t);
    RC_ASSERT(end_gm != nullptr);

    char end_buf[20];
    std::snprintf(end_buf, sizeof(end_buf), "%04d-%02d-%02dT%02d:%02d:%02d",
                  end_gm->tm_year + 1900, end_gm->tm_mon + 1, end_gm->tm_mday,
                  end_gm->tm_hour, end_gm->tm_min, end_gm->tm_sec);
    const std::string end_time(end_buf);

    // Valid base timestep and one valid component
    const int base_timestep = 300;
    std::vector<ClockComponent> components = {
        {ComponentType::kPhysicsScheme, "test_scheme", base_timestep}};

    // Constructing CeceClock with start >= end must throw std::invalid_argument
    RC_ASSERT_THROWS_AS(
        CeceClock(start_time, end_time, base_timestep, components),
        std::invalid_argument);
}

// ============================================================================
// Property 4: Configuration round-trip
// Feature: clock-refresh-intervals, Property 4: Configuration round-trip
// **Validates: Requirements 9.1, 9.2, 9.3, 9.4**
//
// For any valid CeceConfig object containing refresh_interval_seconds fields
// on physics schemes, data streams, and stacking_refresh_interval_seconds on
// the driver, serializing to YAML and parsing back SHALL produce an equivalent
// configuration object.
//
// Since no serializer exists, we test the simpler property: construct a YAML
// string with known refresh interval values, write to a temp file, parse it
// with ParseConfig(), and verify the parsed values match the generated values.
// ============================================================================

RC_GTEST_PROP(CeceClockProperty, Property4_ConfigurationRoundTrip, ()) {
    // Generate random counts of physics schemes [1, 5] and data streams [1, 5]
    const int num_schemes = 1 + *rc::gen::inRange(0, 5);
    const int num_streams = 1 + *rc::gen::inRange(0, 5);

    // Generate refresh intervals as positive multiples of 60 in [60, 3600]
    std::vector<int> scheme_intervals;
    for (int i = 0; i < num_schemes; ++i) {
        const int multiplier = 1 + *rc::gen::inRange(0, 60);  // 1..60
        scheme_intervals.push_back(multiplier * 60);
    }

    std::vector<int> stream_intervals;
    for (int i = 0; i < num_streams; ++i) {
        const int multiplier = 1 + *rc::gen::inRange(0, 60);
        stream_intervals.push_back(multiplier * 60);
    }

    const int stacking_multiplier = 1 + *rc::gen::inRange(0, 60);
    const int stacking_interval = stacking_multiplier * 60;

    // Build a minimal YAML config string
    std::string yaml;
    yaml += "driver:\n";
    yaml += "  start_time: \"2020-01-01T00:00:00\"\n";
    yaml += "  end_time: \"2020-01-02T00:00:00\"\n";
    yaml += "  timestep_seconds: 60\n";
    yaml += "  stacking_refresh_interval_seconds: " + std::to_string(stacking_interval) + "\n";
    yaml += "\n";

    yaml += "physics_schemes:\n";
    for (int i = 0; i < num_schemes; ++i) {
        yaml += "  - name: \"scheme_" + std::to_string(i) + "\"\n";
        yaml += "    language: \"cpp\"\n";
        yaml += "    refresh_interval_seconds: " + std::to_string(scheme_intervals[i]) + "\n";
    }
    yaml += "\n";

    yaml += "cece_data:\n";
    yaml += "  streams:\n";
    for (int i = 0; i < num_streams; ++i) {
        yaml += "    - name: \"stream_" + std::to_string(i) + "\"\n";
        yaml += "      file: \"/tmp/dummy_" + std::to_string(i) + ".nc\"\n";
        yaml += "      refresh_interval_seconds: " + std::to_string(stream_intervals[i]) + "\n";
    }

    // Write YAML to a temporary file
    char tmp_path[] = "/tmp/cece_prop4_XXXXXX";
    int fd = mkstemp(tmp_path);
    RC_ASSERT(fd >= 0);
    close(fd);

    {
        std::ofstream ofs(tmp_path);
        RC_ASSERT(ofs.good());
        ofs << yaml;
    }

    // Parse the config
    CeceConfig config = ParseConfig(std::string(tmp_path));

    // Clean up temp file
    std::remove(tmp_path);

    // Verify physics scheme refresh intervals
    RC_ASSERT(static_cast<int>(config.physics_schemes.size()) == num_schemes);
    for (int i = 0; i < num_schemes; ++i) {
        RC_ASSERT(config.physics_schemes[i].name == "scheme_" + std::to_string(i));
        RC_ASSERT(config.physics_schemes[i].refresh_interval_seconds == scheme_intervals[i]);
    }

    // Verify data stream refresh intervals
    RC_ASSERT(static_cast<int>(config.cece_data.streams.size()) == num_streams);
    for (int i = 0; i < num_streams; ++i) {
        RC_ASSERT(config.cece_data.streams[i].name == "stream_" + std::to_string(i));
        RC_ASSERT(config.cece_data.streams[i].refresh_interval_seconds == stream_intervals[i]);
    }

    // Verify stacking refresh interval
    RC_ASSERT(config.driver_config.stacking_refresh_interval_seconds == stacking_interval);
}

// ============================================================================
// Property 5: Missing refresh interval defaults to base timestep
// Feature: clock-refresh-intervals, Property 5: Missing refresh interval defaults to base timestep
// **Validates: Requirements 2.2, 3.2, 4.2, 10.1, 10.2**
//
// For any component (physics scheme, data stream, or stacking engine) whose
// config omits refresh_interval_seconds, the clock SHALL schedule it with a
// refresh interval equal to the base timestep, making it due at every step.
//
// Since 0 gets resolved to base_timestep before clock construction, we
// simulate this by creating all components with refresh_interval_secs = B.
// After advancing N times, ALL components must appear in the due list at
// EVERY step (because T % B == 0 always holds).
// ============================================================================

RC_GTEST_PROP(CeceClockProperty, Property5_MissingRefreshIntervalDefaultsToBaseTimestep, ()) {
    // Generate a random base timestep B in [60, 3600] seconds
    const int base_timestep = 60 + *rc::gen::inRange(0, 3541);

    // Generate a random step count N in [2, 20]
    const int step_count = 2 + *rc::gen::inRange(0, 19);

    // Fixed start time
    const std::string start_time = "2020-01-01T00:00:00";

    // Compute end_time far enough to accommodate N steps plus margin
    const int64_t start_epoch = 1577836800;  // 2020-01-01T00:00:00 UTC
    const int64_t required_seconds =
        static_cast<int64_t>(step_count + 1) * static_cast<int64_t>(base_timestep);
    const int64_t end_epoch = start_epoch + required_seconds;

    std::time_t end_t = static_cast<std::time_t>(end_epoch);
    std::tm* gm = std::gmtime(&end_t);
    RC_ASSERT(gm != nullptr);

    char end_buf[20];
    std::snprintf(end_buf, sizeof(end_buf), "%04d-%02d-%02dT%02d:%02d:%02d",
                  gm->tm_year + 1900, gm->tm_mon + 1, gm->tm_mday,
                  gm->tm_hour, gm->tm_min, gm->tm_sec);
    const std::string end_time(end_buf);

    // Generate random counts of each component type [1, 5]
    const int num_schemes = 1 + *rc::gen::inRange(0, 5);
    const int num_streams = 1 + *rc::gen::inRange(0, 5);

    // Build components: ALL have refresh_interval_secs = base_timestep
    // (simulating the default when config omits refresh_interval_seconds,
    //  since 0 is resolved to base_timestep before clock construction)
    std::vector<ClockComponent> components;
    for (int i = 0; i < num_schemes; ++i) {
        components.push_back(
            {ComponentType::kPhysicsScheme,
             "scheme_" + std::to_string(i),
             base_timestep});
    }
    for (int i = 0; i < num_streams; ++i) {
        components.push_back(
            {ComponentType::kDataStream,
             "stream_" + std::to_string(i),
             base_timestep});
    }
    components.push_back(
        {ComponentType::kStackingEngine, "stacking", base_timestep});

    const int total_components = num_schemes + num_streams + 1;

    // Construct the clock
    CeceClock clock(start_time, end_time, base_timestep, components);

    // Advance N times and assert ALL components are due at EVERY step
    for (int step = 0; step < step_count; ++step) {
        StepResult result = clock.Advance();
        RC_ASSERT(static_cast<int>(result.due_components.size()) == total_components);
    }
}

// ============================================================================
// Property 6: Scheduling correctness
// Feature: clock-refresh-intervals, Property 6: Scheduling correctness
// **Validates: Requirements 2.3, 3.3, 4.3, 5.1, 5.2, 5.3**
//
// For any set of components with valid refresh intervals (positive multiples
// of base timestep B), after advancing to elapsed time T, a component with
// interval I SHALL be in the due list if and only if T is a multiple of I
// (i.e., T % I == 0), with the special case that all components are due
// when T == B (first step).
// ============================================================================

RC_GTEST_PROP(CeceClockProperty, Property6_SchedulingCorrectness, ()) {
    // 1. Generate a random base timestep B in [60, 600]
    const int base_timestep = 60 + *rc::gen::inRange(0, 541);

    // 2. Generate 2-5 components with random intervals that are multiples of B (1x to 10x)
    const int num_components = 2 + *rc::gen::inRange(0, 4);

    std::vector<ClockComponent> components;
    std::vector<int> intervals;
    for (int i = 0; i < num_components; ++i) {
        const int multiplier = 1 + *rc::gen::inRange(0, 10);
        const int interval = multiplier * base_timestep;
        intervals.push_back(interval);
        components.push_back(
            {ComponentType::kPhysicsScheme,
             "component_" + std::to_string(i),
             interval});
    }

    // 3. Generate a random step count N in [2, 30] (skip step 1 for modulo check)
    const int step_count = 2 + *rc::gen::inRange(0, 29);

    // Fixed start time
    const std::string start_time = "2020-01-01T00:00:00";

    // Compute end_time far enough to accommodate all steps
    const int64_t start_epoch = 1577836800;  // 2020-01-01T00:00:00 UTC
    const int64_t required_seconds =
        static_cast<int64_t>(step_count + 1) * static_cast<int64_t>(base_timestep);
    const int64_t end_epoch = start_epoch + required_seconds;

    std::time_t end_t = static_cast<std::time_t>(end_epoch);
    std::tm* gm = std::gmtime(&end_t);
    RC_ASSERT(gm != nullptr);

    char end_buf[20];
    std::snprintf(end_buf, sizeof(end_buf), "%04d-%02d-%02dT%02d:%02d:%02d",
                  gm->tm_year + 1900, gm->tm_mon + 1, gm->tm_mday,
                  gm->tm_hour, gm->tm_min, gm->tm_sec);
    const std::string end_time(end_buf);

    // 4. Create a CeceClock and advance N times
    CeceClock clock(start_time, end_time, base_timestep, components);

    for (int step = 1; step <= step_count; ++step) {
        StepResult result = clock.Advance();
        const int64_t elapsed = static_cast<int64_t>(step) * static_cast<int64_t>(base_timestep);

        if (step == 1) {
            // 6. First step: all components are due
            RC_ASSERT(static_cast<int>(result.due_components.size()) == num_components);
        } else {
            // 5. For each step after the first, verify component is due IFF elapsed % interval == 0
            for (int c = 0; c < num_components; ++c) {
                const bool should_be_due = (elapsed % intervals[c] == 0);

                // Check if component is in the due list
                bool found_in_due = false;
                for (const auto* comp : result.due_components) {
                    if (comp->name == "component_" + std::to_string(c)) {
                        found_in_due = true;
                        break;
                    }
                }

                RC_ASSERT(found_in_due == should_be_due);
            }
        }
    }
}

// ============================================================================
// Property 7: First-step all-due guarantee
// Feature: clock-refresh-intervals, Property 7: First-step all-due guarantee
// **Validates: Requirements 6.1, 6.2, 6.3**
//
// For any clock configuration with any number of components and any valid
// refresh intervals, the first call to Advance() (when elapsed time
// transitions from 0 to base_timestep) SHALL include all components in the
// due list, regardless of their intervals.
// ============================================================================

RC_GTEST_PROP(CeceClockProperty, Property7_FirstStepAllDueGuarantee, ()) {
    // 1. Generate a random base timestep B in [60, 3600]
    const int base_timestep = 60 + *rc::gen::inRange(0, 3541);

    // 2. Generate random component counts
    const int num_schemes = 1 + *rc::gen::inRange(0, 5);   // 1-5 physics schemes
    const int num_streams = 1 + *rc::gen::inRange(0, 5);   // 1-5 data streams

    // 3. Build components with random intervals (multiples of B, 1x to 20x)
    std::vector<ClockComponent> components;
    for (int i = 0; i < num_schemes; ++i) {
        const int multiplier = 1 + *rc::gen::inRange(0, 20);  // 1..20
        components.push_back(
            {ComponentType::kPhysicsScheme,
             "scheme_" + std::to_string(i),
             multiplier * base_timestep});
    }
    for (int i = 0; i < num_streams; ++i) {
        const int multiplier = 1 + *rc::gen::inRange(0, 20);
        components.push_back(
            {ComponentType::kDataStream,
             "stream_" + std::to_string(i),
             multiplier * base_timestep});
    }
    // Always include one stacking engine with a random interval
    const int stacking_multiplier = 1 + *rc::gen::inRange(0, 20);
    components.push_back(
        {ComponentType::kStackingEngine,
         "stacking",
         stacking_multiplier * base_timestep});

    const int total_components = num_schemes + num_streams + 1;

    // 4. Compute end_time far enough for at least 2 steps
    const std::string start_time = "2020-01-01T00:00:00";
    const int64_t start_epoch = 1577836800;  // 2020-01-01T00:00:00 UTC

    // Find the maximum interval to ensure end_time accommodates it
    int max_interval = base_timestep;
    for (const auto& comp : components) {
        if (comp.refresh_interval_secs > max_interval) {
            max_interval = comp.refresh_interval_secs;
        }
    }
    const int64_t required_seconds =
        static_cast<int64_t>(max_interval) + static_cast<int64_t>(base_timestep);
    const int64_t end_epoch = start_epoch + required_seconds;

    std::time_t end_t = static_cast<std::time_t>(end_epoch);
    std::tm* gm = std::gmtime(&end_t);
    RC_ASSERT(gm != nullptr);

    char end_buf[20];
    std::snprintf(end_buf, sizeof(end_buf), "%04d-%02d-%02dT%02d:%02d:%02d",
                  gm->tm_year + 1900, gm->tm_mon + 1, gm->tm_mday,
                  gm->tm_hour, gm->tm_min, gm->tm_sec);
    const std::string end_time(end_buf);

    // 5. Create the clock
    CeceClock clock(start_time, end_time, base_timestep, components);

    // 6. Call Advance() exactly once (first step)
    StepResult result = clock.Advance();

    // 7. Assert ALL components appear in the due list
    RC_ASSERT(static_cast<int>(result.due_components.size()) == total_components);

    // 8. Verify each component is present by name
    for (const auto& comp : components) {
        bool found = false;
        for (const auto* due : result.due_components) {
            if (due->name == comp.name) {
                found = true;
                break;
            }
        }
        RC_ASSERT(found);
    }
}

// ============================================================================
// Property 8: Components execute before stacking
// Feature: clock-refresh-intervals, Property 8: Components execute before stacking
// **Validates: Requirements 5.4**
//
// For any step where both non-stacking components and the stacking engine
// are due, the stacking engine SHALL appear after all other due components
// in the returned due list.
// ============================================================================

RC_GTEST_PROP(CeceClockProperty, Property8_ComponentsExecuteBeforeStacking, ()) {
    // 1. Generate a random base timestep B in [60, 600]
    const int base_timestep = 60 + *rc::gen::inRange(0, 541);

    // 2. Generate 2-5 non-stacking components with intervals that are multiples of B
    const int num_non_stacking = 2 + *rc::gen::inRange(0, 4);

    std::vector<ClockComponent> components;
    for (int i = 0; i < num_non_stacking; ++i) {
        const int multiplier = 1 + *rc::gen::inRange(0, 5);  // 1..5
        components.push_back(
            {ComponentType::kPhysicsScheme,
             "scheme_" + std::to_string(i),
             multiplier * base_timestep});
    }

    // 3. Add a stacking engine whose interval matches at least one non-stacking
    //    component, guaranteeing they are due together at some steps.
    //    Pick the interval of a random existing component.
    const int stacking_idx = *rc::gen::inRange(0, num_non_stacking);
    const int stacking_interval = components[stacking_idx].refresh_interval_secs;
    components.push_back(
        {ComponentType::kStackingEngine, "stacking", stacking_interval});

    // 4. Advance multiple steps [5, 30]
    const int step_count = 5 + *rc::gen::inRange(0, 26);

    // Fixed start time
    const std::string start_time = "2020-01-01T00:00:00";
    const int64_t start_epoch = 1577836800;  // 2020-01-01T00:00:00 UTC

    // Compute end_time far enough to accommodate all steps
    const int64_t required_seconds =
        static_cast<int64_t>(step_count + 1) * static_cast<int64_t>(base_timestep);
    const int64_t end_epoch = start_epoch + required_seconds;

    std::time_t end_t = static_cast<std::time_t>(end_epoch);
    std::tm* gm = std::gmtime(&end_t);
    RC_ASSERT(gm != nullptr);

    char end_buf[20];
    std::snprintf(end_buf, sizeof(end_buf), "%04d-%02d-%02dT%02d:%02d:%02d",
                  gm->tm_year + 1900, gm->tm_mon + 1, gm->tm_mday,
                  gm->tm_hour, gm->tm_min, gm->tm_sec);
    const std::string end_time(end_buf);

    // 5. Create the clock and advance
    CeceClock clock(start_time, end_time, base_timestep, components);

    bool found_mixed_step = false;

    for (int step = 1; step <= step_count; ++step) {
        StepResult result = clock.Advance();

        // Find if stacking engine is in the due list
        bool stacking_due = false;
        int stacking_pos = -1;
        int last_non_stacking_pos = -1;

        for (int j = 0; j < static_cast<int>(result.due_components.size()); ++j) {
            if (result.due_components[j]->type == ComponentType::kStackingEngine) {
                stacking_due = true;
                stacking_pos = j;
            } else {
                last_non_stacking_pos = j;
            }
        }

        // If both stacking and non-stacking components are due,
        // stacking must appear after ALL non-stacking components
        if (stacking_due && last_non_stacking_pos >= 0) {
            found_mixed_step = true;
            RC_ASSERT(stacking_pos > last_non_stacking_pos);
        }
    }

    // Ensure we actually tested the property at least once
    // (first step guarantees all are due, so this should always hold)
    RC_ASSERT(found_mixed_step);
}

// ============================================================================
// Property 9: Termination signal
// Feature: clock-refresh-intervals, Property 9: Termination signal
// **Validates: Requirements 5.5**
//
// For any valid clock configuration, after advancing enough steps such that
// elapsed_seconds >= (end_time - start_time), IsComplete() SHALL return true
// and StepResult.simulation_complete SHALL be true.
// ============================================================================

RC_GTEST_PROP(CeceClockProperty, Property9_TerminationSignal, ()) {
    // 1. Generate a random base timestep B in [60, 3600] seconds
    const int base_timestep = 60 + *rc::gen::inRange(0, 3541);

    // 2. Generate a random multiplier for duration D = multiplier * B (2x to 20x)
    const int multiplier = 2 + *rc::gen::inRange(0, 19);
    const int64_t duration = static_cast<int64_t>(multiplier) * static_cast<int64_t>(base_timestep);

    // 3. Create clock with start_time and end_time such that (end - start) = D seconds
    const int64_t start_epoch = 1577836800;  // 2020-01-01T00:00:00 UTC
    const int64_t end_epoch = start_epoch + duration;

    // Convert start_epoch to ISO8601
    std::time_t start_t = static_cast<std::time_t>(start_epoch);
    std::tm* start_gm = std::gmtime(&start_t);
    RC_ASSERT(start_gm != nullptr);

    char start_buf[20];
    std::snprintf(start_buf, sizeof(start_buf), "%04d-%02d-%02dT%02d:%02d:%02d",
                  start_gm->tm_year + 1900, start_gm->tm_mon + 1, start_gm->tm_mday,
                  start_gm->tm_hour, start_gm->tm_min, start_gm->tm_sec);
    const std::string start_time(start_buf);

    // Convert end_epoch to ISO8601
    std::time_t end_t = static_cast<std::time_t>(end_epoch);
    std::tm* end_gm = std::gmtime(&end_t);
    RC_ASSERT(end_gm != nullptr);

    char end_buf[20];
    std::snprintf(end_buf, sizeof(end_buf), "%04d-%02d-%02dT%02d:%02d:%02d",
                  end_gm->tm_year + 1900, end_gm->tm_mon + 1, end_gm->tm_mday,
                  end_gm->tm_hour, end_gm->tm_min, end_gm->tm_sec);
    const std::string end_time(end_buf);

    // One component with refresh_interval = base_timestep
    std::vector<ClockComponent> components = {
        {ComponentType::kPhysicsScheme, "test_scheme", base_timestep}};

    // Construct the clock
    CeceClock clock(start_time, end_time, base_timestep, components);

    // 4. Compute the exact number of steps needed: N = D / B
    const int total_steps = static_cast<int>(duration / static_cast<int64_t>(base_timestep));
    RC_ASSERT(total_steps == multiplier);  // sanity check

    // 5. Advance exactly N times, tracking results
    StepResult last_result;
    for (int i = 0; i < total_steps; ++i) {
        // 8. Before the final step, IsComplete() must be false
        if (i == total_steps - 1) {
            RC_ASSERT(clock.IsComplete() == false);
        }

        last_result = clock.Advance();
    }

    // 6. Assert IsComplete() == true after all steps
    RC_ASSERT(clock.IsComplete() == true);

    // 7. Assert the last StepResult.simulation_complete == true
    RC_ASSERT(last_result.simulation_complete == true);
}

// ============================================================================
// Property 10: Invalid interval produces error naming the component
// Feature: clock-refresh-intervals, Property 10: Invalid interval produces error naming the component
// **Validates: Requirements 2.4, 2.5, 3.4, 3.5, 4.4, 4.5, 8.1, 8.2**
//
// Sub-property 1 (Non-positive interval):
// For any component whose refresh_interval_seconds is 0 or negative,
// clock construction SHALL throw std::invalid_argument and the exception
// message SHALL contain the component's name.
//
// Sub-property 2 (Non-multiple interval):
// For any base timestep B > 1 and a component whose refresh_interval_seconds
// is positive but NOT an integer multiple of B, clock construction SHALL
// throw std::invalid_argument and the exception message SHALL contain the
// component's name.
// ============================================================================

RC_GTEST_PROP(CeceClockProperty, Property10_NonPositiveIntervalProducesErrorNamingComponent, ()) {
    // Generate a random component name (non-empty alphanumeric, 3-20 chars)
    const int name_len = 3 + *rc::gen::inRange(0, 18);
    std::string comp_name;
    for (int i = 0; i < name_len; ++i) {
        // Generate lowercase letters a-z
        comp_name += static_cast<char>('a' + *rc::gen::inRange(0, 26));
    }

    // Generate a non-positive interval: 0 or negative in [-10000, 0]
    const int bad_interval = *rc::gen::inRange(-10000, 1);  // [-10000, 0]

    // Valid base timestep and time range
    const int base_timestep = 300;
    const std::string start_time = "2020-01-01T00:00:00";
    const std::string end_time   = "2020-01-02T00:00:00";

    std::vector<ClockComponent> components = {
        {ComponentType::kPhysicsScheme, comp_name, bad_interval}};

    // Construction must throw std::invalid_argument
    bool threw = false;
    std::string error_msg;
    try {
        CeceClock clock(start_time, end_time, base_timestep, components);
    } catch (const std::invalid_argument& e) {
        threw = true;
        error_msg = e.what();
    }

    RC_ASSERT(threw);
    // The error message must contain the component's name
    RC_ASSERT(error_msg.find(comp_name) != std::string::npos);
}

RC_GTEST_PROP(CeceClockProperty, Property10_NonMultipleIntervalProducesErrorNamingComponent, ()) {
    // Generate a random component name (non-empty alphanumeric, 3-20 chars)
    const int name_len = 3 + *rc::gen::inRange(0, 18);
    std::string comp_name;
    for (int i = 0; i < name_len; ++i) {
        comp_name += static_cast<char>('a' + *rc::gen::inRange(0, 26));
    }

    // Generate a base timestep B > 1 so that non-multiples exist
    const int base_timestep = 2 + *rc::gen::inRange(0, 3599);  // [2, 3600]

    // Generate an interval that is positive but NOT a multiple of B.
    // Strategy: pick a random multiple of B, then add an offset in [1, B-1].
    const int multiplier = 1 + *rc::gen::inRange(0, 20);  // 1..20
    const int offset = 1 + *rc::gen::inRange(0, base_timestep - 1);  // [1, B-1]
    const int bad_interval = multiplier * base_timestep + offset;

    const std::string start_time = "2020-01-01T00:00:00";
    const std::string end_time   = "2020-01-02T00:00:00";

    std::vector<ClockComponent> components = {
        {ComponentType::kDataStream, comp_name, bad_interval}};

    // Construction must throw std::invalid_argument
    bool threw = false;
    std::string error_msg;
    try {
        CeceClock clock(start_time, end_time, base_timestep, components);
    } catch (const std::invalid_argument& e) {
        threw = true;
        error_msg = e.what();
    }

    RC_ASSERT(threw);
    // The error message must contain the component's name
    RC_ASSERT(error_msg.find(comp_name) != std::string::npos);
}

// ============================================================================
// Property 11: Conflict detection warning (placeholder)
// Feature: clock-refresh-intervals, Property 11: Conflict detection warning
// **Validates: Requirements 8.3**
//
// For any pair of physics schemes that reference the same export field but
// have different refresh intervals, clock validation SHALL produce a warning
// message naming both schemes.
//
// NOTE: This is a PLACEHOLDER test. The current ClockComponent struct does not
// carry export field metadata, so full conflict detection cannot be tested.
// This test documents the property and verifies that two physics schemes with
// different intervals can coexist without error (since conflict detection only
// produces a warning, not an error). It will be enhanced when export field
// metadata is added to ClockComponent.
// ============================================================================

RC_GTEST_PROP(CeceClockProperty, Property11_ConflictDetectionWarning, ()) {
    // 1. Generate a random base timestep B in [60, 600]
    const int base_timestep = 60 + *rc::gen::inRange(0, 541);

    // 2. Generate two different multipliers for the two physics schemes
    const int multiplier_a = 1 + *rc::gen::inRange(0, 10);  // 1..10
    int multiplier_b = 1 + *rc::gen::inRange(0, 10);        // 1..10
    // Ensure they differ to simulate a conflict scenario
    if (multiplier_b == multiplier_a) {
        multiplier_b = multiplier_a + 1;
    }

    const int interval_a = multiplier_a * base_timestep;
    const int interval_b = multiplier_b * base_timestep;

    // 3. Create two physics schemes with different refresh intervals.
    //    In a real conflict scenario, these would share an export field
    //    (e.g., both writing to "ISOP" emission field). Since ClockComponent
    //    does not carry export field info, we just verify construction succeeds.
    std::vector<ClockComponent> components = {
        {ComponentType::kPhysicsScheme, "scheme_alpha", interval_a},
        {ComponentType::kPhysicsScheme, "scheme_beta", interval_b},
        {ComponentType::kStackingEngine, "stacking", base_timestep}};

    // 4. Fixed time range
    const std::string start_time = "2020-01-01T00:00:00";
    const std::string end_time = "2020-01-02T00:00:00";

    // 5. Verify construction succeeds (no throw) — conflict detection only
    //    produces a warning via std::cerr, not an error/exception.
    //    Full conflict detection requires export field metadata in ClockComponent.
    RC_ASSERT_FALSE(interval_a == interval_b);  // confirm intervals differ

    // Construction must NOT throw even with different intervals on two schemes
    CeceClock clock(start_time, end_time, base_timestep, components);

    // 6. Verify the clock is functional (basic sanity)
    RC_ASSERT(clock.BaseTimestep() == base_timestep);
    RC_ASSERT(clock.ElapsedSeconds() == 0);
    RC_ASSERT(!clock.IsComplete());
}

// ============================================================================
// Property 12: Backward compatibility — uniform intervals
// Feature: clock-refresh-intervals, Property 12: Backward compatibility — uniform intervals
// **Validates: Requirements 10.3**
//
// For any clock configuration where all component refresh intervals equal
// the base timestep, every call to Advance() SHALL return all components
// as due (equivalent to the current implementation without the clock feature).
//
// This is similar to Property 5 but explicitly tests the backward
// compatibility guarantee: when all intervals equal the base timestep,
// behavior is identical to having no clock (everything runs every step).
// ============================================================================

RC_GTEST_PROP(CeceClockProperty, Property12_BackwardCompatibilityUniformIntervals, ()) {
    // 1. Generate a random base timestep B in [60, 3600] seconds
    const int base_timestep = 60 + *rc::gen::inRange(0, 3541);

    // 2. Generate a random number of components (2-10) of mixed types,
    //    ALL with interval = B
    const int num_schemes = 1 + *rc::gen::inRange(0, 5);   // 1-5 physics schemes
    const int num_streams = 1 + *rc::gen::inRange(0, 4);   // 1-4 data streams
    // Always include one stacking engine

    std::vector<ClockComponent> components;
    for (int i = 0; i < num_schemes; ++i) {
        components.push_back(
            {ComponentType::kPhysicsScheme,
             "scheme_" + std::to_string(i),
             base_timestep});
    }
    for (int i = 0; i < num_streams; ++i) {
        components.push_back(
            {ComponentType::kDataStream,
             "stream_" + std::to_string(i),
             base_timestep});
    }
    components.push_back(
        {ComponentType::kStackingEngine, "stacking", base_timestep});

    const int total_components = num_schemes + num_streams + 1;

    // 3. Generate a random step count N in [2, 50]
    const int step_count = 2 + *rc::gen::inRange(0, 49);

    // Fixed start time
    const std::string start_time = "2020-01-01T00:00:00";
    const int64_t start_epoch = 1577836800;  // 2020-01-01T00:00:00 UTC

    // Compute end_time far enough to accommodate N steps plus margin
    const int64_t required_seconds =
        static_cast<int64_t>(step_count + 1) * static_cast<int64_t>(base_timestep);
    const int64_t end_epoch = start_epoch + required_seconds;

    std::time_t end_t = static_cast<std::time_t>(end_epoch);
    std::tm* gm = std::gmtime(&end_t);
    RC_ASSERT(gm != nullptr);

    char end_buf[20];
    std::snprintf(end_buf, sizeof(end_buf), "%04d-%02d-%02dT%02d:%02d:%02d",
                  gm->tm_year + 1900, gm->tm_mon + 1, gm->tm_mday,
                  gm->tm_hour, gm->tm_min, gm->tm_sec);
    const std::string end_time(end_buf);

    // 4. Create a CeceClock and advance N times
    CeceClock clock(start_time, end_time, base_timestep, components);

    // 5. Assert that at EVERY step, ALL components appear in the due list
    for (int step = 0; step < step_count; ++step) {
        StepResult result = clock.Advance();

        // Every step must have all components due (backward compat guarantee)
        RC_ASSERT(static_cast<int>(result.due_components.size()) == total_components);

        // Verify each registered component appears in the due list by name
        for (const auto& comp : components) {
            bool found = false;
            for (const auto* due : result.due_components) {
                if (due->name == comp.name) {
                    found = true;
                    break;
                }
            }
            RC_ASSERT(found);
        }
    }
}

}  // namespace cece
