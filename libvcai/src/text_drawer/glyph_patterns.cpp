// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "glyph_patterns.h"

namespace sf {
    namespace {
        constexpr bool DIGIT_PATTERNS[10][TEXT_CHAR_HEIGHT][TEXT_CHAR_WIDTH] = {
                // 0
                {{false, true, true, true, true, true, true, false},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {false, true, true, true, true, true, true, false}},
                // 1
                {{false, false, false, true, true, false, false, false},
                 {false, false, true, true, true, false, false, false},
                 {false, true, true, true, true, false, false, false},
                 {false, false, false, true, true, false, false, false},
                 {false, false, false, true, true, false, false, false},
                 {false, false, false, true, true, false, false, false},
                 {false, false, false, true, true, false, false, false},
                 {false, false, false, true, true, false, false, false},
                 {false, false, false, true, true, false, false, false},
                 {false, false, false, true, true, false, false, false},
                 {false, false, false, true, true, false, false, false},
                 {false, true, true, true, true, true, true, false}},
                // 2
                {{false, true, true, true, true, true, true, false},
                 {true, true, false, false, false, false, true, true},
                 {false, false, false, false, false, false, true, true},
                 {false, false, false, false, false, true, true, false},
                 {false, false, false, false, true, true, false, false},
                 {false, false, false, true, true, false, false, false},
                 {false, false, true, true, false, false, false, false},
                 {false, true, true, false, false, false, false, false},
                 {true, true, false, false, false, false, false, false},
                 {true, true, false, false, false, false, false, false},
                 {true, true, false, false, false, false, false, false},
                 {true, true, true, true, true, true, true, true}},
                // 3
                {{false, true, true, true, true, true, true, false},
                 {true, true, false, false, false, false, true, true},
                 {false, false, false, false, false, false, true, true},
                 {false, false, false, false, false, false, true, true},
                 {false, false, false, false, false, true, true, false},
                 {false, true, true, true, true, true, true, false},
                 {false, false, false, false, false, true, true, false},
                 {false, false, false, false, false, false, true, true},
                 {false, false, false, false, false, false, true, true},
                 {false, false, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {false, true, true, true, true, true, true, false}},
                // 4
                {{false, false, false, false, true, true, false, false},
                 {false, false, false, true, true, true, false, false},
                 {false, false, true, true, true, true, false, false},
                 {false, true, true, false, true, true, false, false},
                 {true, true, false, false, true, true, false, false},
                 {true, true, false, false, true, true, false, false},
                 {true, true, true, true, true, true, true, true},
                 {false, false, false, false, true, true, false, false},
                 {false, false, false, false, true, true, false, false},
                 {false, false, false, false, true, true, false, false},
                 {false, false, false, false, true, true, false, false},
                 {false, false, false, false, true, true, false, false}},
                // 5
                {{true, true, true, true, true, true, true, true},
                 {true, true, false, false, false, false, false, false},
                 {true, true, false, false, false, false, false, false},
                 {true, true, false, false, false, false, false, false},
                 {true, true, true, true, true, true, true, false},
                 {false, false, false, false, false, false, true, true},
                 {false, false, false, false, false, false, true, true},
                 {false, false, false, false, false, false, true, true},
                 {false, false, false, false, false, false, true, true},
                 {false, false, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {false, true, true, true, true, true, true, false}},
                // 6
                {{false, true, true, true, true, true, true, false},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, false, false},
                 {true, true, false, false, false, false, false, false},
                 {true, true, true, true, true, true, true, false},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {false, true, true, true, true, true, true, false}},
                // 7
                {{true, true, true, true, true, true, true, true},
                 {false, false, false, false, false, false, true, true},
                 {false, false, false, false, false, true, true, false},
                 {false, false, false, false, true, true, false, false},
                 {false, false, false, true, true, false, false, false},
                 {false, false, true, true, false, false, false, false},
                 {false, true, true, false, false, false, false, false},
                 {true, true, false, false, false, false, false, false},
                 {true, true, false, false, false, false, false, false},
                 {true, true, false, false, false, false, false, false},
                 {true, true, false, false, false, false, false, false},
                 {true, true, false, false, false, false, false, false}},
                // 8
                {{false, true, true, true, true, true, true, false},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {false, true, true, true, true, true, true, false},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {false, true, true, true, true, true, true, false}},
                // 9
                {{false, true, true, true, true, true, true, false},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {false, true, true, true, true, true, true, true},
                 {false, false, false, false, false, false, true, true},
                 {false, false, false, false, false, false, true, true},
                 {false, false, false, false, false, false, true, true},
                 {true, true, false, false, false, false, true, true},
                 {false, true, true, true, true, true, true, false}}};

        constexpr bool COLON_PATTERN[TEXT_CHAR_HEIGHT][TEXT_CHAR_WIDTH] = {
                {false, false, false, false, false, false, false, false},
                {false, false, false, false, false, false, false, false},
                {false, false, false, false, false, false, false, false},
                {false, false, true, true, true, true, false, false},
                {false, false, true, true, true, true, false, false},
                {false, false, false, false, false, false, false, false},
                {false, false, false, false, false, false, false, false},
                {false, false, false, false, false, false, false, false},
                {false, false, true, true, true, true, false, false},
                {false, false, true, true, true, true, false, false},
                {false, false, false, false, false, false, false, false},
                {false, false, false, false, false, false, false, false}};

        constexpr bool DOT_PATTERN[TEXT_CHAR_HEIGHT][TEXT_CHAR_WIDTH] = {
                {false, false, false, false, false, false, false, false},
                {false, false, false, false, false, false, false, false},
                {false, false, false, false, false, false, false, false},
                {false, false, false, false, false, false, false, false},
                {false, false, false, false, false, false, false, false},
                {false, false, false, false, false, false, false, false},
                {false, false, false, false, false, false, false, false},
                {false, false, false, false, false, false, false, false},
                {false, false, false, false, false, false, false, false},
                {false, false, true, true, true, true, false, false},
                {false, false, true, true, true, true, false, false},
                {false, false, false, false, false, false, false, false}};

        constexpr bool M_PATTERN[TEXT_CHAR_HEIGHT][TEXT_CHAR_WIDTH] = {
                {true, true, false, false, false, false, true, true},
                {true, true, true, false, false, true, true, true},
                {true, true, true, true, true, true, true, true},
                {true, true, false, true, true, false, true, true},
                {true, true, false, false, false, false, true, true},
                {true, true, false, false, false, false, true, true},
                {true, true, false, false, false, false, true, true},
                {true, true, false, false, false, false, true, true},
                {true, true, false, false, false, false, true, true},
                {true, true, false, false, false, false, true, true},
                {true, true, false, false, false, false, true, true},
                {false, false, false, false, false, false, false, false}};

        constexpr bool K_LOWERCASE_PATTERN[TEXT_CHAR_HEIGHT][TEXT_CHAR_WIDTH] = {
                {true, true, false, false, false, false, false, false},
                {true, true, false, false, false, false, false, false},
                {true, true, false, false, false, false, true, true},
                {true, true, false, false, false, true, true, false},
                {true, true, false, false, true, true, false, false},
                {true, true, false, true, true, false, false, false},
                {true, true, true, true, false, false, false, false},
                {true, true, false, true, true, false, false, false},
                {true, true, false, false, true, true, false, false},
                {true, true, false, false, false, true, true, false},
                {true, true, false, false, false, false, true, true},
                {true, true, false, false, false, false, false, false}};

        constexpr bool B_LOWERCASE_PATTERN[TEXT_CHAR_HEIGHT][TEXT_CHAR_WIDTH] = {
                {true, true, false, false, false, false, false, false},
                {true, true, false, false, false, false, false, false},
                {true, true, false, false, false, false, false, false},
                {true, true, false, false, false, false, false, false},
                {true, true, false, false, false, false, false, false},
                {true, true, true, true, true, true, false, false},
                {true, true, false, false, false, false, true, false},
                {true, true, false, false, false, false, true, false},
                {true, true, false, false, false, false, true, false},
                {true, true, false, false, false, false, true, false},
                {true, true, false, false, false, false, true, false},
                {true, true, true, true, true, true, false, false}};

        constexpr bool P_LOWERCASE_PATTERN[TEXT_CHAR_HEIGHT][TEXT_CHAR_WIDTH] = {
                {false, false, false, false, false, false, false, false},
                {false, false, false, false, false, false, false, false},
                {true, true, true, true, true, true, false, false},
                {true, true, false, false, false, false, true, true},
                {true, true, false, false, false, false, true, true},
                {true, true, false, false, false, false, true, true},
                {true, true, true, true, true, true, false, false},
                {true, true, false, false, false, false, false, false},
                {true, true, false, false, false, false, false, false},
                {true, true, false, false, false, false, false, false},
                {true, true, false, false, false, false, false, false},
                {true, true, false, false, false, false, false, false}};

        constexpr bool S_LOWERCASE_PATTERN[TEXT_CHAR_HEIGHT][TEXT_CHAR_WIDTH] = {
                {false, false, false, false, false, false, false, false},
                {false, false, false, false, false, false, false, false},
                {false, false, false, false, false, false, false, false},
                {false, false, false, false, false, false, false, false},
                {false, true, true, true, true, true, false, false},
                {true, true, false, false, false, false, true, true},
                {true, true, false, false, false, false, false, false},
                {false, true, true, true, true, true, false, false},
                {false, false, false, false, false, false, true, true},
                {false, false, false, false, false, false, true, true},
                {true, true, false, false, false, false, true, true},
                {false, true, true, true, true, true, false, false}};

        constexpr bool F_LOWERCASE_PATTERN[TEXT_CHAR_HEIGHT][TEXT_CHAR_WIDTH] = {
                {false, false, false, false, false, false, false, false},
                {false, false, false, false, false, false, false, false},
                {false, false, false, false, false, false, false, false},
                {false, true, true, true, true, true, false, false},
                {true, true, false, false, false, false, false, false},
                {true, true, false, false, false, false, false, false},
                {true, true, true, true, true, false, false, false},
                {true, true, false, false, false, false, false, false},
                {true, true, false, false, false, false, false, false},
                {true, true, false, false, false, false, false, false},
                {true, true, false, false, false, false, false, false},
                {true, true, false, false, false, false, false, false}};

        constexpr bool D_LOWERCASE_PATTERN[TEXT_CHAR_HEIGHT][TEXT_CHAR_WIDTH] = {
                {false, false, false, false, false, false, true, true},
                {false, false, false, false, false, false, true, true},
                {false, false, false, false, false, false, true, true},
                {false, false, false, false, false, false, true, true},
                {false, false, false, false, false, false, true, true},
                {false, false, true, true, true, true, true, true},
                {false, true, false, false, false, false, true, true},
                {true, false, false, false, false, false, true, true},
                {true, false, false, false, false, false, true, true},
                {true, false, false, false, false, false, true, true},
                {false, true, false, false, false, false, true, true},
                {false, false, true, true, true, true, true, true}};
    } // namespace

    /**
     * @brief Resolves a drawable character enum to its bitmap glyph table.
     * @param char_type Drawable character type.
     * @return Pointer to immutable glyph pattern, or nullptr if unsupported.
     */
    const bool (*get_char_pattern_from_enum(const drawable_character char_type))[TEXT_CHAR_HEIGHT][TEXT_CHAR_WIDTH] {
        switch (char_type) {
            case drawable_character::digit_0:
                return &DIGIT_PATTERNS[0];
            case drawable_character::digit_1:
                return &DIGIT_PATTERNS[1];
            case drawable_character::digit_2:
                return &DIGIT_PATTERNS[2];
            case drawable_character::digit_3:
                return &DIGIT_PATTERNS[3];
            case drawable_character::digit_4:
                return &DIGIT_PATTERNS[4];
            case drawable_character::digit_5:
                return &DIGIT_PATTERNS[5];
            case drawable_character::digit_6:
                return &DIGIT_PATTERNS[6];
            case drawable_character::digit_7:
                return &DIGIT_PATTERNS[7];
            case drawable_character::digit_8:
                return &DIGIT_PATTERNS[8];
            case drawable_character::digit_9:
                return &DIGIT_PATTERNS[9];
            case drawable_character::colon:
                return &COLON_PATTERN;
            case drawable_character::dot:
                return &DOT_PATTERN;
            case drawable_character::k_lowercase:
                return &K_LOWERCASE_PATTERN;
            case drawable_character::b_lowercase:
                return &B_LOWERCASE_PATTERN;
            case drawable_character::p_lowercase:
                return &P_LOWERCASE_PATTERN;
            case drawable_character::s_lowercase:
                return &S_LOWERCASE_PATTERN;
            case drawable_character::m_lowercase:
                return &M_PATTERN;
            case drawable_character::f_lowercase:
                return &F_LOWERCASE_PATTERN;
            case drawable_character::d_lowercase:
                return &D_LOWERCASE_PATTERN;
            default:
                return nullptr;
        }
    }
} // namespace sf
