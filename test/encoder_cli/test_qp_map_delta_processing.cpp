// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/input_types/frame_path_meta.h"
#include "../../encoder_cli/src/processing/frame_processing_utils.h"
#ifdef HAVE_DEEPSTREAM
#include "../../encoder_cli/src/processing/roi_detection_processing/processing_modes/qp_map_delta/qp_map_delta_processing.h"
#endif
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] encoder_config create_base_config() {
        encoder_config config;
        config.width = 640;
        config.height = 480;
        config.framerate = 30;
        config.input_type_value = input_type::file;
        config.background_mode_value = background_mode::qp_map_delta;
        config.effective_background_mode_value = background_mode::qp_map_delta;
        config.qp_map_ctb_size = 64;
        config.qp_map_background_delta = 10;
        return config;
    }

    [[nodiscard]] std::vector<split_rectangle> make_split_rectangles_for(const std::vector<sf::rectangle> &rectangles) {
        std::vector<split_rectangle> split_rectangles;
        split_rectangles.reserve(rectangles.size());
        for (size_t index = 0; index < rectangles.size(); ++index) {
            split_rectangles.emplace_back(rectangles[index], static_cast<uint32_t>(100 + index), 0, sf::yuv_color::blue,
                                          0.95F, false);
        }
        return split_rectangles;
    }
} // namespace

class QpMapDeltaProcessingTest : public GStreamerTestFixture {};

#ifdef HAVE_DEEPSTREAM
TEST_F(QpMapDeltaProcessingTest, ProcessFramePreservesTimestampsAndMaintainsQueueSync) {
    auto config = create_base_config();
    config.debug.draw_borders = true;
    config.debug.save_frames = true;
    global_data data(std::move(config));

    const frame_properties props{data.params.width, data.params.height, "I420"};
    const auto input_buffer_owned = sf::unique_gst_buffer_ptr(
            gst_buffer_new_allocate(nullptr, static_cast<gsize>(props.width * props.height * 3 / 2), nullptr));
    ASSERT_NE(input_buffer_owned, nullptr);

    constexpr GstClockTime pts = 1'000 * GST_MSECOND;
    constexpr GstClockTime dts = 900 * GST_MSECOND;
    constexpr GstClockTime duration = 33 * GST_MSECOND;
    constexpr guint64 offset = 1234;
    constexpr guint64 offset_end = 5678;
    GST_BUFFER_PTS(input_buffer_owned.get()) = pts;
    GST_BUFFER_DTS(input_buffer_owned.get()) = dts;
    GST_BUFFER_DURATION(input_buffer_owned.get()) = duration;
    GST_BUFFER_OFFSET(input_buffer_owned.get()) = offset;
    GST_BUFFER_OFFSET_END(input_buffer_owned.get()) = offset_end;
    GST_BUFFER_FLAGS(input_buffer_owned.get()) = GST_BUFFER_FLAG_DELTA_UNIT;
    ASSERT_TRUE(attach_frame_contract_metadata_to_buffer(input_buffer_owned.get(), frame_path_type::inference, 1));

    const sf::unique_gst_buffer_no_delete_ptr input_buffer(input_buffer_owned.get(), sf::no_delete_deleter{});
    sf::unique_gst_buffer_ptr output_buffer;

    const std::vector<sf::rectangle> input_rectangles = {
            sf::rectangle(20, 30, 100, 80),
            sf::rectangle(300, 120, 120, 90),
    };
    const auto split_rectangles = make_split_rectangles_for(input_rectangles);

    qp_map_delta_process_frame(data, input_buffer, output_buffer, props, input_rectangles, split_rectangles, false, 7);

    ASSERT_NE(output_buffer, nullptr);
    EXPECT_EQ(GST_BUFFER_PTS(output_buffer.get()), pts);
    EXPECT_EQ(GST_BUFFER_DTS(output_buffer.get()), dts);
    EXPECT_EQ(GST_BUFFER_DURATION(output_buffer.get()), duration);
    EXPECT_EQ(GST_BUFFER_OFFSET(output_buffer.get()), offset);
    EXPECT_EQ(GST_BUFFER_OFFSET_END(output_buffer.get()), offset_end);
    EXPECT_EQ(GST_BUFFER_FLAGS(output_buffer.get()), GST_BUFFER_FLAG_DELTA_UNIT);
    ASSERT_TRUE(data.pending_qp_map_payload.has_value());
    EXPECT_EQ(data.pending_qp_map_payload->frame_number, 7);
    EXPECT_EQ(data.pending_qp_map_payload->ctb_size, data.params.qp_map_ctb_size);
    EXPECT_FALSE(data.pending_qp_map_payload->delta_map.empty());

    const auto pri_opt = data.packed_regions_queue.try_pop();
    ASSERT_TRUE(pri_opt.has_value());
    EXPECT_EQ(pri_opt->sequence_id, 1U);
    EXPECT_TRUE(pri_opt->regions.cancel_flag) << "QP-map mode must keep PRI queue synchronized via sentinel";

    const auto display_opt = data.display_regions_queue.try_pop();
    ASSERT_TRUE(display_opt.has_value());
    EXPECT_EQ(display_opt->sequence_id, 1U);
    EXPECT_FALSE(display_opt->regions.cancel_flag)
            << "Display queue should contain ROI metadata when borders are enabled";

    const auto debug_opt = data.debug_regions_queue.try_pop();
    ASSERT_TRUE(debug_opt.has_value());
    EXPECT_EQ(debug_opt->sequence_id, 1U);
    EXPECT_FALSE(debug_opt->regions.cancel_flag)
            << "Debug queue should contain ROI metadata when frame saving is enabled";

    EXPECT_FALSE(data.packed_regions_queue.try_pop().has_value()) << "PRI queue should consume exactly one item";
}

TEST_F(QpMapDeltaProcessingTest, ProcessNoRoisPushesSentinelsAndPreservesTiming) {
    auto config = create_base_config();
    config.debug.draw_borders = true;
    config.debug.save_frames = true;
    global_data data(std::move(config));

    const frame_properties props{data.params.width, data.params.height, "I420"};
    const auto input_buffer_owned = sf::unique_gst_buffer_ptr(
            gst_buffer_new_allocate(nullptr, static_cast<gsize>(props.width * props.height * 3 / 2), nullptr));
    ASSERT_NE(input_buffer_owned, nullptr);

    constexpr GstClockTime pts = 2'000 * GST_MSECOND;
    constexpr GstClockTime dts = 2'000 * GST_MSECOND;
    constexpr GstClockTime duration = 33 * GST_MSECOND;
    GST_BUFFER_PTS(input_buffer_owned.get()) = pts;
    GST_BUFFER_DTS(input_buffer_owned.get()) = dts;
    GST_BUFFER_DURATION(input_buffer_owned.get()) = duration;
    ASSERT_TRUE(attach_frame_contract_metadata_to_buffer(input_buffer_owned.get(), frame_path_type::inference, 2));

    const sf::unique_gst_buffer_no_delete_ptr input_buffer(input_buffer_owned.get(), sf::no_delete_deleter{});
    sf::unique_gst_buffer_ptr output_buffer;
    qp_map_delta_process_no_rois(data, input_buffer, output_buffer, props);

    ASSERT_NE(output_buffer, nullptr);
    EXPECT_EQ(GST_BUFFER_PTS(output_buffer.get()), pts);
    EXPECT_EQ(GST_BUFFER_DTS(output_buffer.get()), dts);
    EXPECT_EQ(GST_BUFFER_DURATION(output_buffer.get()), duration);
    ASSERT_TRUE(data.pending_qp_map_payload.has_value());
    EXPECT_EQ(data.pending_qp_map_payload->ctb_size, data.params.qp_map_ctb_size);
    EXPECT_FALSE(data.pending_qp_map_payload->delta_map.empty());

    const auto pri_opt = data.packed_regions_queue.try_pop();
    ASSERT_TRUE(pri_opt.has_value());
    EXPECT_EQ(pri_opt->sequence_id, 2U);
    EXPECT_TRUE(pri_opt->regions.cancel_flag);

    const auto display_opt = data.display_regions_queue.try_pop();
    ASSERT_TRUE(display_opt.has_value());
    EXPECT_EQ(display_opt->sequence_id, 2U);
    EXPECT_TRUE(display_opt->regions.cancel_flag);

    const auto debug_opt = data.debug_regions_queue.try_pop();
    ASSERT_TRUE(debug_opt.has_value());
    EXPECT_EQ(debug_opt->sequence_id, 2U);
    EXPECT_TRUE(debug_opt->regions.cancel_flag);
}
#else
TEST_F(QpMapDeltaProcessingTest, ProcessingFunctionsUnavailableWithoutDeepStream) {
    GTEST_SKIP() << "DeepStream not available - skipping qp_map_delta processing tests";
}
#endif
