#ifndef CECE_STRING_UTILS_HPP
#define CECE_STRING_UTILS_HPP

/**
 * @file cece_string_utils.hpp
 * @brief Small string helpers shared across CECE.
 */

#include <algorithm>
#include <cctype>
#include <string>

namespace cece {

/// ASCII lower-case, for case-insensitive comparison of config and CF metadata
/// keywords. Locale-independent by design.
inline std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return s;
}

}  // namespace cece

#endif  // CECE_STRING_UTILS_HPP
