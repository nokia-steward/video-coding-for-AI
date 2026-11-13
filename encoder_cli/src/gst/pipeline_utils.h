// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

/**
 * @file pipeline_utils.height
 * @brief Header for common pipeline utility functions.
 */

/**
 * @brief Initializes the pipeline utils debug category.
 */
void init_pipeline_utils_debug_category();

/**
 * @brief Links a tee element to a branch queue element.
 * @param tee The tee element.
 * @param branch_queue The branch queue element to link to.
 * @return true on success, false on failure.
 */
[[nodiscard]] bool link_tee_to_branch(GstElement *tee, GstElement *branch_queue);
