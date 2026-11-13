// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

#include "../../encoder_cli/src/roi/static_roi_data.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] std::filesystem::path make_temp_path(const std::string &suffix) {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path() / ("static_roi_data_test_" + std::to_string(now) + suffix);
    }

    void write_text_file(const std::filesystem::path &path, const std::string &contents) {
        std::ofstream file(path);
        ASSERT_TRUE(file.is_open());
        file << contents;
        file.flush();
        file.close();
    }
} // namespace

class StaticRoiDataTest : public GStreamerTestFixture {
protected:
    void SetUp() override {
        GStreamerTestFixture::SetUp();
        init_static_roi_debug_category();
    }
};

TEST_F(StaticRoiDataTest, ReturnsNulloptForMissingFile) {
    const auto missing_path = make_temp_path(".missing.json");
    const auto loaded = load_static_roi_data(missing_path);
    EXPECT_FALSE(loaded.has_value());
}

TEST_F(StaticRoiDataTest, ReturnsEmptyDataWhenRoiArrayIsEmpty) {
    const auto json_path = make_temp_path(".json");
    write_text_file(json_path, R"json({"rois":[]})json");

    const auto loaded = load_static_roi_data(json_path);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->empty());
    EXPECT_TRUE(loaded->to_rectangles().empty());

    std::filesystem::remove(json_path);
}

TEST_F(StaticRoiDataTest, RejectsRoiWithNonIntegerField) {
    const auto json_path = make_temp_path(".json");
    write_text_file(json_path, R"json({"rois":[{"x":1.5,"y":2,"width":10,"height":10}]})json");

    const auto loaded = load_static_roi_data(json_path);
    EXPECT_FALSE(loaded.has_value());

    std::filesystem::remove(json_path);
}

TEST_F(StaticRoiDataTest, RejectsRoiWithNegativeCoordinatesOrInvalidDimensions) {
    const auto json_path = make_temp_path(".json");
    write_text_file(json_path, R"json({"rois":[{"x":-1,"y":2,"width":10,"height":10}]})json");
    EXPECT_FALSE(load_static_roi_data(json_path).has_value());

    write_text_file(json_path, R"json({"rois":[{"x":1,"y":2,"width":0,"height":10}]})json");
    EXPECT_FALSE(load_static_roi_data(json_path).has_value());

    std::filesystem::remove(json_path);
}

TEST_F(StaticRoiDataTest, ParsesValidRoiEntries) {
    const auto json_path = make_temp_path(".json");
    write_text_file(json_path, R"json({
  "rois": [
    {"x": 10, "y": 20, "width": 30, "height": 40},
    {"x": 100, "y": 200, "width": 50, "height": 60}
  ]
})json");

    const auto loaded = load_static_roi_data(json_path);
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->size(), 2U);

    const auto rects = loaded->to_rectangles();
    ASSERT_EQ(rects.size(), 2U);
    EXPECT_EQ(rects[0].left, 10);
    EXPECT_EQ(rects[0].top, 20);
    EXPECT_EQ(rects[0].width, 30);
    EXPECT_EQ(rects[0].height, 40);
    EXPECT_EQ(rects[1].left, 100);
    EXPECT_EQ(rects[1].top, 200);
    EXPECT_EQ(rects[1].width, 50);
    EXPECT_EQ(rects[1].height, 60);

    std::filesystem::remove(json_path);
}
