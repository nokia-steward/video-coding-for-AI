// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <optional>
#include <vector>
#include "../../../../libvcai/include/rectangle.h"
#include "../ctu_alignment.h"

/**
 * @file positioning.height
 * @brief Declares position finding algorithms for low delay packer.
 *
 * All position finding operates on a CTU grid. Candidate positions are multiples
 * of the CTU size, and occupancy is checked at CTU-rectangle granularity.
 *
 * Note: find_ctu_grid_position is declared in the shared ctu_alignment.height header.
 */

/**
 * @brief Find the best available position in the grid for a rectangle.
 * @param rect The rectangle to place
 * @param max_width Maximum width constraint
 * @param max_height Maximum height constraint
 * @param grid_size Grid cell size
 * @param occupied_positions Currently occupied positions
 * @return Optional position if found, nullopt otherwise
 */
[[nodiscard]] std::optional<sf::rectangle> find_best_position(const sf::rectangle &rect, int max_width, int max_height,
                                                              int grid_size,
                                                              const std::vector<sf::rectangle> &occupied_positions);

/**
 * @brief Find a compact position right next to existing rectangles.
 * @param rect The rectangle to place
 * @param max_width Maximum width constraint
 * @param max_height Maximum height constraint
 * @param occupied_positions Currently occupied positions
 * @return Optional position if found, nullopt otherwise
 */
[[nodiscard]] std::optional<sf::rectangle> find_compact_position(const sf::rectangle &rect, int max_width,
                                                                 int max_height,
                                                                 const std::vector<sf::rectangle> &occupied_positions);

/**
 * @brief Find a corner position (top-left, top-right, bottom-left, bottom-right).
 * @param rect The rectangle to place
 * @param max_width Maximum width constraint
 * @param max_height Maximum height constraint
 * @param occupied_positions Currently occupied positions
 * @return Optional position if found, nullopt otherwise
 */
[[nodiscard]] std::optional<sf::rectangle> find_corner_position(const sf::rectangle &rect, int max_width,
                                                                int max_height,
                                                                const std::vector<sf::rectangle> &occupied_positions);

/**
 * @brief Find an edge position (left, top, right, bottom edges).
 * @param rect The rectangle to place
 * @param max_width Maximum width constraint
 * @param max_height Maximum height constraint
 * @param occupied_positions Currently occupied positions
 * @return Optional position if found, nullopt otherwise
 */
[[nodiscard]] std::optional<sf::rectangle> find_edge_position(const sf::rectangle &rect, int max_width, int max_height,
                                                              const std::vector<sf::rectangle> &occupied_positions);

/**
 * @brief Find position for a new ROI (no previous position) based on weight.
 * @param rect The rectangle to place
 * @param weight The weight of the rectangle
 * @param max_width Maximum width constraint
 * @param max_height Maximum height constraint
 * @param occupied_positions Currently occupied positions
 * @return Optional position if found, nullopt otherwise
 */
[[nodiscard]] std::optional<sf::rectangle> find_weighted_position(const sf::rectangle &rect, int weight, int max_width,
                                                                  int max_height,
                                                                  const std::vector<sf::rectangle> &occupied_positions);

/**
 * @brief Find position near origin (0,0) for high-weight ROIs.
 * @param rect The rectangle to place
 * @param max_width Maximum width constraint
 * @param max_height Maximum height constraint
 * @param grid_size Grid cell size
 * @param occupied_positions Currently occupied positions
 * @return Optional position if found, nullopt otherwise
 */
[[nodiscard]] std::optional<sf::rectangle>
find_position_near_origin(const sf::rectangle &rect, int max_width, int max_height, int grid_size,
                          const std::vector<sf::rectangle> &occupied_positions);

/**
 * @brief Find position near edges, lower-right for low-weight ROIs.
 * @param rect The rectangle to place
 * @param max_width Maximum width constraint
 * @param max_height Maximum height constraint
 * @param occupied_positions Currently occupied positions
 * @return Optional position if found, nullopt otherwise
 */
[[nodiscard]] std::optional<sf::rectangle>
find_position_near_edges_lower_right(const sf::rectangle &rect, int max_width, int max_height,
                                     const std::vector<sf::rectangle> &occupied_positions);
