// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gtest/gtest.h>
#include <optional>
#include <vector>

#include "../../encoder_cli/src/roi/low_delay_packer.h"
#include "../common/test_debug_categories.h"
#include "../libvcai/include/rectangle.h"

/**
 * @file test_low_delay_packer_weighted.cpp
 * @brief Comprehensive tests for weight-based low_delay ROI packing functionality.
 */

class LowDelayPackerWeightedTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize GStreamer if not already initialized
        if (!gst_is_initialized()) {
            gst_init(nullptr, nullptr);
        }
        // Initialize all debug categories (required for code under test to work correctly)
        init_all_test_debug_categories();
        packer = std::make_unique<low_delay_packer>(1024, 1024, 64, 3);
    }

    void TearDown() override { packer.reset(); }

    std::unique_ptr<low_delay_packer> packer;
};

/**
 * @brief Test weight calculation.
 */
TEST_F(LowDelayPackerWeightedTest, WeightCalculation) {
    const sf::rectangle rect1(0, 0, 100, 50);
    const sf::rectangle rect2(0, 0, 200, 100);

    EXPECT_EQ(low_delay_packer::get_weight(rect1), 5000); // 100 * 50
    EXPECT_EQ(low_delay_packer::get_weight(rect2), 20000); // 200 * 100
}
