// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "../../libvcai/include/rectangle.h"
#include "../roi/pri_packer_view.h"
#include "../roi/rectangle_splitter.h"
#include "packing_config.h"

/**
 * @brief Forward declaration for shared application state (used only by the aggregate seam helper).
 */
struct global_data;

/**
 * @file roi_detection_packing.height
 * @brief Functions for packing detected ROIs.
 */

/**
 * @brief Builds the narrow packing config from the aggregate at the application seam.
 * @param data The global data structure.
 * @return The narrow packing configuration consumed by the packing plumbing.
 */
[[nodiscard]] packing_config make_packing_config(const global_data &data);

/**
 * @brief Result of detection packing operation.
 */
struct packing_result {
    std::vector<sf::rectangle> packed_rectangles;
    std::vector<sf::rectangle> original_rois; // Original accumulated ROIs (for SEI message)
    std::vector<size_t> original_to_packed_mapping; // Mapping from original ROI index to packed ROI index
    std::vector<bool> is_predicted; // Whether each original ROI is predicted (red) or detected (blue)
};

/**
 * @brief Performs packing on detected rectangles.
 * @param packer Non-owning view over the active PRI packer.
 * @param config Narrow packing configuration.
 * @param input_rectangles The rectangles to pack.
 * @param split_rectangles The split rectangles with object IDs (for inference mode, empty for static mode).
 * @param is_static Whether this is static ROI mode (uses index as object ID).
 * @param frame_number The frame number for error reporting.
 * @return Packing result with packed rectangles and original ROIs.
 */
[[nodiscard]] packing_result perform_detection_packing(pri_packer_view &packer, const packing_config &config,
                                                       const std::vector<sf::rectangle> &input_rectangles,
                                                       const std::vector<split_rectangle> &split_rectangles,
                                                       bool is_static, int frame_number);

/**
 * @brief Writes a named stage entry to tracker_dump/roi_packing_trace.jsonl.
 * @param config Narrow packing configuration (uses packer_logging gate).
 * @param frame_number Frame number for the emitted stage.
 * @param stage Stage name (for example, "copy_rectangles").
 * @param payload_json JSON fragment to append after the stage field.
 */
void dump_packing_trace_stage(const packing_config &config, int frame_number, std::string_view stage,
                              const std::string &payload_json);

/**
 * @brief Writes source/destination ROI copy rectangles to the packing trace.
 * @param config Narrow packing configuration (uses packer_logging gate).
 * @param frame_number Frame number for the emitted stage.
 * @param source_rectangles Source rectangles used for ROI copy.
 * @param destination_rectangles Destination rectangles used for ROI copy.
 */
void dump_copy_rectangles_trace(const packing_config &config, int frame_number,
                                const std::vector<sf::rectangle> &source_rectangles,
                                const std::vector<sf::rectangle> &destination_rectangles);

/**
 * @brief Writes final PRI ROI region pairs to the packing trace.
 *
 * Each emitted region maps one packed rectangle to one target rectangle, mirroring the
 * metadata relationship used for PRI SEI serialization.
 *
 * @param config Narrow packing configuration (uses packer_logging gate).
 * @param frame_number Frame number for the emitted stage.
 * @param pts Frame PTS value (undefined when pts_valid is false).
 * @param pts_valid Whether PTS is valid for this frame.
 * @param dts Frame DTS value (undefined when dts_valid is false).
 * @param dts_valid Whether DTS is valid for this frame.
 * @param target_rectangles Target rectangles (original ROI positions).
 * @param packed_rectangles Packed rectangles (source positions in packed frame).
 */
void dump_pri_regions_trace(const packing_config &config, int frame_number, uint64_t pts, bool pts_valid, uint64_t dts,
                            bool dts_valid, const std::vector<sf::rectangle> &target_rectangles,
                            const std::vector<sf::rectangle> &packed_rectangles);
