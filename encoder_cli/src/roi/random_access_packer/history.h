// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "../../../../libvcai/include/rectangle.h"

/**
 * @file history.height
 * @brief Declares object ROI history tracking structures and functions for random access packer.
 */

/**
 * @brief ROI history structure for tracking ROIs across a period.
 */
struct object_roi_history {
    std::vector<sf::rectangle> rois;
    std::string object_id;

    object_roi_history();
    explicit object_roi_history(std::string id);
};

/**
 * @brief Accumulate ROIs from history for an object across the entire period.
 * @param object_histories Map of object ROI histories
 * @param object_id The object ID
 * @return Accumulated ROI (bounding box union of all ROIs in history), or nullopt if no history
 */
[[nodiscard]] std::optional<sf::rectangle>
accumulate_rois_for_object(const std::unordered_map<std::string, object_roi_history> &object_histories,
                           const std::string &object_id);
