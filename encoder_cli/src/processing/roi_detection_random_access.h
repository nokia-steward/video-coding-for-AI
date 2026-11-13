// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../global_data.h"

/**
 * @file roi_detection_random_access.h
 * @brief Functions for random access frame processing.
 */

/**
 * @brief Blocking reference loop for random_access_packer mode (tests / direct use).
 *
 * @param data A reference to the shared global_data struct.
 */
void random_access_frame_processing(global_data &data);

/**
 * @brief Starts GstTask for random-access period processing when pack_rois uses random_access_packer.
 * @return false only on task creation failure; true when random-access worker is not needed.
 */
[[nodiscard]] bool start_random_access_gst_task(global_data &data, bypass_gst_poll_slot &slot);

/**
 * @brief Stops and joins the random-access GstTask.
 */
void join_random_access_gst_task(global_data &data, bypass_gst_poll_slot &slot);
