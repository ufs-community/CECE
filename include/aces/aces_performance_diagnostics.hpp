/**
 * @file aces_performance_diagnostics.hpp
 * @brief Performance timing and memory diagnostics for ACES
 */

#ifndef ACES_PERFORMANCE_DIAGNOSTICS_HPP
#define ACES_PERFORMANCE_DIAGNOSTICS_HPP

#include <chrono>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace aces {

/**
 * @struct TimingData
 * @brief Timing information for a component
 */
struct TimingData {
    std::string name;            ///< Component name
    double total_time_ms = 0.0;  ///< Total execution time in milliseconds
    int call_count = 0;          ///< Number of times called
    double min_time_ms = 1e9;    ///< Minimum execution time
    double max_time_ms = 0.0;    ///< Maximum execution time

    /**
     * @brief Get average execution time
     * @return Average time in milliseconds
     */
    double GetAverageTime() const {
        return call_count > 0 ? total_time_ms / call_count : 0.0;
    }
};

/**
 * @struct MemoryData
 * @brief Memory usage information
 */
struct MemoryData {
    std::string name;                    ///< Component name
    long long peak_memory_bytes = 0;     ///< Peak memory usage in bytes
    long long current_memory_bytes = 0;  ///< Current memory usage in bytes
};

/**
 * @class PerformanceDiagnostics
 * @brief Singleton for collecting performance timing and memory diagnostics
 */
class PerformanceDiagnostics {
   public:
    /**
     * @brief Get the singleton instance
     * @return Reference to the diagnostics instance
     */
    static PerformanceDiagnostics& GetInstance() {
        static PerformanceDiagnostics instance;
        return instance;
    }

    /**
     * @brief Start timing a component
     * @param name Component name
     */
    void StartTimer(const std::string& name) {
        timers_[name] = std::chrono::high_resolution_clock::now();
    }

    /**
     * @brief Stop timing a component and record the time
     * @param name Component name
     */
    void StopTimer(const std::string& name) {
        auto end = std::chrono::high_resolution_clock::now();
        auto it = timers_.find(name);
        if (it != timers_.end()) {
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - it->second);
            double elapsed_ms = static_cast<double>(duration.count());

            auto& timing = timing_data_[name];
            timing.name = name;
            timing.total_time_ms += elapsed_ms;
            timing.call_count++;
            timing.min_time_ms = std::min(timing.min_time_ms, elapsed_ms);
            timing.max_time_ms = std::max(timing.max_time_ms, elapsed_ms);

            timers_.erase(it);
        }
    }

    /**
     * @brief Record memory usage for a component
     * @param name Component name
     * @param bytes Memory usage in bytes
     */
    void RecordMemory(const std::string& name, long long bytes) {
        auto& mem = memory_data_[name];
        mem.name = name;
        mem.current_memory_bytes = bytes;
        mem.peak_memory_bytes = std::max(mem.peak_memory_bytes, bytes);
    }

    /**
     * @brief Get timing data for a component
     * @param name Component name
     * @return Timing data
     */
    const TimingData& GetTimingData(const std::string& name) const {
        auto it = timing_data_.find(name);
        if (it != timing_data_.end()) {
            return it->second;
        }
        static TimingData empty;
        return empty;
    }

    /**
     * @brief Get memory data for a component
     * @param name Component name
     * @return Memory data
     */
    const MemoryData& GetMemoryData(const std::string& name) const {
        auto it = memory_data_.find(name);
        if (it != memory_data_.end()) {
            return it->second;
        }
        static MemoryData empty;
        return empty;
    }

    /**
     * @brief Print timing summary
     */
    void PrintTimingSummary() const {
        std::cout << "\n=== ACES Performance Timing Summary ===" << std::endl;
        std::cout << std::string(70, '-') << std::endl;
        std::cout
            << "Component                    | Total (ms) | Calls | Avg (ms) | Min (ms) | Max (ms)"
            << std::endl;
        std::cout << std::string(70, '-') << std::endl;

        double total_time = 0.0;
        for (const auto& [name, timing] : timing_data_) {
            total_time += timing.total_time_ms;
            printf("%-28s | %10.2f | %5d | %8.2f | %8.2f | %8.2f\n", name.c_str(),
                   timing.total_time_ms, timing.call_count, timing.GetAverageTime(),
                   timing.min_time_ms, timing.max_time_ms);
        }

        std::cout << std::string(70, '-') << std::endl;
        printf("%-28s | %10.2f |\n", "TOTAL", total_time);
        std::cout << std::string(70, '-') << std::endl;
    }

    /**
     * @brief Print memory summary
     */
    void PrintMemorySummary() const {
        std::cout << "\n=== ACES Memory Usage Summary ===" << std::endl;
        std::cout << std::string(60, '-') << std::endl;
        std::cout << "Component                    | Current (MB) | Peak (MB)" << std::endl;
        std::cout << std::string(60, '-') << std::endl;

        long long total_current = 0;
        long long total_peak = 0;
        for (const auto& [name, mem] : memory_data_) {
            total_current += mem.current_memory_bytes;
            total_peak += mem.peak_memory_bytes;
            printf("%-28s | %12.2f | %8.2f\n", name.c_str(),
                   mem.current_memory_bytes / 1024.0 / 1024.0,
                   mem.peak_memory_bytes / 1024.0 / 1024.0);
        }

        std::cout << std::string(60, '-') << std::endl;
        printf("%-28s | %12.2f | %8.2f\n", "TOTAL", total_current / 1024.0 / 1024.0,
               total_peak / 1024.0 / 1024.0);
        std::cout << std::string(60, '-') << std::endl;
    }

    /**
     * @brief Clear all timing and memory data
     */
    void Clear() {
        timing_data_.clear();
        memory_data_.clear();
        timers_.clear();
    }

   private:
    PerformanceDiagnostics() = default;
    ~PerformanceDiagnostics() = default;

    // Delete copy and move constructors
    PerformanceDiagnostics(const PerformanceDiagnostics&) = delete;
    PerformanceDiagnostics& operator=(const PerformanceDiagnostics&) = delete;
    PerformanceDiagnostics(PerformanceDiagnostics&&) = delete;
    PerformanceDiagnostics& operator=(PerformanceDiagnostics&&) = delete;

    std::unordered_map<std::string, TimingData> timing_data_;
    std::unordered_map<std::string, MemoryData> memory_data_;
    std::unordered_map<std::string, std::chrono::high_resolution_clock::time_point> timers_;
};

/**
 * @class ScopedTimer
 * @brief RAII timer for automatic timing of code blocks
 */
class ScopedTimer {
   public:
    /**
     * @brief Create a scoped timer
     * @param name Component name
     */
    explicit ScopedTimer(const std::string& name) : name_(name) {
        PerformanceDiagnostics::GetInstance().StartTimer(name_);
    }

    /**
     * @brief Destructor stops the timer
     */
    ~ScopedTimer() {
        PerformanceDiagnostics::GetInstance().StopTimer(name_);
    }

   private:
    std::string name_;
};

}  // namespace aces

// Convenience macros for timing
#define ACES_TIMER_START(name) aces::PerformanceDiagnostics::GetInstance().StartTimer(name)

#define ACES_TIMER_STOP(name) aces::PerformanceDiagnostics::GetInstance().StopTimer(name)

#define ACES_SCOPED_TIMER(name) aces::ScopedTimer _timer_##__LINE__(name)

#define ACES_RECORD_MEMORY(name, bytes) \
    aces::PerformanceDiagnostics::GetInstance().RecordMemory(name, bytes)

#endif  // ACES_PERFORMANCE_DIAGNOSTICS_HPP
