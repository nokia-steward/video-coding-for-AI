// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "text_drawer.h"

struct text_drawer_char_mapping_test : ::testing::Test {};

TEST_F(text_drawer_char_mapping_test, maps_digits_and_special_symbols) {
    EXPECT_EQ(sf::char_to_drawable_character('0'), sf::drawable_character::digit_0);
    EXPECT_EQ(sf::char_to_drawable_character('9'), sf::drawable_character::digit_9);
    EXPECT_EQ(sf::char_to_drawable_character(':'), sf::drawable_character::colon);
    EXPECT_EQ(sf::char_to_drawable_character('.'), sf::drawable_character::dot);
}

TEST_F(text_drawer_char_mapping_test, maps_supported_letters_case_insensitive) {
    EXPECT_EQ(sf::char_to_drawable_character('k'), sf::drawable_character::k_lowercase);
    EXPECT_EQ(sf::char_to_drawable_character('K'), sf::drawable_character::k_lowercase);
    EXPECT_EQ(sf::char_to_drawable_character('b'), sf::drawable_character::b_lowercase);
    EXPECT_EQ(sf::char_to_drawable_character('B'), sf::drawable_character::b_lowercase);
    EXPECT_EQ(sf::char_to_drawable_character('p'), sf::drawable_character::p_lowercase);
    EXPECT_EQ(sf::char_to_drawable_character('P'), sf::drawable_character::p_lowercase);
    EXPECT_EQ(sf::char_to_drawable_character('s'), sf::drawable_character::s_lowercase);
    EXPECT_EQ(sf::char_to_drawable_character('S'), sf::drawable_character::s_lowercase);
    EXPECT_EQ(sf::char_to_drawable_character('m'), sf::drawable_character::m_lowercase);
    EXPECT_EQ(sf::char_to_drawable_character('M'), sf::drawable_character::m_lowercase);
    EXPECT_EQ(sf::char_to_drawable_character('f'), sf::drawable_character::f_lowercase);
    EXPECT_EQ(sf::char_to_drawable_character('F'), sf::drawable_character::f_lowercase);
    EXPECT_EQ(sf::char_to_drawable_character('d'), sf::drawable_character::d_lowercase);
    EXPECT_EQ(sf::char_to_drawable_character('D'), sf::drawable_character::d_lowercase);
}

TEST_F(text_drawer_char_mapping_test, rejects_unsupported_characters) {
    EXPECT_FALSE(sf::char_to_drawable_character('x').has_value());
    EXPECT_FALSE(sf::char_to_drawable_character(' ').has_value());
    EXPECT_FALSE(sf::char_to_drawable_character('_').has_value());
}
