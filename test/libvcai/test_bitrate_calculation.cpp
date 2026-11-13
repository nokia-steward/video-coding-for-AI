// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>
#include "libvcai/include/bitrate_calculation.h"

TEST(BitrateCalculationTest, Basic) {
    // 1MB file, 1 second duration = 8000 kbps
    EXPECT_NEAR(sf::calculate_average_bitrate(1000000, 1 * GST_SECOND), 8000.0, 0.1);
}

TEST(BitrateCalculationTest, ZeroDuration) { EXPECT_EQ(sf::calculate_average_bitrate(1000000, 0), 0.0); }

TEST(BitrateCalculationTest, ZeroSize) { EXPECT_EQ(sf::calculate_average_bitrate(0, 1 * GST_SECOND), 0.0); }
