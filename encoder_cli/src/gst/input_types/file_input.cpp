// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "file_input.h"
#include "../../../libvcai/include/logging.h"
#include "common_input.h"
#include "input_ingress_facade.h"

#include <gst/gst.h>

/**
 * @file file_input.cpp
 * @brief Implementation of file input pipeline creation and management.
 */

/// GStreamer debug category for file input handling.
GST_DEBUG_CATEGORY_STATIC(file_input);

void init_file_input_debug_category() {
    GST_DEBUG_CATEGORY_INIT(file_input, "file-input", 0, "File input pipeline operations");
}

/**
 * @brief Builds the file input pipeline and attaches it to global data.
 * @param data Global data structure to populate with elements.
 * @return True if the pipeline was created successfully.
 */
bool create_file_input_pipeline(global_data &data) {
    input_ingress_facade facade(data);
    sf::debug(file_input, "Creating file input pipeline with file='%s'",
              facade.params.file_name.get() ? facade.params.file_name.get() : "null");

    GstElement *filesrc, *parser, *tee;

    if (!create_common_input_elements(facade, filesrc, parser, tee)) {
        sf::error(file_input, "File input pipeline failed - could not create common elements (filesrc, rawvideoparse, "
                              "tee). Check GStreamer plugin installation.");
        return false;
    }

    input_sink_branch_elements sink_elements{};
    if (!create_input_sink_branch_elements(facade, "input-sink", "File input pipeline", sink_elements)) {
        sf::error(file_input, "File input pipeline failed - could not initialize sink branch elements.");
        return false;
    }

    // Add elements to pipeline
    gst_bin_add_many(GST_BIN(facade.input_pipeline.get()), filesrc, parser, tee, sink_elements.appsink, nullptr);
    if (facade.params.debug.save_frames) {
        gst_bin_add_many(GST_BIN(facade.input_pipeline.get()), sink_elements.queue_original,
                         sink_elements.converter_original, sink_elements.encoder_original, sink_elements.sink_original,
                         nullptr);
    }

    // Link elements
    bool result = gst_element_link_many(filesrc, parser, tee, sink_elements.appsink, nullptr);
    if (!result) {
        sf::error(file_input,
                  "File input pipeline failed - could not link elements (filesrc -> parser -> tee -> appsink). "
                  "Check element capabilities.");
        return false;
    }
    result = link_original_frame_saving_branch(tee, sink_elements.queue_original, sink_elements.converter_original,
                                               sink_elements.encoder_original, sink_elements.sink_original);
    if (!result) {
        sf::error(file_input, "File input pipeline failed - could not link original frame saving branch.");
        return false;
    }

    sf::debug(file_input, "File input pipeline created successfully");
    return result;
}
