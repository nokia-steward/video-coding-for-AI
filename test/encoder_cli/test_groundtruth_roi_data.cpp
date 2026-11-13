// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

#include "../../encoder_cli/src/roi/groundtruth_roi_data.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] std::filesystem::path make_temp_path(const std::string &suffix) {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path() / ("groundtruth_roi_data_test_" + std::to_string(now) + suffix);
    }

    void write_text_file(const std::filesystem::path &path, const std::string &contents) {
        std::ofstream file(path);
        ASSERT_TRUE(file.is_open());
        file << contents;
        file.flush();
        file.close();
    }
} // namespace

class GroundtruthRoiDataTest : public GStreamerTestFixture {
protected:
    void SetUp() override {
        GStreamerTestFixture::SetUp();
        init_groundtruth_roi_debug_category();
    }
};

TEST_F(GroundtruthRoiDataTest, ReturnsNulloptForMissingFile) {
    const auto missing_path = make_temp_path(".missing.json");
    const auto loaded = load_groundtruth_roi_data(missing_path);
    EXPECT_FALSE(loaded.has_value());
}

TEST_F(GroundtruthRoiDataTest, UsesFilenameFrameIndicesWhenAllImagesHaveSuffixes) {
    const auto json_path = make_temp_path(".json");
    write_text_file(json_path, R"json({
  "images": [
    {"id": 101, "file_name": "camera_010.png"},
    {"id": 77, "file_name": "camera_003.jpg"}
  ],
  "annotations": [
    {"image_id": 101, "bbox": [1.2, 2.8, 10.2, 4.6]},
    {"image_id": 77, "bbox": [5.0, 6.0, 7.0, 8.0]}
  ]
})json");

    const auto loaded = load_groundtruth_roi_data(json_path);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->size(), 2U);
    EXPECT_EQ(loaded->total_annotations(), 2U);

    const auto frame_ten = loaded->get_rectangles(10);
    ASSERT_EQ(frame_ten.size(), 1U);
    EXPECT_EQ(frame_ten[0].left, 1);
    EXPECT_EQ(frame_ten[0].top, 3);
    EXPECT_EQ(frame_ten[0].width, 10);
    EXPECT_EQ(frame_ten[0].height, 5);

    const auto frame_three = loaded->get_rectangles(3);
    ASSERT_EQ(frame_three.size(), 1U);
    EXPECT_EQ(frame_three[0].left, 5);
    EXPECT_EQ(frame_three[0].top, 6);
    EXPECT_EQ(frame_three[0].width, 7);
    EXPECT_EQ(frame_three[0].height, 8);

    std::filesystem::remove(json_path);
}

TEST_F(GroundtruthRoiDataTest, FallsBackToImageOrderWhenAnyFilenameCannotBeParsed) {
    const auto json_path = make_temp_path(".json");
    write_text_file(json_path, R"json({
  "images": [
    {"id": 1, "file_name": "frame_010.png"},
    {"id": 2, "file_name": "unindexed.png"}
  ],
  "annotations": [
    {"image_id": 1, "bbox": [0, 0, 4, 4]},
    {"image_id": 2, "bbox": [10, 10, 6, 6]}
  ]
})json");

    const auto loaded = load_groundtruth_roi_data(json_path);
    ASSERT_TRUE(loaded.has_value());

    const auto frame_zero = loaded->get_rectangles(0);
    const auto frame_one = loaded->get_rectangles(1);
    EXPECT_EQ(frame_zero.size(), 1U);
    EXPECT_EQ(frame_one.size(), 1U);
    EXPECT_TRUE(loaded->get_rectangles(10).empty());

    std::filesystem::remove(json_path);
}

TEST_F(GroundtruthRoiDataTest, FiltersInvalidAnnotationsAndKeepsEmptyFrameEntries) {
    const auto json_path = make_temp_path(".json");
    write_text_file(json_path, R"json({
  "images": [
    {"id": 1, "file_name": "frame_000.png"},
    {"id": 2, "file_name": "frame_001.png"}
  ],
  "annotations": [
    {"image_id": 1, "bbox": [0, 0, 0, 10]},
    {"image_id": 999, "bbox": [1, 1, 3, 3]},
    {"image_id": 2, "bbox": [3, 4, 5, 6]}
  ]
})json");

    const auto loaded = load_groundtruth_roi_data(json_path);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->size(), 2U);
    EXPECT_EQ(loaded->total_annotations(), 1U);
    EXPECT_TRUE(loaded->get_rectangles(0).empty());
    ASSERT_EQ(loaded->get_rectangles(1).size(), 1U);

    std::filesystem::remove(json_path);
}
