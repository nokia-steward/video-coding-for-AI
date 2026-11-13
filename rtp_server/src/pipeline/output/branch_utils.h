#pragma once

#include <cstdint>
#include <gst/gst.h>
#include <initializer_list>

struct rtp_pipeline;

/**
 * @brief Checks that all required branch elements are non-null.
 * @param elements Raw element pointers that must be present.
 * @return true if all elements are present, false otherwise.
 */
[[nodiscard]] bool has_all_required_elements(std::initializer_list<GstElement *> elements);

/**
 * @brief Enablement state of an optional output branch.
 */
enum class optional_branch_status {
    disabled,
    enabled,
};

/**
 * @brief Decision derived from pipeline validity and branch enablement.
 */
struct optional_branch_gate_decision {
    bool is_valid_pipeline = false;
    bool should_execute = false;
};

/**
 * @brief Function type that resolves branch enablement from a pipeline.
 */
using optional_branch_status_resolver = optional_branch_status (*)(const rtp_pipeline &pipeline);

/**
 * @brief Computes whether an optional branch should execute for a pipeline.
 * @param pipeline Pipeline pointer to validate and inspect.
 * @param status_resolver Callback that determines whether the branch is enabled.
 * @return Gate decision with pipeline validity and execution intent.
 */
[[nodiscard]] optional_branch_gate_decision
get_optional_branch_gate_decision(const rtp_pipeline *pipeline, optional_branch_status_resolver status_resolver);

/**
 * @brief Configures a queue element with limits and downstream leaky mode.
 * @param queue Queue element to configure.
 * @param max_bytes Maximum bytes in queue.
 * @param max_buffers Maximum number of buffers in queue.
 * @param max_time Maximum queued duration (nanoseconds).
 */
void configure_queue_element(GstElement *queue, guint64 max_bytes, guint max_buffers, guint64 max_time);

/**
 * @brief Links tee src pad to a branch queue sink pad.
 * @param category Debug category used for logs.
 * @param branch_name Branch label for diagnostics.
 * @param port Pipeline port for diagnostics.
 * @param tee_element Tee element that provides source request pad.
 * @param queue_element Queue element whose sink pad is linked.
 * @return true if link succeeds, false otherwise.
 */
[[nodiscard]] bool link_tee_to_branch_queue(GstDebugCategory *category, const char *branch_name, uint16_t port,
                                            GstElement *tee_element, GstElement *queue_element);
