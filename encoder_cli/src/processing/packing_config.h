// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstdint>
#include <filesystem>

#include "../encoder_enums.h"

/**
 * @file packing_config.h
 * @brief Narrow, value-copyable config consumed by the PRI packing plumbing.
 *
 * The packing path used to reach into the whole `global_data` aggregate for its
 * configuration (margins, frame dimensions, trace gating). This value type captures
 * exactly the slice the packers and packing trace need, so packing is testable and
 * decoupled from the aggregate. It mirrors the `roi_strategy_snapshot` seam used for
 * mode selection.
 */
struct packing_config {
    bool pack_rois = false;
    background_mode effective_background_mode_value = background_mode::off;

    int roi_fixed_margin_px = 0;
    roi_margin_mode roi_margin_mode_value = roi_margin_mode::off;

    int width = 0;
    int height = 0;
    int num_frames = 0;

    bool draw_rtp_timestamp = false;
    bool packer_logging = false;
    std::filesystem::path frame_output_root{};

    [[nodiscard]] bool is_background_processing_enabled() const {
        return effective_background_mode_value != background_mode::off;
    }
};
