// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "char_mapping.h"

namespace sf {
    /**
     * @brief Maps a plain character to a drawable bitmap-character identifier.
     * @param c Input character.
     * @return Mapped drawable character, or std::nullopt when unsupported.
     */
    std::optional<drawable_character> char_to_drawable_character(const char c) {
        if (c >= '0' && c <= '9') {
            return static_cast<drawable_character>(static_cast<int>(drawable_character::digit_0) + (c - '0'));
        }
        if (c == ':') {
            return drawable_character::colon;
        }
        if (c == '.') {
            return drawable_character::dot;
        }
        if (c == 'k' || c == 'K') {
            return drawable_character::k_lowercase;
        }
        if (c == 'b' || c == 'B') {
            return drawable_character::b_lowercase;
        }
        if (c == 'p' || c == 'P') {
            return drawable_character::p_lowercase;
        }
        if (c == 's' || c == 'S') {
            return drawable_character::s_lowercase;
        }
        if (c == 'm' || c == 'M') {
            return drawable_character::m_lowercase;
        }
        if (c == 'f' || c == 'F') {
            return drawable_character::f_lowercase;
        }
        if (c == 'd' || c == 'D') {
            return drawable_character::d_lowercase;
        }
        return std::nullopt;
    }
} // namespace sf
