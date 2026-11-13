// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../packing_interface.h"

#include <optional>

/**
 * @file random_access_packing.height
 * @brief Declares functions for random access packing mode.
 */

/**
 * @brief Processes a frame using random access packing mode.
 * @param packer Non-owning view over the active PRI packer.
 * @param config Narrow packing configuration.
 * @param sample Sample to process (will be moved)
 * @param input_rectangles Input rectangles
 * @param split_rectangles Split rectangles
 * @param is_static Whether this is static ROI mode
 * @param frame_number Frame number
 * @return true if frame was handled by packer, false otherwise
 */
[[nodiscard]] bool random_access_process_frame(pri_packer_view &packer, const packing_config &config,
                                               sf::unique_gst_sample_ptr &sample,
                                               const std::vector<sf::rectangle> &input_rectangles,
                                               const std::vector<split_rectangle> &split_rectangles, bool is_static,
                                               int frame_number);

/**
 * @brief Gets packed rectangles for random access mode (not used, returns nullopt).
 * @param packer Non-owning view over the active PRI packer (unused)
 * @param config Narrow packing configuration (unused)
 * @param input_rectangles Input rectangles (unused)
 * @param split_rectangles Split rectangles (unused)
 * @param is_static Whether this is static ROI mode (unused)
 * @param frame_number Frame number (unused)
 * @return nullopt (random_access uses process_frame instead)
 */
[[nodiscard]] std::optional<packing_result> random_access_get_packed_rectangles(
        pri_packer_view &packer, const packing_config &config, const std::vector<sf::rectangle> &input_rectangles,
        const std::vector<split_rectangle> &split_rectangles, bool is_static, int frame_number);
