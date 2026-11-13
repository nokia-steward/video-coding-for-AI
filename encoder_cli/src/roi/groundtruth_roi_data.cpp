// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "groundtruth_roi_data.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <gst/gst.h>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#endif
#include <json/json.h>
#include "../../libvcai/include/logging.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/**
 * @file groundtruth_roi_data.cpp
 * @brief Implementation of COCO-format groundtruth ROI data loading.
 *
 * Parses a COCO JSON file and builds a per-frame mapping of bounding box
 * rectangles. Frame indices are derived from image filenames when possible,
 * otherwise the positional order in the images array is used.
 */

/// GStreamer debug category for groundtruth ROI parsing.
GST_DEBUG_CATEGORY_STATIC(groundtruth_roi);

void init_groundtruth_roi_debug_category() {
    GST_DEBUG_CATEGORY_INIT(groundtruth_roi, "groundtruth-roi", 0,
                            "Loading per-frame groundtruth ROI data from COCO JSON files");
}

GstDebugCategory *get_groundtruth_roi_debug_category() { return groundtruth_roi; }

namespace {
    /**
     * @brief Entry representing a single image in the COCO images array.
     */
    struct image_entry {
        int image_id = 0;
        std::string file_name;
        std::optional<int> frame_index;
    };

    /**
     * @brief Attempts to parse a frame index from an image filename.
     *
     * Looks for a trailing "_NNN" pattern before the file extension.
     * For example, "frame_042.jpg" yields 42.
     *
     * @param file_name Image filename to parse
     * @return Frame index if parseable, std::nullopt otherwise
     */
    [[nodiscard]] std::optional<int> parse_frame_index_from_filename(const std::string &file_name) {
        const auto dot_position = file_name.find_last_of('.');
        const auto underscore_position = file_name.find_last_of('_', dot_position);
        if (dot_position == std::string::npos || underscore_position == std::string::npos ||
            underscore_position + 1 >= dot_position) {
            return std::nullopt;
        }

        const auto number_str = file_name.substr(underscore_position + 1, dot_position - underscore_position - 1);
        if (number_str.empty() || std::any_of(number_str.begin(), number_str.end(), [](const unsigned char character) {
                return std::isdigit(character) == 0;
            })) {
            return std::nullopt;
        }

        try {
            return std::stoi(number_str);
        } catch (const std::exception &) {
            return std::nullopt;
        }
    }

    /**
     * @brief Validates that a COCO JSON root contains the required top-level arrays.
     * @param root Parsed JSON root value
     * @return true if valid, false otherwise
     */
    [[nodiscard]] bool validate_coco_structure(const Json::Value &root) {
        if (!root.isObject()) {
            sf::error(groundtruth_roi, "Groundtruth JSON root is not an object");
            return false;
        }
        if (!root.isMember("images") || !root["images"].isArray()) {
            sf::error(groundtruth_roi, "Groundtruth JSON missing 'images' array");
            return false;
        }
        if (!root.isMember("annotations") || !root["annotations"].isArray()) {
            sf::error(groundtruth_roi, "Groundtruth JSON missing 'annotations' array");
            return false;
        }
        return true;
    }

    /**
     * @brief Parses the images array and builds the image_id to frame_index mapping.
     * @param images_json The COCO "images" JSON array
     * @return Pair of (image entries, whether all filenames had parseable frame indices)
     */
    [[nodiscard]] std::pair<std::vector<image_entry>, bool> parse_images(const Json::Value &images_json) {
        std::vector<image_entry> images;
        images.reserve(images_json.size());
        bool all_have_suffix = true;

        for (const auto &image_json: images_json) {
            if (!image_json.isObject() || !image_json.isMember("id") || !image_json.isMember("file_name")) {
                sf::warning(groundtruth_roi, "Skipping invalid image entry in groundtruth JSON");
                all_have_suffix = false;
                continue;
            }

            const int image_id = image_json["id"].asInt();
            const std::string file_name = image_json["file_name"].asString();
            auto frame_index_opt = parse_frame_index_from_filename(file_name);
            if (!frame_index_opt.has_value()) {
                all_have_suffix = false;
            }

            images.push_back({image_id, file_name, frame_index_opt});
        }

        return {images, all_have_suffix};
    }

    /**
     * @brief Builds image_id -> frame_index mapping from parsed image entries.
     * @param images Parsed image entries
     * @param all_have_suffix Whether all images had parseable filename indices
     * @return Map from image_id to frame_index
     */
    [[nodiscard]] std::unordered_map<int, int> build_image_id_to_frame_index(const std::vector<image_entry> &images,
                                                                             const bool all_have_suffix) {
        std::unordered_map<int, int> mapping;
        mapping.reserve(images.size());

        for (size_t index = 0; index < images.size(); ++index) {
            const int frame_index = all_have_suffix && images[index].frame_index.has_value()
                                            ? images[index].frame_index.value()
                                            : static_cast<int>(index);
            if (mapping.contains(images[index].image_id)) {
                sf::warning(groundtruth_roi, "Duplicate image_id %d in groundtruth JSON", images[index].image_id);
                continue;
            }
            mapping.emplace(images[index].image_id, frame_index);
        }

        return mapping;
    }

    /**
     * @brief Parses annotations and builds the per-frame rectangle map.
     * @param annotations_json The COCO "annotations" JSON array
     * @param image_id_to_frame Map from image_id to frame_index
     * @return Per-frame rectangle map
     */
    [[nodiscard]] std::unordered_map<int, std::vector<sf::rectangle>>
    parse_annotations(const Json::Value &annotations_json, const std::unordered_map<int, int> &image_id_to_frame) {
        std::unordered_map<int, std::vector<sf::rectangle>> frames;

        for (const auto &annotation_json: annotations_json) {
            if (!annotation_json.isObject() || !annotation_json.isMember("image_id") ||
                !annotation_json.isMember("bbox") || !annotation_json["bbox"].isArray() ||
                annotation_json["bbox"].size() != 4) {
                sf::warning(groundtruth_roi, "Skipping invalid annotation entry in groundtruth JSON");
                continue;
            }

            const int image_id = annotation_json["image_id"].asInt();
            const auto frame_iter = image_id_to_frame.find(image_id);
            if (frame_iter == image_id_to_frame.end()) {
                sf::warning(groundtruth_roi, "Annotation references unknown image_id %d", image_id);
                continue;
            }

            const int frame_index = frame_iter->second;
            const auto &bbox_json = annotation_json["bbox"];

            // COCO bbox is [x, y, width, height] in floating point
            const auto bbox_x = static_cast<int>(std::round(bbox_json[0].asDouble()));
            const auto bbox_y = static_cast<int>(std::round(bbox_json[1].asDouble()));
            const auto bbox_w = static_cast<int>(std::round(bbox_json[2].asDouble()));
            const auto bbox_h = static_cast<int>(std::round(bbox_json[3].asDouble()));

            if (bbox_w <= 0 || bbox_h <= 0) {
                sf::warning(groundtruth_roi, "Skipping annotation with non-positive dimensions: w=%d, h=%d", bbox_w,
                            bbox_h);
                continue;
            }

            frames[frame_index].emplace_back(bbox_x, bbox_y, bbox_w, bbox_h);
        }

        return frames;
    }
} // namespace

/**
 * @brief Loads per-frame groundtruth ROI data from a COCO-format JSON file.
 * @param file_path Path to the COCO JSON file.
 * @return Parsed groundtruth ROI data or nullopt on error.
 */
std::optional<groundtruth_roi_data> load_groundtruth_roi_data(const std::filesystem::path &file_path) {
    if (!std::filesystem::exists(file_path)) {
        sf::error(groundtruth_roi, "Groundtruth ROI file not found at %s", file_path.c_str());
        return std::nullopt;
    }

    std::ifstream file_stream(file_path);
    if (!file_stream.is_open()) {
        sf::error(groundtruth_roi, "Failed to open groundtruth ROI file for reading: %s", file_path.c_str());
        return std::nullopt;
    }

    Json::Value root;
    std::string parse_errors;

    if (Json::CharReaderBuilder reader_builder;
        !Json::parseFromStream(reader_builder, file_stream, &root, &parse_errors)) {
        sf::error(groundtruth_roi, "Failed to parse groundtruth JSON file %s: %s", file_path.c_str(),
                  parse_errors.c_str());
        return std::nullopt;
    }

    if (!validate_coco_structure(root)) {
        return std::nullopt;
    }

    const auto &images_json = root["images"];
    const auto &annotations_json = root["annotations"];

    const auto [images, all_have_suffix] = parse_images(images_json);

    if (images.empty()) {
        sf::error(groundtruth_roi, "Groundtruth JSON contains no valid images");
        return std::nullopt;
    }

    const auto image_id_to_frame = build_image_id_to_frame_index(images, all_have_suffix);
    auto frames = parse_annotations(annotations_json, image_id_to_frame);

    // Ensure all images have an entry (even if empty) so frame lookups are predictable
    for (size_t index = 0; index < images.size(); ++index) {
        const int frame_index = all_have_suffix && images[index].frame_index.has_value()
                                        ? images[index].frame_index.value()
                                        : static_cast<int>(index);
        if (!frames.contains(frame_index)) {
            frames[frame_index] = {};
        }
    }

    sf::info(groundtruth_roi, "Loaded groundtruth ROI data: %zu images, %zu frames with annotations", images.size(),
             frames.size());

    return groundtruth_roi_data{std::move(frames)};
}
