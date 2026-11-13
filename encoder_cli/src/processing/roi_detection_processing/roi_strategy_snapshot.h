// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../../encoder_enums.h"

/**
 * @file roi_strategy_snapshot.h
 * @brief Narrow availability/config snapshot consumed by ROI mode selection.
 *
 * The detection, packing, and processing mode factories select an active mode from
 * a small slice of the aggregate: which ROI sources have data available and which
 * packing/processing features are configured. This value type captures that slice
 * so mode selection is testable without constructing the full `global_data`.
 */
struct roi_strategy_snapshot {
    bool inference_engine = false;
    bool groundtruth_roi_available = false;
    bool static_roi_available = false;
    bool pack_rois = false;
    packer_type packer_type_value = packer_type::low_delay;
    background_mode effective_background_mode_value = background_mode::off;

    [[nodiscard]] bool is_background_blur_effective() const {
        return effective_background_mode_value == background_mode::background_blur;
    }

    [[nodiscard]] bool is_qp_map_delta_effective() const {
        return effective_background_mode_value == background_mode::qp_map_delta;
    }

    [[nodiscard]] bool is_background_processing_enabled() const {
        return effective_background_mode_value != background_mode::off;
    }
};
