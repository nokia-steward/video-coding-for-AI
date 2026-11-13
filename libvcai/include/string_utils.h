// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <string>

namespace sf {
    /**
     * @brief Trims leading and trailing whitespace from a string.
     * @param s The string to trim.
     * @return The trimmed string.
     */
    [[nodiscard]] inline std::string trim(const std::string &s) {
        const auto start = s.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) {
            return "";
        }
        const auto end = s.find_last_not_of(" \t\n\r");
        return s.substr(start, end - start + 1);
    }
} // namespace sf
