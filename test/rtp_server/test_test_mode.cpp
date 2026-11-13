// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../../rtp_server/src/pipeline/output/display/test_mode.h"
#include "../../rtp_server/src/pipeline/output/display/test_mode_summary.h"

namespace {
    [[nodiscard]] std::filesystem::path write_temp_json(const std::string &content) {
        const auto temp_dir = std::filesystem::temp_directory_path();
        const auto unique_suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto file_path = temp_dir / ("rtp_server_test_mode_groundtruth_" + unique_suffix + ".json");
        std::ofstream file(file_path);
        file << content;
        return file_path;
    }

    [[nodiscard]] std::filesystem::path make_temp_path(const std::string &suffix) {
        const auto temp_dir = std::filesystem::temp_directory_path();
        const auto unique_suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        return temp_dir / ("rtp_server_test_mode_summary_" + unique_suffix + suffix);
    }

    [[nodiscard]] detection_box make_detection(const int class_id, const float x, const float y, const float width,
                                               const float height) {
        detection_box detection{};
        detection.class_id = class_id;
        detection.x_coord = x;
        detection.y_coord = y;
        detection.width = width;
        detection.height = height;
        detection.confidence = 0.9F;
        return detection;
    }

    [[nodiscard]] semseg_prediction_map make_semseg_prediction(const int width, const int height,
                                                               const std::vector<int> &class_map) {
        semseg_prediction_map prediction{};
        prediction.width = width;
        prediction.height = height;
        prediction.class_map = class_map;
        return prediction;
    }
} // namespace

TEST(TestModeGroundtruth, ParsesFrameIndexFromFilename) {
    const std::string json_content = R"(
{
  "images": [
    { "file_name": "frame_002.png", "id": 1 },
    { "file_name": "frame_010.png", "id": 2 }
  ],
  "annotations": [
    { "image_id": 1, "category_id": 3, "bbox": [0, 0, 10, 10] },
    { "image_id": 2, "category_id": 3, "bbox": [5, 5, 10, 10] }
  ],
  "categories": [
    { "id": 3, "name": "car" }
  ]
}
)";

    const auto file_path = write_temp_json(json_content);
    const auto data_opt = load_groundtruth_data(file_path.string(), nullptr);
    ASSERT_TRUE(data_opt.has_value());

    const auto &data = data_opt.value();
    ASSERT_TRUE(data.frames.contains(2));
    ASSERT_TRUE(data.frames.contains(10));
    EXPECT_EQ(data.frames.at(2).boxes.size(), 1U);
    EXPECT_EQ(data.frames.at(10).boxes.size(), 1U);
}

TEST(TestModeGroundtruth, parse_frame_index_from_filename_handles_zero_padded_and_mixed_inputs) {
    const std::string json_content = R"(
{
  "images": [
    { "file_name": "capture_0007.png", "id": 1 },
    { "file_name": "capture_A.png", "id": 2 }
  ],
  "annotations": [
    { "image_id": 1, "category_id": 3, "bbox": [0, 0, 10, 10] },
    { "image_id": 2, "category_id": 3, "bbox": [0, 0, 10, 10] }
  ]
}
)";

    const auto file_path = write_temp_json(json_content);
    const auto data_opt = load_groundtruth_data(file_path.string(), nullptr);
    ASSERT_TRUE(data_opt.has_value());
    const auto &data = data_opt.value();
    EXPECT_TRUE(data.frames.contains(0));
    EXPECT_TRUE(data.frames.contains(1));
}

TEST(TestModeSummary, BuildsRunSummaryFromMetrics) {
    test_mode_context context{};
    context.iou_threshold = 0.6;
    context.metrics.frames_with_groundtruth = 2;
    context.metrics.true_positive = 3;
    context.metrics.false_positive = 1;
    context.metrics.false_negative = 2;
    context.metrics.matched_pairs = 3;
    context.metrics.total_iou = 2.4;

    const auto summary = build_test_mode_run_summary(context);

    EXPECT_TRUE(summary.has_groundtruth);
    EXPECT_EQ(summary.frames_with_groundtruth, 2U);
    EXPECT_EQ(summary.groundtruth_total, 5U);
    EXPECT_EQ(summary.detection_total, 4U);
    EXPECT_EQ(summary.true_positive, 3U);
    EXPECT_EQ(summary.false_positive, 1U);
    EXPECT_EQ(summary.false_negative, 2U);
    EXPECT_NEAR(summary.precision, 0.75, 1e-6);
    EXPECT_NEAR(summary.recall, 0.6, 1e-6);
    EXPECT_NEAR(summary.f1_score, 0.666666, 1e-5);
    EXPECT_NEAR(summary.mean_iou, 0.8, 1e-6);
    EXPECT_NEAR(summary.map_50, 0.0, 1e-6);
    EXPECT_NEAR(summary.iou_threshold, 0.6, 1e-6);
}

TEST(TestModeGroundtruth, MissingFileReturnsNullopt) {
    const auto data_opt = load_groundtruth_data("/tmp/nonexistent_test_mode_groundtruth.json", nullptr);
    EXPECT_FALSE(data_opt.has_value());
}

TEST(TestModeSemsegGroundtruth, ParsesSparsePointsAndEvalCategories) {
    const std::string json_content = R"(
{
  "groundtruth_type": "semseg_sparse_points_v1",
  "image_width": 1920,
  "image_height": 1080,
  "categories": [
    { "id": 5, "name": "car" },
    { "id": 12, "name": "pedestrian" }
  ],
  "eval_category_ids": [5, 12],
  "frames": [
    { "frame_index": 0, "points": [
      { "x": 10, "y": 20, "class_id": 5 },
      { "x": 11, "y": 21, "class_id": 42 }
    ]},
    { "frame_index": 1, "points": [
      { "x": 15, "y": 25, "class_id": 12 }
    ]}
  ]
}
)";
    const auto file_path = write_temp_json(json_content);
    const auto data_opt = load_semseg_groundtruth_data(file_path.string(), nullptr);
    ASSERT_TRUE(data_opt.has_value());

    const auto &data = data_opt.value();
    EXPECT_EQ(data.image_width, 1920);
    EXPECT_EQ(data.image_height, 1080);
    ASSERT_TRUE(data.category_names.contains(5));
    EXPECT_EQ(data.category_names.at(5), "car");
    ASSERT_TRUE(data.category_names.contains(12));
    EXPECT_EQ(data.category_names.at(12), "pedestrian");
    ASSERT_EQ(data.eval_category_ids.size(), 2U);
    EXPECT_EQ(data.eval_category_ids[0], 5);
    EXPECT_EQ(data.eval_category_ids[1], 12);
    ASSERT_TRUE(data.points_by_frame.contains(0));
    ASSERT_TRUE(data.points_by_frame.contains(1));
    // class_id 42 is filtered out because it is not in eval_category_ids.
    EXPECT_EQ(data.points_by_frame.at(0).size(), 1U);
    EXPECT_EQ(data.points_by_frame.at(1).size(), 1U);
}

TEST(TestModeSummary, MapsDetectionClassesToGroundtruthIds) {
    test_mode_context context{};
    groundtruth_box groundtruth{};
    groundtruth.category_id = 3;
    groundtruth.x_coord = 0.0F;
    groundtruth.y_coord = 0.0F;
    groundtruth.width = 10.0F;
    groundtruth.height = 10.0F;

    frame_groundtruth frame{};
    frame.image_id = 1;
    frame.boxes.push_back(groundtruth);
    context.groundtruth.frames.emplace(0, frame);
    context.detection_class_to_groundtruth_id.emplace(2, 3);

    const detection_box detection = make_detection(2, 0.0F, 0.0F, 10.0F, 10.0F);
    evaluate_test_mode_frame(context, 0, {detection}, nullptr);

    EXPECT_EQ(context.metrics.frames_with_groundtruth, 1U);
    EXPECT_EQ(context.metrics.true_positive, 1U);
    EXPECT_EQ(context.metrics.false_positive, 0U);
    EXPECT_EQ(context.metrics.false_negative, 0U);
    EXPECT_EQ(context.metrics.matched_pairs, 1U);
    EXPECT_NEAR(context.metrics.total_iou, 1.0, 1e-6);
}

TEST(TestModeSummary, WritesSummaryFiles) {
    test_mode_run_summary summary{};
    summary.has_groundtruth = true;
    summary.frames_with_groundtruth = 1;
    summary.groundtruth_total = 2;
    summary.detection_total = 3;
    summary.true_positive = 1;
    summary.false_positive = 2;
    summary.false_negative = 1;
    summary.precision = 0.333;
    summary.recall = 0.5;
    summary.f1_score = 0.4;
    summary.mean_iou = 0.2;
    summary.map_50 = 0.55;
    summary.iou_threshold = 0.5;

    const auto markdown_path = make_temp_path(".md");
    const auto json_path = make_temp_path(".json");

    test_mode_frame_result frame{};
    frame.frame_index = 0;
    frame.groundtruth_count = 2;
    frame.detection_count = 3;
    frame.true_positive = 1;
    frame.false_positive = 2;
    frame.false_negative = 1;
    frame.precision = 0.333;
    frame.recall = 0.5;
    frame.f1_score = 0.4;
    frame.mean_iou = 0.2;
    frame.detection_classes = "3(car)";
    frame.groundtruth_classes = "3(car)";
    const std::vector<test_mode_frame_result> frames{frame};

    ASSERT_TRUE(write_test_mode_run_summary_markdown(summary, frames, 5004, markdown_path.string(), nullptr));
    ASSERT_TRUE(write_test_mode_run_summary_json(summary, frames, 5004, json_path.string(), nullptr));

    std::ifstream markdown_file(markdown_path);
    std::stringstream markdown_buffer;
    markdown_buffer << markdown_file.rdbuf();
    const std::string markdown_contents = markdown_buffer.str();
    EXPECT_NE(markdown_contents.find("Test Mode Summary"), std::string::npos);
    EXPECT_NE(markdown_contents.find("Frame 0"), std::string::npos);
    EXPECT_NE(markdown_contents.find("Precision"), std::string::npos);
    EXPECT_NE(markdown_contents.find("mAP@IoU threshold"), std::string::npos);

    std::ifstream json_file(json_path);
    std::stringstream json_buffer;
    json_buffer << json_file.rdbuf();
    const std::string json_contents = json_buffer.str();
    EXPECT_NE(json_contents.find("\"frames\""), std::string::npos);
    EXPECT_NE(json_contents.find("\"frame_index\""), std::string::npos);
    EXPECT_NE(json_contents.find("\"precision\""), std::string::npos);
    EXPECT_NE(json_contents.find("\"mean_iou\""), std::string::npos);
    EXPECT_NE(json_contents.find("\"map_50\""), std::string::npos);
    EXPECT_EQ(json_contents.find("\"groundtruth_type\""), std::string::npos);
    EXPECT_EQ(json_contents.find("\"pixel_coverage\""), std::string::npos);
}

TEST(TestModeGroundtruth, FallsBackToImageOrderWhenSuffixMissing) {
    const std::string json_content = R"(
{
  "images": [
    { "file_name": "frameA.png", "id": 5 },
    { "file_name": "frameB.png", "id": 6 }
  ],
  "annotations": [
    { "image_id": 5, "category_id": 1, "bbox": [0, 0, 10, 10] },
    { "image_id": 6, "category_id": 1, "bbox": [5, 5, 10, 10] }
  ]
}
)";

    const auto file_path = write_temp_json(json_content);
    const auto data_opt = load_groundtruth_data(file_path.string(), nullptr);
    ASSERT_TRUE(data_opt.has_value());

    const auto &data = data_opt.value();
    ASSERT_TRUE(data.frames.contains(0));
    ASSERT_TRUE(data.frames.contains(1));
    EXPECT_EQ(data.frames.at(0).image_id, 5);
    EXPECT_EQ(data.frames.at(1).image_id, 6);
}

TEST(TestModeGroundtruth, ParsesCategoryNames) {
    const std::string json_content = R"(
{
  "images": [
    { "file_name": "frame_000.png", "id": 1 }
  ],
  "annotations": [
    { "image_id": 1, "category_id": 1, "bbox": [0, 0, 10, 10] }
  ],
  "categories": [
    { "id": 1, "name": "person" },
    { "id": 3, "name": "car" }
  ]
}
)";

    const auto file_path = write_temp_json(json_content);
    const auto data_opt = load_groundtruth_data(file_path.string(), nullptr);
    ASSERT_TRUE(data_opt.has_value());

    const auto &data = data_opt.value();
    ASSERT_EQ(data.category_names.size(), 2U);
    EXPECT_EQ(data.category_names.at(1), "person");
    EXPECT_EQ(data.category_names.at(3), "car");
}

TEST(TestModeGroundtruth, RejectsNonObjectJsonRoot) {
    const auto file_path = write_temp_json(R"(
[
  { "images": [] }
]
)");

    const auto data_opt = load_groundtruth_data(file_path.string(), nullptr);
    EXPECT_FALSE(data_opt.has_value());
}

TEST(TestModeGroundtruth, RejectsMissingImagesOrAnnotationsArrays) {
    const auto file_path = write_temp_json(R"(
{
  "images": {},
  "annotations": []
}
)");

    const auto data_opt = load_groundtruth_data(file_path.string(), nullptr);
    EXPECT_FALSE(data_opt.has_value());
}

TEST(TestModeGroundtruth, MixedFilenameSuffixesUseImageOrderForAllFrames) {
    const std::string json_content = R"(
{
  "images": [
    { "file_name": "frame_050.png", "id": 9 },
    { "file_name": "frameB.png", "id": 10 }
  ],
  "annotations": [
    { "image_id": 9, "category_id": 1, "bbox": [0, 0, 10, 10] },
    { "image_id": 10, "category_id": 1, "bbox": [5, 5, 10, 10] }
  ]
}
)";

    const auto file_path = write_temp_json(json_content);
    const auto data_opt = load_groundtruth_data(file_path.string(), nullptr);
    ASSERT_TRUE(data_opt.has_value());

    const auto &data = data_opt.value();
    EXPECT_TRUE(data.frames.contains(0));
    EXPECT_TRUE(data.frames.contains(1));
    EXPECT_FALSE(data.frames.contains(50));
    EXPECT_EQ(data.frames.at(0).image_id, 9);
    EXPECT_EQ(data.frames.at(1).image_id, 10);
    EXPECT_EQ(data.frames.at(0).boxes.size(), 1U);
    EXPECT_EQ(data.frames.at(1).boxes.size(), 1U);
}

TEST(TestModeGroundtruth, SkipsInvalidAndUnknownAnnotations) {
    const std::string json_content = R"(
{
  "images": [
    { "file_name": "frame_000.png", "id": 1 },
    { "file_name": "frame_001.png", "id": 2 }
  ],
  "annotations": [
    { "image_id": 1, "category_id": 7, "bbox": [1, 2, 3, 4] },
    { "image_id": 1, "category_id": 7, "bbox": [9, 9, 9] },
    { "image_id": 42, "category_id": 7, "bbox": [0, 0, 1, 1] }
  ]
}
)";

    const auto file_path = write_temp_json(json_content);
    const auto data_opt = load_groundtruth_data(file_path.string(), nullptr);
    ASSERT_TRUE(data_opt.has_value());

    const auto &data = data_opt.value();
    ASSERT_TRUE(data.frames.contains(0));
    ASSERT_TRUE(data.frames.contains(1));
    ASSERT_EQ(data.frames.at(0).boxes.size(), 1U);
    EXPECT_EQ(data.frames.at(0).boxes[0].category_id, 7);
    EXPECT_FLOAT_EQ(data.frames.at(0).boxes[0].x_coord, 1.0F);
    EXPECT_FLOAT_EQ(data.frames.at(0).boxes[0].y_coord, 2.0F);
    EXPECT_FLOAT_EQ(data.frames.at(0).boxes[0].width, 3.0F);
    EXPECT_FLOAT_EQ(data.frames.at(0).boxes[0].height, 4.0F);
    EXPECT_TRUE(data.frames.at(1).boxes.empty());
}

TEST(TestModeGroundtruth, HugeNumericSuffixFallsBackToImageOrder) {
    const std::string json_content = R"(
{
  "images": [
    { "file_name": "frame_999999999999999999999999.png", "id": 1 },
    { "file_name": "frame_002.png", "id": 2 }
  ],
  "annotations": [
    { "image_id": 1, "category_id": 4, "bbox": [1, 1, 4, 4] },
    { "image_id": 2, "category_id": 4, "bbox": [2, 2, 5, 5] }
  ]
}
)";

    const auto file_path = write_temp_json(json_content);
    const auto data_opt = load_groundtruth_data(file_path.string(), nullptr);
    ASSERT_TRUE(data_opt.has_value());

    const auto &data = data_opt.value();
    EXPECT_TRUE(data.frames.contains(0));
    EXPECT_TRUE(data.frames.contains(1));
    EXPECT_FALSE(data.frames.contains(2));
    ASSERT_EQ(data.frames.at(0).boxes.size(), 1U);
    ASSERT_EQ(data.frames.at(1).boxes.size(), 1U);
    EXPECT_EQ(data.frames.at(0).image_id, 1);
    EXPECT_EQ(data.frames.at(1).image_id, 2);
}

TEST(TestModeGroundtruth, CategoriesArraySkipsInvalidEntriesAndKeepsValidNames) {
    const std::string json_content = R"(
{
  "images": [
    { "file_name": "frame_000.png", "id": 1 }
  ],
  "annotations": [
    { "image_id": 1, "category_id": 7, "bbox": [0, 0, 10, 10] }
  ],
  "categories": [
    { "id": 7, "name": "car" },
    { "id": 8 },
    { "name": "person" },
    123
  ]
}
)";

    const auto file_path = write_temp_json(json_content);
    const auto data_opt = load_groundtruth_data(file_path.string(), nullptr);
    ASSERT_TRUE(data_opt.has_value());

    const auto &data = data_opt.value();
    ASSERT_EQ(data.category_names.size(), 1U);
    EXPECT_EQ(data.category_names.at(7), "car");
    EXPECT_FALSE(data.category_names.contains(8));
}

TEST(TestModeGroundtruth, AllInvalidImageEntriesResultInNullopt) {
    const std::string json_content = R"(
{
  "images": [
    { "id": 1 },
    { "file_name": "frame_000.png" },
    42
  ],
  "annotations": []
}
)";

    const auto file_path = write_temp_json(json_content);
    const auto data_opt = load_groundtruth_data(file_path.string(), nullptr);
    EXPECT_FALSE(data_opt.has_value());
}

TEST(TestModeGroundtruth, InvalidImageEntriesAreIgnoredWhenValidOnesExist) {
    const std::string json_content = R"(
{
  "images": [
    { "id": 1 },
    { "file_name": "frame_005.png", "id": 5 }
  ],
  "annotations": [
    { "image_id": 5, "category_id": 3, "bbox": [0, 1, 2, 3] }
  ]
}
)";

    const auto file_path = write_temp_json(json_content);
    const auto data_opt = load_groundtruth_data(file_path.string(), nullptr);
    ASSERT_TRUE(data_opt.has_value());

    const auto &data = data_opt.value();
    EXPECT_TRUE(data.frames.contains(0));
    EXPECT_FALSE(data.frames.contains(5));
    ASSERT_EQ(data.frames.at(0).boxes.size(), 1U);
    EXPECT_EQ(data.frames.at(0).image_id, 5);
}

TEST(TestModeEvaluation, UpdatesMetricsForPerfectMatch) {
    test_mode_context context{};
    context.iou_threshold = 0.5;

    frame_groundtruth frame{};
    frame.image_id = 1;
    frame.boxes.push_back({1, 0.0F, 0.0F, 10.0F, 10.0F});
    context.groundtruth.frames.emplace(0, frame);

    const std::vector<detection_box> detections = {make_detection(1, 0.0F, 0.0F, 10.0F, 10.0F)};
    evaluate_test_mode_frame(context, 0, detections, nullptr);

    EXPECT_EQ(context.metrics.true_positive, 1U);
    EXPECT_EQ(context.metrics.false_positive, 0U);
    EXPECT_EQ(context.metrics.false_negative, 0U);
    EXPECT_EQ(context.metrics.matched_pairs, 1U);
    EXPECT_EQ(context.metrics.frames_with_groundtruth, 1U);
}

TEST(TestModeEvaluation, HandlesNoDetections) {
    test_mode_context context{};
    context.iou_threshold = 0.5;

    frame_groundtruth frame{};
    frame.image_id = 1;
    frame.boxes.push_back({1, 0.0F, 0.0F, 10.0F, 10.0F});
    context.groundtruth.frames.emplace(0, frame);

    std::vector<detection_box> detections;
    evaluate_test_mode_frame(context, 0, detections, nullptr);

    EXPECT_EQ(context.metrics.true_positive, 0U);
    EXPECT_EQ(context.metrics.false_positive, 0U);
    EXPECT_EQ(context.metrics.false_negative, 1U);
    EXPECT_EQ(context.metrics.matched_pairs, 0U);
    EXPECT_EQ(context.metrics.frames_with_groundtruth, 1U);
}

TEST(TestModeEvaluation, HandlesClassMismatch) {
    test_mode_context context{};
    context.iou_threshold = 0.5;

    frame_groundtruth frame{};
    frame.image_id = 1;
    frame.boxes.push_back({1, 0.0F, 0.0F, 10.0F, 10.0F});
    context.groundtruth.frames.emplace(0, frame);

    const std::vector<detection_box> detections = {make_detection(2, 0.0F, 0.0F, 10.0F, 10.0F)};
    evaluate_test_mode_frame(context, 0, detections, nullptr);

    EXPECT_EQ(context.metrics.true_positive, 0U);
    EXPECT_EQ(context.metrics.false_positive, 1U);
    EXPECT_EQ(context.metrics.false_negative, 1U);
    EXPECT_EQ(context.metrics.matched_pairs, 0U);
}

TEST(TestModeEvaluation, HandlesMultipleDetectionsToSingleGroundtruth) {
    test_mode_context context{};
    context.iou_threshold = 0.5;

    frame_groundtruth frame{};
    frame.image_id = 1;
    frame.boxes.push_back({1, 0.0F, 0.0F, 10.0F, 10.0F});
    context.groundtruth.frames.emplace(0, frame);

    const std::vector<detection_box> detections = {make_detection(1, 0.0F, 0.0F, 10.0F, 10.0F),
                                                   make_detection(1, 20.0F, 20.0F, 5.0F, 5.0F)};

    evaluate_test_mode_frame(context, 0, detections, nullptr);

    EXPECT_EQ(context.metrics.true_positive, 1U);
    EXPECT_EQ(context.metrics.false_positive, 1U);
    EXPECT_EQ(context.metrics.false_negative, 0U);
    EXPECT_EQ(context.metrics.matched_pairs, 1U);
}

TEST(TestModeSummary, ComputesMapFromConfidenceRanking) {
    test_mode_context context{};
    context.iou_threshold = 0.5;

    frame_groundtruth frame{};
    frame.image_id = 1;
    frame.boxes.push_back({1, 0.0F, 0.0F, 10.0F, 10.0F});
    context.groundtruth.frames.emplace(0, frame);

    detection_box high_confidence_false_positive = make_detection(1, 30.0F, 30.0F, 5.0F, 5.0F);
    high_confidence_false_positive.confidence = 0.95F;
    detection_box low_confidence_true_positive = make_detection(1, 0.0F, 0.0F, 10.0F, 10.0F);
    low_confidence_true_positive.confidence = 0.40F;

    evaluate_test_mode_frame(context, 0, {high_confidence_false_positive, low_confidence_true_positive}, nullptr);
    const auto summary = build_test_mode_run_summary(context);

    EXPECT_TRUE(summary.has_groundtruth);
    EXPECT_NEAR(summary.map_50, 0.5, 1e-6);
}

TEST(TestModeEvaluation,
     safe_divide_compute_iou_solve_hungarian_min_cost_extract_unique_detection_classes_are_covered_via_frame_evaluation) {
    test_mode_context context{};
    context.iou_threshold = 0.5;
    frame_groundtruth frame{};
    frame.image_id = 1;
    frame.boxes.push_back({1, 0.0F, 0.0F, 10.0F, 10.0F});
    frame.boxes.push_back({2, 20.0F, 20.0F, 10.0F, 10.0F});
    context.groundtruth.frames.emplace(0, frame);

    std::vector<detection_box> detections = {
            make_detection(1, 0.0F, 0.0F, 10.0F, 10.0F),
            make_detection(2, 20.0F, 20.0F, 10.0F, 10.0F),
            make_detection(1, 40.0F, 40.0F, 5.0F, 5.0F),
    };
    evaluate_test_mode_frame(context, 0, detections, nullptr);

    EXPECT_EQ(context.metrics.true_positive, 2U);
    EXPECT_EQ(context.metrics.false_positive, 1U);
    EXPECT_EQ(context.metrics.false_negative, 0U);
    ASSERT_EQ(context.frame_results.size(), 1U);
    EXPECT_FALSE(context.frame_results[0].detection_classes.empty());
}

TEST(TestModeEvaluation, FiltersDetectionsByAllowedDetectionClassIds) {
    test_mode_context context{};
    context.iou_threshold = 0.5;
    context.allowed_detection_class_ids = {3};

    frame_groundtruth frame{};
    frame.image_id = 1;
    frame.boxes.push_back({3, 0.0F, 0.0F, 10.0F, 10.0F});
    context.groundtruth.frames.emplace(0, frame);

    const std::vector<detection_box> detections = {
            make_detection(3, 0.0F, 0.0F, 10.0F, 10.0F),
            make_detection(1, 0.0F, 0.0F, 10.0F, 10.0F),
    };

    evaluate_test_mode_frame(context, 0, detections, nullptr);
    EXPECT_EQ(context.metrics.true_positive, 1U);
    EXPECT_EQ(context.metrics.false_positive, 0U);
    EXPECT_EQ(context.metrics.false_negative, 0U);
}

TEST(TestModeEvaluation, FiltersGroundtruthByAllowedGroundtruthCategoryIds) {
    test_mode_context context{};
    context.iou_threshold = 0.5;
    context.allowed_groundtruth_category_ids = {3};

    frame_groundtruth frame{};
    frame.image_id = 1;
    frame.boxes.push_back({1, 0.0F, 0.0F, 10.0F, 10.0F});
    frame.boxes.push_back({3, 20.0F, 20.0F, 10.0F, 10.0F});
    context.groundtruth.frames.emplace(0, frame);

    const std::vector<detection_box> detections = {make_detection(3, 20.0F, 20.0F, 10.0F, 10.0F)};
    evaluate_test_mode_frame(context, 0, detections, nullptr);

    EXPECT_EQ(context.metrics.true_positive, 1U);
    EXPECT_EQ(context.metrics.false_positive, 0U);
    EXPECT_EQ(context.metrics.false_negative, 0U);
}

TEST(TestModeEvaluation, IouThresholdBoundaryIsCountedAsMatch) {
    test_mode_context context{};
    context.iou_threshold = 1.0;

    frame_groundtruth frame{};
    frame.image_id = 1;
    frame.boxes.push_back({1, 0.0F, 0.0F, 10.0F, 10.0F});
    context.groundtruth.frames.emplace(0, frame);

    const std::vector<detection_box> detections = {make_detection(1, 0.0F, 0.0F, 10.0F, 10.0F)};
    evaluate_test_mode_frame(context, 0, detections, nullptr);

    EXPECT_EQ(context.metrics.true_positive, 1U);
    EXPECT_EQ(context.metrics.false_positive, 0U);
    EXPECT_EQ(context.metrics.false_negative, 0U);
}

TEST(TestModeEvaluation, UnmappedDetectionClassIsRejectedWhenClassMappingExists) {
    test_mode_context context{};
    context.iou_threshold = 0.5;
    context.detection_class_to_groundtruth_id.emplace(2, 3);

    frame_groundtruth frame{};
    frame.image_id = 1;
    frame.boxes.push_back({3, 0.0F, 0.0F, 10.0F, 10.0F});
    context.groundtruth.frames.emplace(0, frame);

    // Class 9 is not in the mapping, so it maps to -1 and cannot match.
    const std::vector<detection_box> detections = {make_detection(9, 0.0F, 0.0F, 10.0F, 10.0F)};
    evaluate_test_mode_frame(context, 0, detections, nullptr);

    EXPECT_EQ(context.metrics.true_positive, 0U);
    EXPECT_EQ(context.metrics.false_positive, 1U);
    EXPECT_EQ(context.metrics.false_negative, 1U);
}

TEST(TestModeEvaluation, MissingGroundtruthFrameDoesNotMutateAggregatedMetrics) {
    test_mode_context context{};
    context.iou_threshold = 0.5;

    const auto metrics_before = context.metrics;
    evaluate_test_mode_frame(context, 42, {make_detection(1, 0.0F, 0.0F, 10.0F, 10.0F)}, nullptr);

    EXPECT_EQ(context.metrics.frames_with_groundtruth, metrics_before.frames_with_groundtruth);
    EXPECT_EQ(context.metrics.true_positive, metrics_before.true_positive);
    EXPECT_EQ(context.metrics.false_positive, metrics_before.false_positive);
    EXPECT_EQ(context.metrics.false_negative, metrics_before.false_negative);
    EXPECT_TRUE(context.frame_results.empty());
}

TEST(TestModeEvaluation, FrameResultClassLabelsUseSortedUniqueNamedFormat) {
    test_mode_context context{};
    context.iou_threshold = 0.5;
    context.groundtruth.category_names.emplace(1, "person");
    context.groundtruth.category_names.emplace(3, "car");

    frame_groundtruth frame{};
    frame.image_id = 1;
    frame.boxes.push_back({3, 20.0F, 20.0F, 10.0F, 10.0F});
    frame.boxes.push_back({1, 0.0F, 0.0F, 10.0F, 10.0F});
    context.groundtruth.frames.emplace(0, frame);

    const std::vector<detection_box> detections = {
            make_detection(3, 20.0F, 20.0F, 10.0F, 10.0F),
            make_detection(1, 0.0F, 0.0F, 10.0F, 10.0F),
            make_detection(3, 40.0F, 40.0F, 10.0F, 10.0F),
    };
    evaluate_test_mode_frame(context, 0, detections, nullptr);

    ASSERT_EQ(context.frame_results.size(), 1U);
    EXPECT_EQ(context.frame_results[0].detection_classes, "1(person),3(car)");
    EXPECT_EQ(context.frame_results[0].groundtruth_classes, "1(person),3(car)");
}

TEST(TestModeSummary, NoGroundtruthFramesProducesEmptyRunSummary) {
    test_mode_context context{};
    context.iou_threshold = 0.65;

    const auto summary = build_test_mode_run_summary(context);
    EXPECT_FALSE(summary.has_groundtruth);
    EXPECT_EQ(summary.frames_with_groundtruth, 0U);
    EXPECT_EQ(summary.groundtruth_total, 0U);
    EXPECT_EQ(summary.detection_total, 0U);
    EXPECT_EQ(summary.true_positive, 0U);
    EXPECT_EQ(summary.false_positive, 0U);
    EXPECT_EQ(summary.false_negative, 0U);
    EXPECT_NEAR(summary.precision, 0.0, 1e-9);
    EXPECT_NEAR(summary.recall, 0.0, 1e-9);
    EXPECT_NEAR(summary.f1_score, 0.0, 1e-9);
    EXPECT_NEAR(summary.mean_iou, 0.0, 1e-9);
    EXPECT_NEAR(summary.map_50, 0.0, 1e-9);
    EXPECT_NEAR(summary.iou_threshold, 0.65, 1e-9);
}

TEST(TestModeEvaluation, DetectionBelowIouThresholdCountsAsMissAndRecordsSnapshot) {
    test_mode_context context{};
    context.iou_threshold = 0.5;

    frame_groundtruth frame{};
    frame.image_id = 7;
    frame.boxes.push_back({1, 0.0F, 0.0F, 10.0F, 10.0F});
    context.groundtruth.frames.emplace(3, frame);

    const std::vector<detection_box> detections = {make_detection(1, 6.0F, 6.0F, 10.0F, 10.0F)};
    evaluate_test_mode_frame(context, 3, detections, nullptr);

    EXPECT_EQ(context.metrics.frames_with_groundtruth, 1U);
    EXPECT_EQ(context.metrics.true_positive, 0U);
    EXPECT_EQ(context.metrics.false_positive, 1U);
    EXPECT_EQ(context.metrics.false_negative, 1U);
    EXPECT_EQ(context.metrics.matched_pairs, 0U);
    EXPECT_NEAR(context.metrics.total_iou, 0.0, 1e-9);

    ASSERT_EQ(context.frame_results.size(), 1U);
    EXPECT_EQ(context.frame_results[0].frame_index, 3);
    EXPECT_EQ(context.frame_results[0].groundtruth_count, 1U);
    EXPECT_EQ(context.frame_results[0].detection_count, 1U);
    EXPECT_EQ(context.frame_results[0].true_positive, 0U);
    EXPECT_EQ(context.frame_results[0].false_positive, 1U);
    EXPECT_EQ(context.frame_results[0].false_negative, 1U);
    EXPECT_NEAR(context.frame_results[0].precision, 0.0, 1e-9);
    EXPECT_NEAR(context.frame_results[0].recall, 0.0, 1e-9);
    EXPECT_NEAR(context.frame_results[0].f1_score, 0.0, 1e-9);
    EXPECT_NEAR(context.frame_results[0].mean_iou, 0.0, 1e-9);

    ASSERT_EQ(context.bbox_frame_snapshots.size(), 1U);
    EXPECT_EQ(context.bbox_frame_snapshots[0].frame_index, 3);
    EXPECT_EQ(context.bbox_frame_snapshots[0].detections.size(), 1U);
    EXPECT_EQ(context.bbox_frame_snapshots[0].groundtruth_boxes.size(), 1U);
}

TEST(TestModeEvaluation, UnmappedDetectionsAreNotIncludedInClassLabelStrings) {
    test_mode_context context{};
    context.iou_threshold = 0.5;
    context.groundtruth.category_names.emplace(3, "car");
    context.detection_class_to_groundtruth_id.emplace(2, 3);

    frame_groundtruth frame{};
    frame.image_id = 1;
    frame.boxes.push_back({3, 0.0F, 0.0F, 10.0F, 10.0F});
    context.groundtruth.frames.emplace(0, frame);

    const std::vector<detection_box> detections = {
            make_detection(2, 0.0F, 0.0F, 10.0F, 10.0F), // mapped to class 3, should match
            make_detection(9, 0.0F, 0.0F, 10.0F, 10.0F), // mapped to -1, should remain unmatched
    };
    evaluate_test_mode_frame(context, 0, detections, nullptr);

    EXPECT_EQ(context.metrics.true_positive, 1U);
    EXPECT_EQ(context.metrics.false_positive, 1U);
    EXPECT_EQ(context.metrics.false_negative, 0U);

    ASSERT_EQ(context.frame_results.size(), 1U);
    EXPECT_EQ(context.frame_results[0].detection_count, 2U);
    EXPECT_EQ(context.frame_results[0].detection_classes, "3(car)");
    EXPECT_EQ(context.frame_results[0].groundtruth_classes, "3(car)");
}

TEST(TestModeSummary, MapAveragesAcrossClassesAtConfiguredThreshold) {
    test_mode_context context{};
    context.iou_threshold = 0.5;

    frame_groundtruth frame{};
    frame.image_id = 11;
    frame.boxes.push_back({1, 0.0F, 0.0F, 10.0F, 10.0F}); // class 1: will match
    frame.boxes.push_back({2, 20.0F, 20.0F, 10.0F, 10.0F}); // class 2: will be missed
    context.groundtruth.frames.emplace(0, frame);

    detection_box class1_match = make_detection(1, 0.0F, 0.0F, 10.0F, 10.0F);
    class1_match.confidence = 0.9F;
    detection_box class2_miss = make_detection(2, 50.0F, 50.0F, 10.0F, 10.0F);
    class2_miss.confidence = 0.8F;

    evaluate_test_mode_frame(context, 0, {class1_match, class2_miss}, nullptr);
    const auto summary = build_test_mode_run_summary(context);

    EXPECT_TRUE(summary.has_groundtruth);
    EXPECT_EQ(summary.true_positive, 1U);
    EXPECT_EQ(summary.false_positive, 1U);
    EXPECT_EQ(summary.false_negative, 1U);
    EXPECT_NEAR(summary.map_50, 0.5, 1e-6);
}

TEST(TestModeSummary, BuildFrameResultsReturnsIndependentCopy) {
    test_mode_context context{};
    test_mode_frame_result frame{};
    frame.frame_index = 9;
    frame.precision = 0.25;
    context.frame_results.push_back(frame);

    auto copied_results = build_test_mode_frame_results(context);
    ASSERT_EQ(copied_results.size(), 1U);
    EXPECT_EQ(copied_results[0].frame_index, 9);
    EXPECT_NEAR(copied_results[0].precision, 0.25, 1e-9);

    copied_results[0].precision = 0.75;
    EXPECT_NEAR(context.frame_results[0].precision, 0.25, 1e-9);
}

TEST(TestModeSemsegEvaluation, UpdatesPerClassIoUAndSummary) {
    test_mode_context context{};
    context.semseg_groundtruth.eval_category_ids = {5, 12};
    context.semseg_groundtruth.category_names.emplace(5, "car");
    context.semseg_groundtruth.category_names.emplace(12, "pedestrian");
    context.semseg_groundtruth.points_by_frame.emplace(0, std::vector<semseg_groundtruth_point>{{0, 0, 5}, {1, 0, 12}});

    const auto prediction = make_semseg_prediction(2, 1, {5, 12});
    evaluate_semseg_test_mode_frame(context, 0, prediction, 2, 1, nullptr);

    EXPECT_EQ(context.semseg_frames_with_groundtruth, 1U);
    EXPECT_EQ(context.semseg_points_evaluated, 2U);
    ASSERT_TRUE(context.semseg_metrics_by_class.contains(5));
    ASSERT_TRUE(context.semseg_metrics_by_class.contains(12));
    EXPECT_EQ(context.semseg_metrics_by_class.at(5).intersection, 1U);
    EXPECT_EQ(context.semseg_metrics_by_class.at(5).union_count, 1U);
    EXPECT_EQ(context.semseg_metrics_by_class.at(12).intersection, 1U);
    EXPECT_EQ(context.semseg_metrics_by_class.at(12).union_count, 1U);

    const auto summary = build_semseg_test_mode_run_summary(context);
    EXPECT_TRUE(summary.has_groundtruth);
    EXPECT_EQ(summary.frames_with_groundtruth, 1U);
    EXPECT_EQ(summary.points_evaluated, 2U);
    EXPECT_NEAR(summary.mean_iou, 1.0, 1e-9);
    ASSERT_EQ(summary.class_summaries.size(), 2U);

    const auto frame_results = build_semseg_test_mode_frame_results(context);
    ASSERT_EQ(frame_results.size(), 1U);
    EXPECT_EQ(frame_results[0].frame_index, 0);
    EXPECT_EQ(frame_results[0].groundtruth_point_count, 2U);
    EXPECT_EQ(frame_results[0].classes_with_union, 2U);
    EXPECT_NEAR(frame_results[0].mean_iou, 1.0, 1e-9);
}

TEST(TestModeSemsegSummary, WritesSemsegSummaryFiles) {
    semseg_test_mode_run_summary summary{};
    summary.has_groundtruth = true;
    summary.frames_with_groundtruth = 2;
    summary.points_evaluated = 42;
    summary.mean_iou = 0.75;
    summary.class_summaries = {
            semseg_test_mode_class_summary{5, "car", 9, 12, 0.75},
            semseg_test_mode_class_summary{12, "pedestrian", 6, 8, 0.75},
    };

    semseg_test_mode_frame_result frame{};
    frame.frame_index = 0;
    frame.groundtruth_point_count = 21;
    frame.classes_with_union = 2;
    frame.mean_iou = 0.75;

    const auto markdown_path = make_temp_path("_semseg.md");
    const auto json_path = make_temp_path("_semseg.json");

    ASSERT_TRUE(write_semseg_test_mode_run_summary_markdown(summary, {frame}, 5004, markdown_path.string(), nullptr));
    ASSERT_TRUE(write_semseg_test_mode_run_summary_json(summary, {frame}, 5004, json_path.string(), nullptr));

    std::ifstream markdown_file(markdown_path);
    std::stringstream markdown_buffer;
    markdown_buffer << markdown_file.rdbuf();
    const std::string markdown_contents = markdown_buffer.str();
    EXPECT_NE(markdown_contents.find("SemSeg Test Mode Summary"), std::string::npos);
    EXPECT_NE(markdown_contents.find("Per-class IoU"), std::string::npos);
    EXPECT_NE(markdown_contents.find("Frame 0"), std::string::npos);

    std::ifstream json_file(json_path);
    std::stringstream json_buffer;
    json_buffer << json_file.rdbuf();
    const std::string json_contents = json_buffer.str();
    EXPECT_NE(json_contents.find("\"points_evaluated\""), std::string::npos);
    EXPECT_NE(json_contents.find("\"classes\""), std::string::npos);
    EXPECT_NE(json_contents.find("\"frames\""), std::string::npos);
}

TEST(TestModeEvaluation, FiltersCanYieldEmptyFrameResultsWithoutNaNs) {
    test_mode_context context{};
    context.iou_threshold = 0.5;
    context.allowed_detection_class_ids = {99};
    context.allowed_groundtruth_category_ids = {42};

    frame_groundtruth frame{};
    frame.image_id = 1;
    frame.boxes.push_back({1, 0.0F, 0.0F, 10.0F, 10.0F});
    context.groundtruth.frames.emplace(0, frame);

    const std::vector<detection_box> detections = {make_detection(1, 0.0F, 0.0F, 10.0F, 10.0F)};
    evaluate_test_mode_frame(context, 0, detections, nullptr);

    EXPECT_EQ(context.metrics.frames_with_groundtruth, 1U);
    EXPECT_EQ(context.metrics.true_positive, 0U);
    EXPECT_EQ(context.metrics.false_positive, 0U);
    EXPECT_EQ(context.metrics.false_negative, 0U);
    EXPECT_EQ(context.metrics.matched_pairs, 0U);
    EXPECT_NEAR(context.metrics.total_iou, 0.0, 1e-9);

    ASSERT_EQ(context.frame_results.size(), 1U);
    EXPECT_EQ(context.frame_results[0].groundtruth_count, 0U);
    EXPECT_EQ(context.frame_results[0].detection_count, 0U);
    EXPECT_NEAR(context.frame_results[0].precision, 0.0, 1e-9);
    EXPECT_NEAR(context.frame_results[0].recall, 0.0, 1e-9);
    EXPECT_NEAR(context.frame_results[0].f1_score, 0.0, 1e-9);
    EXPECT_NEAR(context.frame_results[0].mean_iou, 0.0, 1e-9);

    ASSERT_EQ(context.bbox_frame_snapshots.size(), 1U);
    EXPECT_TRUE(context.bbox_frame_snapshots[0].detections.empty());
    EXPECT_TRUE(context.bbox_frame_snapshots[0].groundtruth_boxes.empty());
}

TEST(TestModeEvaluation, ZeroAreaBoxesDoNotGenerateFalsePositiveMatches) {
    test_mode_context context{};
    context.iou_threshold = 0.1;

    frame_groundtruth frame{};
    frame.image_id = 1;
    frame.boxes.push_back({1, 0.0F, 0.0F, 0.0F, 10.0F});
    context.groundtruth.frames.emplace(0, frame);

    const std::vector<detection_box> detections = {make_detection(1, 0.0F, 0.0F, 10.0F, 10.0F)};
    evaluate_test_mode_frame(context, 0, detections, nullptr);

    EXPECT_EQ(context.metrics.true_positive, 0U);
    EXPECT_EQ(context.metrics.false_positive, 1U);
    EXPECT_EQ(context.metrics.false_negative, 1U);
    EXPECT_EQ(context.metrics.matched_pairs, 0U);
    EXPECT_NEAR(context.metrics.total_iou, 0.0, 1e-9);
}

TEST(TestModeSummary, FormattingIncludesNoGroundtruthNotesAndEmptyFrameSection) {
    test_mode_run_summary summary{};
    summary.has_groundtruth = false;
    summary.iou_threshold = 0.5;

    const auto markdown = format_test_mode_summary_markdown(summary, {}, 5004);
    EXPECT_NE(markdown.find("no frames with groundtruth evaluated"), std::string::npos);
    EXPECT_NE(markdown.find("No frame results captured"), std::string::npos);

    const auto json = format_test_mode_summary_json(summary, {}, 5004);
    EXPECT_NE(json.find("\"note\": \"no frames with groundtruth evaluated\""), std::string::npos);
    EXPECT_NE(json.find("\"frames\": []"), std::string::npos);
}

TEST(TestModeSummary, SummaryWritersFailWhenParentDirectoryDoesNotExist) {
    test_mode_run_summary summary{};
    summary.has_groundtruth = true;

    const auto bad_root = std::filesystem::temp_directory_path() /
                          ("rtp_server_missing_summary_dir_" +
                           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto markdown_path = bad_root / "missing" / "summary.md";
    const auto json_path = bad_root / "missing" / "summary.json";

    EXPECT_FALSE(write_test_mode_run_summary_markdown(summary, {}, 5004, markdown_path.string(), nullptr));
    EXPECT_FALSE(write_test_mode_run_summary_json(summary, {}, 5004, json_path.string(), nullptr));
}

TEST(TestModeSummary, WriteSummaryFileSupportsCreateAndOverwrite) {
    const auto summary_path = make_temp_path(".txt");

    ASSERT_TRUE(write_test_mode_summary_file(summary_path.string(), "first\n", nullptr));
    {
        std::ifstream file(summary_path);
        std::stringstream buffer;
        buffer << file.rdbuf();
        EXPECT_EQ(buffer.str(), "first\n");
    }

    ASSERT_TRUE(write_test_mode_summary_file(summary_path.string(), "second\n", nullptr));
    {
        std::ifstream file(summary_path);
        std::stringstream buffer;
        buffer << file.rdbuf();
        EXPECT_EQ(buffer.str(), "second\n");
    }
}

TEST(TestModeSummary, WriteSummaryFileFailsForDirectoryPath) {
    const auto dir_path = make_temp_path("_dir");
    std::filesystem::create_directories(dir_path);

    EXPECT_FALSE(write_test_mode_summary_file(dir_path.string(), "content", nullptr));
    std::filesystem::remove_all(dir_path);
}

TEST(TestModeEvaluation, FrameResultsUseNumericClassLabelsWhenCategoryNamesAreMissing) {
    test_mode_context context{};
    context.iou_threshold = 0.5;

    frame_groundtruth frame{};
    frame.image_id = 1;
    frame.boxes.push_back({3, 20.0F, 20.0F, 10.0F, 10.0F});
    frame.boxes.push_back({1, 0.0F, 0.0F, 10.0F, 10.0F});
    context.groundtruth.frames.emplace(0, frame);

    const std::vector<detection_box> detections = {
            make_detection(3, 20.0F, 20.0F, 10.0F, 10.0F),
            make_detection(1, 0.0F, 0.0F, 10.0F, 10.0F),
            make_detection(3, 40.0F, 40.0F, 10.0F, 10.0F),
    };
    evaluate_test_mode_frame(context, 0, detections, nullptr);

    ASSERT_EQ(context.frame_results.size(), 1U);
    EXPECT_EQ(context.frame_results[0].detection_classes, "1,3");
    EXPECT_EQ(context.frame_results[0].groundtruth_classes, "1,3");
}
