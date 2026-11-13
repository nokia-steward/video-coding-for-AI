#include "file_branch.h"
#include "../../pipeline.h"
#include "../../pipeline/utils.h"
#include "../branch_utils.h"
#include "gst/gpu_elements.h"
#include "logging.h"

#include <gst/gst.h>

// Debug category for file output branch operations.
GST_DEBUG_CATEGORY_STATIC(file_branch_category);
#define GST_CAT_DEFAULT file_branch_category

namespace {
    [[nodiscard]] optional_branch_status resolve_file_branch_status(const rtp_pipeline &pipeline) {
        const bool file_output_enabled = pipeline.output_directory.has_value() && !pipeline.output_directory->empty();
        return file_output_enabled ? optional_branch_status::enabled : optional_branch_status::disabled;
    }

    [[nodiscard]] optional_branch_gate_decision gate_file_branch(const rtp_pipeline *pipeline) {
        return get_optional_branch_gate_decision(pipeline, resolve_file_branch_status);
    }
} // namespace

output_branch_functions create_file_branch_functions() {
    return {.is_enabled = [](const rtp_pipeline *pipeline) -> bool {
                const optional_branch_gate_decision gate_decision = gate_file_branch(pipeline);
                return gate_decision.is_valid_pipeline && gate_decision.should_execute;
            },

            .create_elements = [](rtp_pipeline *pipeline) -> bool {
                const optional_branch_gate_decision gate_decision = gate_file_branch(pipeline);
                if (!gate_decision.is_valid_pipeline) {
                    return false;
                }
                if (!gate_decision.should_execute) {
                    return true; // Not an error if disabled
                }

                pipeline->queue_file = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("queue", nullptr));
                try {
                    // RAII_EXEMPT: GstBin takes ownership of this raw GstElement pointer.
                    pipeline->h265_encoder =
                            sf::unique_gst_element_no_delete_ptr(create_h265_encoder("file-encoder", 0).release());
                } catch (const std::exception &error) {
                    sf::error(file_branch_category, "Failed to create GPU encoder for port %u: %s",
                              pipeline->get_port(), error.what());
                    return false;
                }
                pipeline->filesink =
                        sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("filesink", "filesink"));

                if (!has_all_required_elements(
                            {pipeline->queue_file.get(), pipeline->h265_encoder.get(), pipeline->filesink.get()})) {
                    sf::error(file_branch_category, "Failed to create file branch elements for port %u",
                              pipeline->get_port());
                    return false;
                }

                return true;
            },

            .configure_elements = [](rtp_pipeline *pipeline) -> bool {
                const optional_branch_gate_decision gate_decision = gate_file_branch(pipeline);
                if (!gate_decision.is_valid_pipeline) {
                    return false;
                }
                if (!gate_decision.should_execute) {
                    return true;
                }

                // Configure queue
                configure_queue_element(pipeline->queue_file.get(), static_cast<guint64>(100 * 1024 * 1024),
                                        static_cast<guint>(200), static_cast<guint64>(5 * GST_SECOND));

                // Generate filename and set filesink location
                pipeline->filename = generate_filename(*pipeline);
                g_object_set(G_OBJECT(pipeline->filesink.get()), "location", pipeline->filename.c_str(), nullptr);
                sf::info(file_branch_category, "File saving enabled - Saving stream to file: %s for port %u",
                         pipeline->filename.c_str(), pipeline->get_port());

                return true;
            },

            .link_branch = [](rtp_pipeline *pipeline) -> bool {
                const optional_branch_gate_decision gate_decision = gate_file_branch(pipeline);
                if (!gate_decision.is_valid_pipeline) {
                    return false;
                }
                if (!gate_decision.should_execute) {
                    return true;
                }

                // Link: queue_file -> h265_encoder -> filesink
                return gst_element_link_many(pipeline->queue_file.get(), pipeline->h265_encoder.get(),
                                             pipeline->filesink.get(), nullptr);
            },

            .link_to_tee = [](rtp_pipeline *pipeline, GstElement *tee_element) -> bool {
                const optional_branch_gate_decision gate_decision = gate_file_branch(pipeline);
                if (!gate_decision.is_valid_pipeline || !tee_element) {
                    return false;
                }
                if (!gate_decision.should_execute) {
                    return true;
                }

                return link_tee_to_branch_queue(file_branch_category, "file saving", pipeline->get_port(), tee_element,
                                                pipeline->queue_file.get());
            },

            .add_to_pipeline = [](rtp_pipeline *pipeline, GstElement *output_pipeline) -> bool {
                const optional_branch_gate_decision gate_decision = gate_file_branch(pipeline);
                if (!gate_decision.is_valid_pipeline || !output_pipeline) {
                    return false;
                }
                if (!gate_decision.should_execute) {
                    return true;
                }

                gst_bin_add_many(GST_BIN(output_pipeline), pipeline->queue_file.get(), pipeline->h265_encoder.get(),
                                 pipeline->filesink.get(), nullptr);
                return true;
            },

            .cleanup = [](rtp_pipeline *pipeline) -> void {
                (void) pipeline;
                // No special cleanup needed - elements are managed by pipeline
            },

            .get_branch_name = []() -> const char * { return "file"; }};
}

output_branch_context create_file_branch_context() { return {.functions = create_file_branch_functions()}; }
