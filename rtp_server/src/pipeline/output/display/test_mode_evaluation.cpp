#include "test_mode_evaluation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "logging.h"

namespace {
    [[nodiscard]] double safe_divide(const double numerator, const double denominator) {
        if (denominator <= 0.0) {
            return 0.0;
        }
        return numerator / denominator;
    }

    [[nodiscard]] float compute_iou(const detection_box &detection, const groundtruth_box &groundtruth) {
        const float detection_right = detection.x_coord + detection.width;
        const float detection_bottom = detection.y_coord + detection.height;
        const float groundtruth_right = groundtruth.x_coord + groundtruth.width;
        const float groundtruth_bottom = groundtruth.y_coord + groundtruth.height;

        const float intersect_left = std::max(detection.x_coord, groundtruth.x_coord);
        const float intersect_top = std::max(detection.y_coord, groundtruth.y_coord);
        const float intersect_right = std::min(detection_right, groundtruth_right);
        const float intersect_bottom = std::min(detection_bottom, groundtruth_bottom);

        const float intersect_width = std::max(0.0F, intersect_right - intersect_left);
        const float intersect_height = std::max(0.0F, intersect_bottom - intersect_top);
        const float intersect_area = intersect_width * intersect_height;

        const float detection_area = std::max(0.0F, detection.width) * std::max(0.0F, detection.height);
        const float groundtruth_area = std::max(0.0F, groundtruth.width) * std::max(0.0F, groundtruth.height);
        const float union_area = detection_area + groundtruth_area - intersect_area;

        if (union_area <= 0.0F) {
            return 0.0F;
        }

        return intersect_area / union_area;
    }

    [[nodiscard]] std::vector<int> solve_hungarian_min_cost(const std::vector<std::vector<double>> &cost_matrix) {
        const size_t size = cost_matrix.size();
        std::vector<double> potential_row(size, 0.0);
        std::vector<double> potential_col(size, 0.0);
        std::vector<size_t> match_col(size, 0);
        std::vector<size_t> predecessor(size, 0);

        for (size_t row = 1; row < size; ++row) {
            match_col[0] = row;
            size_t current_col = 0;
            std::vector<double> min_value(size, std::numeric_limits<double>::infinity());
            std::vector<bool> used(size, false);

            do {
                used[current_col] = true;
                const size_t matched_row = match_col[current_col];
                double delta = std::numeric_limits<double>::infinity();
                size_t next_col = 0;

                for (size_t col = 1; col < size; ++col) {
                    if (used[col]) {
                        continue;
                    }
                    const double current_cost =
                            cost_matrix[matched_row][col] - potential_row[matched_row] - potential_col[col];
                    if (current_cost < min_value[col]) {
                        min_value[col] = current_cost;
                        predecessor[col] = current_col;
                    }
                    if (min_value[col] < delta) {
                        delta = min_value[col];
                        next_col = col;
                    }
                }

                for (size_t col = 0; col < size; ++col) {
                    if (used[col]) {
                        potential_row[match_col[col]] += delta;
                        potential_col[col] -= delta;
                    } else {
                        min_value[col] -= delta;
                    }
                }

                current_col = next_col;
            } while (match_col[current_col] != 0);

            do {
                const size_t previous_col = predecessor[current_col];
                match_col[current_col] = match_col[previous_col];
                current_col = previous_col;
            } while (current_col != 0);
        }

        std::vector<int> assignment(size, 0);
        for (size_t col = 1; col < size; ++col) {
            if (match_col[col] > 0) {
                assignment[match_col[col]] = static_cast<int>(col);
            }
        }
        return assignment;
    }

    struct match_summary {
        size_t true_positive = 0;
        size_t false_positive = 0;
        size_t false_negative = 0;
        size_t matched_pairs = 0;
        double total_iou = 0.0;
    };

    [[nodiscard]] std::vector<int> extract_unique_detection_classes(const std::vector<detection_box> &detections) {
        std::unordered_set<int> unique_ids;
        unique_ids.reserve(detections.size());
        for (const auto &detection: detections) {
            unique_ids.insert(detection.class_id);
        }
        std::vector<int> result(unique_ids.begin(), unique_ids.end());
        std::sort(result.begin(), result.end());
        return result;
    }

    [[nodiscard]] std::vector<int>
    extract_unique_groundtruth_classes(const std::vector<groundtruth_box> &groundtruth_boxes) {
        std::unordered_set<int> unique_ids;
        unique_ids.reserve(groundtruth_boxes.size());
        for (const auto &groundtruth: groundtruth_boxes) {
            unique_ids.insert(groundtruth.category_id);
        }
        std::vector<int> result(unique_ids.begin(), unique_ids.end());
        std::sort(result.begin(), result.end());
        return result;
    }

    [[nodiscard]] std::string format_class_list(const std::vector<int> &class_ids,
                                                const std::unordered_map<int, std::string> &category_names) {
        if (class_ids.empty()) {
            return "";
        }
        std::string result;
        bool first = true;
        for (const int class_id: class_ids) {
            if (class_id < 0) {
                continue;
            }
            const auto name_iter = category_names.find(class_id);
            if (!first) {
                result.append(",");
            }
            result.append(std::to_string(class_id));
            if (name_iter != category_names.end() && !name_iter->second.empty()) {
                result.push_back('(');
                result.append(name_iter->second);
                result.push_back(')');
            }
            first = false;
        }
        return result;
    }

    [[nodiscard]] match_summary match_detections_to_groundtruth(const std::vector<detection_box> &detections,
                                                                const std::vector<groundtruth_box> &groundtruth_boxes,
                                                                const double iou_threshold) {
        if (detections.empty() && groundtruth_boxes.empty()) {
            return {};
        }

        const size_t detection_count = detections.size();
        const size_t groundtruth_count = groundtruth_boxes.size();
        const size_t matrix_size = std::max(detection_count, groundtruth_count);

        std::vector<std::vector<float>> iou_matrix(detection_count, std::vector<float>(groundtruth_count, 0.0F));
        for (size_t det_index = 0; det_index < detection_count; ++det_index) {
            for (size_t gt_index = 0; gt_index < groundtruth_count; ++gt_index) {
                if (detections[det_index].class_id != groundtruth_boxes[gt_index].category_id) {
                    continue;
                }
                iou_matrix[det_index][gt_index] = compute_iou(detections[det_index], groundtruth_boxes[gt_index]);
            }
        }

        std::vector<std::vector<double>> cost_matrix(matrix_size + 1, std::vector<double>(matrix_size + 1, 1.0));
        for (size_t det_index = 0; det_index < detection_count; ++det_index) {
            for (size_t gt_index = 0; gt_index < groundtruth_count; ++gt_index) {
                cost_matrix[det_index + 1][gt_index + 1] = 1.0 - static_cast<double>(iou_matrix[det_index][gt_index]);
            }
        }

        const auto assignment = solve_hungarian_min_cost(cost_matrix);
        match_summary summary{};

        std::vector<bool> groundtruth_matched(groundtruth_count, false);
        for (size_t det_index = 0; det_index < detection_count; ++det_index) {
            const int assigned_col = assignment[det_index + 1];
            if (assigned_col <= 0) {
                continue;
            }
            const size_t gt_index = static_cast<size_t>(assigned_col - 1);
            if (gt_index >= groundtruth_count) {
                continue;
            }
            if (detections[det_index].class_id != groundtruth_boxes[gt_index].category_id) {
                continue;
            }
            const float iou_value = iou_matrix[det_index][gt_index];
            if (iou_value >= static_cast<float>(iou_threshold)) {
                summary.true_positive++;
                summary.matched_pairs++;
                summary.total_iou += static_cast<double>(iou_value);
                groundtruth_matched[gt_index] = true;
            }
        }

        summary.false_positive = detection_count - summary.true_positive;
        summary.false_negative = groundtruth_count - summary.true_positive;
        return summary;
    }

    [[nodiscard]] std::unordered_set<int> build_allowed_set(const std::vector<int> &allowed_ids) {
        std::unordered_set<int> result;
        result.reserve(allowed_ids.size());
        for (const int id: allowed_ids) {
            result.insert(id);
        }
        return result;
    }

    [[nodiscard]] int map_detection_class_id(const test_mode_context &context, const int class_id) {
        if (context.detection_class_to_groundtruth_id.empty()) {
            return class_id;
        }
        const auto mapping_iter = context.detection_class_to_groundtruth_id.find(class_id);
        if (mapping_iter == context.detection_class_to_groundtruth_id.end()) {
            return -1;
        }
        return mapping_iter->second;
    }

    [[nodiscard]] int map_semseg_prediction_class_id(const test_mode_context &context, const int class_id) {
        if (context.semseg_prediction_class_to_groundtruth_id.empty()) {
            return class_id;
        }
        const auto mapping_iter = context.semseg_prediction_class_to_groundtruth_id.find(class_id);
        if (mapping_iter == context.semseg_prediction_class_to_groundtruth_id.end()) {
            return -1;
        }
        return mapping_iter->second;
    }

    [[nodiscard]] std::vector<detection_box>
    apply_detection_class_mapping(const test_mode_context &context, const std::vector<detection_box> &detections) {
        std::vector<detection_box> mapped;
        mapped.reserve(detections.size());
        for (const auto &detection: detections) {
            detection_box mapped_detection = detection;
            mapped_detection.class_id = map_detection_class_id(context, detection.class_id);
            mapped.push_back(mapped_detection);
        }
        return mapped;
    }

    [[nodiscard]] std::vector<detection_box> filter_detections_by_class(const std::vector<detection_box> &detections,
                                                                        const std::unordered_set<int> &allowed_ids) {
        if (allowed_ids.empty()) {
            return detections;
        }
        std::vector<detection_box> filtered;
        filtered.reserve(detections.size());
        for (const auto &detection: detections) {
            if (allowed_ids.contains(detection.class_id)) {
                filtered.push_back(detection);
            }
        }
        return filtered;
    }

    [[nodiscard]] std::vector<groundtruth_box>
    filter_groundtruth_by_class(const std::vector<groundtruth_box> &groundtruth_boxes,
                                const std::unordered_set<int> &allowed_ids) {
        if (allowed_ids.empty()) {
            return groundtruth_boxes;
        }
        std::vector<groundtruth_box> filtered;
        filtered.reserve(groundtruth_boxes.size());
        for (const auto &groundtruth: groundtruth_boxes) {
            if (allowed_ids.contains(groundtruth.category_id)) {
                filtered.push_back(groundtruth);
            }
        }
        return filtered;
    }

    [[nodiscard]] int clamp_coordinate(const int value, const int upper_bound) {
        if (upper_bound <= 0) {
            return 0;
        }
        return std::max(0, std::min(value, upper_bound - 1));
    }

    [[nodiscard]] int remap_coordinate(const int value, const int src_size, const int dst_size) {
        if (src_size <= 0 || dst_size <= 0) {
            return 0;
        }
        const double scale = static_cast<double>(dst_size) / static_cast<double>(src_size);
        const int remapped = static_cast<int>(std::floor(static_cast<double>(value) * scale));
        return clamp_coordinate(remapped, dst_size);
    }

    [[nodiscard]] std::vector<int> get_semseg_eval_category_ids(const test_mode_context &context) {
        if (!context.semseg_groundtruth.eval_category_ids.empty()) {
            return context.semseg_groundtruth.eval_category_ids;
        }
        std::vector<int> category_ids;
        category_ids.reserve(context.semseg_groundtruth.category_names.size());
        for (const auto &[category_id, _]: context.semseg_groundtruth.category_names) {
            category_ids.push_back(category_id);
        }
        std::sort(category_ids.begin(), category_ids.end());
        return category_ids;
    }
} // namespace

void evaluate_test_mode_frame(test_mode_context &context, const int frame_index,
                              const std::vector<detection_box> &detections, GstDebugCategory *log_category) {
    std::lock_guard<std::mutex> lock(context.mutex);

    const auto allowed_detection_ids = build_allowed_set(context.allowed_detection_class_ids);
    const auto filtered_detections = filter_detections_by_class(detections, allowed_detection_ids);
    const auto mapped_detections = apply_detection_class_mapping(context, filtered_detections);
    const auto frame_iter = context.groundtruth.frames.find(frame_index);
    if (frame_iter == context.groundtruth.frames.end()) {
        sf::warning(log_category, "Test mode: no groundtruth for frame %d (detections: %zu)", frame_index,
                    detections.size());
        return;
    }

    const auto &groundtruth_boxes = frame_iter->second.boxes;
    const auto allowed_groundtruth_ids = build_allowed_set(context.allowed_groundtruth_category_ids);
    const auto filtered_groundtruth = filter_groundtruth_by_class(groundtruth_boxes, allowed_groundtruth_ids);
    const auto match_stats =
            match_detections_to_groundtruth(mapped_detections, filtered_groundtruth, context.iou_threshold);

    context.metrics.frames_with_groundtruth++;
    context.metrics.true_positive += match_stats.true_positive;
    context.metrics.false_positive += match_stats.false_positive;
    context.metrics.false_negative += match_stats.false_negative;
    context.metrics.matched_pairs += match_stats.matched_pairs;
    context.metrics.total_iou += match_stats.total_iou;

    const double frame_precision =
            safe_divide(static_cast<double>(match_stats.true_positive),
                        static_cast<double>(match_stats.true_positive + match_stats.false_positive));
    const double frame_recall =
            safe_divide(static_cast<double>(match_stats.true_positive),
                        static_cast<double>(match_stats.true_positive + match_stats.false_negative));
    const double frame_f1 = safe_divide(2.0 * frame_precision * frame_recall, frame_precision + frame_recall);
    const double frame_mean_iou = safe_divide(match_stats.total_iou, static_cast<double>(match_stats.matched_pairs));

    const auto detection_classes = extract_unique_detection_classes(mapped_detections);
    const auto groundtruth_classes = extract_unique_groundtruth_classes(filtered_groundtruth);
    const std::string detection_labels = format_class_list(detection_classes, context.groundtruth.category_names);
    const std::string groundtruth_labels = format_class_list(groundtruth_classes, context.groundtruth.category_names);

    test_mode_frame_result frame_result{};
    frame_result.frame_index = frame_index;
    frame_result.groundtruth_count = filtered_groundtruth.size();
    frame_result.detection_count = mapped_detections.size();
    frame_result.true_positive = match_stats.true_positive;
    frame_result.false_positive = match_stats.false_positive;
    frame_result.false_negative = match_stats.false_negative;
    frame_result.precision = frame_precision;
    frame_result.recall = frame_recall;
    frame_result.f1_score = frame_f1;
    frame_result.mean_iou = frame_mean_iou;
    frame_result.detection_classes = detection_labels;
    frame_result.groundtruth_classes = groundtruth_labels;
    context.frame_results.push_back(frame_result);

    test_mode_bbox_frame_snapshot bbox_snapshot{};
    bbox_snapshot.frame_index = frame_index;
    bbox_snapshot.detections = mapped_detections;
    bbox_snapshot.groundtruth_boxes = filtered_groundtruth;
    context.bbox_frame_snapshots.push_back(std::move(bbox_snapshot));

    if (!context.groundtruth.category_names.empty()) {
        sf::info(log_category,
                 "Test mode frame %d: gt=%zu det=%zu tp=%zu fp=%zu fn=%zu precision=%.3f recall=%.3f f1=%.3f "
                 "mean_iou=%.3f "
                 "det_classes=%s gt_classes=%s",
                 frame_index, filtered_groundtruth.size(), mapped_detections.size(), match_stats.true_positive,
                 match_stats.false_positive, match_stats.false_negative, frame_precision, frame_recall, frame_f1,
                 frame_mean_iou, detection_labels.c_str(), groundtruth_labels.c_str());
    } else {
        sf::info(log_category,
                 "Test mode frame %d: gt=%zu det=%zu tp=%zu fp=%zu fn=%zu precision=%.3f recall=%.3f f1=%.3f "
                 "mean_iou=%.3f",
                 frame_index, filtered_groundtruth.size(), mapped_detections.size(), match_stats.true_positive,
                 match_stats.false_positive, match_stats.false_negative, frame_precision, frame_recall, frame_f1,
                 frame_mean_iou);
    }
}

void evaluate_semseg_test_mode_frame(test_mode_context &context, const int frame_index,
                                     const semseg_prediction_map &prediction, const int frame_width,
                                     const int frame_height, GstDebugCategory *log_category) {
    if (prediction.width <= 0 || prediction.height <= 0 || prediction.class_map.empty()) {
        return;
    }
    if (prediction.class_map.size() < static_cast<size_t>(prediction.width * prediction.height)) {
        sf::warning(log_category, "SemSeg test mode frame %d: prediction map size mismatch", frame_index);
        return;
    }

    std::lock_guard<std::mutex> lock(context.mutex);

    const auto frame_iter = context.semseg_groundtruth.points_by_frame.find(frame_index);
    if (frame_iter == context.semseg_groundtruth.points_by_frame.end()) {
        return;
    }

    const auto eval_category_ids = get_semseg_eval_category_ids(context);
    if (eval_category_ids.empty()) {
        return;
    }

    const auto eval_category_set = build_allowed_set(eval_category_ids);
    const auto &groundtruth_points = frame_iter->second;
    if (groundtruth_points.empty()) {
        return;
    }

    std::unordered_map<int, semseg_class_metrics> frame_metrics_by_class;
    frame_metrics_by_class.reserve(eval_category_ids.size());
    for (const int class_id: eval_category_ids) {
        frame_metrics_by_class.emplace(class_id, semseg_class_metrics{});
    }

    size_t evaluated_points = 0;
    for (const auto &point: groundtruth_points) {
        if (!eval_category_set.contains(point.class_id)) {
            continue;
        }
        const int remapped_x = remap_coordinate(point.x_coord, frame_width, prediction.width);
        const int remapped_y = remap_coordinate(point.y_coord, frame_height, prediction.height);
        const size_t index = static_cast<size_t>(remapped_y * prediction.width + remapped_x);
        if (index >= prediction.class_map.size()) {
            continue;
        }

        const int gt_class = point.class_id;
        const int mapped_prediction_class = map_semseg_prediction_class_id(context, prediction.class_map[index]);
        const bool predicted_in_eval_set = eval_category_set.contains(mapped_prediction_class);
        ++evaluated_points;

        for (const int class_id: eval_category_ids) {
            const bool gt_is_class = (gt_class == class_id);
            const bool pred_is_class = predicted_in_eval_set && (mapped_prediction_class == class_id);

            auto &global_metrics = context.semseg_metrics_by_class[class_id];
            auto &frame_metrics = frame_metrics_by_class[class_id];

            if (gt_is_class && pred_is_class) {
                ++global_metrics.intersection;
                ++frame_metrics.intersection;
            }
            if (gt_is_class || pred_is_class) {
                ++global_metrics.union_count;
                ++frame_metrics.union_count;
            }
        }
    }

    if (evaluated_points == 0) {
        return;
    }

    ++context.semseg_frames_with_groundtruth;
    context.semseg_points_evaluated += evaluated_points;

    size_t frame_classes_with_union = 0;
    double frame_iou_sum = 0.0;
    for (const int class_id: eval_category_ids) {
        const auto metrics_iter = frame_metrics_by_class.find(class_id);
        if (metrics_iter == frame_metrics_by_class.end() || metrics_iter->second.union_count == 0) {
            continue;
        }
        ++frame_classes_with_union;
        frame_iou_sum += safe_divide(static_cast<double>(metrics_iter->second.intersection),
                                     static_cast<double>(metrics_iter->second.union_count));
    }
    const double frame_mean_iou =
            frame_classes_with_union > 0 ? frame_iou_sum / static_cast<double>(frame_classes_with_union) : 0.0;

    semseg_test_mode_frame_result frame_result{};
    frame_result.frame_index = frame_index;
    frame_result.groundtruth_point_count = evaluated_points;
    frame_result.classes_with_union = frame_classes_with_union;
    frame_result.mean_iou = frame_mean_iou;
    context.semseg_frame_results.push_back(frame_result);
}
