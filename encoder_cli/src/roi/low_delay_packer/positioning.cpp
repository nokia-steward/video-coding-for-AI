// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "positioning.h"
#include "../../../../libvcai/include/rectangle.h"
#include "ctu_alignment.h"
#include "utils.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

/**
 * @file positioning.cpp
 * @brief Implementation of position finding algorithms for low delay packer.
 *
 * All position helpers generate candidate positions that are multiples of the CTU
 * size so that ROI boundaries always fall on CTU grid lines.
 */

namespace {
    /**
     * @brief Find compact position helper — tries CTU-aligned positions adjacent to
     *        existing rectangles.
     */
    [[nodiscard]] std::optional<sf::rectangle>
    find_compact_position_impl(const sf::rectangle &rect, const int max_width, const int max_height,
                               const std::vector<sf::rectangle> &occupied_positions) {
        if (occupied_positions.empty()) {
            if (can_fit_at_position(rect, 0, 0, max_width, max_height, occupied_positions)) {
                return sf::rectangle(0, 0, rect.width, rect.height);
            }
            return std::nullopt;
        }

        std::vector<std::pair<int, int>> candidate_positions;

        for (const auto &occupied: occupied_positions) {
            // Right of occupied rectangle — snap to occupied's right edge (already CTU-aligned
            // when occupied rects are CTU-aligned).
            candidate_positions.emplace_back(occupied.left + occupied.width, occupied.top);
            // Below occupied rectangle
            candidate_positions.emplace_back(occupied.left, occupied.top + occupied.height);
        }

        // Sort candidates by Manhattan distance from origin (prefer top-left).
        std::ranges::sort(candidate_positions,
                          [](const auto &a, const auto &b) { return a.first + a.second < b.first + b.second; });

        for (const auto &[x, y]: candidate_positions) {
            if (x >= 0 && y >= 0 && x + rect.width <= max_width && y + rect.height <= max_height) {
                if (can_fit_at_position(rect, x, y, max_width, max_height, occupied_positions)) {
                    return sf::rectangle(x, y, rect.width, rect.height);
                }
            }
        }

        return std::nullopt;
    }

    /**
     * @brief Find corner position helper — tries four corner positions.
     */
    [[nodiscard]] std::optional<sf::rectangle>
    find_corner_position_impl(const sf::rectangle &rect, const int max_width, const int max_height,
                              const std::vector<sf::rectangle> &occupied_positions) {
        std::vector<std::pair<int, int>> corner_positions = {{0, 0},
                                                             {0, max_height - rect.height},
                                                             {max_width - rect.width, 0},
                                                             {max_width - rect.width, max_height - rect.height}};

        std::ranges::sort(corner_positions,
                          [](const auto &a, const auto &b) { return (a.first + a.second) < (b.first + b.second); });

        for (const auto &[x, y]: corner_positions) {
            if (x >= 0 && y >= 0 && x + rect.width <= max_width && y + rect.height <= max_height &&
                can_fit_at_position(rect, x, y, max_width, max_height, occupied_positions)) {
                return sf::rectangle(x, y, rect.width, rect.height);
            }
        }

        return std::nullopt;
    }

    /**
     * @brief Find edge position helper — tries positions along edges using CTU-sized steps.
     */
    [[nodiscard]] std::optional<sf::rectangle>
    find_edge_position_impl(const sf::rectangle &rect, const int max_width, const int max_height,
                            const std::vector<sf::rectangle> &occupied_positions) {
        for (int y = 0; y <= max_height - rect.height; ++y) {
            if (can_fit_at_position(rect, 0, y, max_width, max_height, occupied_positions)) {
                return sf::rectangle(0, y, rect.width, rect.height);
            }
        }

        for (int x = 0; x <= max_width - rect.width; ++x) {
            if (can_fit_at_position(rect, x, 0, max_width, max_height, occupied_positions)) {
                return sf::rectangle(x, 0, rect.width, rect.height);
            }
        }

        for (int y = 0; y <= max_height - rect.height; ++y) {
            if (can_fit_at_position(rect, max_width - rect.width, y, max_width, max_height, occupied_positions)) {
                return sf::rectangle(max_width - rect.width, y, rect.width, rect.height);
            }
        }

        for (int x = 0; x <= max_width - rect.width; ++x) {
            if (can_fit_at_position(rect, x, max_height - rect.height, max_width, max_height, occupied_positions)) {
                return sf::rectangle(x, max_height - rect.height, rect.width, rect.height);
            }
        }

        return std::nullopt;
    }

    /**
     * @brief Find position near origin helper — tries positions near (0,0) using grid steps.
     */
    [[nodiscard]] std::optional<sf::rectangle>
    find_position_near_origin_impl(const sf::rectangle &rect, const int max_width, const int max_height,
                                   const int grid_size, const std::vector<sf::rectangle> &occupied_positions) {
        if (can_fit_at_position(rect, 0, 0, max_width, max_height, occupied_positions)) {
            return sf::rectangle(0, 0, rect.width, rect.height);
        }

        const int search_limit_x = std::min(max_width / 4, max_width - rect.width);
        const int search_limit_y = std::min(max_height / 4, max_height - rect.height);

        std::vector<std::pair<int, int>> candidates;
        for (int y = 0; y <= search_limit_y; y += grid_size) {
            for (int x = 0; x <= search_limit_x; x += grid_size) {
                if (x != 0 || y != 0) {
                    candidates.emplace_back(x, y);
                }
            }
        }

        std::ranges::sort(candidates, [](const auto &a, const auto &b) {
            const int dist_a = a.first + a.second;
            const int dist_b = b.first + b.second;
            if (dist_a != dist_b) {
                return dist_a < dist_b;
            }
            return a.first < b.first;
        });

        for (const auto &[x, y]: candidates) {
            if (can_fit_at_position(rect, x, y, max_width, max_height, occupied_positions)) {
                return sf::rectangle(x, y, rect.width, rect.height);
            }
        }

        const int expanded_limit_x = std::min(max_width / 2, max_width - rect.width);
        const int expanded_limit_y = std::min(max_height / 2, max_height - rect.height);

        for (int y = 0; y <= expanded_limit_y; ++y) {
            for (int x = 0; x <= expanded_limit_x; ++x) {
                if (can_fit_at_position(rect, x, y, max_width, max_height, occupied_positions)) {
                    return sf::rectangle(x, y, rect.width, rect.height);
                }
            }
        }

        return std::nullopt;
    }

    /**
     * @brief Find position near edges lower-right helper.
     */
    [[nodiscard]] std::optional<sf::rectangle>
    find_position_near_edges_lower_right_impl(const sf::rectangle &rect, const int max_width, const int max_height,
                                              const std::vector<sf::rectangle> &occupied_positions) {
        std::vector<std::pair<int, int>> candidates;

        for (const auto &occupied: occupied_positions) {
            candidates.emplace_back(occupied.left + occupied.width, occupied.top);
            candidates.emplace_back(occupied.left, occupied.top + occupied.height);
            candidates.emplace_back(occupied.left + occupied.width, occupied.top + occupied.height);
        }

        std::ranges::sort(candidates, [](const auto &a, const auto &b) {
            if (a.second != b.second) {
                return a.second > b.second;
            }
            return a.first > b.first;
        });

        for (const auto &[x, y]: candidates) {
            if (x >= 0 && y >= 0 && x + rect.width <= max_width && y + rect.height <= max_height) {
                if (can_fit_at_position(rect, x, y, max_width, max_height, occupied_positions)) {
                    return sf::rectangle(x, y, rect.width, rect.height);
                }
            }
        }

        for (int horizontal_position = max_width - rect.width; horizontal_position >= 0; --horizontal_position) {
            const int bottom_edge_row = max_height - rect.height;
            if (can_fit_at_position(rect, horizontal_position, bottom_edge_row, max_width, max_height,
                                    occupied_positions)) {
                return sf::rectangle(horizontal_position, bottom_edge_row, rect.width, rect.height);
            }
        }

        for (int vertical_position = max_height - rect.height; vertical_position >= 0; --vertical_position) {
            const int right_edge_column = max_width - rect.width;
            if (can_fit_at_position(rect, right_edge_column, vertical_position, max_width, max_height,
                                    occupied_positions)) {
                return sf::rectangle(right_edge_column, vertical_position, rect.width, rect.height);
            }
        }

        return find_best_position(rect, max_width, max_height, 1, occupied_positions);
    }
} // namespace

// Note: find_ctu_grid_position is now in the shared ctu_alignment.cpp module.

std::optional<sf::rectangle> find_best_position(const sf::rectangle &rect, const int max_width, const int max_height,
                                                const int grid_size,
                                                const std::vector<sf::rectangle> &occupied_positions) {
    if (const auto compact_pos = find_compact_position_impl(rect, max_width, max_height, occupied_positions)) {
        return compact_pos;
    }

    if (const auto corner_pos = find_corner_position_impl(rect, max_width, max_height, occupied_positions)) {
        return corner_pos;
    }

    if (const auto edge_pos = find_edge_position_impl(rect, max_width, max_height, occupied_positions)) {
        return edge_pos;
    }

    for (int y = 0; y <= max_height - rect.height; y += grid_size) {
        for (int x = 0; x <= max_width - rect.width; x += grid_size) {
            if (can_fit_at_position(rect, x, y, max_width, max_height, occupied_positions)) {
                return sf::rectangle(x, y, rect.width, rect.height);
            }
        }
    }

    for (int y = 0; y <= max_height - rect.height; ++y) {
        for (int x = 0; x <= max_width - rect.width; ++x) {
            if (can_fit_at_position(rect, x, y, max_width, max_height, occupied_positions)) {
                return sf::rectangle(x, y, rect.width, rect.height);
            }
        }
    }

    return std::nullopt;
}

std::optional<sf::rectangle> find_compact_position(const sf::rectangle &rect, const int max_width, const int max_height,
                                                   const std::vector<sf::rectangle> &occupied_positions) {
    return find_compact_position_impl(rect, max_width, max_height, occupied_positions);
}

std::optional<sf::rectangle> find_corner_position(const sf::rectangle &rect, const int max_width, const int max_height,
                                                  const std::vector<sf::rectangle> &occupied_positions) {
    return find_corner_position_impl(rect, max_width, max_height, occupied_positions);
}

std::optional<sf::rectangle> find_edge_position(const sf::rectangle &rect, const int max_width, const int max_height,
                                                const std::vector<sf::rectangle> &occupied_positions) {
    return find_edge_position_impl(rect, max_width, max_height, occupied_positions);
}

std::optional<sf::rectangle> find_weighted_position(const sf::rectangle &rect, const int weight, const int max_width,
                                                    const int max_height,
                                                    const std::vector<sf::rectangle> &occupied_positions) {
    if (const auto compact_pos = find_compact_position(rect, max_width, max_height, occupied_positions)) {
        return compact_pos;
    }

    constexpr int medium_roi_weight = 100 * 100;
    constexpr int weight_threshold = medium_roi_weight;

    if (weight >= weight_threshold) {
        if (const auto pos = find_position_near_origin(rect, max_width, max_height, 16, occupied_positions)) {
            return pos;
        }
        return find_best_position(rect, max_width, max_height, 16, occupied_positions);
    }

    if (const auto pos = find_position_near_edges_lower_right(rect, max_width, max_height, occupied_positions)) {
        return pos;
    }

    return find_best_position(rect, max_width, max_height, 16, occupied_positions);
}

std::optional<sf::rectangle> find_position_near_origin(const sf::rectangle &rect, const int max_width,
                                                       const int max_height, const int grid_size,
                                                       const std::vector<sf::rectangle> &occupied_positions) {
    return find_position_near_origin_impl(rect, max_width, max_height, grid_size, occupied_positions);
}

std::optional<sf::rectangle>
find_position_near_edges_lower_right(const sf::rectangle &rect, const int max_width, const int max_height,
                                     const std::vector<sf::rectangle> &occupied_positions) {
    return find_position_near_edges_lower_right_impl(rect, max_width, max_height, occupied_positions);
}
