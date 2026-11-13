// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>
#include <stdexcept>
#include <string>

namespace sf {
    /**
     * @file error.h
     * @brief Provides simple error-checking helper functions.
     */

    /**
     * @brief Checks a boolean condition and throws a std::runtime_error if it is false.
     * @param condition The condition to check.
     * @param message The error message to use in the exception.
     */
    inline void check_error(const bool condition, const std::string &message = "Unknown Error!") {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    /**
     * @brief Checks a GError pointer and throws a std::runtime_error if it is not null.
     * @param error The GError pointer to check.
     */
    inline void check_gst_error(const GError *error) {
        if (error) {
            throw std::runtime_error(error->message);
        }
    }
} // namespace sf
