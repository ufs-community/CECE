/**
 * @file cece_clock.cpp
 * @brief Implementation of CeceClock — central time-management for CECE.
 *
 * Manages per-component refresh intervals using integer-second arithmetic.
 * Validates all intervals at construction and determines which components
 * are due for execution at each timestep. Pure C++ with no ESMF dependency.
 *
 * @author CECE Team
 * @date 2024
 */

#include "cece/cece_clock.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace cece {

// ---------------------------------------------------------------------------
// ParseISO8601
// ---------------------------------------------------------------------------

int64_t CeceClock::ParseISO8601(const std::string& iso) {
    // Expected format: "YYYY-MM-DDTHH:MM:SS"
    if (iso.size() != 19 || iso[4] != '-' || iso[7] != '-' || iso[10] != 'T' || iso[13] != ':' || iso[16] != ':') {
        throw std::invalid_argument("Invalid ISO8601 format: \"" + iso + "\". Expected \"YYYY-MM-DDTHH:MM:SS\".");
    }

    std::tm tm_val{};
    tm_val.tm_year = std::stoi(iso.substr(0, 4)) - 1900;
    tm_val.tm_mon = std::stoi(iso.substr(5, 2)) - 1;
    tm_val.tm_mday = std::stoi(iso.substr(8, 2));
    tm_val.tm_hour = std::stoi(iso.substr(11, 2));
    tm_val.tm_min = std::stoi(iso.substr(14, 2));
    tm_val.tm_sec = std::stoi(iso.substr(17, 2));
    tm_val.tm_isdst = 0;

    // Use timegm where available (POSIX), fall back to portable UTC conversion.
#if defined(_WIN32)
    // On Windows, _mkgmtime provides UTC conversion.
    std::time_t epoch = _mkgmtime(&tm_val);
#else
    // POSIX: timegm converts struct tm in UTC to time_t.
    std::time_t epoch = timegm(&tm_val);
#endif

    if (epoch == static_cast<std::time_t>(-1)) {
        throw std::invalid_argument("Failed to convert ISO8601 time: \"" + iso + "\".");
    }

    return static_cast<int64_t>(epoch);
}

// ---------------------------------------------------------------------------
// DecomposeTime
// ---------------------------------------------------------------------------

void CeceClock::DecomposeTime(int64_t epoch_secs, int& hour, int& dow, int& month) {
    std::time_t t = static_cast<std::time_t>(epoch_secs);
    std::tm* gm = std::gmtime(&t);
    if (!gm) {
        hour = 0;
        dow = 0;
        month = 0;
        return;
    }
    hour = gm->tm_hour;  // 0-23
    dow = gm->tm_wday;   // 0-6, Sunday=0
    month = gm->tm_mon;  // 0-11
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

CeceClock::CeceClock(const std::string& start_time_iso8601, const std::string& end_time_iso8601, int base_timestep_secs,
                     const std::vector<ClockComponent>& components)
    : base_timestep_secs_(base_timestep_secs), elapsed_seconds_(0) {
    // Parse time boundaries
    start_epoch_secs_ = ParseISO8601(start_time_iso8601);
    end_epoch_secs_ = ParseISO8601(end_time_iso8601);

    // Validate: start_time < end_time
    if (start_epoch_secs_ >= end_epoch_secs_) {
        throw std::invalid_argument("start_time must be earlier than end_time. Got start=\"" + start_time_iso8601 + "\", end=\"" + end_time_iso8601 +
                                    "\".");
    }

    // Validate: base_timestep > 0
    if (base_timestep_secs_ <= 0) {
        throw std::invalid_argument("base_timestep_secs must be a positive integer. Got " + std::to_string(base_timestep_secs_) + ".");
    }

    // Validate each component's refresh interval
    for (const auto& comp : components) {
        if (comp.refresh_interval_secs <= 0) {
            throw std::invalid_argument("Component \"" + comp.name + "\" has refresh_interval_secs=" + std::to_string(comp.refresh_interval_secs) +
                                        ", which is not a positive integer.");
        }
        if (comp.refresh_interval_secs % base_timestep_secs_ != 0) {
            throw std::invalid_argument("Component \"" + comp.name + "\" has refresh_interval_secs=" + std::to_string(comp.refresh_interval_secs) +
                                        ", which is not an integer multiple of base_timestep_secs=" + std::to_string(base_timestep_secs_) + ".");
        }
        components_.push_back(ComponentState{comp, -1});
    }

    // Conflict detection placeholder:
    // The ClockComponent struct does not carry export field information,
    // so full conflict detection (same export field, different intervals)
    // cannot be performed here. When export field metadata is added to
    // ClockComponent, iterate physics scheme pairs and warn via std::cerr
    // if two schemes share an export field but have different intervals.
}

// ---------------------------------------------------------------------------
// Advance
// ---------------------------------------------------------------------------

StepResult CeceClock::Advance() {
    StepResult result;

    // If already complete, return immediately with empty due list
    if (IsComplete()) {
        result.elapsed_seconds = static_cast<int>(elapsed_seconds_);
        result.simulation_complete = true;
        DecomposeTime(start_epoch_secs_ + elapsed_seconds_, result.hour_of_day, result.day_of_week, result.month);
        return result;
    }

    // Advance by one base timestep
    elapsed_seconds_ += base_timestep_secs_;

    result.elapsed_seconds = static_cast<int>(elapsed_seconds_);
    result.simulation_complete = IsComplete();

    // Calendar decomposition from absolute time
    DecomposeTime(start_epoch_secs_ + elapsed_seconds_, result.hour_of_day, result.day_of_week, result.month);

    bool is_first_step = (elapsed_seconds_ == base_timestep_secs_);

    // Collect due components: non-stacking first, stacking last
    std::vector<const ClockComponent*> non_stacking;
    std::vector<const ClockComponent*> stacking;

    for (auto& cs : components_) {
        bool due = false;

        if (is_first_step) {
            // First-step guarantee: all components are due
            due = true;
        } else {
            // Component is due if elapsed_seconds is a multiple of its interval
            due = (elapsed_seconds_ % cs.component.refresh_interval_secs == 0);
        }

        if (due) {
            if (cs.component.type == ComponentType::kStackingEngine) {
                stacking.push_back(&cs.component);
            } else {
                non_stacking.push_back(&cs.component);
            }
            cs.last_executed_at = elapsed_seconds_;
        }
    }

    // Stacking engine always appears last
    result.due_components = std::move(non_stacking);
    result.due_components.insert(result.due_components.end(), stacking.begin(), stacking.end());

    return result;
}

// ---------------------------------------------------------------------------
// IsComplete
// ---------------------------------------------------------------------------

bool CeceClock::IsComplete() const {
    return elapsed_seconds_ >= (end_epoch_secs_ - start_epoch_secs_);
}

// ---------------------------------------------------------------------------
// ElapsedSeconds
// ---------------------------------------------------------------------------

int64_t CeceClock::ElapsedSeconds() const {
    return elapsed_seconds_;
}

// ---------------------------------------------------------------------------
// BaseTimestep
// ---------------------------------------------------------------------------

int CeceClock::BaseTimestep() const {
    return base_timestep_secs_;
}

}  // namespace cece
