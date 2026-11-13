// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "linking.h"
#include "../pipeline.h"
#include "logging.h"

#include <gst/gst.h>
#include <optional>

// Debug category for pipeline-level logging (defined elsewhere).
GST_DEBUG_CATEGORY_EXTERN(pipeline_category);

namespace {
    /**
     * @brief Convert a GstCaps pointer to a printable string.
     * @param caps Caps to stringify (may be null).
     * @return String representation or "null" if unavailable.
     */
    [[nodiscard]] std::string caps_to_string(GstCaps *caps) {
        if (!caps) {
            return "null";
        }
        char *caps_string = gst_caps_to_string(caps);
        if (!caps_string) {
            return "null";
        }
        std::string result(caps_string);
        g_free(caps_string);
        return result;
    }

    /**
     * @brief Log current and template caps for a specific pad.
     * @param element Element owning the pad.
     * @param pad_name Name of the pad to inspect.
     * @param element_label Label used in log messages.
     * @param port Port number for log context.
     */
    void log_pad_caps(GstElement *element, const char *pad_name, const char *element_label, guint16 port) {
        if (!element || !pad_name || !element_label) {
            sf::warning(pipeline_category, "Cannot log pad caps for port %u due to invalid inputs", port);
            return;
        }

        GstPad *pad = gst_element_get_static_pad(element, pad_name);
        if (!pad) {
            sf::error(pipeline_category, "Missing %s pad on %s for port %u", pad_name, element_label, port);
            return;
        }

        GstCaps *current_caps = gst_pad_get_current_caps(pad);
        const std::string current_caps_string = caps_to_string(current_caps);
        if (current_caps) {
            gst_caps_unref(current_caps);
        }

        GstCaps *query_caps = gst_pad_query_caps(pad, nullptr);
        const std::string query_caps_string = caps_to_string(query_caps);
        if (query_caps) {
            gst_caps_unref(query_caps);
        }

        sf::error(pipeline_category, "%s %s pad caps for port %u - current: %s, template: %s", element_label, pad_name,
                  port, current_caps_string.c_str(), query_caps_string.c_str());

        gst_object_unref(pad);
    }

    /**
     * @brief Link two elements and emit detailed caps logs on failure.
     * @param upstream Upstream element providing the src pad.
     * @param downstream Downstream element providing the sink pad.
     * @param upstream_label Label used for upstream logging.
     * @param downstream_label Label used for downstream logging.
     * @param port Port number for log context.
     * @return true if linking succeeds, false otherwise.
     */
    [[nodiscard]] bool link_elements_with_debug(GstElement *upstream, GstElement *downstream,
                                                const char *upstream_label, const char *downstream_label,
                                                guint16 port) {
        if (!upstream || !downstream) {
            sf::error(pipeline_category, "Invalid elements while linking %s -> %s for port %u", upstream_label,
                      downstream_label, port);
            return false;
        }

        if (gst_element_link(upstream, downstream) != TRUE) {
            sf::error(pipeline_category, "Failed to link %s -> %s for port %u", upstream_label, downstream_label, port);
            log_pad_caps(upstream, "src", upstream_label, port);
            log_pad_caps(downstream, "sink", downstream_label, port);
            return false;
        }

        return true;
    }
} // namespace

bool link_input_pipeline(rtp_pipeline &pipeline) {
    if (!pipeline.transport_ctx.functions.link_input_pipeline) {
        return false;
    }
    return pipeline.transport_ctx.functions.link_input_pipeline(&pipeline.transport_build_ctx);
}

bool link_decode_and_output_pipeline(rtp_pipeline &pipeline) {
    if (!link_elements_with_debug(pipeline.queue_encoded.get(), pipeline.sf_h265_sei_extract.get(), "queue_encoded",
                                  "sf_h265_sei_extract", pipeline.port)) {
        return false;
    }

    if (!link_elements_with_debug(pipeline.sf_h265_sei_extract.get(), pipeline.h265_decoder.get(),
                                  "sf_h265_sei_extract", "h265_decoder", pipeline.port)) {
        return false;
    }

    if (!link_elements_with_debug(pipeline.h265_decoder.get(), pipeline.decoded_converter.get(), "h265_decoder",
                                  "decoded_converter", pipeline.port)) {
        return false;
    }

    if (!link_elements_with_debug(pipeline.decoded_converter.get(), pipeline.decoded_capsfilter.get(),
                                  "decoded_converter", "decoded_capsfilter", pipeline.port)) {
        return false;
    }

    if (!link_elements_with_debug(pipeline.decoded_capsfilter.get(), pipeline.sf_pri_reconstruct.get(),
                                  "decoded_capsfilter", "sf_pri_reconstruct", pipeline.port)) {
        return false;
    }

    const bool enable_luma_backscale =
            pipeline.server_config == nullptr || pipeline.server_config->enable_luma_backscale;
    if (enable_luma_backscale) {
        if (!link_elements_with_debug(pipeline.sf_pri_reconstruct.get(), pipeline.sf_eoi_luma_backscale.get(),
                                      "sf_pri_reconstruct", "sf_eoi_luma_backscale", pipeline.port)) {
            return false;
        }
        if (!link_elements_with_debug(pipeline.sf_eoi_luma_backscale.get(), pipeline.sf_tet_apply.get(),
                                      "sf_eoi_luma_backscale", "sf_tet_apply", pipeline.port)) {
            return false;
        }
    } else if (!link_elements_with_debug(pipeline.sf_pri_reconstruct.get(), pipeline.sf_tet_apply.get(),
                                         "sf_pri_reconstruct", "sf_tet_apply", pipeline.port)) {
        return false;
    }

    if (!link_elements_with_debug(pipeline.sf_tet_apply.get(), pipeline.queue_decoded.get(), "sf_tet_apply",
                                  "queue_decoded", pipeline.port)) {
        return false;
    }

    if (!link_elements_with_debug(pipeline.queue_decoded.get(), pipeline.decoded_output_capsfilter.get(),
                                  "queue_decoded", "decoded_output_capsfilter", pipeline.get_port())) {
        return false;
    }

    if (!link_elements_with_debug(pipeline.decoded_output_capsfilter.get(), pipeline.gpu_video_convert.get(),
                                  "decoded_output_capsfilter", "gpu_video_convert", pipeline.get_port())) {
        return false;
    }

    if (!link_elements_with_debug(pipeline.gpu_video_convert.get(), pipeline.tee.get(), "gpu_video_convert", "tee",
                                  pipeline.get_port())) {
        return false;
    }

    if (!pipeline.output_controller.link_branch_chains(pipeline)) {
        sf::error(pipeline_category, "Failed to link output branch chains for port %u", pipeline.get_port());
        return false;
    }

    if (!pipeline.output_controller.link_branches_to_tee(pipeline, pipeline.tee.get())) {
        sf::error(pipeline_category, "Failed to link output branches to tee for port %u", pipeline.get_port());
        return false;
    }

    return true;
}
