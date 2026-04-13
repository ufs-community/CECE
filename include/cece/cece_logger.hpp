/**
 * @file cece_logger.hpp
 * @brief Logging system for CECE with configurable log levels
 */

#ifndef CECE_LOGGER_HPP
#define CECE_LOGGER_HPP

#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace cece {

/**
 * @enum LogLevel
 * @brief Enumeration of logging levels
 */
enum class LogLevel {
    ERROR = 0,    ///< Error messages
    WARNING = 1,  ///< Warning messages
    INFO = 2,     ///< Informational messages
    DEBUG = 3     ///< Debug messages
};

/**
 * @class CeceLogger
 * @brief Thread-safe singleton logger for CECE with configurable log levels
 *
 * Usage:
 * ```cpp
 * auto& logger = CeceLogger::GetInstance();
 * logger.SetLogLevel(LogLevel::DEBUG);
 * logger.LogError("Error message");
 * logger.LogWarning("Warning message");
 * logger.LogInfo("Info message");
 * logger.LogDebug("Debug message");
 * ```
 */
class CeceLogger {
   public:
    /**
     * @brief Get the singleton logger instance
     * @return Reference to the logger
     */
    static CeceLogger& GetInstance() {
        static CeceLogger instance;
        return instance;
    }

    /**
     * @brief Set the current log level
     * @param level The log level to set
     */
    void SetLogLevel(LogLevel level) {
        std::lock_guard<std::mutex> lock(mutex_);
        log_level_ = level;
    }

    /**
     * @brief Get the current log level
     * @return The current log level
     */
    LogLevel GetLogLevel() const {
        return log_level_;
    }

    /**
     * @brief Log an error message
     */
    void LogError(const std::string& message, const std::string& file = "", int line = 0) {
        if (log_level_ >= LogLevel::ERROR) {
            LogMessage("ERROR", message, file, line, std::cerr);
        }
    }

    /**
     * @brief Log a warning message
     */
    void LogWarning(const std::string& message, const std::string& file = "", int line = 0) {
        if (log_level_ >= LogLevel::WARNING) {
            LogMessage("WARNING", message, file, line, std::cerr);
        }
    }

    /**
     * @brief Log an info message
     */
    void LogInfo(const std::string& message, const std::string& file = "", int line = 0) {
        if (log_level_ >= LogLevel::INFO) {
            LogMessage("INFO", message, file, line, std::cout);
        }
    }

    /**
     * @brief Log a debug message
     */
    void LogDebug(const std::string& message, const std::string& file = "", int line = 0) {
        if (log_level_ >= LogLevel::DEBUG) {
            LogMessage("DEBUG", message, file, line, std::cout);
        }
    }

   private:
    LogLevel log_level_ = LogLevel::INFO;
    std::mutex mutex_;

    CeceLogger() = default;
    ~CeceLogger() = default;

    // Delete copy and move constructors
    CeceLogger(const CeceLogger&) = delete;
    CeceLogger& operator=(const CeceLogger&) = delete;
    CeceLogger(CeceLogger&&) = delete;
    CeceLogger& operator=(CeceLogger&&) = delete;

    /**
     * @brief Internal method to log a message with formatting.
     * Uses localtime_r for thread safety and a mutex to prevent interleaved output.
     */
    void LogMessage(const std::string& level, const std::string& message, const std::string& file,
                    int line, std::ostream& stream) {
        // Get current time using thread-safe localtime_r
        auto now = std::time(nullptr);
        struct tm tm_buf{};
        localtime_r(&now, &tm_buf);

        // Build the full message in a local buffer to avoid interleaving
        std::ostringstream oss;
        oss << "[" << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << "] "
            << "[" << level << "] ";

        if (!file.empty() && line > 0) {
            oss << "[" << file << ":" << line << "] ";
        }

        oss << message << "\n";

        // Write atomically under lock
        std::lock_guard<std::mutex> lock(mutex_);
        stream << oss.str();
        stream.flush();
    }
};

}  // namespace cece

// Convenience macros for logging
#define CECE_LOG_ERROR(msg) cece::CeceLogger::GetInstance().LogError(msg, __FILE__, __LINE__)

#define CECE_LOG_WARNING(msg) cece::CeceLogger::GetInstance().LogWarning(msg, __FILE__, __LINE__)

#define CECE_LOG_INFO(msg) cece::CeceLogger::GetInstance().LogInfo(msg, __FILE__, __LINE__)

#define CECE_LOG_DEBUG(msg) cece::CeceLogger::GetInstance().LogDebug(msg, __FILE__, __LINE__)

#endif  // CECE_LOGGER_HPP
