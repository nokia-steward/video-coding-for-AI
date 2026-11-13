#include "branch_utils.h"
#include "logging.h"

#include "gst_raii.h"

bool has_all_required_elements(const std::initializer_list<GstElement *> elements) {
    for (const auto *element: elements) {
        if (element == nullptr) {
            return false;
        }
    }
    return true;
}

optional_branch_gate_decision get_optional_branch_gate_decision(const rtp_pipeline *pipeline,
                                                                const optional_branch_status_resolver status_resolver) {
    if (pipeline == nullptr || status_resolver == nullptr) {
        return {.is_valid_pipeline = false, .should_execute = false};
    }

    const optional_branch_status status = status_resolver(*pipeline);
    return {.is_valid_pipeline = true, .should_execute = (status == optional_branch_status::enabled)};
}

void configure_queue_element(GstElement *queue, const guint64 max_bytes, const guint max_buffers,
                             const guint64 max_time) {
    if (!queue) {
        return;
    }

    // leaky=2 means GST_QUEUE_LEAK_DOWNSTREAM (drop oldest buffers when queue is full)
    g_object_set(G_OBJECT(queue), "max-size-bytes", static_cast<guint>(max_bytes), "max-size-buffers", max_buffers,
                 "max-size-time", max_time, "leaky", 2, nullptr);
}

bool link_tee_to_branch_queue(GstDebugCategory *category, const char *branch_name, const uint16_t port,
                              GstElement *tee_element, GstElement *queue_element) {
    if (!tee_element || !queue_element) {
        sf::error(category, "Failed to link tee to %s branch for port %u: missing tee or queue element", branch_name,
                  port);
        return false;
    }

    const sf::unique_gst_pad_ptr tee_src_pad(gst_element_request_pad_simple(tee_element, "src_%u"));
    const sf::unique_gst_pad_ptr sink_pad(gst_element_get_static_pad(queue_element, "sink"));
    if (!tee_src_pad || !sink_pad) {
        sf::error(category, "Failed to get pads for %s branch tee linking for port %u", branch_name, port);
        return false;
    }

    const GstPadLinkReturn link_ret = gst_pad_link(tee_src_pad.get(), sink_pad.get());
    if (link_ret != GST_PAD_LINK_OK) {
        sf::error(category, "Failed to link tee to %s branch for port %u", branch_name, port);
        return false;
    }

    sf::info(category, "Successfully linked %s branch for port %u", branch_name, port);
    return true;
}
