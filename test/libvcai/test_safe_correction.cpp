// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>
#include <vector>
#include "libvcai/include/area_copy.h"

/**
 * @file test_safe_correction.cpp
 * @brief Tests to verify that safe even dimension correction compresses area to stay within frame bounds.
 */

class SafeCorrectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // No setup needed
    }

    void TearDown() override {
        // No cleanup needed
    }
};

/**
 * @brief Test safe correction behavior that compresses area to stay within bounds
 */
TEST_F(SafeCorrectionTest, SafeCorrectionCompressesArea) {
    const int frame_width = 100;
    const int frame_height = 100;

    // Test cases that demonstrate the safe compression strategy
    std::vector<sf::rectangle> test_rectangles = {
            {11, 11, 21, 21}, // All odd - should compress to (12, 12, 20, 20)
            {10, 10, 21, 21}, // Odd dimensions - should compress to (10, 10, 20, 20)
            {11, 11, 20, 20}, // Odd coordinates - should move to (12, 12, 20, 20)
            {99, 99, 3, 3}, // Near frame edge - should stay in bounds
            {1, 1, 3, 3}, // Small rectangle with odd dimensions
    };

    for (const auto &original_rect: test_rectangles) {
        const sf::rectangle corrected_rect = sf::ensure_even_dimensions(original_rect, frame_width, frame_height);

        // Verify even dimensions
        EXPECT_TRUE(sf::has_even_dimensions(corrected_rect)) << "Corrected rectangle should have even dimensions";

        // Verify bounds safety
        EXPECT_GE(corrected_rect.left, 0) << "X should be non-negative";
        EXPECT_GE(corrected_rect.top, 0) << "Y should be non-negative";
        EXPECT_LE(corrected_rect.left + corrected_rect.width, frame_width) << "Should fit within frame width";
        EXPECT_LE(corrected_rect.top + corrected_rect.height, frame_height) << "Should fit within frame height";

        // Verify compression strategy (coordinates move to even, dimensions compress)
        if (original_rect.left % 2 != 0) {
            EXPECT_EQ(corrected_rect.left % 2, 0) << "X should be even after correction";
            // X should move to nearest even coordinate (up or down depending on bounds)
            EXPECT_TRUE(corrected_rect.left == original_rect.left + 1 || corrected_rect.left == original_rect.left - 1)
                    << "X should move to nearest even coordinate";
        }
        if (original_rect.top % 2 != 0) {
            EXPECT_EQ(corrected_rect.top % 2, 0) << "Y should be even after correction";
            // Y should move to nearest even coordinate (up or down depending on bounds)
            EXPECT_TRUE(corrected_rect.top == original_rect.top + 1 || corrected_rect.top == original_rect.top - 1)
                    << "Y should move to nearest even coordinate";
        }
        if (original_rect.width % 2 != 0) {
            EXPECT_LE(corrected_rect.width, original_rect.width) << "Width should compress for even dimension";
        }
        if (original_rect.height % 2 != 0) {
            EXPECT_LE(corrected_rect.height, original_rect.height) << "Height should compress for even dimension";
        }

        // Verify minimum size
        EXPECT_GE(corrected_rect.width, 2) << "Width should be at least 2";
        EXPECT_GE(corrected_rect.height, 2) << "Height should be at least 2";
    }
}

/**
 * @brief Test edge cases near frame boundaries
 */
TEST_F(SafeCorrectionTest, EdgeCasesNearFrameBoundaries) {
    const int frame_width = 100;
    const int frame_height = 100;

    // Test edge cases
    std::vector<sf::rectangle> edge_cases = {
            {98, 98, 3, 3}, // Very close to bottom-right corner
            {0, 0, 3, 3}, // Top-left corner with odd dimensions
            {99, 0, 3, 3}, // Top-right corner
            {0, 99, 3, 3}, // Bottom-left corner
            {50, 50, 1, 1}, // Single pixel (should become 2x2)
    };

    for (const auto &original_rect: edge_cases) {
        const sf::rectangle corrected_rect = sf::ensure_even_dimensions(original_rect, frame_width, frame_height);

        // Verify even dimensions
        EXPECT_TRUE(sf::has_even_dimensions(corrected_rect)) << "Corrected rectangle should have even dimensions";

        // Verify bounds safety
        EXPECT_GE(corrected_rect.left, 0) << "X should be non-negative";
        EXPECT_GE(corrected_rect.top, 0) << "Y should be non-negative";
        EXPECT_LE(corrected_rect.left + corrected_rect.width, frame_width) << "Should fit within frame width";
        EXPECT_LE(corrected_rect.top + corrected_rect.height, frame_height) << "Should fit within frame height";

        // Verify minimum size
        EXPECT_GE(corrected_rect.width, 2) << "Width should be at least 2";
        EXPECT_GE(corrected_rect.height, 2) << "Height should be at least 2";
    }
}

/**
 * @brief Test the specific example from the user: (11, 11, 21, 21) -> (12, 12, 20, 20)
 */
TEST_F(SafeCorrectionTest, UserExampleCompression) {
    const int frame_width = 100;
    const int frame_height = 100;

    const sf::rectangle original = {11, 11, 21, 21};
    const sf::rectangle expected = {12, 12, 20, 20};

    const sf::rectangle corrected = sf::ensure_even_dimensions(original, frame_width, frame_height);

    // Verify the correction matches the expected behavior
    EXPECT_EQ(corrected.left, expected.left) << "X coordinate should match expected";
    EXPECT_EQ(corrected.top, expected.top) << "Y coordinate should match expected";
    EXPECT_EQ(corrected.width, expected.width) << "Width should match expected";
    EXPECT_EQ(corrected.height, expected.height) << "Height should match expected";

    // Verify even dimensions
    EXPECT_TRUE(sf::has_even_dimensions(corrected)) << "Corrected rectangle should have even dimensions";

    // Verify bounds safety
    EXPECT_LE(corrected.left + corrected.width, frame_width) << "Should fit within frame width";
    EXPECT_LE(corrected.top + corrected.height, frame_height) << "Should fit within frame height";
}
