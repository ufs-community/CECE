/**
 * @file cece_logger.hpp
 * @brief Logging system for CECE with configurable log levels backed by HELM::LOGS
 */

#ifndef CECE_LOGGER_HPP
#define CECE_LOGGER_HPP

#include <mpi.h>

#include <iostream>
#include <logs/logs.hpp>
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
 * @brief Thread-safe singleton logger for CECE backed by helm/libs/logs
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
        if (level == LogLevel::ERROR) {
            logger_.set_threshold(logs::Severity_Level::ERROR);
        } else if (level == LogLevel::WARNING) {
            logger_.set_threshold(logs::Severity_Level::WARNING);
        } else if (level == LogLevel::INFO) {
            logger_.set_threshold(logs::Severity_Level::INFO);
        } else if (level == LogLevel::DEBUG) {
            logger_.set_threshold(logs::Severity_Level::DEBUG);
        }
    }

    /**
     * @brief Get the current log level
     * @return The current log level
     */
    LogLevel GetLogLevel() const {
        auto thresh = logger_.threshold();
        if (thresh == logs::Severity_Level::ERROR || thresh == logs::Severity_Level::FATAL) {
            return LogLevel::ERROR;
        } else if (thresh == logs::Severity_Level::WARNING) {
            return LogLevel::WARNING;
        } else if (thresh == logs::Severity_Level::INFO) {
            return LogLevel::INFO;
        } else {
            return LogLevel::DEBUG;
        }
    }

    /**
     * @brief Configure the MPI communicator for the underlying HELM LOGS logger
     */
    void ConfigureCommunicator(MPI_Comm comm) {
        logger_.configure_communicator(comm);
    }

    /**
     * @brief Log an error message
     */
    void LogError(const std::string& message, const std::string& file = "", int line = 0) {
        EnsureCommunicatorConfigured();
        logs::Submit_Options opts;
        if (!file.empty() && line > 0) {
            opts.location = logs::Source_Location{file, line, ""};
        }
        logger_.log(logs::Severity_Level::ERROR, message, opts);
    }

    /**
     * @brief Log a warning message
     */
    void LogWarning(const std::string& message, const std::string& file = "", int line = 0) {
        EnsureCommunicatorConfigured();
        if (logger_.rank() > 0) return;  // Only Rank 0 logs warnings
        logs::Submit_Options opts;
        if (!file.empty() && line > 0) {
            opts.location = logs::Source_Location{file, line, ""};
        }
        logger_.log(logs::Severity_Level::WARNING, message, opts);
    }

    /**
     * @brief Log an info message
     */
    void LogInfo(const std::string& message, const std::string& file = "", int line = 0) {
        EnsureCommunicatorConfigured();
        if (logger_.rank() > 0) return;  // Only Rank 0 logs info messages
        logs::Submit_Options opts;
        if (!file.empty() && line > 0) {
            opts.location = logs::Source_Location{file, line, ""};
        }
        logger_.log(logs::Severity_Level::INFO, message, opts);
    }

    /**
     * @brief Log a debug message (logged across all ranks with rank attribution)
     */
    void LogDebug(const std::string& message, const std::string& file = "", int line = 0) {
        EnsureCommunicatorConfigured();
        logs::Submit_Options opts;
        if (!file.empty() && line > 0) {
            opts.location = logs::Source_Location{file, line, ""};
        }
        logger_.log(logs::Severity_Level::DEBUG, message, opts);
    }

   private:
    logs::Logger logger_;

    CeceLogger() {
        // By default, add a stdout sink so user-facing logs are visible in typical stdout
        logger_.add_sink(logs::Sink(std::cout));
        // Check if MPI is already initialized and auto-configure if so
        EnsureCommunicatorConfigured();
    }
    ~CeceLogger() = default;

    // Delete copy and move constructors
    CeceLogger(const CeceLogger&) = delete;
    CeceLogger& operator=(const CeceLogger&) = delete;
    CeceLogger(CeceLogger&&) = delete;
    CeceLogger& operator=(CeceLogger&&) = delete;

    void EnsureCommunicatorConfigured() {
        if (logger_.rank() == -1) {
            int mpi_initialized = 0;
            MPI_Initialized(&mpi_initialized);
            if (mpi_initialized) {
                logger_.configure_communicator(MPI_COMM_WORLD);
            }
        }
    }
};

}  // namespace cece

// Convenience macros for logging
#define CECE_LOG_ERROR(msg) cece::CeceLogger::GetInstance().LogError(msg, __FILE__, __LINE__)
#define CECE_LOG_WARNING(msg) cece::CeceLogger::GetInstance().LogWarning(msg, __FILE__, __LINE__)
#define CECE_LOG_INFO(msg) cece::CeceLogger::GetInstance().LogInfo(msg, __FILE__, __LINE__)
#define CECE_LOG_DEBUG(msg) cece::CeceLogger::GetInstance().LogDebug(msg, __FILE__, __LINE__)

#endif  // CECE_LOGGER_HPP
