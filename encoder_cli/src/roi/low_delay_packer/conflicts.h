// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <optional>
#include <vector>
#include "../../../../libvcai/include/rectangle.h"

/**
 * @file conflicts.height
 * @brief Declares functions for conflict detection and resolution in low delay packer.
 */

/**
 * @brief Find conflicting ROIs that overlap with a desired position.
 * @param desired_rect The rectangle at desired position
 * @param occupied_positions Currently occupied positions
 * @param occupied_indices Indices of occupied positions (for tracking which ROI is which)
 * @return Vector of indices of conflicting ROIs, sorted by weight (ascending - smallest first)
 */
[[nodiscard]] std::vector<size_t> find_conflicting_rois(const sf::rectangle &desired_rect,
                                                        const std::vector<sf::rectangle> &occupied_positions,
                                                        const std::vector<size_t> &occupied_indices);

/**
 * @brief Try to relocate a conflicting ROI to make room for a grown ROI.
 * @param conflicting_roi The ROI to relocate
 * @param grown_roi_rect The grown ROI that needs space
 * @param grown_roi_previous_pos Previous position of the grown ROI
 * @param max_width Maximum width constraint
 * @param max_height Maximum height constraint
 * @param occupied_positions Currently occupied positions
 * @return Optional new position for the conflicting ROI, nullopt if relocation failed
 */
[[nodiscard]] std::optional<sf::rectangle>
relocate_conflicting_roi(const sf::rectangle &conflicting_roi, const sf::rectangle &grown_roi_rect,
                         const sf::rectangle &grown_roi_previous_pos, int max_width, int max_height,
                         const std::vector<sf::rectangle> &occupied_positions);

/**
 * @brief Resolve conflicts for a grown ROI by relocating smaller conflicting ROIs.
 * @param grown_roi_rect The grown ROI rectangle
 * @param grown_roi_previous_pos Previous position of the grown ROI
 * @param desired_position Desired position for the grown ROI
 * @param max_width Maximum width constraint
 * @param max_height Maximum height constraint
 * @param occupied_positions Currently occupied positions (will be modified)
 * @param occupied_indices Indices of occupied positions (will be modified)
 * @param packed_rects Packed rectangles array (will be modified)
 * @param frame_number Frame number for logging
 * @return true if conflicts were resolved, false otherwise
 */
[[nodiscard]] bool resolve_conflicts_for_grown_roi(const sf::rectangle &grown_roi_rect,
                                                   const sf::rectangle &grown_roi_previous_pos,
                                                   const sf::rectangle &desired_position, int max_width, int max_height,
                                                   std::vector<sf::rectangle> &occupied_positions,
                                                   std::vector<size_t> &occupied_indices,
                                                   std::vector<sf::rectangle> &packed_rects, int frame_number);
