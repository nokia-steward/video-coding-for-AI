// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include "../../../../libvcai/include/rectangle.h"

/**
 * @file history.height
 * @brief Declares ROI history tracking structures and functions for low delay packer.
 */

/**
 * @brief ROI history structure for tracking frames.
 */
struct roi_history {
    std::deque<sf::rectangle> frames;
    std::string object_id;

    roi_history();
    explicit roi_history(std::string id);
};

/**
 * @brief Update ROI history for an object with a new ROI.
 * @param roi_histories Map of ROI histories
 * @param object_id The object ID
 * @param roi The new ROI for this object
 * @param num_frames Maximum number of frames to keep
 */
void update_roi_history(std::unordered_map<std::string, roi_history> &roi_histories, const std::string &object_id,
                        const sf::rectangle &roi, int num_frames);

/**
 * @brief Accumulate ROIs from history for an object.
 * @param roi_histories Map of ROI histories
 * @param object_id The object ID
 * @return Accumulated ROI (bounding box union of all ROIs in history), or nullopt if no history
 */
[[nodiscard]] std::optional<sf::rectangle>
accumulate_rois_for_object(const std::unordered_map<std::string, roi_history> &roi_histories,
                           const std::string &object_id);
