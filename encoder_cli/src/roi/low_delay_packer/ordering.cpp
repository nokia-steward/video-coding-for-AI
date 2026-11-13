// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "ordering.h"
#include "../../../../libvcai/include/rectangle.h"
#include "../packer_common.h"
#include "utils.h"

#include <algorithm>
#include <ranges>
#include <vector>

/**
 * @file ordering.cpp
 * @brief Implementation of rectangle ordering algorithms for low delay packer.
 */

/**
 * @brief Sorts rectangles by area in descending order.
 * @param rectangles Rectangles to sort.
 * @return Sorted vector of rectangles.
 */
std::vector<stable_rectangle> get_sorted_by_area_desc(const std::vector<stable_rectangle> &rectangles) {
    std::vector<stable_rectangle> sorted = rectangles;
    std::ranges::sort(sorted, [](const auto &a, const auto &b) {
        return get_rectangle_weight(a.rect) > get_rectangle_weight(b.rect);
    });
    return sorted;
}

/**
 * @brief Sorts rectangles by area in ascending order.
 * @param rectangles Rectangles to sort.
 * @return Sorted vector of rectangles.
 */
std::vector<stable_rectangle> get_sorted_by_area_asc(const std::vector<stable_rectangle> &rectangles) {
    std::vector<stable_rectangle> sorted = rectangles;
    std::ranges::sort(sorted, [](const auto &a, const auto &b) {
        return get_rectangle_weight(a.rect) < get_rectangle_weight(b.rect);
    });
    return sorted;
}

/**
 * @brief Sorts rectangles by width in descending order.
 * @param rectangles Rectangles to sort.
 * @return Sorted vector of rectangles.
 */
std::vector<stable_rectangle> get_sorted_by_width_desc(const std::vector<stable_rectangle> &rectangles) {
    std::vector<stable_rectangle> sorted = rectangles;
    std::ranges::sort(sorted, [](const auto &a, const auto &b) { return a.rect.width > b.rect.width; });
    return sorted;
}

/**
 * @brief Sorts rectangles by height in descending order.
 * @param rectangles Rectangles to sort.
 * @return Sorted vector of rectangles.
 */
std::vector<stable_rectangle> get_sorted_by_height_desc(const std::vector<stable_rectangle> &rectangles) {
    std::vector<stable_rectangle> sorted = rectangles;
    std::ranges::sort(sorted, [](const auto &a, const auto &b) { return a.rect.height > b.rect.height; });
    return sorted;
}

/**
 * @brief Returns indices in the original order.
 * @param rectangles Input rectangle list.
 * @return Index vector in original order.
 */
std::vector<size_t> get_original_order(const std::vector<stable_rectangle> &rectangles) {
    std::vector<size_t> indices(rectangles.size());
    for (size_t i = 0; i < indices.size(); ++i) {
        indices[i] = i;
    }
    return indices;
}

/**
 * @brief Returns indices sorted by area descending.
 * @param rectangles Input rectangle list.
 * @return Index vector sorted by area descending.
 */
std::vector<size_t> get_sorted_by_area_desc_indices(const std::vector<stable_rectangle> &rectangles) {
    std::vector<size_t> indices(rectangles.size());
    for (size_t i = 0; i < indices.size(); ++i) {
        indices[i] = i;
    }
    std::ranges::sort(indices, [&rectangles](const size_t a, const size_t b) {
        return get_rectangle_weight(rectangles[a].rect) > get_rectangle_weight(rectangles[b].rect);
    });
    return indices;
}

/**
 * @brief Returns indices sorted by area ascending.
 * @param rectangles Input rectangle list.
 * @return Index vector sorted by area ascending.
 */
std::vector<size_t> get_sorted_by_area_asc_indices(const std::vector<stable_rectangle> &rectangles) {
    std::vector<size_t> indices(rectangles.size());
    for (size_t i = 0; i < indices.size(); ++i) {
        indices[i] = i;
    }
    std::ranges::sort(indices, [&rectangles](const size_t a, const size_t b) {
        return get_rectangle_weight(rectangles[a].rect) < get_rectangle_weight(rectangles[b].rect);
    });
    return indices;
}

/**
 * @brief Returns indices sorted by width descending.
 * @param rectangles Input rectangle list.
 * @return Index vector sorted by width descending.
 */
std::vector<size_t> get_sorted_by_width_desc_indices(const std::vector<stable_rectangle> &rectangles) {
    std::vector<size_t> indices(rectangles.size());
    for (size_t i = 0; i < indices.size(); ++i) {
        indices[i] = i;
    }
    std::ranges::sort(indices, [&rectangles](const size_t a, const size_t b) {
        return rectangles[a].rect.width > rectangles[b].rect.width;
    });
    return indices;
}

/**
 * @brief Returns indices sorted by height descending.
 * @param rectangles Input rectangle list.
 * @return Index vector sorted by height descending.
 */
std::vector<size_t> get_sorted_by_height_desc_indices(const std::vector<stable_rectangle> &rectangles) {
    std::vector<size_t> indices(rectangles.size());
    for (size_t i = 0; i < indices.size(); ++i) {
        indices[i] = i;
    }
    std::ranges::sort(indices, [&rectangles](const size_t a, const size_t b) {
        return rectangles[a].rect.height > rectangles[b].rect.height;
    });
    return indices;
}
