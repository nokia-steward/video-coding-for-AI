// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../../../../../libvcai/include/rectangle.h"
#include "../../../roi/pri_packer_view.h"
#include "../../../roi/rectangle_splitter.h"
#include "../../packing_config.h"
#include "../../roi_detection_packing.h"
#include "../roi_strategy_snapshot.h"

#include <functional>
#include <optional>
#include <vector>

/**
 * @file packing_interface.height
 * @brief Declares the interface for ROI packing mode implementations.
 */

/**
 * @brief Forward declaration for frame dimension metadata.
 */
struct frame_properties;

/**
 * @brief Function table for ROI packing modes.
 */
struct packing_mode_functions {
    /**
     * @brief Processes a frame using the packer (for random_access mode).
     * @param packer Non-owning view over the active PRI packer.
     * @param config Narrow packing configuration.
     * @param sample Sample to process (will be moved)
     * @param input_rectangles Input rectangles
     * @param split_rectangles Split rectangles
     * @param is_static Whether this is static ROI mode
     * @param frame_number Frame number
     * @return true if frame was handled by packer, false otherwise
     */
    std::function<bool(pri_packer_view &packer, const packing_config &config, sf::unique_gst_sample_ptr &sample,
                       const std::vector<sf::rectangle> &input_rectangles,
                       const std::vector<split_rectangle> &split_rectangles, bool is_static, int frame_number)>
            process_frame;

    /**
     * @brief Gets packed rectangles for a frame (for low_delay mode).
     * @param packer Non-owning view over the active PRI packer.
     * @param config Narrow packing configuration.
     * @param input_rectangles Input rectangles
     * @param split_rectangles Split rectangles
     * @param is_static Whether this is static ROI mode
     * @param frame_number Frame number
     * @return Optional packing result
     */
    std::function<std::optional<packing_result>(
            pri_packer_view &packer, const packing_config &config, const std::vector<sf::rectangle> &input_rectangles,
            const std::vector<split_rectangle> &split_rectangles, bool is_static, int frame_number)>
            get_packed_rectangles;

    /**
     * @brief Checks if this packing mode is enabled.
     * @param snapshot Availability/config snapshot
     * @return true if enabled, false otherwise
     */
    std::function<bool(const roi_strategy_snapshot &snapshot)> is_enabled;
};

/**
 * @brief Context for a packing mode.
 */
struct packing_mode_context {
    const char *name;
    packing_mode_functions functions;
    bool is_enabled;
};
