// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "history.h"
#include "../../../../libvcai/include/rectangle.h"

#include <deque>
#include <optional>
#include <string>
#include <unordered_map>

/**
 * @file history.cpp
 * @brief Implementation of ROI history tracking for low delay packer.
 */

roi_history::roi_history() : frames(), object_id() {}

roi_history::roi_history(std::string id) : frames(), object_id(std::move(id)) {}

void update_roi_history(std::unordered_map<std::string, roi_history> &roi_histories, const std::string &object_id,
                        const sf::rectangle &roi, const int num_frames) {
    auto &history = roi_histories[object_id];
    if (history.object_id.empty()) {
        history.object_id = object_id;
    }

    history.frames.push_back(roi);

    while (history.frames.size() > static_cast<size_t>(num_frames)) {
        history.frames.pop_front();
    }
}

std::optional<sf::rectangle>
accumulate_rois_for_object(const std::unordered_map<std::string, roi_history> &roi_histories,
                           const std::string &object_id) {
    const auto it = roi_histories.find(object_id);
    if (it == roi_histories.end() || it->second.frames.empty()) {
        return std::nullopt;
    }

    const auto &frames = it->second.frames;
    if (frames.empty()) {
        return std::nullopt;
    }

    sf::rectangle accumulated = frames[0];
    for (size_t i = 1; i < frames.size(); ++i) {
        accumulated.expand_with(frames[i]);
    }

    return accumulated;
}
