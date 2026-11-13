// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "encoded_frame_item.h"
#include "gst_raii.h"

#include <cstdint>

/**
 * @file encoder_submission_item.h
 * @brief Queue item for decoupled encoder appsrc submission in file mode.
 */

struct encoder_submission_item {
    sf::unique_gst_buffer_ptr buffer;
    encoded_frame_luma_adaptation_state luma_adaptation{};
    uint64_t contract_sequence_id = 0;
};
