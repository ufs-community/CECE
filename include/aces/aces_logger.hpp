/**
 * @file aces_logger.hpp
 * @brief Logging system for ACES with configurable log levels
 */

#ifndef ACES_LOGGER_HPP
#define ACES_LOGGER_HPP

#include <iostream>
#include <sstream>
#include <string>
#include <memory>
#include <ctime>
#include <iomanip>

namespace aces {

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
 * @class AcesLogger
 * @brief Singleton logger for ACES with configurable log levels
 *
 * Usage:
 * ```cpp
 * auto& logger = AcesLogger::GetInstance();
 * logger.SetLogLevel(LogLevel::DEBUG);
 * logger.LogError("Error message");
 * logger.LogWarning("Warning message");
 * logger.LogInfo("Info message");
 * logger.LogDebug("Debug message");
 * ```
 */
class AcesLogger {
 public:
    /**
     * @brief Get the singleton logger instance
     * @return Reference to the logger
     */
    static AcesLogger& GetInstance() {
        static AcesLogger instance;
        return instance;
    }

    /**
     * @brief Set the current log level
     * @param level The log level to set
     */
    void SetLogLevel(LogLevel level) {
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
     * @param message The error message
     * @param file Source file name (optional)
     * @param line Source line number (optional)
     */
    void LogError(const std::string& message, const std::string& file = "", int line = 0) {
        if (log_level_ >= LogLevel::ERROR) {
            LogMessage("ERROR", message, file, line, std::cerr);
        }
    }

    /**
     * @brief Log a warning message
     * @param message The warning message
     * @param file Source file name (optional)
     * @param line Source line number (optional)
     */
    void LogWarning(const std::string& message, const std::string& file = "", int line = 0) {
        if (log_level_ >= LogLevel::WARNING) {
            LogMessage("WARNING", message, file, line, std::cerr);
        }
    }

    /**
     * @brief Log an info message
     * @param message The info message
     * @param file Source file name (optional)
     * @param line Source line number (optional)
     */
    void LogInfo(const std::string& message, const std::string& file = "", int line = 0) {
        if (log_level_ >= LogLevel::INFO) {
            LogMessage("INFO", message, file, line, std::cout);
        }
    }

    /**
     * @brief Log a debug message
     * @param message The debug message
     * @param file Source file name (optional)
     * @param line Source line number (optional)
     */
    void LogDebug(const std::string& message, const std::string& file = "", int line = 0) {
        if (log_level_ >= LogLevel::DEBUG) {
            LogMessage("DEBUG", message, file, line, std::cout);
        }
    }

 private:
    LogLevel log_level_ = LogLevel::INFO;

    AcesLogger() = default;
    ~AcesLogger() = default;

    // Delete copy and move constructors
    AcesLogger(const AcesLogger&) = delete;
    AcesLogger& operator=(const AcesLogger&) = delete;
    AcesLogger(AcesLogger&&) = delete;
    AcesLogger& operator=(AcesLogger&&) = delete;

    /**
     * @brief Internal method to log a message with formatting
     */
    void LogMessage(const std::string& level, const std::string& message,
                   const std::string& file, int line, std::ostream& stream) {
        // Get current time
        auto now = std::time(nullptr);
        auto tm = *std::localtime(&now);

        // Format: [TIMESTAMP] [LEVEL] [FILE:LINE] MESSAGE
        stream << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] "
               << "[" << level << "] ";

        if (!file.empty() && line > 0) {
            stream << "[" << file << ":" << line << "] ";
        }

        stream << message << std::endl;
    }
};

}  // namespace aces

// Convenience macros for logging
#define ACES_LOG_ERROR(msg) \
    aces::AcesLogger::GetInstance().LogError(msg, __FILE__, __LINE__)

#define ACES_LOG_WARNING(msg) \
    aces::AcesLogger::GetInstance().LogWarning(msg, __FILE__, __LINE__)

#define ACES_LOG_INFO(msg) \
    aces::AcesLogger::GetInstance().LogInfo(msg, __FILE__, __LINE__)

#define ACES_LOG_DEBUG(msg) \
    aces::AcesLogger::GetInstance().LogDebug(msg, __FILE__, __LINE__)

#endif  // ACES_LOGGER_HPP
