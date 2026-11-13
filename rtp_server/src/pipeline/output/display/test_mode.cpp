#include "test_mode.h"

#include <algorithm>
#include <cinttypes>
#include <limits>
#include <unordered_map>
#include <vector>

#include "logging.h"
#include "test_mode_summary.h"

namespace {
    [[nodiscard]] double safe_divide(const double numerator, const double denominator) {
        if (denominator <= 0.0) {
            return 0.0;
        }
        return numerator / denominator;
    }

    [[nodiscard]] double compute_iou(const detection_box &detection, const groundtruth_box &groundtruth) {
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
            return 0.0;
        }
        return static_cast<double>(intersect_area / union_area);
    }

    struct scored_detection {
        int frame_index = 0;
        float confidence = 0.0F;
        detection_box detection;
    };

    [[nodiscard]] double compute_average_precision(const std::vector<int> &true_positive_flags,
                                                   const std::vector<int> &false_positive_flags,
                                                   const size_t groundtruth_total) {
        if (groundtruth_total == 0 || true_positive_flags.empty()) {
            return 0.0;
        }

        std::vector<double> recalls;
        std::vector<double> precisions;
        recalls.reserve(true_positive_flags.size());
        precisions.reserve(true_positive_flags.size());

        uint64_t cumulative_true_positive = 0;
        uint64_t cumulative_false_positive = 0;
        for (size_t index = 0; index < true_positive_flags.size(); ++index) {
            cumulative_true_positive += static_cast<uint64_t>(true_positive_flags[index]);
            cumulative_false_positive += static_cast<uint64_t>(false_positive_flags[index]);

            const double precision =
                    safe_divide(static_cast<double>(cumulative_true_positive),
                                static_cast<double>(cumulative_true_positive + cumulative_false_positive));
            const double recall =
                    safe_divide(static_cast<double>(cumulative_true_positive), static_cast<double>(groundtruth_total));
            precisions.push_back(precision);
            recalls.push_back(recall);
        }

        std::vector<double> monotonic_precisions;
        std::vector<double> monotonic_recalls;
        monotonic_precisions.reserve(precisions.size() + 2);
        monotonic_recalls.reserve(recalls.size() + 2);

        monotonic_precisions.push_back(0.0);
        monotonic_precisions.insert(monotonic_precisions.end(), precisions.begin(), precisions.end());
        monotonic_precisions.push_back(0.0);

        monotonic_recalls.push_back(0.0);
        monotonic_recalls.insert(monotonic_recalls.end(), recalls.begin(), recalls.end());
        monotonic_recalls.push_back(1.0);

        for (size_t reverse_index = monotonic_precisions.size() - 1; reverse_index > 0; --reverse_index) {
            monotonic_precisions[reverse_index - 1] =
                    std::max(monotonic_precisions[reverse_index - 1], monotonic_precisions[reverse_index]);
        }

        double average_precision = 0.0;
        for (size_t index = 0; index + 1 < monotonic_recalls.size(); ++index) {
            const double recall_delta = monotonic_recalls[index + 1] - monotonic_recalls[index];
            if (recall_delta > 0.0) {
                average_precision += recall_delta * monotonic_precisions[index + 1];
            }
        }
        return average_precision;
    }

    [[nodiscard]] double compute_map_at_iou_threshold(const std::vector<test_mode_bbox_frame_snapshot> &snapshots,
                                                      const double iou_threshold) {
        std::unordered_map<int, std::unordered_map<int, std::vector<groundtruth_box>>> groundtruth_by_class_by_frame;
        std::unordered_map<int, std::vector<scored_detection>> detections_by_class;

        for (const auto &snapshot: snapshots) {
            for (const auto &groundtruth: snapshot.groundtruth_boxes) {
                groundtruth_by_class_by_frame[groundtruth.category_id][snapshot.frame_index].push_back(groundtruth);
            }
            for (const auto &detection: snapshot.detections) {
                scored_detection scored{};
                scored.frame_index = snapshot.frame_index;
                scored.confidence = detection.confidence;
                scored.detection = detection;
                detections_by_class[detection.class_id].push_back(scored);
            }
        }

        if (groundtruth_by_class_by_frame.empty()) {
            return 0.0;
        }

        double ap_sum = 0.0;
        size_t class_count = 0;

        for (const auto &[class_id, class_groundtruth_by_frame]: groundtruth_by_class_by_frame) {
            size_t groundtruth_total = 0;
            std::unordered_map<int, std::vector<bool>> matched_groundtruth_by_frame;

            for (const auto &[frame_index, frame_groundtruth]: class_groundtruth_by_frame) {
                groundtruth_total += frame_groundtruth.size();
                matched_groundtruth_by_frame[frame_index] = std::vector<bool>(frame_groundtruth.size(), false);
            }
            if (groundtruth_total == 0) {
                continue;
            }

            std::vector<scored_detection> class_detections;
            const auto detections_iter = detections_by_class.find(class_id);
            if (detections_iter != detections_by_class.end()) {
                class_detections = detections_iter->second;
            }

            std::stable_sort(class_detections.begin(), class_detections.end(),
                             [](const scored_detection &lhs, const scored_detection &rhs) {
                                 return lhs.confidence > rhs.confidence;
                             });

            std::vector<int> true_positive_flags;
            std::vector<int> false_positive_flags;
            true_positive_flags.reserve(class_detections.size());
            false_positive_flags.reserve(class_detections.size());

            for (const auto &scored_detection_item: class_detections) {
                const auto frame_groundtruth_iter = class_groundtruth_by_frame.find(scored_detection_item.frame_index);
                if (frame_groundtruth_iter == class_groundtruth_by_frame.end()) {
                    true_positive_flags.push_back(0);
                    false_positive_flags.push_back(1);
                    continue;
                }

                const auto &frame_groundtruth = frame_groundtruth_iter->second;
                double best_iou = 0.0;
                size_t best_groundtruth_index = std::numeric_limits<size_t>::max();
                for (size_t groundtruth_index = 0; groundtruth_index < frame_groundtruth.size(); ++groundtruth_index) {
                    const double current_iou =
                            compute_iou(scored_detection_item.detection, frame_groundtruth[groundtruth_index]);
                    if (current_iou > best_iou) {
                        best_iou = current_iou;
                        best_groundtruth_index = groundtruth_index;
                    }
                }

                if (best_groundtruth_index == std::numeric_limits<size_t>::max() || best_iou < iou_threshold) {
                    true_positive_flags.push_back(0);
                    false_positive_flags.push_back(1);
                    continue;
                }

                auto &matched_for_frame = matched_groundtruth_by_frame[scored_detection_item.frame_index];
                if (matched_for_frame[best_groundtruth_index]) {
                    true_positive_flags.push_back(0);
                    false_positive_flags.push_back(1);
                    continue;
                }

                matched_for_frame[best_groundtruth_index] = true;
                true_positive_flags.push_back(1);
                false_positive_flags.push_back(0);
            }

            ap_sum += compute_average_precision(true_positive_flags, false_positive_flags, groundtruth_total);
            class_count++;
        }

        if (class_count == 0) {
            return 0.0;
        }
        return ap_sum / static_cast<double>(class_count);
    }
} // namespace

test_mode_run_summary build_test_mode_run_summary(const test_mode_context &context) {
    std::lock_guard<std::mutex> lock(context.mutex);

    test_mode_run_summary summary{};
    summary.frames_with_groundtruth = context.metrics.frames_with_groundtruth;
    summary.true_positive = context.metrics.true_positive;
    summary.false_positive = context.metrics.false_positive;
    summary.false_negative = context.metrics.false_negative;
    summary.groundtruth_total = context.metrics.true_positive + context.metrics.false_negative;
    summary.detection_total = context.metrics.true_positive + context.metrics.false_positive;
    summary.precision =
            safe_divide(static_cast<double>(context.metrics.true_positive),
                        static_cast<double>(context.metrics.true_positive + context.metrics.false_positive));
    summary.recall = safe_divide(static_cast<double>(context.metrics.true_positive),
                                 static_cast<double>(context.metrics.true_positive + context.metrics.false_negative));
    summary.f1_score = safe_divide(2.0 * summary.precision * summary.recall, summary.precision + summary.recall);
    summary.mean_iou = safe_divide(context.metrics.total_iou, static_cast<double>(context.metrics.matched_pairs));
    summary.map_50 = compute_map_at_iou_threshold(context.bbox_frame_snapshots, context.iou_threshold);
    summary.has_groundtruth = context.metrics.frames_with_groundtruth > 0;
    summary.iou_threshold = context.iou_threshold;

    return summary;
}

semseg_test_mode_run_summary build_semseg_test_mode_run_summary(const test_mode_context &context) {
    std::lock_guard<std::mutex> lock(context.mutex);

    semseg_test_mode_run_summary summary{};
    summary.frames_with_groundtruth = context.semseg_frames_with_groundtruth;
    summary.points_evaluated = context.semseg_points_evaluated;
    summary.has_groundtruth = context.semseg_frames_with_groundtruth > 0;

    std::vector<int> class_ids = context.semseg_groundtruth.eval_category_ids;
    if (class_ids.empty()) {
        class_ids.reserve(context.semseg_metrics_by_class.size());
        for (const auto &[class_id, _]: context.semseg_metrics_by_class) {
            class_ids.push_back(class_id);
        }
        std::sort(class_ids.begin(), class_ids.end());
    }

    size_t classes_with_union = 0;
    double iou_sum = 0.0;
    summary.class_summaries.reserve(class_ids.size());
    for (const int class_id: class_ids) {
        semseg_test_mode_class_summary class_summary{};
        class_summary.class_id = class_id;
        const auto name_iter = context.semseg_groundtruth.category_names.find(class_id);
        if (name_iter != context.semseg_groundtruth.category_names.end()) {
            class_summary.class_name = name_iter->second;
        }

        const auto metrics_iter = context.semseg_metrics_by_class.find(class_id);
        if (metrics_iter != context.semseg_metrics_by_class.end()) {
            class_summary.intersection = metrics_iter->second.intersection;
            class_summary.union_count = metrics_iter->second.union_count;
        }
        class_summary.iou = safe_divide(static_cast<double>(class_summary.intersection),
                                        static_cast<double>(class_summary.union_count));
        if (class_summary.union_count > 0) {
            ++classes_with_union;
            iou_sum += class_summary.iou;
        }
        summary.class_summaries.push_back(class_summary);
    }

    summary.mean_iou = classes_with_union > 0 ? iou_sum / static_cast<double>(classes_with_union) : 0.0;
    return summary;
}

std::vector<test_mode_frame_result> build_test_mode_frame_results(const test_mode_context &context) {
    std::lock_guard<std::mutex> lock(context.mutex);
    return context.frame_results;
}

std::vector<semseg_test_mode_frame_result> build_semseg_test_mode_frame_results(const test_mode_context &context) {
    std::lock_guard<std::mutex> lock(context.mutex);
    return context.semseg_frame_results;
}

void log_test_mode_run_summary(const test_mode_run_summary &summary, const guint16 port,
                               GstDebugCategory *log_category) {
    if (!summary.has_groundtruth) {
        sf::info(log_category, "Test mode summary for port %u: no frames with groundtruth evaluated", port);
        return;
    }

    sf::info(log_category,
             "Test mode summary for port %u: frames=%" PRIu64 " gt_total=%" PRIu64 " det_total=%" PRIu64 " tp=%" PRIu64
             " fp=%" PRIu64 " fn=%" PRIu64 " precision=%.3f recall=%.3f f1=%.3f mean_iou=%.3f map_50=%.3f",
             port, summary.frames_with_groundtruth, summary.groundtruth_total, summary.detection_total,
             summary.true_positive, summary.false_positive, summary.false_negative, summary.precision, summary.recall,
             summary.f1_score, summary.mean_iou, summary.map_50);
}

void log_semseg_test_mode_run_summary(const semseg_test_mode_run_summary &summary, const guint16 port,
                                      GstDebugCategory *log_category) {
    if (!summary.has_groundtruth) {
        sf::info(log_category, "SemSeg test mode summary for port %u: no frames with groundtruth evaluated", port);
        return;
    }

    sf::info(log_category, "SemSeg test mode summary for port %u: frames=%" PRIu64 " points=%" PRIu64 " mean_iou=%.3f",
             port, summary.frames_with_groundtruth, summary.points_evaluated, summary.mean_iou);
}

bool write_test_mode_run_summary_markdown(const test_mode_run_summary &summary,
                                          const std::vector<test_mode_frame_result> &frame_results, const guint16 port,
                                          const std::string &path, GstDebugCategory *log_category) {
    return write_test_mode_summary_file(path, format_test_mode_summary_markdown(summary, frame_results, port),
                                        log_category);
}

bool write_test_mode_run_summary_json(const test_mode_run_summary &summary,
                                      const std::vector<test_mode_frame_result> &frame_results, const guint16 port,
                                      const std::string &path, GstDebugCategory *log_category) {
    return write_test_mode_summary_file(path, format_test_mode_summary_json(summary, frame_results, port),
                                        log_category);
}

bool write_semseg_test_mode_run_summary_markdown(const semseg_test_mode_run_summary &summary,
                                                 const std::vector<semseg_test_mode_frame_result> &frame_results,
                                                 const guint16 port, const std::string &path,
                                                 GstDebugCategory *log_category) {
    return write_test_mode_summary_file(path, format_semseg_test_mode_summary_markdown(summary, frame_results, port),
                                        log_category);
}

bool write_semseg_test_mode_run_summary_json(const semseg_test_mode_run_summary &summary,
                                             const std::vector<semseg_test_mode_frame_result> &frame_results,
                                             const guint16 port, const std::string &path,
                                             GstDebugCategory *log_category) {
    return write_test_mode_summary_file(path, format_semseg_test_mode_summary_json(summary, frame_results, port),
                                        log_category);
}
