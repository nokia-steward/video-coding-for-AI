// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include <atomic>

#include "../../encoder_cli/src/bridge/context/metadata_contract_runtime.h"
#include "../../encoder_cli/src/gst/input_types/frame_path_meta.h"
#include "../../encoder_cli/src/gst/output_pipeline/output_roi_sequence_correlation.h"
#include "../../libvcai/include/gst_raii.h"
#include "../common/gst_test_fixture.h"

// Display/debug border probes: when resolve_decoded_branch_roi_sequence_id sets used_strict_frame_order, the branch
// pops the next bundle via FIFO from display_regions_queue / debug_regions_queue (see display_branch.cpp /
// debug_branch.cpp). PTS/meta paths still use sequence-keyed pop plus fail-stop on miss.

namespace {
    [[nodiscard]] sf::unique_gst_buffer_ptr make_writable_video_buffer() {
        return sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, 64, nullptr));
    }
} // namespace

class OutputRoiSequenceCorrelationTest : public GStreamerTestFixture {};

TEST_F(OutputRoiSequenceCorrelationTest, resolve_prefers_pts_map_when_registered) {
    metadata_contract_runtime rt{};
    std::atomic_uint64_t cursor{99U};
    constexpr GstClockTime pts = 12345;
    rt.register_output_sequence(pts, 42U);

    sf::unique_gst_buffer_ptr buffer = make_writable_video_buffer();
    ASSERT_NE(buffer, nullptr);
    GST_BUFFER_PTS(buffer.get()) = pts;
    ASSERT_TRUE(attach_frame_contract_metadata_to_buffer(buffer.get(), frame_path_type::inference, 7U));

    const auto r = resolve_decoded_branch_roi_sequence_id(pts, buffer.get(), rt, cursor);
    EXPECT_TRUE(r.used_pts_map);
    EXPECT_FALSE(r.used_frame_contract_meta);
    EXPECT_FALSE(r.used_strict_frame_order);
    EXPECT_EQ(r.sequence_id, 42U);
    EXPECT_EQ(cursor.load(), 99U);
}

TEST_F(OutputRoiSequenceCorrelationTest, resolve_uses_frame_contract_meta_when_pts_map_misses) {
    metadata_contract_runtime rt{};
    std::atomic_uint64_t cursor{1U};
    constexpr GstClockTime pts = 999;
    sf::unique_gst_buffer_ptr buffer = make_writable_video_buffer();
    ASSERT_NE(buffer, nullptr);
    GST_BUFFER_PTS(buffer.get()) = pts;
    ASSERT_TRUE(attach_frame_contract_metadata_to_buffer(buffer.get(), frame_path_type::inference, 11U));

    const auto r = resolve_decoded_branch_roi_sequence_id(pts, buffer.get(), rt, cursor);
    EXPECT_FALSE(r.used_pts_map);
    EXPECT_TRUE(r.used_frame_contract_meta);
    EXPECT_FALSE(r.used_strict_frame_order);
    EXPECT_EQ(r.sequence_id, 11U);
}

TEST_F(OutputRoiSequenceCorrelationTest, resolve_uses_strict_order_cursor_when_pts_and_meta_absent) {
    metadata_contract_runtime rt{};
    std::atomic_uint64_t cursor{3U};
    sf::unique_gst_buffer_ptr buffer = make_writable_video_buffer();
    ASSERT_NE(buffer, nullptr);
    GST_BUFFER_PTS(buffer.get()) = GST_CLOCK_TIME_NONE;

    const auto r = resolve_decoded_branch_roi_sequence_id(GST_CLOCK_TIME_NONE, buffer.get(), rt, cursor);
    EXPECT_FALSE(r.used_pts_map);
    EXPECT_FALSE(r.used_frame_contract_meta);
    EXPECT_TRUE(r.used_strict_frame_order);
    EXPECT_EQ(r.sequence_id, 3U);
}

TEST_F(OutputRoiSequenceCorrelationTest, advance_cursor_increments_strict_order_state) {
    std::atomic_uint64_t cursor{10U};
    advance_decoded_branch_roi_sequence_cursor(cursor, 10U);
    EXPECT_EQ(cursor.load(), 11U);
}
