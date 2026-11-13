// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "file_h265_branch.h"
#include "../../../../../libvcai/include/logging.h"
#include "../../../../encoder_enums.h"
#include "../../../pipeline_utils.h"
#include "../../elements.h"
#include "../../output_pipeline_debug.h"
#include "../../paths.h"
#include "../output_branch_facade.h"

#include <gst/gst.h>

/**
 * @file file_h265_branch.cpp
 * @brief Implementation of H.265 file branch creation and linking.
 */

namespace {
    /**
     * @brief Check if H.265 file branch should be enabled.
     * @param data The narrow output branch state view.
     * @return true if H.265 file branch should be enabled, false otherwise.
     */
    [[nodiscard]] bool is_file_h265_branch_enabled(const output_branch_facade &data) {
        return data.params.debug.enable_h265_branch;
    }

    /**
     * @brief Creates H.265 file branch elements.
     * @param elements Output structure to populate.
     * @return true if successful, false otherwise.
     */
    [[nodiscard]] bool create_file_h265_branch_elements(output_pipeline_elements &elements) {
        elements.queue_file = gst_element_factory_make("queue", "q-file");
        if (!elements.queue_file) {
            sf::error(output_pipeline, "Failed to create queue for H.265 file branch");
            return false;
        }
        elements.sink_file = gst_element_factory_make("filesink", "s-file");
        if (!elements.sink_file) {
            sf::error(output_pipeline, "Failed to create filesink for H.265 file branch");
            return false;
        }
        return true;
    }

    /**
     * @brief Configures H.265 file branch elements.
     * @param data The narrow output branch state view.
     * @param elements Output pipeline elements structure.
     * @return true on success, false on failure.
     */
    [[nodiscard]] bool configure_file_h265_branch_elements(const output_branch_facade &data,
                                                           output_pipeline_elements &elements) {
        // Configure H.265 file branch queue
        if (elements.queue_file) {
            // Uniform queue size for synchronization across all branches
            const int effective_framerate = data.params.framerate > 0 ? data.params.framerate : 60;
            const int max_buffers = 2 * effective_framerate;
            const int leaky_mode = static_cast<int>(queue_leaky_mode::no_leak);
            g_object_set(elements.queue_file, "max-size-buffers", max_buffers, "max-size-time", 0, "leaky", leaky_mode,
                         nullptr);
            sf::debug(output_pipeline, "Configured H.265 file branch queue: max-size-buffers=%d, leaky=no_leak",
                      max_buffers);
        }

        // Configure H.265 file sink path (done in configure_output_paths, but we can do it here too)
        // Actually, paths are configured separately in configure_output_paths(), so we don't need to do it here
        // The path configuration will be handled by the main pipeline creation code

        return true;
    }

    /**
     * @brief Links H.265 file branch elements.
     * @param elements The pipeline elements.
     * @return true if successful, false otherwise.
     */
    [[nodiscard]] bool link_file_h265_branch(const output_pipeline_elements &elements) {
        return gst_element_link_many(elements.queue_file, elements.sink_file, nullptr);
    }

    /**
     * @brief Links tee to H.265 file branch.
     * @param tee The tee element.
     * @param elements The pipeline elements.
     * @return true if successful, false otherwise.
     */
    [[nodiscard]] bool link_tee_to_file_h265_branch(GstElement *tee, const output_pipeline_elements &elements) {
        return link_tee_to_branch(tee, elements.queue_file);
    }

    /**
     * @brief Adds H.265 file branch elements to pipeline.
     * @param data The narrow output branch state view.
     * @param elements The pipeline elements.
     */
    void add_file_h265_branch_to_pipeline(const output_branch_facade &data, const output_pipeline_elements &elements) {
        gst_bin_add_many(GST_BIN(data.output_pipeline.get()), elements.queue_file, elements.sink_file, nullptr);
    }
} // namespace

output_branch_functions create_file_h265_branch_functions() {
    return output_branch_functions{
            .is_enabled = [](const output_branch_facade &data) { return is_file_h265_branch_enabled(data); },
            .create_elements =
                    [](const output_branch_facade &data, output_pipeline_elements &elements) {
                        if (!is_file_h265_branch_enabled(data)) {
                            return true; // Not enabled, but not an error
                        }
                        return create_file_h265_branch_elements(elements);
                    },
            .configure_elements =
                    [](const output_branch_facade &data, output_pipeline_elements &elements) {
                        if (!is_file_h265_branch_enabled(data)) {
                            return true; // Not enabled, but not an error
                        }
                        return configure_file_h265_branch_elements(data, elements);
                    },
            .link_branch =
                    [](const output_pipeline_elements &elements) {
                        if (!elements.queue_file) {
                            return true; // Not created, but not an error
                        }
                        return link_file_h265_branch(elements);
                    },
            .link_to_tee =
                    [](GstElement *tee, const output_pipeline_elements &elements) {
                        if (!elements.queue_file) {
                            return true; // Not created, but not an error
                        }
                        return link_tee_to_file_h265_branch(tee, elements);
                    },
            .add_to_pipeline =
                    [](const output_branch_facade &data, const output_pipeline_elements &elements) {
                        if (is_file_h265_branch_enabled(data)) {
                            add_file_h265_branch_to_pipeline(data, elements);
                        }
                    },
            .get_branch_name = []() { return "file-h265"; },
            .setup_signals =
                    [](const output_branch_facade &, const output_pipeline_elements &) {
                        return true; // No signals needed for H.265 file branch
                    }};
}

output_branch_context create_file_h265_branch_context() {
    return output_branch_context{.functions = create_file_h265_branch_functions()};
}
