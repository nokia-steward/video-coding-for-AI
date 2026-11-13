#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <gst/gst.h>

/**
 * @brief Groundtruth bounding box for a single object.
 */
struct groundtruth_box {
    int category_id = 0;
    float x_coord = 0.0F;
    float y_coord = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

/**
 * @brief Groundtruth data for a single frame.
 */
struct frame_groundtruth {
    int image_id = 0;
    std::vector<groundtruth_box> boxes;

    frame_groundtruth() : image_id(0), boxes() {}
};

/**
 * @brief Parsed groundtruth dataset.
 */
struct groundtruth_data {
    std::unordered_map<int, frame_groundtruth> frames;
    std::unordered_map<int, std::string> category_names;

    groundtruth_data() : frames(), category_names() {}
};

/**
 * @brief Detection bounding box extracted from inference metadata.
 */
struct detection_box {
    int class_id = 0;
    float x_coord = 0.0F;
    float y_coord = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    float confidence = 0.0F;
};

/**
 * @brief Sparse semantic-segmentation groundtruth point.
 */
struct semseg_groundtruth_point {
    int x_coord = 0;
    int y_coord = 0;
    int class_id = 0;
};

/**
 * @brief Parsed sparse semantic-segmentation groundtruth dataset.
 */
struct semseg_groundtruth_data {
    std::unordered_map<int, std::vector<semseg_groundtruth_point>> points_by_frame;
    std::unordered_map<int, std::string> category_names;
    std::vector<int> eval_category_ids;
    int image_width = 0;
    int image_height = 0;

    semseg_groundtruth_data() :
        points_by_frame(), category_names(), eval_category_ids(), image_width(0), image_height(0) {}
};

/**
 * @brief Predicted semantic-segmentation map for one frame.
 */
struct semseg_prediction_map {
    int width = 0;
    int height = 0;
    std::vector<int> class_map;
};

/**
 * @brief Aggregated test mode metrics.
 */
struct test_mode_metrics {
    uint64_t frames_with_groundtruth = 0;
    uint64_t true_positive = 0;
    uint64_t false_positive = 0;
    uint64_t false_negative = 0;
    uint64_t matched_pairs = 0;
    double total_iou = 0.0;
};

/**
 * @brief Per-frame metrics for test mode output.
 */
struct test_mode_frame_result {
    int frame_index = 0;
    size_t groundtruth_count = 0;
    size_t detection_count = 0;
    size_t true_positive = 0;
    size_t false_positive = 0;
    size_t false_negative = 0;
    double precision = 0.0;
    double recall = 0.0;
    double f1_score = 0.0;
    double mean_iou = 0.0;
    std::string detection_classes;
    std::string groundtruth_classes;
};

/**
 * @brief Per-frame semantic-segmentation metrics.
 */
struct semseg_test_mode_frame_result {
    int frame_index = 0;
    size_t groundtruth_point_count = 0;
    size_t classes_with_union = 0;
    double mean_iou = 0.0;
};

/**
 * @brief Accumulated per-class semantic-segmentation counters.
 */
struct semseg_class_metrics {
    uint64_t intersection = 0;
    uint64_t union_count = 0;
};

/**
 * @brief Stored frame data used to compute mAP across the full run.
 *
 * mAP needs confidence-sorted detections over all evaluated frames.
 * This snapshot stores the filtered detections and groundtruth boxes
 * exactly as they were used during frame-level evaluation.
 */
struct test_mode_bbox_frame_snapshot {
    int frame_index = 0;
    std::vector<detection_box> detections;
    std::vector<groundtruth_box> groundtruth_boxes;
};

/**
 * @brief Summary metrics for the full test mode run.
 */
struct test_mode_run_summary {
    bool has_groundtruth = false;
    uint64_t frames_with_groundtruth = 0;
    uint64_t groundtruth_total = 0;
    uint64_t detection_total = 0;
    uint64_t true_positive = 0;
    uint64_t false_positive = 0;
    uint64_t false_negative = 0;
    double precision = 0.0;
    double recall = 0.0;
    double f1_score = 0.0;
    double mean_iou = 0.0;
    /// Mean Average Precision at the configured IoU threshold (bbox mode only).
    double map_50 = 0.0;
    double iou_threshold = 0.5;
};

/**
 * @brief Per-class semantic-segmentation summary row.
 */
struct semseg_test_mode_class_summary {
    int class_id = 0;
    std::string class_name;
    uint64_t intersection = 0;
    uint64_t union_count = 0;
    double iou = 0.0;
};

/**
 * @brief Summary metrics for the full semantic-segmentation test mode run.
 */
struct semseg_test_mode_run_summary {
    bool has_groundtruth = false;
    uint64_t frames_with_groundtruth = 0;
    uint64_t points_evaluated = 0;
    double mean_iou = 0.0;
    std::vector<semseg_test_mode_class_summary> class_summaries;
};

/**
 * @brief Test mode state and metrics.
 */
struct test_mode_context {
    groundtruth_data groundtruth;
    semseg_groundtruth_data semseg_groundtruth;
    double iou_threshold = 0.5;
    mutable std::mutex mutex;
    test_mode_metrics metrics;
    std::unordered_map<int, int> detection_class_to_groundtruth_id;
    std::unordered_map<int, int> semseg_prediction_class_to_groundtruth_id;
    std::unordered_map<int, std::string> detection_class_names;
    std::vector<test_mode_frame_result> frame_results;
    std::vector<test_mode_bbox_frame_snapshot> bbox_frame_snapshots;
    std::unordered_map<int, semseg_class_metrics> semseg_metrics_by_class;
    std::vector<semseg_test_mode_frame_result> semseg_frame_results;
    uint64_t semseg_frames_with_groundtruth = 0;
    uint64_t semseg_points_evaluated = 0;
    std::vector<int> allowed_detection_class_ids;
    std::vector<int> allowed_groundtruth_category_ids;

    test_mode_context(const test_mode_context &) = delete;
    test_mode_context &operator=(const test_mode_context &) = delete;

    test_mode_context() :
        groundtruth(), semseg_groundtruth(), iou_threshold(0.5), mutex(), metrics(),
        detection_class_to_groundtruth_id(), semseg_prediction_class_to_groundtruth_id(), detection_class_names(),
        frame_results(), bbox_frame_snapshots(), semseg_metrics_by_class(), semseg_frame_results(),
        semseg_frames_with_groundtruth(0), semseg_points_evaluated(0), allowed_detection_class_ids(),
        allowed_groundtruth_category_ids() {}
};

/**
 * @brief Load groundtruth data from a COCO-style JSON file.
 * @param path Path to the JSON file.
 * @param log_category Logging category for diagnostics.
 * @return Parsed groundtruth data on success, std::nullopt on failure.
 */
[[nodiscard]] std::optional<groundtruth_data> load_groundtruth_data(const std::string &path,
                                                                    GstDebugCategory *log_category);

/**
 * @brief Load sparse semantic-segmentation groundtruth data from JSON.
 * @param path Path to semseg JSON file.
 * @param log_category Logging category for diagnostics.
 * @return Parsed semseg groundtruth data on success, std::nullopt on failure.
 */
[[nodiscard]] std::optional<semseg_groundtruth_data> load_semseg_groundtruth_data(const std::string &path,
                                                                                  GstDebugCategory *log_category);

/**
 * @brief Evaluate a single frame's detections and update running metrics.
 * @param context Test mode context (metrics are updated in-place).
 * @param frame_index Frame index used to locate groundtruth.
 * @param detections Detected objects for the frame.
 * @param log_category Logging category for diagnostics.
 */
void evaluate_test_mode_frame(test_mode_context &context, int frame_index, const std::vector<detection_box> &detections,
                              GstDebugCategory *log_category);

/**
 * @brief Evaluate a single frame's semseg prediction map against sparse points.
 * @param context Test mode context (metrics are updated in-place).
 * @param frame_index Frame index used to locate semseg groundtruth.
 * @param prediction Predicted segmentation class map.
 * @param frame_width Source frame width used for coordinate remapping.
 * @param frame_height Source frame height used for coordinate remapping.
 * @param log_category Logging category for diagnostics.
 */
void evaluate_semseg_test_mode_frame(test_mode_context &context, int frame_index,
                                     const semseg_prediction_map &prediction, int frame_width, int frame_height,
                                     GstDebugCategory *log_category);

/**
 * @brief Build a summary for the full test mode run.
 * @param context Test mode context containing aggregated metrics.
 * @return Summary snapshot for final reporting.
 */
[[nodiscard]] test_mode_run_summary build_test_mode_run_summary(const test_mode_context &context);

/**
 * @brief Build a summary for the full semantic-segmentation test mode run.
 * @param context Test mode context containing aggregated semseg metrics.
 * @return Semseg summary snapshot for final reporting.
 */
[[nodiscard]] semseg_test_mode_run_summary build_semseg_test_mode_run_summary(const test_mode_context &context);

/**
 * @brief Build per-frame results for the full test mode run.
 * @param context Test mode context containing aggregated metrics and frame data.
 * @return Per-frame results snapshot for final reporting.
 */
[[nodiscard]] std::vector<test_mode_frame_result> build_test_mode_frame_results(const test_mode_context &context);

/**
 * @brief Build per-frame semseg results for the full test mode run.
 * @param context Test mode context containing semseg frame data.
 * @return Per-frame semseg results snapshot for final reporting.
 */
[[nodiscard]] std::vector<semseg_test_mode_frame_result>
build_semseg_test_mode_frame_results(const test_mode_context &context);

/**
 * @brief Log a summary for the full test mode run.
 * @param summary Summary snapshot for final reporting.
 * @param port Port number for log context.
 * @param log_category Logging category for diagnostics.
 */
void log_test_mode_run_summary(const test_mode_run_summary &summary, guint16 port, GstDebugCategory *log_category);

/**
 * @brief Log a semantic-segmentation summary for the full test mode run.
 * @param summary Summary snapshot for final reporting.
 * @param port Port number for log context.
 * @param log_category Logging category for diagnostics.
 */
void log_semseg_test_mode_run_summary(const semseg_test_mode_run_summary &summary, guint16 port,
                                      GstDebugCategory *log_category);

/**
 * @brief Write a markdown summary for the full test mode run.
 * @param summary Summary snapshot for final reporting.
 * @param port Port number for context.
 * @param path Output file path.
 * @param log_category Logging category for diagnostics.
 * @return True on success, false on failure.
 */
[[nodiscard]] bool write_test_mode_run_summary_markdown(const test_mode_run_summary &summary,
                                                        const std::vector<test_mode_frame_result> &frame_results,
                                                        guint16 port, const std::string &path,
                                                        GstDebugCategory *log_category);

/**
 * @brief Write a json summary for the full test mode run.
 * @param summary Summary snapshot for final reporting.
 * @param port Port number for context.
 * @param path Output file path.
 * @param log_category Logging category for diagnostics.
 * @return True on success, false on failure.
 */
[[nodiscard]] bool write_test_mode_run_summary_json(const test_mode_run_summary &summary,
                                                    const std::vector<test_mode_frame_result> &frame_results,
                                                    guint16 port, const std::string &path,
                                                    GstDebugCategory *log_category);

/**
 * @brief Write a markdown summary for the full semseg test mode run.
 * @param summary Summary snapshot for final reporting.
 * @param frame_results Per-frame semseg results.
 * @param port Port number for context.
 * @param path Output file path.
 * @param log_category Logging category for diagnostics.
 * @return True on success, false on failure.
 */
[[nodiscard]] bool
write_semseg_test_mode_run_summary_markdown(const semseg_test_mode_run_summary &summary,
                                            const std::vector<semseg_test_mode_frame_result> &frame_results,
                                            guint16 port, const std::string &path, GstDebugCategory *log_category);

/**
 * @brief Write a json summary for the full semseg test mode run.
 * @param summary Summary snapshot for final reporting.
 * @param frame_results Per-frame semseg results.
 * @param port Port number for context.
 * @param path Output file path.
 * @param log_category Logging category for diagnostics.
 * @return True on success, false on failure.
 */
[[nodiscard]] bool
write_semseg_test_mode_run_summary_json(const semseg_test_mode_run_summary &summary,
                                        const std::vector<semseg_test_mode_frame_result> &frame_results, guint16 port,
                                        const std::string &path, GstDebugCategory *log_category);
