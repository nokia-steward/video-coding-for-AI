// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <array>
#include <gtest/gtest.h>

#include "../../libvcai/src/text_drawer/glyph_patterns.h"

struct glyph_patterns_test : ::testing::Test {};

TEST_F(glyph_patterns_test, returns_pattern_for_each_supported_drawable_character) {
    const std::array<sf::drawable_character, 19> supported = {
            sf::drawable_character::digit_0,     sf::drawable_character::digit_1,
            sf::drawable_character::digit_2,     sf::drawable_character::digit_3,
            sf::drawable_character::digit_4,     sf::drawable_character::digit_5,
            sf::drawable_character::digit_6,     sf::drawable_character::digit_7,
            sf::drawable_character::digit_8,     sf::drawable_character::digit_9,
            sf::drawable_character::colon,       sf::drawable_character::dot,
            sf::drawable_character::k_lowercase, sf::drawable_character::b_lowercase,
            sf::drawable_character::p_lowercase, sf::drawable_character::s_lowercase,
            sf::drawable_character::m_lowercase, sf::drawable_character::f_lowercase,
            sf::drawable_character::d_lowercase,
    };

    for (const auto character: supported) {
        EXPECT_NE(sf::get_char_pattern_from_enum(character), nullptr);
    }
}

TEST_F(glyph_patterns_test, returns_nullptr_for_unknown_drawable_character) {
    const auto unknown_character = static_cast<sf::drawable_character>(255);
    EXPECT_EQ(sf::get_char_pattern_from_enum(unknown_character), nullptr);
}

TEST_F(glyph_patterns_test, digit_zero_pattern_contains_expected_outline_points) {
    const auto *pattern = sf::get_char_pattern_from_enum(sf::drawable_character::digit_0);
    ASSERT_NE(pattern, nullptr);

    EXPECT_FALSE((*pattern)[0][0]);
    EXPECT_TRUE((*pattern)[0][1]);
    EXPECT_TRUE((*pattern)[0][6]);
    EXPECT_FALSE((*pattern)[0][7]);
    EXPECT_TRUE((*pattern)[11][1]);
    EXPECT_TRUE((*pattern)[11][6]);
}

TEST_F(glyph_patterns_test, colon_pattern_has_two_dot_clusters) {
    const auto *pattern = sf::get_char_pattern_from_enum(sf::drawable_character::colon);
    ASSERT_NE(pattern, nullptr);

    EXPECT_TRUE((*pattern)[3][2]);
    EXPECT_TRUE((*pattern)[4][5]);
    EXPECT_TRUE((*pattern)[8][2]);
    EXPECT_TRUE((*pattern)[9][5]);
    EXPECT_FALSE((*pattern)[0][0]);
}
