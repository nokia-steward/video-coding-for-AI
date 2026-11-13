// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "low_delay_packing.h"
#include "../../../../../../libvcai/include/logging.h"
#include "../../../../roi/pri_packer_view.h"
#include "../../../roi_detection_packing.h"

#include <optional>

/**
 * @file low_delay_packing.cpp
 * @brief Implementation of low delay packing mode for ROI processing.
 */

GST_DEBUG_CATEGORY_EXTERN(roi_detection_processing);

/**
 * @brief Processes a frame using low delay packing mode (never handles frames).
 * @param packer Non-owning view over the active PRI packer (unused)
 * @param config Narrow packing configuration (unused)
 * @param sample Sample to process (unused for low_delay, returns false)
 * @param input_rectangles Input rectangles
 * @param split_rectangles Split rectangles
 * @param is_static Whether this is static ROI mode
 * @param frame_number Frame number
 * @return false (low_delay doesn't handle frames via process_frame)
 */
bool low_delay_process_frame(pri_packer_view &, const packing_config &, sf::unique_gst_sample_ptr &,
                             const std::vector<sf::rectangle> &, const std::vector<split_rectangle> &, bool, int) {
    // Low delay mode doesn't handle frames via process_frame
    // It uses get_packed_rectangles instead
    return false;
}

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
std::optional<packing_result> low_delay_get_packed_rectangles(pri_packer_view &packer, const packing_config &config,
                                                              const std::vector<sf::rectangle> &input_rectangles,
                                                              const std::vector<split_rectangle> &split_rectangles,
                                                              const bool is_static, const int frame_number) {
    return perform_detection_packing(packer, config, input_rectangles, split_rectangles, is_static, frame_number);
}
