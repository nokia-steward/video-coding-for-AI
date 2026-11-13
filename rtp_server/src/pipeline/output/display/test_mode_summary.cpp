#include "test_mode_summary.h"

#include <fstream>
#include <iomanip>
#include <sstream>

#include "logging.h"

std::string format_test_mode_summary_markdown(const test_mode_run_summary &summary,
                                              const std::vector<test_mode_frame_result> &frame_results,
                                              const guint16 port) {
    std::ostringstream stream;
    stream << "# Test Mode Summary\n\n";
    stream << "- Port: " << port << "\n";
    stream << "- IoU threshold: " << std::fixed << std::setprecision(3) << summary.iou_threshold << "\n";
    stream << "- Frames with groundtruth: " << summary.frames_with_groundtruth << "\n";
    stream << "- Groundtruth total: " << summary.groundtruth_total << "\n";
    stream << "- Detection total: " << summary.detection_total << "\n";
    stream << "- True positive: " << summary.true_positive << "\n";
    stream << "- False positive: " << summary.false_positive << "\n";
    stream << "- False negative: " << summary.false_negative << "\n";
    stream << "- Precision: " << std::fixed << std::setprecision(3) << summary.precision << "\n";
    stream << "- Recall: " << std::fixed << std::setprecision(3) << summary.recall << "\n";
    stream << "- F1 score: " << std::fixed << std::setprecision(3) << summary.f1_score << "\n";
    stream << "- Mean IoU: " << std::fixed << std::setprecision(3) << summary.mean_iou << "\n";
    stream << "- mAP@IoU threshold: " << std::fixed << std::setprecision(3) << summary.map_50 << "\n";
    if (!summary.has_groundtruth) {
        stream << "- Note: no frames with groundtruth evaluated\n";
    }
    stream << "\n";
    stream << "## Frame Results\n\n";
    if (frame_results.empty()) {
        stream << "- No frame results captured\n";
        return stream.str();
    }
    for (const auto &frame: frame_results) {
        stream << "- Frame " << frame.frame_index << ": gt=" << frame.groundtruth_count
               << " det=" << frame.detection_count << " tp=" << frame.true_positive << " fp=" << frame.false_positive
               << " fn=" << frame.false_negative << " precision=" << std::fixed << std::setprecision(3)
               << frame.precision << " recall=" << std::fixed << std::setprecision(3) << frame.recall
               << " f1=" << std::fixed << std::setprecision(3) << frame.f1_score << " mean_iou=" << std::fixed
               << std::setprecision(3) << frame.mean_iou;
        if (!frame.detection_classes.empty()) {
            stream << " det_classes=" << frame.detection_classes;
        }
        if (!frame.groundtruth_classes.empty()) {
            stream << " gt_classes=" << frame.groundtruth_classes;
        }
        stream << "\n";
    }
    return stream.str();
}

std::string format_test_mode_summary_json(const test_mode_run_summary &summary,
                                          const std::vector<test_mode_frame_result> &frame_results,
                                          const guint16 port) {
    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"port\": " << port << ",\n";
    stream << "  \"iou_threshold\": " << std::fixed << std::setprecision(3) << summary.iou_threshold << ",\n";
    stream << "  \"has_groundtruth\": " << (summary.has_groundtruth ? "true" : "false") << ",\n";
    stream << "  \"frames_with_groundtruth\": " << summary.frames_with_groundtruth << ",\n";
    stream << "  \"groundtruth_total\": " << summary.groundtruth_total << ",\n";
    stream << "  \"detection_total\": " << summary.detection_total << ",\n";
    stream << "  \"true_positive\": " << summary.true_positive << ",\n";
    stream << "  \"false_positive\": " << summary.false_positive << ",\n";
    stream << "  \"false_negative\": " << summary.false_negative << ",\n";
    stream << "  \"precision\": " << std::fixed << std::setprecision(3) << summary.precision << ",\n";
    stream << "  \"recall\": " << std::fixed << std::setprecision(3) << summary.recall << ",\n";
    stream << "  \"f1_score\": " << std::fixed << std::setprecision(3) << summary.f1_score << ",\n";
    stream << "  \"mean_iou\": " << std::fixed << std::setprecision(3) << summary.mean_iou << ",\n";
    stream << "  \"map_50\": " << std::fixed << std::setprecision(3) << summary.map_50;
    if (!summary.has_groundtruth) {
        stream << ",\n  \"note\": \"no frames with groundtruth evaluated\"";
    }
    stream << ",\n  \"frames\": [";
    if (!frame_results.empty()) {
        stream << "\n";
        for (size_t index = 0; index < frame_results.size(); ++index) {
            const auto &frame = frame_results[index];
            stream << "    {\n";
            stream << "      \"frame_index\": " << frame.frame_index << ",\n";
            stream << "      \"groundtruth_count\": " << frame.groundtruth_count << ",\n";
            stream << "      \"detection_count\": " << frame.detection_count << ",\n";
            stream << "      \"true_positive\": " << frame.true_positive << ",\n";
            stream << "      \"false_positive\": " << frame.false_positive << ",\n";
            stream << "      \"false_negative\": " << frame.false_negative << ",\n";
            stream << "      \"precision\": " << std::fixed << std::setprecision(3) << frame.precision << ",\n";
            stream << "      \"recall\": " << std::fixed << std::setprecision(3) << frame.recall << ",\n";
            stream << "      \"f1_score\": " << std::fixed << std::setprecision(3) << frame.f1_score << ",\n";
            stream << "      \"mean_iou\": " << std::fixed << std::setprecision(3) << frame.mean_iou;
            if (!frame.detection_classes.empty()) {
                stream << ",\n      \"detection_classes\": \"" << frame.detection_classes << "\"";
            }
            if (!frame.groundtruth_classes.empty()) {
                stream << ",\n      \"groundtruth_classes\": \"" << frame.groundtruth_classes << "\"";
            }
            stream << "\n    }";
            if (index + 1 < frame_results.size()) {
                stream << ",";
            }
            stream << "\n";
        }
        stream << "  ";
    }
    stream << "]\n";
    stream << "}\n";
    return stream.str();
}

std::string format_semseg_test_mode_summary_markdown(const semseg_test_mode_run_summary &summary,
                                                     const std::vector<semseg_test_mode_frame_result> &frame_results,
                                                     const guint16 port) {
    std::ostringstream stream;
    stream << "# SemSeg Test Mode Summary\n\n";
    stream << "- Port: " << port << "\n";
    stream << "- Frames with groundtruth: " << summary.frames_with_groundtruth << "\n";
    stream << "- Evaluated points: " << summary.points_evaluated << "\n";
    stream << "- Mean IoU: " << std::fixed << std::setprecision(3) << summary.mean_iou << "\n";
    if (!summary.has_groundtruth) {
        stream << "- Note: no frames with semseg groundtruth evaluated\n";
    }
    stream << "\n";

    stream << "## Per-class IoU\n\n";
    if (summary.class_summaries.empty()) {
        stream << "- No per-class semseg results captured\n";
    } else {
        for (const auto &class_summary: summary.class_summaries) {
            stream << "- Class " << class_summary.class_id;
            if (!class_summary.class_name.empty()) {
                stream << " (" << class_summary.class_name << ")";
            }
            stream << ": intersection=" << class_summary.intersection << " union=" << class_summary.union_count
                   << " iou=" << std::fixed << std::setprecision(3) << class_summary.iou << "\n";
        }
    }
    stream << "\n";

    stream << "## Frame Results\n\n";
    if (frame_results.empty()) {
        stream << "- No semseg frame results captured\n";
        return stream.str();
    }
    for (const auto &frame: frame_results) {
        stream << "- Frame " << frame.frame_index << ": points=" << frame.groundtruth_point_count
               << " active_classes=" << frame.classes_with_union << " mean_iou=" << std::fixed << std::setprecision(3)
               << frame.mean_iou << "\n";
    }
    return stream.str();
}

std::string format_semseg_test_mode_summary_json(const semseg_test_mode_run_summary &summary,
                                                 const std::vector<semseg_test_mode_frame_result> &frame_results,
                                                 const guint16 port) {
    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"port\": " << port << ",\n";
    stream << "  \"has_groundtruth\": " << (summary.has_groundtruth ? "true" : "false") << ",\n";
    stream << "  \"frames_with_groundtruth\": " << summary.frames_with_groundtruth << ",\n";
    stream << "  \"points_evaluated\": " << summary.points_evaluated << ",\n";
    stream << "  \"mean_iou\": " << std::fixed << std::setprecision(3) << summary.mean_iou << ",\n";
    stream << "  \"classes\": [";
    if (!summary.class_summaries.empty()) {
        stream << "\n";
        for (size_t index = 0; index < summary.class_summaries.size(); ++index) {
            const auto &class_summary = summary.class_summaries[index];
            stream << "    {\n";
            stream << "      \"class_id\": " << class_summary.class_id << ",\n";
            stream << "      \"class_name\": \"" << class_summary.class_name << "\",\n";
            stream << "      \"intersection\": " << class_summary.intersection << ",\n";
            stream << "      \"union\": " << class_summary.union_count << ",\n";
            stream << "      \"iou\": " << std::fixed << std::setprecision(3) << class_summary.iou << "\n";
            stream << "    }";
            if (index + 1 < summary.class_summaries.size()) {
                stream << ",";
            }
            stream << "\n";
        }
        stream << "  ";
    }
    stream << "],\n";

    stream << "  \"frames\": [";
    if (!frame_results.empty()) {
        stream << "\n";
        for (size_t index = 0; index < frame_results.size(); ++index) {
            const auto &frame = frame_results[index];
            stream << "    {\n";
            stream << "      \"frame_index\": " << frame.frame_index << ",\n";
            stream << "      \"groundtruth_point_count\": " << frame.groundtruth_point_count << ",\n";
            stream << "      \"classes_with_union\": " << frame.classes_with_union << ",\n";
            stream << "      \"mean_iou\": " << std::fixed << std::setprecision(3) << frame.mean_iou << "\n";
            stream << "    }";
            if (index + 1 < frame_results.size()) {
                stream << ",";
            }
            stream << "\n";
        }
        stream << "  ";
    }
    stream << "]\n";
    stream << "}\n";
    return stream.str();
}

bool write_test_mode_summary_file(const std::string &path, const std::string &contents,
                                  GstDebugCategory *log_category) {
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        sf::warning(log_category, "Failed to open test mode summary file: %s", path.c_str());
        return false;
    }

    file << contents;
    if (!file.good()) {
        sf::warning(log_category, "Failed to write test mode summary file: %s", path.c_str());
        return false;
    }

    sf::info(log_category, "Wrote test mode summary file: %s", path.c_str());
    return true;
}
