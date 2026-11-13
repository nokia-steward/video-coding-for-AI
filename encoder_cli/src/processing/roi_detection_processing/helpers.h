// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../../../../libvcai/include/rectangle.h"
#include "../../roi/rectangle_splitter.h"
#include "../../roi/roi_manager.h"

#include <string>
#include <utility>
#include <vector>

/**
 * @file helpers.height
 * @brief Declares helper functions for ROI detection processing.
 */

/**
 * @brief Forward declaration for frame dimension metadata.
 */
struct frame_properties;

/**
 * @brief Extracts rectangles from tracked objects using rectangle splitting for overlaps.
 * @param tracked_objects The tracked objects.
 * @param props The frame properties.
 * @param frame_number The frame number.
 * @return Vector of split rectangles with sub-IDs and original object IDs.
 */
[[nodiscard]] std::vector<split_rectangle>
extract_split_rectangles_from_tracked_objects(const std::vector<detected_object> &tracked_objects,
                                              const frame_properties &props, int frame_number);

/**
 * @brief Logs detection debug information.
 * @param input_rectangles The detected rectangles.
 * @param frame_number The frame number.
 */
void log_detection_debug_info(const std::vector<sf::rectangle> &input_rectangles, int frame_number);

/**
 * @brief Logs tracking information for detected objects.
 * @param tracked_objects The tracked objects.
 * @param frame_number The frame number.
 */
void log_tracking_info(const std::vector<detected_object> &tracked_objects, int frame_number);

/**
 * @brief Applies fixed-pixel per-side dilation and clamps ROI to frame bounds.
 * @param roi Input ROI.
 * @param margin_px Margin in pixels per side.
 * @param frame_width Frame width.
 * @param frame_height Frame height.
 * @return Dilated-and-clamped ROI, or empty ROI if invalid after clamping.
 */
[[nodiscard]] sf::rectangle inflate_and_clamp_roi(const sf::rectangle &roi, int margin_px, int frame_width,
                                                  int frame_height);

/**
 * @brief Applies fixed-pixel dilation to a list of ROIs.
 * @param input_rectangles Input ROIs.
 * @param margin_px Margin in pixels per side.
 * @param frame_width Frame width.
 * @param frame_height Frame height.
 * @return Dilated-and-clamped ROI list with same order and count as input.
 */
[[nodiscard]] std::vector<sf::rectangle> apply_fixed_roi_margin(const std::vector<sf::rectangle> &input_rectangles,
                                                                int margin_px, int frame_width, int frame_height);

/**
 * @brief Applies fixed-pixel dilation to ROI+ID pairs.
 * @param rectangles_with_ids Input ROIs with IDs.
 * @param margin_px Margin in pixels per side.
 * @param frame_width Frame width.
 * @param frame_height Frame height.
 * @return Dilated-and-clamped ROI+ID list with same order and count as input.
 */
[[nodiscard]] std::vector<std::pair<sf::rectangle, std::string>>
apply_fixed_roi_margin_with_ids(const std::vector<std::pair<sf::rectangle, std::string>> &rectangles_with_ids,
                                int margin_px, int frame_width, int frame_height);

/**
 * @brief Prepares rectangles with IDs for random_access_packer.
 * @param input_rectangles Input rectangles
 * @param split_rectangles Split rectangles (for inference mode)
 * @param is_static Whether this is static ROI mode
 * @return Vector of rectangles with IDs
 */
[[nodiscard]] std::vector<std::pair<sf::rectangle, std::string>>
prepare_rectangles_with_ids_for_random_access(const std::vector<sf::rectangle> &input_rectangles,
                                              const std::vector<split_rectangle> &split_rectangles, bool is_static);
