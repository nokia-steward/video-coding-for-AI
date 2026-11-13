// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "pipeline_utils.h"
#include "../../libvcai/include/logging.h"
#include "../libvcai/include/gst_raii.h"

/**
 * @file pipeline_utils.cpp
 * @brief Implementation of common pipeline utility functions.
 */

/// GStreamer debug category for pipeline utility functions.
GST_DEBUG_CATEGORY_STATIC(pipeline_utils);

void init_pipeline_utils_debug_category() {
    GST_DEBUG_CATEGORY_INIT(pipeline_utils, "pipeline-utils", 0, "Common pipeline utility functions");
}

/**
 * @brief Links a tee element to a branch queue element.
 * @param tee The tee element.
 * @param branch_queue The branch queue element to link to.
 * @param logger The logger to use for logging (defaults to null logger if not provided).
 * @return true on success, false on failure.
 */
bool link_tee_to_branch(GstElement *tee, GstElement *branch_queue) {
    // Check for null parameters
    if (!tee) {
        sf::error(pipeline_utils, "Cannot link tee to branch: tee element is null");
        return false;
    }

    if (!branch_queue) {
        sf::error(pipeline_utils, "Cannot link tee to branch: branch_queue element is null");
        return false;
    }

    const sf::unique_gst_pad_ptr tee_pad(gst_element_request_pad_simple(tee, "src_%u"));
    if (!tee_pad) {
        sf::error(pipeline_utils, "Failed to request a source pad from the tee.");
        return false;
    }

    const sf::unique_gst_pad_ptr queue_pad(gst_element_get_static_pad(branch_queue, "sink"));
    if (!queue_pad) {
        sf::error(pipeline_utils, "Failed to get sink pad from the branch queue '%s'.", GST_ELEMENT_NAME(branch_queue));
        return false;
    }

    if (gst_pad_link(tee_pad.get(), queue_pad.get()) != GST_PAD_LINK_OK) {
        sf::error(pipeline_utils, "Tee could not be linked to branch queue '%s'.", GST_ELEMENT_NAME(branch_queue));
        return false;
    }

    sf::debug(pipeline_utils, "Successfully linked tee to branch with queue '%s'", GST_ELEMENT_NAME(branch_queue));
    return true;
}
