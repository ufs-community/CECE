/**
 * @file cece_fatal.hpp
 * @brief Fatal-error reporting that is always captured in the run log.
 *
 * The standalone driver redirects std::cout to a null sink on non-root MPI
 * ranks to keep the log readable. Fatal errors, however, must never be lost:
 * they need to appear in the log (stdout) on every rank, regardless of that
 * suppression, and also on stderr.
 *
 * This helper stashes the process's *real* stdout stream buffer (captured
 * before any redirection) so fatal messages can always be written to it.
 */

#ifndef CECE_FATAL_HPP
#define CECE_FATAL_HPP

#include <iostream>
#include <ostream>
#include <string>

namespace cece {

/**
 * @brief Accessor for the process's real stdout stream buffer.
 *
 * Defaults to the current std::cout buffer on first use. Call
 * SetRealStdoutBuf() early in startup (before any per-rank redirection of
 * std::cout) to guarantee it points at the genuine stdout.
 */
inline std::streambuf*& RealStdoutBuf() {
    static std::streambuf* buf = std::cout.rdbuf();
    return buf;
}

/**
 * @brief Record the real stdout buffer before std::cout is redirected.
 */
inline void SetRealStdoutBuf(std::streambuf* buf) {
    RealStdoutBuf() = buf;
}

/**
 * @brief Emit a fatal-error message to the real stdout (log) and to stderr.
 *
 * Writes on every rank, bypassing any std::cout suppression, so fatal errors
 * are always captured in the run log.
 */
inline void LogFatal(const std::string& message) {
    std::ostream log_out(RealStdoutBuf());
    log_out << message << std::endl;
    std::cerr << message << std::endl;
}

}  // namespace cece

#endif  // CECE_FATAL_HPP
