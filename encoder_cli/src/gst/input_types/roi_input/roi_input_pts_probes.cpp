// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../../processing/memory_contract_helper.h"
#include "../input_ingress_facade.h"
#include "../roi_input.h"
#include "roi_input_internal.h"

#include <gst/gst.h>

#include "../../../../libvcai/include/logging.h"

GST_DEBUG_CATEGORY_EXTERN(roi_input);

namespace {
    struct pts_probe_context {
        ingress_pts_storage *storage = nullptr;
        memory_policy_runtime *memory_runtime = nullptr;
    };

    [[nodiscard]] ingress_pts_storage &get_or_create_pts_storage(input_ingress_facade &data) {
        return data.ingress_runtime.ensure_pts_storage();
    }
} // namespace

uint64_t get_pts_storage_queue_size(const input_ingress_facade &data) { return data.ingress_runtime.pts_queue_size(); }

namespace {

    /**
     * @brief Pad probe callback to store original PTS before streammux.
     * Only stores PTS for frames going to inference path (valve_inference is open).
     */
    GstPadProbeReturn store_pts_probe([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
        if (!(GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER)) {
            return GST_PAD_PROBE_OK;
        }

        const auto *context = static_cast<const pts_probe_context *>(user_data);
        ingress_pts_storage *storage = context ? context->storage : nullptr;
        if (storage == nullptr) {
            return GST_PAD_PROBE_OK;
        }

        // Get valve_inference state to check if frame is going to inference
        // We need access to global_data for this, but user_data only has storage
        // For now, store PTS for all frames - we'll filter when restoring
        // Actually, better: check valve state via the element
        GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        if (!buffer) {
            return GST_PAD_PROBE_OK;
        }

        const GstClockTime pts = GST_BUFFER_PTS(buffer);
        if (pts != GST_CLOCK_TIME_NONE) {
            // Store PTS in buffer qdata so we can check it later
            // Actually, we can't easily check routing here. Let's use a different approach:
            // Store PTS for all frames, but only restore for inference frames
            // The restore probe will be called only for inference frames
            std::lock_guard lock(storage->mutex);
            storage->pts_queue.push(pts);
        }

        return GST_PAD_PROBE_OK;
    }

    /**
     * @brief Pad probe callback to restore original PTS after inference path.
     * Restores PTS from the stored queue (FIFO order matches frame order).
     */
    GstPadProbeReturn restore_pts_probe([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
        if (!(GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER)) {
            return GST_PAD_PROBE_OK;
        }

        auto *context = static_cast<pts_probe_context *>(user_data);
        ingress_pts_storage *storage = context ? context->storage : nullptr;
        memory_policy_runtime *const memory_runtime = context ? context->memory_runtime : nullptr;
        if (storage == nullptr) {
            return GST_PAD_PROBE_OK;
        }

        GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        if (!buffer) {
            return GST_PAD_PROBE_OK;
        }

        // Get oldest stored PTS (FIFO order matches frame order through streammux)
        std::lock_guard lock(storage->mutex);
        if (!storage->pts_queue.empty()) {
            const GstClockTime stored_pts = storage->pts_queue.front();
            storage->pts_queue.pop();

            GstBuffer *target_buffer = buffer;
            if (memory_runtime != nullptr) {
                const auto writable_result = ensure_writable_probe_buffer_with_fallback(
                        info, buffer, *memory_runtime, roi_input, "roi_input.restore_pts_probe");
                if (writable_result.status == memory_contract_status::fatal_stop || writable_result.buffer == nullptr) {
                    return GST_PAD_PROBE_DROP;
                }
                target_buffer = writable_result.buffer;
            } else {
                target_buffer = gst_buffer_make_writable(buffer);
                if (target_buffer == nullptr) {
                    return GST_PAD_PROBE_DROP;
                }
                GST_PAD_PROBE_INFO_DATA(info) = target_buffer;
            }

            GST_BUFFER_PTS(target_buffer) = stored_pts;
            // Preserve DTS and duration from current buffer
            if (GST_BUFFER_DTS(buffer) != GST_CLOCK_TIME_NONE) {
                GST_BUFFER_DTS(target_buffer) = GST_BUFFER_DTS(buffer);
            }
            if (GST_BUFFER_DURATION(buffer) != GST_CLOCK_TIME_NONE) {
                GST_BUFFER_DURATION(target_buffer) = GST_BUFFER_DURATION(buffer);
            }
        } else {
            sf::warning(roi_input, "No stored PTS available for inference frame");
        }

        return GST_PAD_PROBE_OK;
    }
} // namespace
bool setup_pts_preservation_probes(input_ingress_facade &data, deepstream_elements &elements) {
    init_roi_input_debug_category();

    // Create storage for PTS mapping
    auto &pts_storage = get_or_create_pts_storage(data);

    // Stable handle to the memory-policy slice for the long-lived restore probe.
    memory_policy_runtime *const memory_runtime_handle = &data.memory_runtime;

    // Add probe on valve_inference src pad (after valve, before streammux) to store PTS
    // This ensures we only store PTS for frames that actually go to inference path
    GstPad *src_pad_valve = gst_element_get_static_pad(elements.valve_inference, "src");
    if (!src_pad_valve) {
        sf::error(roi_input, "Failed to get src pad from valve_inference for PTS preservation");
        return false;
    }

    auto *store_probe_context = g_new(pts_probe_context, 1);
    *store_probe_context = pts_probe_context{.storage = &pts_storage, .memory_runtime = memory_runtime_handle};
    gst_pad_add_probe(src_pad_valve, GST_PAD_PROBE_TYPE_BUFFER, store_pts_probe, store_probe_context,
                      [](gpointer user_data) { g_free(user_data); });
    gst_object_unref(src_pad_valve);
    sf::info(roi_input, "Added PTS storage probe after inference valve (before streammux)");

    // Add probe on videoconvert_inference src pad (after inference path) to restore PTS
    GstPad *src_pad_after_inference = gst_element_get_static_pad(elements.videoconvert_inference, "src");
    if (!src_pad_after_inference) {
        sf::error(roi_input, "Failed to get src pad from videoconvert_inference for PTS restoration");
        return false;
    }

    auto *restore_probe_context = g_new(pts_probe_context, 1);
    *restore_probe_context = pts_probe_context{.storage = &pts_storage, .memory_runtime = memory_runtime_handle};
    gst_pad_add_probe(src_pad_after_inference, GST_PAD_PROBE_TYPE_BUFFER, restore_pts_probe, restore_probe_context,
                      [](gpointer user_data) { g_free(user_data); });
    gst_object_unref(src_pad_after_inference);
    sf::info(roi_input, "Added PTS restoration probe after inference path");

    // Setup performance monitoring probes
    setup_inference_performance_probes(data, elements);

    return true;
}
