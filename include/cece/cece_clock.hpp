#ifndef CECE_CLOCK_HPP
#define CECE_CLOCK_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace cece {

/**
 * @brief Identifies a schedulable component type.
 */
enum class ComponentType : uint8_t { kPhysicsScheme, kDataStream, kStackingEngine };

/**
 * @brief A component registered with the clock for scheduling.
 */
struct ClockComponent {
    ComponentType type;
    std::string name;           ///< e.g., "megan", "GLOBAL_ANTHROPOGENIC"
    int refresh_interval_secs;  ///< must be > 0 and multiple of base_timestep
};

/**
 * @brief Returned by Advance() — the set of components due this step.
 */
struct StepResult {
    std::vector<const ClockComponent*> due_components;  ///< ordered: schemes/streams first, stacking last
    int elapsed_seconds;                                ///< total seconds since start_time
    int hour_of_day;                                    ///< 0-23
    int day_of_week;                                    ///< 0-6, Sunday=0
    int month;                                          ///< 0-11
    bool simulation_complete;
};

/**
 * @class CeceClock
 * @brief Central time-management class that tracks simulation time and determines
 *        which components are due for execution at each timestep.
 *
 * @details The clock uses integer-second arithmetic, validates all intervals at
 *          construction, and supports per-component refresh intervals. All ESMF
 *          coupling remains external; this class has no ESMF dependency.
 */
class CeceClock {
   public:
    /**
     * @brief Constructs and validates the clock from config values.
     * @param start_time_iso8601 Simulation start time in "YYYY-MM-DDTHH:MM:SS" format.
     * @param end_time_iso8601 Simulation end time in "YYYY-MM-DDTHH:MM:SS" format.
     * @param base_timestep_secs Base timestep in seconds (must be > 0).
     * @param components Components to schedule (intervals must be positive multiples of base).
     * @throws std::invalid_argument on validation failure.
     */
    CeceClock(const std::string& start_time_iso8601, const std::string& end_time_iso8601, int base_timestep_secs,
              const std::vector<ClockComponent>& components);

    /**
     * @brief Advances the clock by one base timestep and returns due components.
     * @return StepResult containing due components and current time info.
     */
    StepResult Advance();

    /**
     * @brief Returns true if the simulation has reached or exceeded end_time.
     */
    bool IsComplete() const;

    /**
     * @brief Returns the current elapsed seconds since start_time.
     */
    int64_t ElapsedSeconds() const;

    /**
     * @brief Returns the base timestep in seconds.
     */
    int BaseTimestep() const;

   private:
    int64_t start_epoch_secs_;  ///< start_time as seconds since Unix epoch
    int64_t end_epoch_secs_;    ///< end_time as seconds since Unix epoch
    int base_timestep_secs_;
    int64_t elapsed_seconds_ = 0;  ///< seconds since start_time

    struct ComponentState {
        ClockComponent component;
        int64_t last_executed_at = -1;  ///< -1 means never executed
    };
    std::vector<ComponentState> components_;

    /**
     * @brief Extracts hour, day-of-week, and month from absolute epoch seconds.
     */
    static void DecomposeTime(int64_t epoch_secs, int& hour, int& dow, int& month);

    /**
     * @brief Converts ISO8601 "YYYY-MM-DDTHH:MM:SS" string to epoch seconds.
     * @throws std::invalid_argument if the format is invalid.
     */
    static int64_t ParseISO8601(const std::string& iso);
};

}  // namespace cece

#endif  // CECE_CLOCK_HPP
