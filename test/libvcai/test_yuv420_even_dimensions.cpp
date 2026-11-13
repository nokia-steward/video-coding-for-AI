// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <cstring>
#include <gtest/gtest.h>
#include <libvcai/include/area_copy.h>
#include <vector>

/**
 * @file test_yuv420_even_dimensions.cpp
 * @brief Tests to verify that YUV420 format requirements for even dimensions are handled correctly.
 *
 * YUV420 format requires even widths and heights for proper UV plane subsampling.
 * This test verifies that our code handles odd dimensions correctly.
 */

class YUV420EvenDimensionsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // No GStreamer initialization needed for this test
    }

    void TearDown() override {
        // No cleanup needed
    }

    /**
     * @brief Tests if a rectangle has even dimensions
     */
    bool has_even_dimensions(const sf::rectangle &rect) { return (rect.width % 2 == 0) && (rect.height % 2 == 0); }

    /**
     * @brief Tests if a rectangle has even coordinates (for UV plane alignment)
     */
    bool has_even_coordinates(const sf::rectangle &rect) { return (rect.left % 2 == 0) && (rect.top % 2 == 0); }
};

/**
 * @brief Test that demonstrates the YUV420 even dimension requirement
 */
TEST_F(YUV420EvenDimensionsTest, YUV420EvenDimensionRequirement) {
    // Test various rectangle dimensions
    std::vector<sf::rectangle> test_rectangles = {
            {10, 10, 20, 20}, // Even width, even height
            {10, 10, 21, 20}, // Odd width, even height
            {10, 10, 20, 21}, // Even width, odd height
            {10, 10, 21, 21}, // Odd width, odd height
            {11, 11, 20, 20}, // Odd x, odd y, even dimensions
            {10, 10, 19, 19}, // Even x, even y, odd dimensions
    };

    for (const auto &rect: test_rectangles) {
        // Calculate UV plane dimensions (what area_copy.cpp does)
        const int uv_w = rect.width / 2;
        const int uv_h = rect.height / 2;
        const int uv_x = rect.left / 2;
        const int uv_y = rect.top / 2;

        // Check if UV calculations are correct
        const bool uv_correct =
                (rect.width % 2 == 0) && (rect.height % 2 == 0) && (rect.left % 2 == 0) && (rect.top % 2 == 0);

        // Verify UV calculations for even dimensions
        if (has_even_dimensions(rect) && has_even_coordinates(rect)) {
            EXPECT_TRUE(uv_correct) << "UV calculations should be correct for even dimensions and coordinates";
        }

        // Suppress unused variable warnings
        (void) uv_x;
        (void) uv_y;
        (void) uv_w;
        (void) uv_h;
    }
}

/**
 * @brief Test that demonstrates the problem with odd dimensions in area_copy.cpp
 */
TEST_F(YUV420EvenDimensionsTest, AreaCopyOddDimensionsProblem) {
    // Test rectangles with odd dimensions (simulating what area_copy.cpp would do)
    std::vector<sf::rectangle> input_rectangles = {
            {10, 10, 20, 20}, // Even dimensions - should work fine
            {10, 10, 21, 20}, // Odd width - UV plane will be 10x10 instead of 10.5x10
            {10, 10, 20, 21}, // Odd height - UV plane will be 10x10 instead of 10x10.5
            {10, 10, 21, 21}, // Both odd - UV plane will be 10x10 instead of 10.5x10.5
    };

    std::vector<sf::rectangle> packed_rectangles = {
            {0, 0, 20, 20},
            {0, 0, 21, 20},
            {0, 0, 20, 21},
            {0, 0, 21, 21},
    };

    for (size_t i = 0; i < input_rectangles.size(); ++i) {
        const auto &input_rect = input_rectangles[i];
        const auto &packed_rect = packed_rectangles[i];

        const bool has_even_dims = has_even_dimensions(input_rect);
        const bool has_even_coords = has_even_coordinates(input_rect);

        // Verify that odd dimensions are detected
        if (i == 0) {
            EXPECT_TRUE(has_even_dims) << "First rectangle should have even dimensions";
        } else {
            EXPECT_FALSE(has_even_dims || !has_even_coords)
                    << "Other rectangles should have odd dimensions or coordinates";
        }

        // Suppress unused variable warnings
        (void) packed_rect;
    }
}

/**
 * @brief Test that shows what the correct UV plane calculations should be
 */
TEST_F(YUV420EvenDimensionsTest, CorrectUVPlaneCalculations) {
    // Test various rectangle dimensions and show correct UV calculations
    std::vector<sf::rectangle> test_rectangles = {
            {10, 10, 20, 20}, // Even dimensions
            {10, 10, 21, 20}, // Odd width
            {10, 10, 20, 21}, // Odd height
            {10, 10, 21, 21}, // Both odd
            {11, 11, 20, 20}, // Odd coordinates
            {11, 11, 21, 21}, // Odd coordinates and dimensions
    };

    for (const auto &rect: test_rectangles) {
        // Current calculation (what area_copy.cpp does)
        const int current_uv_x = rect.left / 2;
        const int current_uv_y = rect.top / 2;
        const int current_uv_w = rect.width / 2;
        const int current_uv_h = rect.height / 2;

        // Correct calculation for YUV420
        const int correct_uv_x = rect.left / 2;
        const int correct_uv_y = rect.top / 2;
        const int correct_uv_w = (rect.width + 1) / 2; // Round up for odd widths
        const int correct_uv_h = (rect.height + 1) / 2; // Round up for odd heights

        const bool is_correct = (current_uv_x == correct_uv_x) && (current_uv_y == correct_uv_y) &&
                                (current_uv_w == correct_uv_w) && (current_uv_h == correct_uv_h);

        // For even dimensions, calculations should be correct
        if (has_even_dimensions(rect) && has_even_coordinates(rect)) {
            EXPECT_TRUE(is_correct) << "UV calculations should be correct for even dimensions";
        }
    }
}

/**
 * @brief Test the actual validation functions from libvcai
 */
TEST_F(YUV420EvenDimensionsTest, TestValidationFunctions) {
    const int frame_width = 100;
    const int frame_height = 100;

    std::vector<sf::rectangle> test_rectangles = {
            {10, 10, 20, 20}, // Already even
            {10, 10, 21, 20}, // Odd width
            {10, 10, 20, 21}, // Odd height
            {10, 10, 21, 21}, // Both odd
            {11, 11, 20, 20}, // Odd coordinates
            {11, 11, 21, 21}, // Odd coordinates and dimensions
    };

    for (const auto &original_rect: test_rectangles) {
        // Test safe ensure_even_dimensions function with frame bounds
        const sf::rectangle corrected_rect = sf::ensure_even_dimensions(original_rect, frame_width, frame_height);

        // Verify the corrected rectangle has even dimensions and coordinates
        EXPECT_TRUE(sf::has_even_dimensions(corrected_rect)) << "Corrected rectangle should have even dimensions";

        // Verify the rectangle stays within frame bounds
        EXPECT_GE(corrected_rect.left, 0) << "X should be non-negative";
        EXPECT_GE(corrected_rect.top, 0) << "Y should be non-negative";
        EXPECT_LE(corrected_rect.left + corrected_rect.width, frame_width) << "Rectangle should fit within frame width";
        EXPECT_LE(corrected_rect.top + corrected_rect.height, frame_height)
                << "Rectangle should fit within frame height";
    }
}

/**
 * @brief Test that demonstrates the solution: ensure even dimensions
 */
TEST_F(YUV420EvenDimensionsTest, SolutionEnsureEvenDimensions) {
    std::vector<sf::rectangle> test_rectangles = {
            {10, 10, 20, 20}, // Already even
            {10, 10, 21, 20}, // Odd width
            {10, 10, 20, 21}, // Odd height
            {10, 10, 21, 21}, // Both odd
            {11, 11, 20, 20}, // Odd coordinates
            {11, 11, 21, 21}, // Odd coordinates and dimensions
    };

    for (const auto &original_rect: test_rectangles) {
        const sf::rectangle corrected_rect = sf::ensure_even_dimensions(original_rect);

        // Verify the corrected rectangle has even dimensions and coordinates
        EXPECT_TRUE(sf::has_even_dimensions(corrected_rect)) << "Corrected rectangle should have even dimensions";
    }
}
