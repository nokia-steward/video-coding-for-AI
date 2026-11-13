// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../global_data.h"

/**
 * @file encoder_submission_processing.h
 * @brief GstTask that drains encoder submission queue to encoding appsrc.
 */

[[nodiscard]] bool start_encoder_submission_gst_task(global_data &data, bypass_gst_poll_slot &slot);
void join_encoder_submission_gst_task(global_data &data, bypass_gst_poll_slot &slot);
void init_encoder_submission_processing_debug_category();
