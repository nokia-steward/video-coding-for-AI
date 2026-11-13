// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <vector>

#include "packed_regions_info.h"
#include "rectangle.h"

/**
 * @file packed_regions_info_utils.height
 * @brief Provides high-level utility functions for creating packed_regions_info structs.
 */

/**
 * @struct creation_params
 * @brief Parameters for creating a packed_regions_info struct.
 */
struct creation_params {
    int reconstructed_width{};
    int reconstructed_height{};
    unsigned int log2_unit_size = 0;
    bool persistence_flag = true;
};

/**
 * @brief Creates a packed_regions_info struct from high-level ROI locations.
 *
 * This function simplifies the creation of a packed_regions_info SEI message
 * by taking descriptions of region layouts and calculating the necessary low-level fields.
 *
 * @param roi_data A vector of rectangles describing the final, logical layout
 * of the ROIs in the reconstructed target picture.
 * @param packed_rois A vector of rectangles describing where the ROIs are located
 * in the source (packed) picture. The order must match roi_data.
 * @param params Additional parameters for the SEI message creation.
 * @return A fully populated packed_regions_info struct.
 */
[[nodiscard]] sf::packed_regions_info create_packed_regions_info(const std::vector<sf::rectangle> &roi_data,
                                                                 const std::vector<sf::rectangle> &packed_rois,
                                                                 const creation_params &params);
