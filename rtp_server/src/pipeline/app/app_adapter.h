// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <glib.h>
#include <gst/gst.h>

struct rtp_pipeline;

/**
 * @brief App-side adapter that owns non-media concerns and fail-fast policy.
 */
struct pipeline_app_adapter {
    explicit pipeline_app_adapter(rtp_pipeline &pipeline_ref);

    [[nodiscard]] bool handle_rtp_metadata_probe_payload(GstBuffer *buffer) const;
    [[nodiscard]] bool handle_pad_probe_buffer(GstElement *parent_element, GstBuffer *buffer) const;
    [[nodiscard]] bool update_decoded_session_statistics(guint32 frame_size) const;

    /**
     * @brief Request a controlled fail-fast stop on adapter contract failure.
     * @return Always false so callers can directly return the result.
     */
    [[nodiscard]] bool request_fail_fast_stop(const char *failure_reason) const;

private:
    rtp_pipeline &pipeline;
};
