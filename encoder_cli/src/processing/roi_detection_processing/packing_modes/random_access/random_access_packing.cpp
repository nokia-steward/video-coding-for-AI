// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "random_access_packing.h"
#include "../../../../../../libvcai/include/logging.h"
#include "../../../../roi/pri_packer_view.h"
#include "../../helpers.h"

#include <optional>

/**
 * @file random_access_packing.cpp
 * @brief Implementation of random access packing mode for ROI processing.
 */

GST_DEBUG_CATEGORY_EXTERN(roi_detection_processing);

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
bool random_access_process_frame(pri_packer_view &packer, const packing_config &config,
                                 sf::unique_gst_sample_ptr &sample, const std::vector<sf::rectangle> &input_rectangles,
                                 const std::vector<split_rectangle> &split_rectangles, const bool is_static,
                                 const int frame_number) {
    if (!packer.has_active_random_access()) {
        return false; // Not using random_access_packer (or packer pointer is null)
    }

    // For random_access_packer: just feed frames to packer, don't process here
    // Frame Processing thread will handle complete periods
    std::vector<std::pair<sf::rectangle, std::string>> source_rectangles_with_ids;
    std::optional<std::vector<std::pair<sf::rectangle, std::string>>> packing_rectangles_with_ids;

    if (!input_rectangles.empty()) {
        source_rectangles_with_ids =
                prepare_rectangles_with_ids_for_random_access(input_rectangles, split_rectangles, is_static);

        const bool should_apply_fixed_margin =
                config.roi_fixed_margin_px > 0 && config.roi_margin_mode_value != roi_margin_mode::off;
        if (should_apply_fixed_margin) {
            const auto expanded_rectangles_with_ids = apply_fixed_roi_margin_with_ids(
                    source_rectangles_with_ids, config.roi_fixed_margin_px, config.width, config.height);
            if (config.roi_margin_mode_value == roi_margin_mode::content) {
                source_rectangles_with_ids = expanded_rectangles_with_ids;
            } else if (config.roi_margin_mode_value == roi_margin_mode::placement) {
                packing_rectangles_with_ids = expanded_rectangles_with_ids;
            }
        }
    }

    // Add frame to packer (this will buffer it and process when period completes)
    // Packer takes ownership of the sample - this keeps the buffer valid
    if (!sample) {
        sf::error(roi_detection_processing,
                  "Attempting to add null sample for frame %d to random_access_packer - this should not happen!",
                  frame_number);
        // Don't add null samples - this will cause issues downstream
        return true; // Signal that we handled it (by not handling it)
    }
    packer.add_random_access_frame(frame_number, std::move(sample), source_rectangles_with_ids,
                                   packing_rectangles_with_ids);

    // Don't process frames here - Frame Processing thread will handle them
    return true; // Signal that packer handled the frame
}

/**
 * @brief Gets packed rectangles for random access mode (not used, returns nullopt).
 * @return nullopt (random_access uses process_frame instead)
 */
std::optional<packing_result> random_access_get_packed_rectangles(pri_packer_view &, const packing_config &,
                                                                  const std::vector<sf::rectangle> &,
                                                                  const std::vector<split_rectangle> &, bool, int) {
    // Random access mode doesn't use get_packed_rectangles
    // It uses process_frame instead
    return std::nullopt;
}
