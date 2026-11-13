// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "history.h"
#include "../../../../libvcai/include/rectangle.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @file history.cpp
 * @brief Implementation of object ROI history tracking for random access packer.
 */

object_roi_history::object_roi_history() : rois(), object_id() {}

object_roi_history::object_roi_history(std::string id) : rois(), object_id(std::move(id)) {}

std::optional<sf::rectangle>
accumulate_rois_for_object(const std::unordered_map<std::string, object_roi_history> &object_histories,
                           const std::string &object_id) {
    const auto it = object_histories.find(object_id);
    if (it == object_histories.end() || it->second.rois.empty()) {
        return std::nullopt;
    }

    const auto &rois = it->second.rois;
    if (rois.empty()) {
        return std::nullopt;
    }

    // Compute bounding box union of all ROIs in history
    sf::rectangle accumulated = rois[0];
    for (size_t i = 1; i < rois.size(); ++i) {
        accumulated.expand_with(rois[i]);
    }

    return accumulated;
}
