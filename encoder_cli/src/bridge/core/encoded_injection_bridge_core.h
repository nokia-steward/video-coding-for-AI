// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

#include "../../../libvcai/include/gst_raii.h"
#include "../../../libvcai/include/packed_regions_info.h"
#include "../../encoded_frame_item.h"
#include "../context/encoded_injection_transform_deps.h"

namespace encoded_injection_bridge_core {
    enum class step_status { ok, recoverable_drop, fatal_stop, drain_complete };

    struct transform_result {
        step_status status = step_status::ok;
        bool count_sei_injection = false;
        sf::unique_gst_buffer_ptr owned_buffer{};
        bool use_input_buffer_ref = true;
    };

    [[nodiscard]] transform_result transform_with_metadata(encoded_injection_transform_deps deps,
                                                           GstBuffer *input_buffer,
                                                           const sf::packed_regions_info &region_info,
                                                           const encoded_frame_luma_adaptation_state &luma_state);

    [[nodiscard]] transform_result transform_without_metadata(encoded_injection_transform_deps deps,
                                                              GstBuffer *input_buffer,
                                                              const encoded_frame_luma_adaptation_state &luma_state);
} // namespace encoded_injection_bridge_core
