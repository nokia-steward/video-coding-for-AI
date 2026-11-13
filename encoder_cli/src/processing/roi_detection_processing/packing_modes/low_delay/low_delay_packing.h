// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../packing_interface.h"

#include <optional>

/**
 * @file low_delay_packing.height
 * @brief Declares functions for low delay packing mode.
 */

/**
 * @brief Processes a frame using low delay packing mode.
 * @param packer Non-owning view over the active PRI packer (unused for low_delay)
 * @param config Narrow packing configuration (unused for low_delay)
 * @param sample Sample to process (unused for low_delay, returns false)
 * @param input_rectangles Input rectangles
 * @param split_rectangles Split rectangles
 * @param is_static Whether this is static ROI mode
 * @param frame_number Frame number
 * @return false (low_delay doesn't handle frames via process_frame)
 */
[[nodiscard]] bool low_delay_process_frame(pri_packer_view &packer, const packing_config &config,
                                           sf::unique_gst_sample_ptr &sample,
                                           const std::vector<sf::rectangle> &input_rectangles,
                                           const std::vector<split_rectangle> &split_rectangles, bool is_static,
                                           int frame_number);

/**
 * @brief Gets packed rectangles for low delay mode.
 * @param packer Non-owning view over the active PRI packer.
 * @param config Narrow packing configuration.
 * @param input_rectangles Input rectangles
 * @param split_rectangles Split rectangles
 * @param is_static Whether this is static ROI mode
 * @param frame_number Frame number
 * @return Optional packing result
 */
[[nodiscard]] std::optional<packing_result>
low_delay_get_packed_rectangles(pri_packer_view &packer, const packing_config &config,
                                const std::vector<sf::rectangle> &input_rectangles,
                                const std::vector<split_rectangle> &split_rectangles, bool is_static, int frame_number);
