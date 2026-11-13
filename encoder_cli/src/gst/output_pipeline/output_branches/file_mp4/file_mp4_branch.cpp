// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "file_mp4_branch.h"
#include "../../../../../libvcai/include/logging.h"
#include "../../../../encoder_enums.h"
#include "../../../pipeline_utils.h"
#include "../../elements.h"
#include "../../output_pipeline_debug.h"
#include "../output_branch_facade.h"

#include <gst/gst.h>

/**
 * @file file_mp4_branch.cpp
 * @brief Implementation of MP4 file branch creation and linking.
 */

namespace {
    /**
     * @brief Check if MP4 branch should be enabled.
     * @param data The narrow output branch state view.
     * @return true if MP4 branch should be enabled, false otherwise.
     */
    [[nodiscard]] bool is_file_mp4_branch_enabled(const output_branch_facade &data) {
        return data.params.debug.enable_mp4_branch;
    }

    /**
     * @brief Creates MP4 branch elements.
     * @param elements Output structure to populate.
     * @return true if successful, false otherwise.
     */
    [[nodiscard]] bool create_file_mp4_branch_elements(output_pipeline_elements &elements) {
        elements.queue_mp4 = gst_element_factory_make("queue", "q-mp4");
        if (!elements.queue_mp4) {
            sf::error(output_pipeline, "Failed to create queue for MP4 branch");
            return false;
        }
        elements.parser_mp4 = gst_element_factory_make("h265parse", "p-mp4");
        if (!elements.parser_mp4) {
            sf::error(output_pipeline, "Failed to create h265parse for MP4 branch");
            return false;
        }
        elements.muxer_mp4 = gst_element_factory_make("mp4mux", "m-mp4");
        if (!elements.muxer_mp4) {
            sf::error(output_pipeline, "Failed to create mp4mux for MP4 branch");
            return false;
        }
        elements.sink_mp4 = gst_element_factory_make("filesink", "s-mp4");
        if (!elements.sink_mp4) {
            sf::error(output_pipeline, "Failed to create filesink for MP4 branch");
            return false;
        }
        return true;
    }

    /**
     * @brief Configures MP4 branch elements.
     * @param data The narrow output branch state view.
     * @param elements Output pipeline elements structure.
     * @return true on success, false on failure.
     */
    [[nodiscard]] bool configure_file_mp4_branch_elements(const output_branch_facade &data,
                                                          output_pipeline_elements &elements) {
        // Configure MP4 branch queue
        if (elements.queue_mp4) {
            // Uniform queue size for synchronization across all branches
            const int effective_framerate = data.params.framerate > 0 ? data.params.framerate : 60;
            const int max_buffers = 2 * effective_framerate;
            const int leaky_mode = static_cast<int>(queue_leaky_mode::no_leak);
            g_object_set(elements.queue_mp4, "max-size-buffers", max_buffers, "max-size-time", 0, "leaky", leaky_mode,
                         nullptr);
            sf::debug(output_pipeline, "Configured MP4 branch queue: max-size-buffers=%d, leaky=no_leak", max_buffers);
        }

        return true;
    }

    /**
     * @brief Links MP4 branch elements.
     * @param elements The pipeline elements.
     * @return true if successful, false otherwise.
     */
    [[nodiscard]] bool link_file_mp4_branch(const output_pipeline_elements &elements) {
        return gst_element_link_many(elements.queue_mp4, elements.parser_mp4, elements.muxer_mp4, elements.sink_mp4,
                                     nullptr);
    }

    /**
     * @brief Links tee to MP4 branch.
     * @param tee The tee element.
     * @param elements The pipeline elements.
     * @return true if successful, false otherwise.
     */
    [[nodiscard]] bool link_tee_to_file_mp4_branch(GstElement *tee, const output_pipeline_elements &elements) {
        return link_tee_to_branch(tee, elements.queue_mp4);
    }

    /**
     * @brief Adds MP4 branch elements to pipeline.
     * @param data The narrow output branch state view.
     * @param elements The pipeline elements.
     */
    void add_file_mp4_branch_to_pipeline(const output_branch_facade &data, const output_pipeline_elements &elements) {
        gst_bin_add_many(GST_BIN(data.output_pipeline.get()), elements.queue_mp4, elements.parser_mp4,
                         elements.muxer_mp4, elements.sink_mp4, nullptr);
    }
} // namespace

output_branch_functions create_file_mp4_branch_functions() {
    return output_branch_functions{
            .is_enabled = [](const output_branch_facade &data) { return is_file_mp4_branch_enabled(data); },
            .create_elements =
                    [](const output_branch_facade &data, output_pipeline_elements &elements) {
                        if (!is_file_mp4_branch_enabled(data)) {
                            return true; // Not enabled, but not an error
                        }
                        return create_file_mp4_branch_elements(elements);
                    },
            .configure_elements =
                    [](const output_branch_facade &data, output_pipeline_elements &elements) {
                        if (!is_file_mp4_branch_enabled(data)) {
                            return true; // Not enabled, but not an error
                        }
                        return configure_file_mp4_branch_elements(data, elements);
                    },
            .link_branch =
                    [](const output_pipeline_elements &elements) {
                        if (!elements.queue_mp4) {
                            return true; // Not created, but not an error
                        }
                        return link_file_mp4_branch(elements);
                    },
            .link_to_tee =
                    [](GstElement *tee, const output_pipeline_elements &elements) {
                        if (!elements.queue_mp4) {
                            return true; // Not created, but not an error
                        }
                        return link_tee_to_file_mp4_branch(tee, elements);
                    },
            .add_to_pipeline =
                    [](const output_branch_facade &data, const output_pipeline_elements &elements) {
                        if (is_file_mp4_branch_enabled(data)) {
                            add_file_mp4_branch_to_pipeline(data, elements);
                        }
                    },
            .get_branch_name = []() { return "file-mp4"; },
            .setup_signals =
                    [](const output_branch_facade &, const output_pipeline_elements &) {
                        return true; // No signals needed for MP4 branch
                    }};
}

output_branch_context create_file_mp4_branch_context() {
    return output_branch_context{.functions = create_file_mp4_branch_functions()};
}
