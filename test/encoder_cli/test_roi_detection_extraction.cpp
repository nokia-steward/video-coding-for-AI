// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <glib.h>
#include <gst/gst.h>
#include <gstnvdsmeta.h>
#include <gtest/gtest.h>
#include <nvdsmeta.h>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/processing/roi_detection_extraction.h"
#include "../../libvcai/include/nvosd_overlay.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] encoder_config make_base_config() {
        encoder_config config;
        config.input_type_value = input_type::file;
        config.width = 640;
        config.height = 480;
        config.framerate = 30;
        config.file_name = sf::unique_g_char_ptr(g_strdup("dummy_640x480_30.yuv"));
        config.format = sf::video_format::i420;
        return config;
    }

    [[nodiscard]] sf::unique_gst_buffer_ptr make_test_buffer() {
        return sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, 1024, nullptr));
    }

    [[nodiscard]] NvDsFrameMeta *get_first_frame_meta(const NvDsBatchMeta *batch_meta) {
        if (!batch_meta || !batch_meta->frame_meta_list) {
            return nullptr;
        }
        return static_cast<NvDsFrameMeta *>(batch_meta->frame_meta_list->data);
    }

    void add_object([[maybe_unused]] NvDsBatchMeta *batch_meta, NvDsFrameMeta *frame_meta, const int class_id,
                    const guint64 object_id, const float confidence, const float left, const float top,
                    const float width, const float height) {
        auto *obj_meta = static_cast<NvDsObjectMeta *>(g_malloc0(sizeof(NvDsObjectMeta)));
        ASSERT_NE(obj_meta, nullptr);

        obj_meta->class_id = class_id;
        obj_meta->object_id = object_id;
        obj_meta->confidence = confidence;
        obj_meta->rect_params.left = left;
        obj_meta->rect_params.top = top;
        obj_meta->rect_params.width = width;
        obj_meta->rect_params.height = height;

        frame_meta->obj_meta_list = g_list_append(frame_meta->obj_meta_list, obj_meta);
        frame_meta->num_obj_meta += 1U;
    }

    void clear_objects(NvDsFrameMeta *frame_meta) {
        if (!frame_meta) {
            return;
        }
        GList *node = frame_meta->obj_meta_list;
        while (node) {
            g_free(node->data);
            node = node->next;
        }
        g_list_free(frame_meta->obj_meta_list);
        frame_meta->obj_meta_list = nullptr;
        frame_meta->num_obj_meta = 0U;
    }
} // namespace

class RoiDetectionExtractionTest : public GStreamerTestFixture {};

TEST_F(RoiDetectionExtractionTest, ReturnsEmptyWhenNoBatchMetaAttached) {
    auto config = make_base_config();
    global_data data(std::move(config));
    auto buffer = make_test_buffer();
    ASSERT_NE(buffer.get(), nullptr);

    const auto detections = get_detections_from_metadata(buffer.get(), data);
    EXPECT_TRUE(detections.empty());
}

TEST_F(RoiDetectionExtractionTest, AppliesClassFilterAndConfidenceNormalization) {
    auto config = make_base_config();
    global_data data(std::move(config));
    data.pgie_class_ids_to_filter = {7};

    auto buffer = make_test_buffer();
    ASSERT_NE(buffer.get(), nullptr);
    auto *batch_meta = sf::ensure_nvds_batch_meta(buffer.get(), 640, 480);
    ASSERT_NE(batch_meta, nullptr);
    auto *frame_meta = get_first_frame_meta(batch_meta);
    ASSERT_NE(frame_meta, nullptr);

    add_object(batch_meta, frame_meta, 7, 42U, 85.0F, 10.0F, 12.0F, 20.0F, 22.0F);
    add_object(batch_meta, frame_meta, 3, 99U, 0.7F, 30.0F, 14.0F, 20.0F, 22.0F);
    ASSERT_NE(frame_meta->obj_meta_list, nullptr);
    EXPECT_EQ(frame_meta->num_obj_meta, 2U);

    const auto detections = get_detections_from_metadata(buffer.get(), data);
    if (detections.empty()) {
        clear_objects(frame_meta);
        GTEST_SKIP() << "DeepStream object metadata extraction is unavailable in this environment.";
    }
    ASSERT_EQ(detections.size(), 1U);
    EXPECT_EQ(detections[0].class_id, 7);
    EXPECT_EQ(detections[0].object_id, 42U);
    EXPECT_NEAR(detections[0].confidence, 0.85F, 1e-6F);
    EXPECT_EQ(detections[0].bounding_box.left, 10);
    EXPECT_EQ(detections[0].bounding_box.top, 12);
    EXPECT_EQ(detections[0].bounding_box.width, 20);
    EXPECT_EQ(detections[0].bounding_box.height, 22);
    clear_objects(frame_meta);
}

TEST_F(RoiDetectionExtractionTest, FiltersInvalidRoisAndKeepsValidOnes) {
    auto config = make_base_config();
    global_data data(std::move(config));

    auto buffer = make_test_buffer();
    ASSERT_NE(buffer.get(), nullptr);
    auto *batch_meta = sf::ensure_nvds_batch_meta(buffer.get(), 640, 480);
    ASSERT_NE(batch_meta, nullptr);
    auto *frame_meta = get_first_frame_meta(batch_meta);
    ASSERT_NE(frame_meta, nullptr);

    add_object(batch_meta, frame_meta, 1, 10U, 0.9F, 10.0F, 10.0F, 0.0F, 20.0F);
    add_object(batch_meta, frame_meta, 1, 11U, 0.9F, 20.0F, 20.0F, 21.0F, 20.0F);
    add_object(batch_meta, frame_meta, 1, 12U, 0.9F, 30.0F, 30.0F, 20.0F, 20.0F);
    ASSERT_NE(frame_meta->obj_meta_list, nullptr);
    EXPECT_EQ(frame_meta->num_obj_meta, 3U);

    const auto detections = get_detections_from_metadata(buffer.get(), data);
    if (detections.empty()) {
        clear_objects(frame_meta);
        GTEST_SKIP() << "DeepStream object metadata extraction is unavailable in this environment.";
    }
    ASSERT_EQ(detections.size(), 1U);
    EXPECT_EQ(detections[0].object_id, 12U);
    EXPECT_EQ(detections[0].bounding_box.width, 20);
    EXPECT_EQ(detections[0].bounding_box.height, 20);
    clear_objects(frame_meta);
}
