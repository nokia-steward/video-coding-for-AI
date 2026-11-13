#include "test_mode_groundtruth.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <json/json.h>

#include "logging.h"

namespace {
    struct image_entry {
        int image_id = 0;
        std::string file_name;
        std::optional<int> frame_index;
    };

    [[nodiscard]] std::optional<int> parse_frame_index_from_filename(const std::string &file_name) {
        const auto dot_position = file_name.find_last_of('.');
        const auto underscore_position = file_name.find_last_of('_', dot_position);
        if (dot_position == std::string::npos || underscore_position == std::string::npos ||
            underscore_position + 1 >= dot_position) {
            return std::nullopt;
        }

        const auto number_str = file_name.substr(underscore_position + 1, dot_position - underscore_position - 1);
        if (number_str.empty() || std::any_of(number_str.begin(), number_str.end(),
                                              [](unsigned char character) { return !std::isdigit(character); })) {
            return std::nullopt;
        }

        try {
            return std::stoi(number_str);
        } catch (const std::exception &) {
            return std::nullopt;
        }
    }

    [[nodiscard]] std::unordered_set<int> build_eval_category_set(const std::vector<int> &ids) {
        std::unordered_set<int> result;
        result.reserve(ids.size());
        for (const int id: ids) {
            result.insert(id);
        }
        return result;
    }
} // namespace

std::optional<groundtruth_data> load_groundtruth_data(const std::string &path, GstDebugCategory *log_category) {
    std::ifstream file_stream(path);
    if (!file_stream.is_open()) {
        sf::error(log_category, "Test mode: failed to open groundtruth file: %s", path.c_str());
        return std::nullopt;
    }

    Json::CharReaderBuilder reader_builder;
    Json::Value root;
    std::string parse_errors;
    if (!Json::parseFromStream(reader_builder, file_stream, &root, &parse_errors)) {
        sf::error(log_category, "Test mode: failed to parse groundtruth JSON: %s", parse_errors.c_str());
        return std::nullopt;
    }

    if (!root.isObject()) {
        sf::error(log_category, "Test mode: groundtruth JSON root must be an object");
        return std::nullopt;
    }

    const auto &images_json = root["images"];
    const auto &annotations_json = root["annotations"];
    const auto &categories_json = root["categories"];

    if (!images_json.isArray() || !annotations_json.isArray()) {
        sf::error(log_category, "Test mode: groundtruth JSON must contain images and annotations arrays");
        return std::nullopt;
    }

    std::vector<image_entry> images;
    images.reserve(images_json.size());

    bool all_have_suffix = true;
    for (const auto &image_json: images_json) {
        if (!image_json.isObject() || !image_json.isMember("id") || !image_json.isMember("file_name")) {
            sf::warning(log_category, "Test mode: skipping invalid image entry in groundtruth JSON");
            all_have_suffix = false;
            continue;
        }

        const int image_id = image_json["id"].asInt();
        const std::string file_name = image_json["file_name"].asString();
        const auto frame_index_opt = parse_frame_index_from_filename(file_name);
        if (!frame_index_opt.has_value()) {
            all_have_suffix = false;
        }

        images.push_back({image_id, file_name, frame_index_opt});
    }

    if (images.empty()) {
        sf::error(log_category, "Test mode: groundtruth JSON contains no images");
        return std::nullopt;
    }

    std::unordered_map<int, int> image_id_to_frame_index;
    image_id_to_frame_index.reserve(images.size());
    for (size_t index = 0; index < images.size(); ++index) {
        const int frame_index = all_have_suffix && images[index].frame_index.has_value()
                                        ? images[index].frame_index.value()
                                        : static_cast<int>(index);
        if (image_id_to_frame_index.contains(images[index].image_id)) {
            sf::warning(log_category, "Test mode: duplicate image_id %d in groundtruth JSON", images[index].image_id);
            continue;
        }
        image_id_to_frame_index.emplace(images[index].image_id, frame_index);
    }

    groundtruth_data data;
    if (categories_json.isArray()) {
        for (const auto &category_json: categories_json) {
            if (!category_json.isObject() || !category_json.isMember("id") || !category_json.isMember("name")) {
                continue;
            }
            data.category_names.emplace(category_json["id"].asInt(), category_json["name"].asString());
        }
    }

    for (const auto &annotation_json: annotations_json) {
        if (!annotation_json.isObject() || !annotation_json.isMember("image_id") || !annotation_json.isMember("bbox") ||
            !annotation_json["bbox"].isArray() || annotation_json["bbox"].size() != 4 ||
            !annotation_json.isMember("category_id")) {
            sf::warning(log_category, "Test mode: skipping invalid annotation entry in groundtruth JSON");
            continue;
        }

        const int image_id = annotation_json["image_id"].asInt();
        const auto frame_index_iter = image_id_to_frame_index.find(image_id);
        if (frame_index_iter == image_id_to_frame_index.end()) {
            sf::warning(log_category, "Test mode: annotation references unknown image_id %d", image_id);
            continue;
        }

        const int frame_index = frame_index_iter->second;
        const auto &bbox_json = annotation_json["bbox"];
        groundtruth_box box{};
        box.x_coord = static_cast<float>(bbox_json[0].asDouble());
        box.y_coord = static_cast<float>(bbox_json[1].asDouble());
        box.width = static_cast<float>(bbox_json[2].asDouble());
        box.height = static_cast<float>(bbox_json[3].asDouble());
        box.category_id = annotation_json["category_id"].asInt();

        auto &frame = data.frames[frame_index];
        frame.image_id = image_id;
        frame.boxes.push_back(box);
    }

    for (size_t index = 0; index < images.size(); ++index) {
        const auto &image = images[index];
        const int frame_index =
                all_have_suffix && image.frame_index.has_value() ? image.frame_index.value() : static_cast<int>(index);
        auto &frame = data.frames[frame_index];
        frame.image_id = image.image_id;
    }

    sf::info(log_category, "Test mode: loaded groundtruth with %zu images and %zu frames", images.size(),
             data.frames.size());
    if (!all_have_suffix) {
        sf::info(log_category, "Test mode: filename suffix missing, using image array order for frame mapping");
    }

    return data;
}

std::optional<semseg_groundtruth_data> load_semseg_groundtruth_data(const std::string &path,
                                                                    GstDebugCategory *log_category) {
    std::ifstream file_stream(path);
    if (!file_stream.is_open()) {
        sf::error(log_category, "SemSeg test mode: failed to open groundtruth file: %s", path.c_str());
        return std::nullopt;
    }

    Json::CharReaderBuilder reader_builder;
    Json::Value root;
    std::string parse_errors;
    if (!Json::parseFromStream(reader_builder, file_stream, &root, &parse_errors)) {
        sf::error(log_category, "SemSeg test mode: failed to parse groundtruth JSON: %s", parse_errors.c_str());
        return std::nullopt;
    }
    if (!root.isObject()) {
        sf::error(log_category, "SemSeg test mode: groundtruth JSON root must be an object");
        return std::nullopt;
    }

    semseg_groundtruth_data data;

    if (root.isMember("image_width")) {
        data.image_width = root["image_width"].asInt();
    }
    if (root.isMember("image_height")) {
        data.image_height = root["image_height"].asInt();
    }

    const auto &categories_json = root["categories"];
    if (categories_json.isArray()) {
        for (const auto &category_json: categories_json) {
            if (!category_json.isObject() || !category_json.isMember("id") || !category_json.isMember("name")) {
                continue;
            }
            data.category_names.emplace(category_json["id"].asInt(), category_json["name"].asString());
        }
    }

    const auto &eval_categories_json = root["eval_category_ids"];
    if (eval_categories_json.isArray()) {
        data.eval_category_ids.reserve(eval_categories_json.size());
        for (const auto &category_id_json: eval_categories_json) {
            if (!category_id_json.isInt()) {
                continue;
            }
            data.eval_category_ids.push_back(category_id_json.asInt());
        }
    } else {
        data.eval_category_ids.reserve(data.category_names.size());
        for (const auto &[category_id, _]: data.category_names) {
            data.eval_category_ids.push_back(category_id);
        }
        std::sort(data.eval_category_ids.begin(), data.eval_category_ids.end());
    }

    const auto eval_set = build_eval_category_set(data.eval_category_ids);

    const auto &frames_json = root["frames"];
    if (!frames_json.isArray()) {
        sf::error(log_category, "SemSeg test mode: groundtruth JSON must contain a frames array");
        return std::nullopt;
    }

    size_t total_points = 0;
    for (const auto &frame_json: frames_json) {
        if (!frame_json.isObject() || !frame_json.isMember("frame_index") || !frame_json.isMember("points") ||
            !frame_json["points"].isArray()) {
            sf::warning(log_category, "SemSeg test mode: skipping invalid frame entry");
            continue;
        }

        const int frame_index = frame_json["frame_index"].asInt();
        auto &frame_points = data.points_by_frame[frame_index];
        const auto &points_json = frame_json["points"];
        frame_points.reserve(points_json.size());

        for (const auto &point_json: points_json) {
            if (!point_json.isObject() || !point_json.isMember("x") || !point_json.isMember("y") ||
                !point_json.isMember("class_id")) {
                continue;
            }

            semseg_groundtruth_point point{};
            point.x_coord = point_json["x"].asInt();
            point.y_coord = point_json["y"].asInt();
            point.class_id = point_json["class_id"].asInt();

            if (!eval_set.empty() && !eval_set.contains(point.class_id)) {
                continue;
            }
            frame_points.push_back(point);
        }
        total_points += frame_points.size();
    }

    if (data.points_by_frame.empty()) {
        sf::error(log_category, "SemSeg test mode: groundtruth JSON contains no valid frame points");
        return std::nullopt;
    }

    sf::info(log_category, "SemSeg test mode: loaded sparse groundtruth with %zu frames and %zu eval points",
             data.points_by_frame.size(), total_points);
    return data;
}
