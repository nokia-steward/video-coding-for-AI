// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>
#include "../../encoder_cli/src/utils/rectangle_manager.h"

TEST(RectangleManagerTest, BasicUpdate) {
    rectangle_manager manager(1920, 1080);
    const auto &initial_rects = manager.get_updated_rectangles(0);
    ASSERT_EQ(initial_rects.size(), 3);

    // Copy the initial state
    std::vector<bouncing_rectangle> rects_before_update = initial_rects;

    // Update after 1 second (1e9 nanoseconds)
    const auto &updated_rects = manager.get_updated_rectangles(1000000000);
    ASSERT_EQ(updated_rects.size(), 3);

    // Check that positions have changed
    bool position_changed = false;
    for (size_t i = 0; i < updated_rects.size(); ++i) {
        if (updated_rects[i].geometry.left != rects_before_update[i].geometry.left ||
            updated_rects[i].geometry.top != rects_before_update[i].geometry.top) {
            position_changed = true;
            break;
        }
    }
    EXPECT_TRUE(position_changed);
}
